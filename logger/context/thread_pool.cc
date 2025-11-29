#include "context/thread_pool.h"

namespace logger {
    namespace ctx {
//“组合的数据”（例如 std::string、计数器、unique_ptr 等）
//都有自己合法的析构方式，放在 ThreadInfo 里会被“自动”按成员顺序销毁，不需要你手写清理代码。RAII 会替你“弄掉它们”
//所以只需要析构ptr就行
ThreadPool::ThreadInfo::~ThreadInfo() {
    if (ptr && ptr->joinable()) {
        ptr->join();
    }
}

void ThreadPool::AddThread_() {
    ThreadInfoPtr thread_ptr = std::make_shared<ThreadInfo>();      //创造一个空指针，方便保管线程对象
    auto func = [this]() {
        for( ; ;) {         //无限循环，一种风格的体现，while(true)也可以。
            Task task;
            {
                std::unique_lock<std::mutex> lock(this->task_mutex_);

                this->task_cv_.wait(lock,[this] {return (!this->tasks_.empty()) || this->is_shutdown_;});     //满足条件继续往下走
            
                if (this->is_shutdown_) {       //判断顺序很重要
                    break;
                }

                if (this->tasks_.empty()) {
                    continue;
                }

                task = std::move(this->tasks_.front());
                
                this->tasks_.pop();

            }
            task();
        }
    };
    //func是一个函数，想要把它变成线程指针存储起来只能这样
    thread_ptr->ptr = std::make_shared<std::thread> (std::move(func));      //这里不能加()加了就是调用它了，我们是存着，后面再调用
    this->worker_threads_.emplace_back(thread_ptr);

}

void ThreadPool::Stop() {
    if (this->is_vailable_.load()) {
        this->is_shutdown_.store(true);
        this->task_cv_.notify_all();        //让所有线程退出
        this->is_vailable_.store(false);
    }
    this->worker_threads_.clear();          //清楚工作队列
}


bool ThreadPool::Start() {
    if (this->is_vailable_.load()) {
        return false;
    }

    this->is_vailable_.store(true);
    uint32_t thread_count = this->thread_count_.load();         //直接使用也不是不行
    //说下这样写的优点：优点：
    //取“快照”，循环过程中上界不被并发修改影响（更一致）。
    //减少每次迭代的原子读取开销（性能更稳）。



    for(int i = 0;i < thread_count; ++i) {
        AddThread_();
    }

    return true;
}

    }
}
