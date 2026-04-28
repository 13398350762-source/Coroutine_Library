#include"IOManager.h"
#include <unistd.h>    
#include <sys/epoll.h> 
#include <fcntl.h>     
#include <cstring>

namespace sylar
{

IOManager* IOManager::getThis()
{
    return dynamic_cast<IOManager*>(Scheduler::getThis());
}

IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event)
{
    assert(event==READ||event==WRITE);
    switch(event)
    {
        case READ:
            return read;
        case WRITE:
            return write;
    }
    throw std::invalid_argument("Unsupported Event Type");
}

void IOManager::FdContext::resetEventContext(EventContext& mtx)
{
    mtx.scheduler=nullptr;
    mtx.fiber.reset();
    mtx.cb=nullptr;
}

void IOManager::FdContext::triggerEvent(Event event)
{
    assert(events&event);
    events=(Event)(events&~event);
    EventContext& mtx=getEventContext(event);
    if(mtx.cb)
    {
        mtx.scheduler->scheduleLock(&mtx.cb);
    }
    else
    {
        mtx.scheduler->scheduleLock(&mtx.fiber);
    }
    resetEventContext(mtx);
    return;
}

IOManager::IOManager(size_t threads,bool usecaller,const std::string& name):Scheduler(threads, usecaller, name), TimerManager()
{
    m_epfd=epoll_create(5000);
    if(m_epfd < 0)
    {
        std::cerr << "epoll_create failed, errno = "
                  << errno << ", error = "
                  << strerror(errno) << std::endl;
        exit(1);
    }

    std::cout << "m_epfd = " << m_epfd << std::endl;

    int rt=pipe(m_tickleFds);
    assert(!rt);

    epoll_event event;
    event.events=EPOLLIN|EPOLLET;
    event.data.fd=m_tickleFds[0];

    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    assert(!rt);

    rt=epoll_ctl(m_epfd,EPOLL_CTL_ADD,m_tickleFds[0],&event);
    assert(!rt);

    contextResize(32);
    start();
}

IOManager::~IOManager()
{
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for(int i=0;i<m_fdContexts.size();i++)
    {
        if(m_fdContexts[i])
        delete m_fdContexts[i];
    }
}

void IOManager::contextResize(size_t size)
{
    m_fdContexts.resize(size);
    for(int i=0;i<size;i++)
    {
        if(m_fdContexts[i]==nullptr)
        {
            m_fdContexts[i]=new FdContext();
            m_fdContexts[i]->fd=i;
        }
    }
}

int IOManager::addEvent(int fd,Event event,std::function<void()>cb)
{
    FdContext* fd_ctx=nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if(m_fdContexts.size()>fd)
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        contextResize(fd*1.5);
        fd_ctx=m_fdContexts[fd];
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);
    if(fd_ctx->events&event)
    {
        return -1;
    }

    int op=fd_ctx->events?EPOLL_CTL_MOD:EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events=fd_ctx->events|event;
    epevent.data.ptr=fd_ctx;
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr<<"addEvent: epoll_ctl failed "<<strerror(errno)<<std::endl;
        return -1;
    }

    ++m_pendingEventCount;

    fd_ctx->events=(Event)(fd_ctx->events|event);

    FdContext::EventContext& event_ctx=fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler&&!event_ctx.fiber&&!event_ctx.cb);
    event_ctx.scheduler=Scheduler::getThis();
    if(cb)
    {
        event_ctx.cb.swap(cb);
    }
    else
    {
        event_ctx.fiber=Fiber::getThis();
        assert(event_ctx.fiber->getState()==Fiber::RUNNING);
    }
    return 0;
} 

bool IOManager::deleteEvent(int fd,Event event)
{
    FdContext* fd_ctx=nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if(m_fdContexts.size()>fd)
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;
    }

    std::unique_lock<std::mutex> lock(fd_ctx->mutex);
    if(!(fd_ctx->events&event))
    {
        return false;
    }

    Event new_events=(Event)(fd_ctx->events&~event);
    int op=new_events?EPOLL_CTL_MOD:EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events=new_events;
    epevent.data.ptr=fd_ctx;
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr<<"delEvent: epoll_ctl failed"<<std::endl;
        return false;
    }
    m_pendingEventCount--;
    fd_ctx->events=new_events;
    FdContext::EventContext& event_ctx=fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd,Event event)
{
    FdContext* fd_ctx=nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if(m_fdContexts.size()>fd)
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;
    }

    std::unique_lock<std::mutex> write_lock(fd_ctx->mutex);
    if(!(fd_ctx->events&event))
    {
        return false;
    }

    Event new_event=(Event)(fd_ctx->events&~event);
    int op=new_event?EPOLL_CTL_MOD:EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events=new_event;
    epevent.data.ptr=fd_ctx;
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr<<"cancelEvent: epoll_ctl failed"<<std::endl;
        return false;
    }
    m_pendingEventCount--;
    fd_ctx->triggerEvent(event);
    return true;
}

bool IOManager::cancelAll(int fd)
{
    FdContext* fd_ctx=nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if(m_fdContexts.size()>fd)
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;
    }

    std::unique_lock<std::mutex> write_lock(fd_ctx->mutex);
    if(!fd_ctx->events)
    {
        return false;
    }

    int op=EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events=0;
    epevent.data.ptr=fd_ctx;
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr<<"cancelEvent: epoll_ctl failed"<<std::endl;
        return false;
    }
    if(fd_ctx->events&READ)
    {
        m_pendingEventCount--;
        fd_ctx->triggerEvent(READ);
    }

    if(fd_ctx->events&WRITE)
    {
        m_pendingEventCount--;
        fd_ctx->triggerEvent(WRITE);
    }
    assert(fd_ctx->events==0);
    return true;
}

void IOManager::tickle()
{
    if(!hasIdleThreads())
    {
        return;
    }
    int rt=write(m_tickleFds[1],"T",1);
    assert(rt==1);
}

void IOManager::idle()
{
    static const uint64_t MAX_EVENTS=256;
    std::unique_ptr<epoll_event[]> Events(new epoll_event[MAX_EVENTS]);

    while(true)
    {
        if(stopping())
        {
            break;
        }

        int rt=0;
        while(true)
        {
            static const uint64_t MAX_TIMEOUT=5000;
            uint64_t next_timeout=getNextTimer();
            next_timeout=std::min(next_timeout,MAX_TIMEOUT);
            rt=epoll_wait(m_epfd,Events.get(),MAX_EVENTS,(int)next_timeout);
            if(rt<0&&errno==EINTR)
            {
                continue;
            }
            else
            {
                break;
            }
        }
        
        std::vector<std::function<void()>>cbs;
        listExpiredCb(cbs);
        if(!cbs.empty())
        {
            for(const auto& cb:cbs)
            {
                scheduleLock(cb);
            }
            cbs.clear();
        }

        for(int i=0;i<rt;i++)
        {
            epoll_event event=Events[i];
            if(event.data.fd==m_tickleFds[0])
            {
                uint8_t dummy[256];
                while(read(m_tickleFds[0],dummy,sizeof(dummy))>0);
                continue;
            }

            FdContext* fd_ctx=(FdContext*)event.data.ptr;
            std::lock_guard<std::mutex> lock(fd_ctx->mutex);

            if(event.events&(EPOLLERR|EPOLLHUP))
            {
                event.events |=(EPOLLIN|EPOLLOUT)&fd_ctx->events;
            }

            int real_events=NONE;
            if(event.events&EPOLLIN)
            {
                real_events|=READ;
            }
            if(event.events&EPOLLOUT)
            {
                real_events|=WRITE;
            }
            if((fd_ctx->events&real_events)==NONE)
            {
                continue;
            }

            int left_events=(fd_ctx->events&~real_events);
            int op=left_events?EPOLL_CTL_MOD:EPOLL_CTL_DEL;
            event.events=left_events;
            rt=epoll_ctl(m_epfd,op,fd_ctx->fd,&event);
            if(rt)
            {
                std::cerr<<"idle: epoll_ctl failed"<<std::endl;
                return;
            }

            if(real_events&READ)
            {
                fd_ctx->triggerEvent(READ);
                m_pendingEventCount--;
            }
            if(real_events&WRITE)
            {
                fd_ctx->triggerEvent(WRITE);
                m_pendingEventCount--;
            }
        }
        Fiber::getThis()->yield();
    }
}

bool IOManager::stopping()
{
    uint64_t timeout=getNextTimer();
    return timeout==~0ull&&m_pendingEventCount==0&&Scheduler::stopping();
}

void IOManager::OnTimerInsertedAtFront()
{
    tickle();
}

}
