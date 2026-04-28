#include"Scheduler.h"

namespace sylar
{
    static thread_local Scheduler* t_scheduler=nullptr;
    Scheduler::Scheduler(size_t threads,bool use_caller,const std::string& name):useCaller(use_caller),m_name(name)
    {
        assert(threads>0&&Scheduler::getThis()==nullptr);

        setThis();
        Thread::setName(m_name);
        if(use_caller)
        {
            threads--;

            Fiber::getThis();

            m_schedulerFiber=std::make_shared<Fiber>(std::bind(&Scheduler::run,this),0,false);
            Fiber::setSchedulerFiber(m_schedulerFiber.get());
            m_rootThread=Thread::GetThreadId();
            m_threadsId.push_back(m_rootThread);
        }
        m_threadsCount=threads;
    }

    Scheduler::~Scheduler()
    {
        assert(m_stopping==true);
        if(getThis()==this)
        {
            t_scheduler=nullptr;
        }
    }

    Scheduler* Scheduler::getThis()
    {
        return t_scheduler;
    }

    void Scheduler::setThis()
    {
        t_scheduler=this;
    }

    bool Scheduler::stopping()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_stopping && m_task.empty() && m_activethreadsCount==0;
    }

    void Scheduler::start()
    {
        if(m_stopping)
        {
            std::cerr<<"Scheduler is stopped"<<std::endl;
            return;
        }

        assert(m_threads.empty());
        m_threads.resize(m_threadsCount);
        for(int i=0;i<m_threadsCount;i++)
        {
            m_threads[i]=std::make_shared<Thread>(std::bind(&Scheduler::run,this),m_name+"-"+std::to_string(i));
            m_threadsId.emplace_back(m_threads[i]->getId());
        }
    }

    void Scheduler::run()
    {
        int thread_id= Thread::GetThreadId();

        setThis();

        if(thread_id!=m_rootThread)
        {
            Fiber::getThis();
        }

        std::shared_ptr<Fiber> idle_fiber=std::make_shared<Fiber>(std::bind(&Scheduler::idle,this));
        ScheduleTask task;

        while(true)
        {
            task.reset();
            bool tickle_me=false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it=m_task.begin();
                while(it!=m_task.end())
                {
                    if(it->thread!=-1&&it->thread!=thread_id)
                    {
                        it++;
                        tickle_me=true;
                        continue;
                    }

                    assert(it->f||it->cb);
                    task=*it;
                    it=m_task.erase(it);
                    m_activethreadsCount++;
                    break;
                }
                tickle_me=tickle_me||(it!=m_task.end());
            }

            if(tickle_me)
            {
                tickle();
            }

            if(task.cb)
            {
                std::shared_ptr<Fiber> f=std::make_shared<Fiber>(task.cb);
                {
                    std::lock_guard<std::mutex> lock(f->m_mutex);
                    f->resume();
                }
                m_activethreadsCount--;
                task.reset();
            }
            else if(task.f)
            {
                {
                    std::lock_guard<std::mutex> lock(task.f->m_mutex);
                    if(task.f->getState()!=Fiber::TERM)
                    {
                        task.f->resume();
                    }
                }
                m_activethreadsCount--;
                task.reset();
            }
            else
            {
                if(idle_fiber->getState()==Fiber::TERM)
                {
                    break;
                }
                m_idlethreadsCount++;
                idle_fiber->resume();
                m_idlethreadsCount--;
            }
        }
    }

    void Scheduler::idle()
    {
        while(!stopping())
        {
            sleep(1);
            Fiber::getThis()->yield();
        }

    }

    void Scheduler::tickle()
    {

    }
    
    void Scheduler::stop()
    {
        if(stopping())
        return;
        
        m_stopping=true;
        if(useCaller)
        {
            assert(getThis()==this);
        }
        else
        {
            assert(getThis()!=this);
        }

        for(int i=0;i<m_threadsCount;i++)
        {
            tickle();
        }

        if(m_schedulerFiber)
        {
            tickle();
        }

        if(m_schedulerFiber)
        {
            m_schedulerFiber->resume();
        }

        std::vector<std::shared_ptr<Thread>> thr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            thr.swap(m_threads);
        }

        for(auto it:thr)
        {
            it->join();
        }
    }
};