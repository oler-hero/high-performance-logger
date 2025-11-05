#pragma once

#include<atomic>
#include<condition_variable>
#include<cstdint>
#include<functional>
#include<future>
#include<memory>
#include<mutex>
#include<queue>
#include<thread>
#include<vector>



namespace logger {
namespace ctx {

class ThreadPool {

 public:
    explicit ThreadPool(uint32_t thread_count) {
        this->is_shutdown_.store(false);
        this->is_vailable_.store(false);
        this->thread_count_ = thread_count;
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        Stop();         //一个隐式的内联函数
    }

    bool Start();       //创建并启动 thread_count_ 个工作线程，将 is_available_ 置为 true

    void Stop();        //进入停机


    template<typename F,typename... Args>
    void RunTask(F&& f, Args&&... args) {     //无返回值任务提交函数
        if (this->is_shutdown_.load() || !this->is_vailable_.load()) {
            return ;
        }
        auto task = std::bind(std::forward<F>(f),std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex> lock(this->task_mutex_);
            this->tasks_.emplace([task]() {task(); });       
        }

        this->task_cv_.notify_one();
        //唤醒等待的工作线程来取新任务
    }


    //返回一个共享指针，指向一个 future；这个 future 的值类型是调用 f(args...) 的返回类型
    template<typename F,typename... Args>
    auto RunRetTask(F&& f,Args&&... args)-> std::shared_ptr<std::future<std::result_of_t<F(Args...)>>> {
        if (this->is_shutdown_.load() || !this->is_vailable_.load()) {
            return nullptr;
        }

        using return_type = std::result_of_t<F(Args...)>;       //类型萃取

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::move(bound));
        std::future<return_type> res = task->get_future();
        
        {
            std::lock_guard<std::mutex> lock(this->task_mutex_);
            this->tasks_.emplace([task]() { (*task)();} );
        }
        this->task_cv_.notify_one();
        return std::make_shared<std::future<return_type>>(std::move(res));

    }


 private:
    void AddThread_();      //创建单个工作线程并加入 worker_threads_
 private:
    using ThreadPtr = std::shared_ptr<std::thread>;
    using Task = std::function<void()>;

    //这是一个把“线程本体”与“线程相关元数据/管理逻辑”包到一起的载体（结构体）
    struct ThreadInfo {
        ThreadInfo() = default;
        ~ThreadInfo();

        ThreadPtr ptr{nullptr};    //类内非静态成员默认初始化,只允许花括号和等号
    };

    using ThreadInfoPtr = std::shared_ptr<ThreadInfo>;

 private:
    std::vector<ThreadInfoPtr> worker_threads_;     //工作线程集合
    std::queue<Task> tasks_;                         //任务队列

    std::mutex task_mutex_;                         //保护队列的互斥量
    std::condition_variable task_cv_;               //无任务时工作线程阻塞，有任务时唤醒

    std::atomic<uint32_t> thread_count_;            //目标线程数
    std::atomic<bool> is_shutdown_;                 //是否停机
    std::atomic<bool> is_vailable_;                 //是否接收新任务
};    


}
}


