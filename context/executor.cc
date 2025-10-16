#include "context/executor.h"

namespace logger {
    namespace ctx {

Executor::ExecutorTimer::ExecutorTimer() {
    running_.store(false);
    thread_pool_ = std::make_unique<ThreadPool>(1);
    repeated_task_id_.store(0);
}

Executor::ExecutorTimer::~ExecutorTimer() {
    Stop();
}

void Executor::ExecutorTimer::Stop() {
    running_.store(false);          //更改状态
    cond_.notify_all();             //释放所有等待线程
    thread_pool_.reset();           //重置线程池

    //不处理，避免数据竞争风险
    //repeated_id_state_set_.clear();

}

bool Executor::ExecutorTimer::Start() {
    //开启内部线程池，并把"定时器的循环函数 Run_()"作为任务提交给线程池，让它在后台持续运行。
    if (running_.load()) {
        return true;
    }
    running_.store(true);
    bool ret = thread_pool_->Start();
    //this 是当前对象的指针（Executor::ExecutorTimer*）。
    //成员函数必须绑定到一个对象实例才能调用，this 就是"在哪个对象上调用 Run_()"
    //成员函数必须通过对象调用（obj.Run_() 或 obj_ptr->Run_()），不能单独存在。
    //所以只能取地址+传对象，this 指的是当前正在执行 Start() 的那个 ExecutorTimer 对象，传的是这个对象
    thread_pool_->RunTask(&Executor::ExecutorTimer::Run_,this);
    return ret;
}

void Executor::ExecutorTimer::Run_() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(mutex_);    
        if(queue_.empty()) {
            cond_.wait(lock, [this]{ return !running_.load() || !queue_.empty();});
            continue;
        }
        auto s = queue_.top();
        auto diff = s.time_point - std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::microseconds>(diff).count() > 0) {
            cond_.wait_for(lock, diff);
            continue;
        }else {
            queue_.pop();
            lock.unlock();
            thread_pool_->RunTask(std::move(s.task));   //任务执行交给线程池，避免阻塞定时循环
        }

    }
    
}



void Executor::ExecutorTimer::PostDelayedTask(Task task, const std::chrono::microseconds& delta) {
    InternalS s;
    s.time_point = std::chrono::high_resolution_clock::now() + delta;
    s.task = std::move(task);   //这个没必要加锁
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(s));      //提升性能
        
    }
    //为什么锁后再唤醒：锁外 notify 通常快 5-10%（高并发场景）。锁内锁外其实差不多的
    cond_.notify_one();             //通知一个避免惊群,不知道是否影响拓展性，大概率no
}


void Executor::ExecutorTimer::PostRepeatedTask_(Task task,
                               const std::chrono::microseconds& delta,                         
                               RepeatedTaskId repeated_task_id, 
                               uint64_t repeat_num) {
    if (repeated_id_state_set_.find(repeated_task_id) == repeated_id_state_set_.end() || repeat_num == 0) {
        return ;
    }
    auto task_copy = task;                  //因为后续不能两次move
    thread_pool_->RunTask(std::move(task));

    Task func = 
        std::bind(&Executor::ExecutorTimer::PostTask_, this, std::move(task_copy), delta, repeated_task_id, repeat_num -1);
    InternalS s;
    s.time_point = std::chrono::high_resolution_clock::now() + delta;
    s.repeated_id = repeated_task_id;
    s.task = std::move(func);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(s));              //因为InternalS 可能没有合适的构造函数，所以用push。并且这样逻辑更清晰
        
    }
    cond_.notify_one();
}

RepeatedTaskId Executor::ExecutorTimer::PostRepeatedTask(Task task, const std::chrono::microseconds& delta, uint64_t repeat_num) {
    auto id = GetNextRepeatedTaskId();
    repeated_id_state_set_.insert(id);
    PostRepeatedTask_(task, delta, id, repeat_num);
    return id;
}                             

//PostTask_ 是一个内部转发函数，作为"重复任务续订"的统一入口，当前实现只是简单转发给 PostRepeatedTask_，未来可在这里插入"续订前的额外逻辑"
void Executor::ExecutorTimer::PostTask_(Task task, std::chrono::microseconds delta, RepeatedTaskId repeated_task_id, uint64_t repeat_num) {
    PostRepeatedTask_(std::move(task), delta, repeated_task_id, repeat_num);
}

void Executor::ExecutorTimer::CancelRepeatedTask(RepeatedTaskId task_id) {
    repeated_id_state_set_.erase(task_id);
}


TaskRunnerTag Executor::ExecutorContext::GetNextRunnerTag() {
    static uint64_t index = 0;
    ++index;
    return index;
}



Executor::ExecutorContext::TaskRunner* Executor::ExecutorContext::GetTaskRunner(const TaskRunnerTag& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task_runner_dict_.find(tag) == task_runner_dict_.end()) {
        return nullptr;
    }
    
    //"从智能指针提取裸指针" → 用 .get()
    return task_runner_dict_[tag].get();  
}

TaskRunnerTag Executor::ExecutorContext::AddTaskRunner(const TaskRunnerTag& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskRunnerTag latest_tag = tag;
    //为什么是while，因为极端情况，可能会连续冲突，所以必须直到找到空闲位置为止
    while (task_runner_dict_.find(latest_tag) != task_runner_dict_.end()) {
        latest_tag = GetNextRunnerTag();
    }
    TaskRunnerPtr runner = std::make_unique<ThreadPool>(1);
    runner->Start();   
    task_runner_dict_.emplace(latest_tag, std::move(runner));     
    return latest_tag ;     //到这里一个特定的strand创建好了，我们可以直接使用它。
}

Executor::Executor() {
    executor_context_ = std::make_unique<ExecutorContext>();    //为什么括号里面不传值
    executor_timer_ = std::make_unique<ExecutorTimer>();
}

Executor::~Executor() {
    executor_context_.reset();
    executor_timer_.reset();
}

TaskRunnerTag Executor::AddTaskRunner(const TaskRunnerTag& tag) {
    return executor_context_->AddTaskRunner(tag);
    
}

void Executor::PostTask(const TaskRunnerTag& runner_tag, Task task) {
    
    ExecutorContext::TaskRunner* task_runner = executor_context_->GetTaskRunner(runner_tag);
    if (!task_runner) { return; }
    task_runner->RunTask(std::move(task));

}



    }
}


