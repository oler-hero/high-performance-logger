#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "context/thread_pool.h"



namespace logger {
    namespace ctx {

using Task = std::function<void(void)>;
using TaskRunnerTag = uint64_t;
using RepeatedTaskId = uint64_t;


class Executor {
    class ExecutorTimer {   //“定时任务调度器”：按时间点执行一次性任务和可重复任务，并把真正的执行交给内部的线程池，自己只负责“准点叫醒与分发”。
    public:
        struct InternalS {
            std::chrono::time_point<std::chrono::high_resolution_clock> time_point;     //何时执行
            Task task;                                                                  //要执行的函数
            RepeatedTaskId repeated_id;                                                 //重复任务的标识

            //std::priority_queue 默认是“最大堆”（top 是“最大”的元素），比较器默认用 std::less<T>，而 std::less<T> 会调用你类型的 operator<
            //operator< 定义的是"谁更小"的语义
            //若 this->time_point > b.time_point（即"我的时间更晚"），返回 true，意思是"我更小"
            bool operator<(const InternalS &b) const { return time_point > b.time_point; }

        };

    public:
        ExecutorTimer();
        ~ExecutorTimer();

        ExecutorTimer(const ExecutorTimer&) = delete;
        ExecutorTimer& operator=(const ExecutorTimer&) = delete;

        bool Start();           //启动定时器循环（通常开一个后台线程执行 Run_）
        void Stop();            //停止：running_=false，cond_.notify_all() 唤醒线程退出；可清空队列与回收资源

        //std::chrono::duration模板类的特化版本,表示微秒
        //计算执行时间 = now + delta，压入 queue_，唤醒 cond_（若这个任务更早，应打断当前等待）
        void PostDelayedTask(Task task, const std::chrono::microseconds& delta);


        //先按 now+delta 压入一次；执行后在 Run_ 内根据 repeat_num 递减并再次以 now+delta 续订，直到次数用尽或被取消
        //返回 RepeatedTaskId，供取消用
        RepeatedTaskId PostRepeatedTask(Task task, const std::chrono::microseconds& delta, uint64_t repeat_num);

        //把 id 放入 repeated_id_state_set_，Run_ 在续订或执行前检查，命中则不再调度
        void CancelRepeatedTask(RepeatedTaskId task_id);


    private:
        void Run_();       //定时器核心循环（通常在独立线程中运行）让定时任务"到点了就执行"


        //内部统一入队接口（避免公共接口重复逻辑）
        void PostRepeatedTask_(Task task,
                               const std::chrono::microseconds& delta,                         
                               RepeatedTaskId repeated_task_id, 
                               uint64_t repeat_num );
        
        //把任务真正压入优先队列。
        void PostTask_(Task task, std::chrono::microseconds delta, RepeatedTaskId repeated_task_id, uint64_t repeat_num);

        //原子递增生成唯一 ID
        RepeatedTaskId GetNextRepeatedTaskId() {
            return repeated_task_id_++;
        }

    private:
        std::priority_queue<InternalS> queue_;
        std::mutex mutex_;
        std::condition_variable cond_;                                  //保护队列；当加入更早的任务或停止时，用条件变量唤醒定时器线程
        std::atomic<bool> running_;                                     //是否运行中的标志，控制 Run_ 循环退出
        std::unique_ptr<ThreadPool> thread_pool_;                       //真正执行任务的线程池；定时器线程只负责“到点了就把 task 丢给线程池执行”

        std::atomic<RepeatedTaskId> repeated_task_id_;                  //生成重复任务的唯一 ID
        std::unordered_set<RepeatedTaskId> repeated_id_state_set_;      //集合用于取消（标记某个重复任务不再续订）

    };
    //资源管理器
    class ExecutorContext {
        public:
            ExecutorContext() = default;
            ~ExecutorContext() = default;

            ExecutorContext(const ExecutorContext&) = delete;
            ExecutorContext& operator=(const ExecutorContext&) = delete;

            TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

        private:
            using TaskRunner = ThreadPool;
            using TaskRunnerPtr = std::unique_ptr<TaskRunner>;
            friend class Executor;

        private:
            TaskRunner* GetTaskRunner(const TaskRunnerTag& tag);

            TaskRunnerTag GetNextRunnerTag();

        private:
            std::unordered_map<TaskRunnerTag, TaskRunnerPtr> task_runner_dict_;
            std::mutex mutex_;

    };

public:
    Executor();
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    //创建并注册一个新的线程池，返回其 tag
    TaskRunnerTag AddTaskRunner(const TaskRunnerTag& tag);

    //把任务投递到指定线程池异步执行
    void PostTask(const TaskRunnerTag& runner_tag, Task task);

    template <typename R, typename P>       //让用户可以用任意时间单位传参，提升易用性
    void PostDelayedTask(const TaskRunnerTag& runner_tag, Task task, const std::chrono::duration<R,P> delta) {
        Task func = std::bind(&Executor::PostTask, this, runner_tag, std::move(task));
        
        executor_timer_->Start();                                                   //但是内部统一用微秒作为标准单位
        executor_timer_->PostDelayedTask(std::move(func), std::chrono::duration_cast<std::chrono::microseconds>(delta));
    }

    template <typename R, typename P>
    RepeatedTaskId PostRepeatedTask(const TaskRunnerTag& runner_tag,
                                    Task task,
                                    const std::chrono::duration<R, P>& delta,
                                    uint64_t repeat_num ) {
                                        
        Task func = std::bind(&Executor::PostTask, this, runner_tag, std::move(task));
        executor_timer_->Start();
        return executor_timer_->PostRepeatedTask(std::move(func), std::chrono::duration_cast<std::chrono::microseconds>(delta), repeat_num);
                                    
    }
    void CancelRepeatedTask(RepeatedTaskId task_id) {
        executor_timer_->CancelRepeatedTask(task_id);
    }

    template <typename F, typename ...Args>
    auto PostTaskAndGetResult(const TaskRunnerTag& runner_tag,
                              F&& f,  
                              Args&&... args  ) -> std::shared_ptr<std::future<std::result_of_t<F(Args...)>>> {        //std::future 不可拷贝，用 shared_ptr 包一层方便传递
        ExecutorContext::TaskRunner* task_runner = executor_context_->GetTaskRunner(runner_tag);
        if (!task_runner) {
            return nullptr;
        }
        auto ret = task_runner->RunRetTask(std::forward<F>(f), std::forward<Args>(args)...);
        return ret;
        //如果想上面那样使用bind封装，那么我们没有任何办法获取到返回值，这就是我们这样设计的原因
}



    


private:
    std::unique_ptr<ExecutorContext> executor_context_;
    std::unique_ptr<ExecutorTimer> executor_timer_;


};

    }
}


