#pragma once

#include "context/executor.h"

#include <memory>

namespace logger {
    namespace ctx {
class ExecutorManager;
class Context {
    public:
    //返回全局 Executor 访问口（典型写法：转发到 executor_manager_
     Executor* GetExecutor();


     //泄露式单例模式
     static Context* GetInstance() {
        static Context* instance = new Context();
        return instance;
     }
     //按 tag 创建/注册一个任务执行器（线程池），并返回实际生效的 tag（可能因冲突而变化）
     TaskRunnerTag NewTaskRunner(TaskRunnerTag tag);

     Context(const Context&) = delete;              
     Context& operator=(const Context&) = delete;

    private:
    //pimpl模式的实现属于
     std::unique_ptr<ExecutorManager> executor_manager_;

     //私有构造，禁止外部直接 new，配合 GetInstance 实现单例
     Context();
};


    }

}

//定义点宏出来方便我们使用



#define CONTEXT logger::ctx::Context::GetInstance()

#define EXECUTOR CONTEXT->GetExecutor()

#define NEW_TASK_RUNNER(tag) CONTEXT->NewTaskRunner(tag)

#define POST_TASK(runner_tag, task) EXECUTOR->PostTask(runner_tag, task)

//作用是：执行空任务，等待把队列刷干净
#define WAIT_TASK_IDLE(runner_tag) EXECUTOR->PostTaskAndGetResult(runner_tag, []() {})->wait()

#define POST_REPEATED_TASK(runner_tag, task, delta, repeat_num) \
    EXECUTOR->PostRepeatedTask(runner_tag, task, delta, repeat_num)







