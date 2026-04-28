#include"Fiber.h"

namespace sylar
{
        //当前协程
        static thread_local Fiber* t_fiber=nullptr;

        //主协程
        static thread_local std::shared_ptr<Fiber> t_thread_fiber=nullptr;

        //调度协程
        static thread_local Fiber* scheduler_fiber=nullptr;

        static std::atomic<uint64_t> s_fiber_id{0};
        static std::atomic<uint64_t> s_fiber_count{0};

        Fiber::Fiber()
        {
                setThis(this);
                m_state=RUNNING;
                if(getcontext(&m_ctx))
                {
                        std::cerr<<"getcontext error"<<std::endl;
                        pthread_exit(nullptr);
                }
                m_id=s_fiber_id++;
                s_fiber_count++;
        }

        Fiber::Fiber(std::function<void()>cb,uint64_t stacksize,bool runInScheduler)
        {
                m_stacksize=stacksize>0?stacksize:128*1024;
                m_state=READY;
                m_cb=cb;
                m_runInScheduler=runInScheduler;
                if(getcontext(&m_ctx))
                {
                        std::cerr<<"getcontext error"<<std::endl;
                        pthread_exit(nullptr);
                }
                m_stackptr=malloc(m_stacksize);
                m_ctx.uc_stack.ss_sp=m_stackptr;
                m_ctx.uc_stack.ss_size=m_stacksize;
                m_ctx.uc_link=nullptr;
                makecontext(&m_ctx,&Fiber::MainFunc,0);
                m_id=s_fiber_id++;
                s_fiber_count++;
        }

        Fiber::~Fiber()
        {
                s_fiber_count--;
                if(m_stackptr)
                {
                        free(m_stackptr);
                }
        }

        void Fiber::reset(std::function<void()>cb)
        {
                assert(m_stackptr!=nullptr&&m_state==TERM);
                m_cb=cb;
                if(getcontext(&m_ctx))
                {
                        std::cerr<<"getcontext error"<<std::endl;
                        pthread_exit(nullptr);
                }
                m_ctx.uc_stack.ss_sp=m_stackptr;
                m_ctx.uc_stack.ss_size=m_stacksize;
                m_ctx.uc_link=nullptr;
                makecontext(&m_ctx,&Fiber::MainFunc,0);
                m_state=READY;
        }

        void Fiber::resume()
        {
                assert(m_state==READY);
                setThis(this);
                m_state=RUNNING;
                if(m_runInScheduler)
                {
                        if(swapcontext(&scheduler_fiber->m_ctx,&m_ctx))
                        {
                                std::cerr<<"swapcontext error"<<std::endl;
                                pthread_exit(nullptr);
                        }
                }
                else
                {
                        if(swapcontext(&t_thread_fiber->m_ctx,&m_ctx))
                        {
                                std::cerr<<"swapcontext error"<<std::endl;
                                pthread_exit(nullptr);
                        }
                }
        }

        void Fiber::yield()
        {
                assert(m_state==RUNNING||m_state==TERM);
                
                if(m_state!=TERM)
                m_state=READY;

                if(m_runInScheduler)
                {
                        setSchedulerFiber(scheduler_fiber);
                        if(swapcontext(&m_ctx,&scheduler_fiber->m_ctx))
                        {
                                std::cerr<<"swapcontext error"<<std::endl;
                                pthread_exit(nullptr);
                        }
                }
                else
                {
                        setSchedulerFiber(t_thread_fiber.get());
                        if(swapcontext(&m_ctx,&t_thread_fiber->m_ctx))
                        {
                                std::cerr<<"swapcontext error"<<std::endl;
                                pthread_exit(nullptr);
                        }
                }
        }

        void Fiber::setThis(Fiber* f)
        {
                t_fiber=f;
        }

        std::shared_ptr<Fiber> Fiber::getThis()
        {
                if(t_fiber)
                return t_fiber->shared_from_this();
                std::shared_ptr<Fiber> main_fiber(new Fiber());
                t_thread_fiber=main_fiber;
                scheduler_fiber=main_fiber.get();
                assert(t_fiber==main_fiber.get());
                return t_fiber->shared_from_this();
        }

        void Fiber::setSchedulerFiber(Fiber* f)
        {
                scheduler_fiber=f;
        }

        uint64_t Fiber::getFiberId()
        {
                if(t_fiber)
                return t_fiber->m_id;

                return (uint64_t)-1;
        }
        
        void Fiber::MainFunc()
        {
                std::shared_ptr<Fiber> cur=getThis();
                assert(cur!=nullptr);
                std::function<void()>cb=cur->m_cb;
                cb();
                cur->m_state=TERM;
                cur->m_cb=nullptr;

                std::shared_ptr<Fiber> f=cur;
                cur.reset();
                f->yield();
                return;
        }
}