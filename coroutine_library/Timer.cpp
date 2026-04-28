#include"Timer.h"

namespace sylar
{
    bool Timer::cancel()
    {
        std::unique_lock<std::shared_mutex> lock(m_manager->m_mutex);

        if(m_cb)
        {
            m_cb=nullptr;
        }
        else
        {
            return false;    
        }

        auto it=m_manager->m_timers.find(shared_from_this());
        if(it!=m_manager->m_timers.end())
        {
            m_manager->m_timers.erase(it);
        }
        return true;
   }

   bool Timer::refresh()
   {
        std::unique_lock<std::shared_mutex> lock(m_manager->m_mutex);

        if(!m_cb)
        {
            return false;
        }

        auto it=m_manager->m_timers.find(shared_from_this());
        if(it==m_manager->m_timers.end())
        {
            return false;
        }
        m_manager->m_timers.erase(it);
        m_next=std::chrono::system_clock::now()+std::chrono::milliseconds(m_ms);
        m_manager->m_timers.insert(shared_from_this());
        return true;
   }

   bool Timer::reset(uint64_t ms,bool from_now)
   {
        if(ms==m_ms&&!from_now)
        {
            return true;
        }

        {
            std::unique_lock<std::shared_mutex> lock(m_manager->m_mutex);

            auto it=m_manager->m_timers.find(shared_from_this());
            if(it!=m_manager->m_timers.end())
            {
                return false;
            }
            m_manager->m_timers.erase(it);
        }

        m_next=from_now?std::chrono::system_clock::now()+std::chrono::milliseconds(ms):m_next-std::chrono::milliseconds(ms-m_ms);
        m_ms=ms;
        m_manager->addTimer(shared_from_this());
        return true;
   }

   Timer::Timer(uint64_t ms,std::function<void()> cb,bool recurring,TimerManager* manager):m_ms(ms),m_cb(cb),m_recurring(recurring),m_manager(manager)
   {
        m_next=std::chrono::system_clock::now()+std::chrono::milliseconds(m_ms);
   }

   bool Timer::Comparator::operator()(const std::shared_ptr<Timer>lhs,const std::shared_ptr<Timer>rhs)const
   {
        return lhs->m_next<rhs->m_next;
   }

   TimerManager::TimerManager()
   {
        m_previouseTime=std::chrono::system_clock::now();
   }

   TimerManager::~TimerManager()
   {

   }

    std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms,std::function<void()>cb,bool recurring)
    {
        std::shared_ptr<Timer> timer(new Timer(ms, cb, recurring, this));
        addTimer(timer);
        return timer;
    }

    void TimerManager::addTimer(std::shared_ptr<Timer> timer)
    {
        bool at_front=false;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            auto it=m_timers.insert(timer).first;
            at_front=(it==m_timers.begin())&&!m_tickled;
            if(at_front)
            {
                m_tickled=true;
            }
        }

        if(at_front)
        {
            OnTimerInsertedAtFront();
        }
    }

    static void OnTimer(std::weak_ptr<void> weak_cond,std::function<void()>cb)
    {
        std::shared_ptr<void> tmp=weak_cond.lock();
        if(tmp)
        {
            cb();
        }
    }

    std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms,std::function<void()>cb,std::weak_ptr<void>weak_cond,bool recurring)
    {
        return addTimer(ms,std::bind(&OnTimer,weak_cond,cb),recurring);
    }

    uint64_t TimerManager::getNextTimer()
    {
        m_tickled=false;
        if(m_timers.empty())
        {
            return ~0ull;
        }

        auto now=std::chrono::system_clock::now();
        auto time=(*m_timers.begin())->m_next;
        if(now>=time)
        {
            return 0;
        }
        else
        {
            auto duration=std::chrono::duration_cast<std::chrono::milliseconds>(time-now);
            return static_cast<uint64_t>(duration.count());
        }
    }

    bool TimerManager::detectClockRollOver()
    {
        bool rollover=false;
        auto now=std::chrono::system_clock::now();
        if(now<(m_previouseTime-std::chrono::milliseconds(60*60*1000)))
        {
            rollover=true;
        }
        m_previouseTime=now;
        return true;
    }

    bool TimerManager::hasTimer()
    {
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        return !m_timers.empty();
    }

    void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs)
    {
        auto now=std::chrono::system_clock::now();
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);

        bool rollover=detectClockRollOver();

        while(!m_timers.empty()&&(rollover||((*m_timers.begin())->m_next<=now)))
        {
            auto it=*m_timers.begin();
            m_timers.erase(it);
            cbs.push_back(it->m_cb);
            if(it->m_recurring)
            {
                it->m_next=std::chrono::system_clock::now()+std::chrono::milliseconds(it->m_ms);
                addTimer(it);
            }
            else
            {
                it->m_cb=nullptr;
            }
        }
    }

    void TimerManager::OnTimerInsertedAtFront()
    {

    }
}