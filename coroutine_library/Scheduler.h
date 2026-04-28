#pragma once
#include"Fiber.h"
#include"Thread.h"
#include <sys/epoll.h>
#include<vector>

namespace sylar
{

class Scheduler
{
public:
    Scheduler(size_t threads=1,bool use_caller=true,const std::string& name="scheduler");
    virtual ~Scheduler();
    const std::string& getName()const {return m_name;}

public:
    static Scheduler* getThis(); 

protected:
    void setThis();

public:
    template<typename FiberOrCb>
    void scheduleLock(FiberOrCb f,int thread=-1)
    {
        bool need_tickle=false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            ScheduleTask task(f,thread);
            need_tickle=m_task.empty();
            if(task.f||task.cb)
            {
                m_task.push_back(task);
            }
        }

        if(need_tickle)
        {
            tickle();
        }
    }

    virtual void start();

    virtual void stop();
protected:
    virtual void tickle();

    virtual void run();

    virtual void idle();

    virtual bool stopping();

    bool hasIdleThreads(){return m_idlethreadsCount>0;}

private:
    struct ScheduleTask
    {
        std::shared_ptr<Fiber> f;
        std::function<void()> cb;
        int thread;
        ScheduleTask()
        {
            f=nullptr;
            cb=nullptr;
            thread=-1;
        }

        ScheduleTask(std::shared_ptr<Fiber> f_,int thread_)
        {
            f=f_;
            thread=thread_;
        }

        ScheduleTask(std::shared_ptr<Fiber>* f_,int thread_)
        {
            f.swap(*f_);
            thread=thread_;
        }

        ScheduleTask(std::function<void()>f_,int thread_)
        {
            cb=f_;
            thread=thread_;
        }

        ScheduleTask(std::function<void()>* f_,int thread_)
        {
            cb.swap(*f_);
            thread=thread_;
        }

        void reset()
        {
            f=nullptr;
            cb=nullptr;
            thread=-1;
        }
    };

private:
    std::string m_name;
    std::mutex m_mutex;
    std::vector<ScheduleTask> m_task;
    std::vector<std::shared_ptr<Thread>> m_threads;
    std::vector<int> m_threadsId;
    size_t m_threadsCount=0;
    std::atomic<size_t> m_activethreadsCount{0};
    std::atomic<size_t> m_idlethreadsCount{0};
    bool useCaller;
    std::shared_ptr<Fiber> m_schedulerFiber;
    int m_rootThread=-1;
    bool m_stopping=false;
};


}





