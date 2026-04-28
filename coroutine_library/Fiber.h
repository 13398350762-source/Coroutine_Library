#pragma once
#include<iostream>
#include<ucontext.h>
#include<string>
#include<functional>
#include<mutex>
#include<cassert>
#include<unistd.h>
#include<memory>
#include<atomic>

namespace sylar
{
    class Fiber:public std::enable_shared_from_this<Fiber>
    {
    public:
        enum State
        {
            READY,
            RUNNING,
            TERM
        };
    private:
        Fiber();
    public:
        Fiber(std::function<void()>cb,uint64_t stacksize=0,bool runInScheduler=true);
        ~Fiber();

        void reset(std::function<void()>cb);
        void yield();
        void resume();
        uint64_t getId() const { return m_id; }
        State getState()const{return m_state;}
    public:
        static void setThis(Fiber* f);
        static std::shared_ptr<Fiber> getThis();
        static void setSchedulerFiber(Fiber* f);
        static uint64_t getFiberId();
        static void MainFunc();
    public:
        std::mutex m_mutex;
    private:
        uint64_t m_id;
        uint32_t m_stacksize;
        std::function<void()> m_cb;
        State m_state=READY;
        void* m_stackptr=nullptr;
        ucontext_t m_ctx;
        bool m_runInScheduler;
    };
}