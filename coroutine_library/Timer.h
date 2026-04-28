#include<vector>
#include<mutex>
#include<set>
#include<memory>
#include<functional>
#include<assert.h>
#include<shared_mutex>

namespace sylar
{

class TimerManager;

class Timer:public std::enable_shared_from_this<Timer>
{
    friend class TimerManager;
public:
    

    bool cancel();

    bool refresh();

    bool reset(uint64_t ms,bool from_now);
private:
    Timer(uint64_t ms,std::function<void()> cb,bool recurring,TimerManager* manager);
private:
    uint64_t m_ms;
    std::function<void()> m_cb;
    bool m_recurring;
    TimerManager* m_manager;
    std::chrono::time_point<std::chrono::system_clock> m_next;
private:
    struct Comparator
    {
        bool operator()(const std::shared_ptr<Timer>lhs,const std::shared_ptr<Timer>rhs)const;
    };
};

class TimerManager
{
    friend class Timer;
public:
    
    TimerManager();
    virtual ~TimerManager();
    std::shared_ptr<Timer> addTimer(uint64_t ms,std::function<void()>cb,bool recurring=false);
    std::shared_ptr<Timer> addConditionTimer(uint64_t ms,std::function<void()>cb,std::weak_ptr<void>weak_cond,bool recurring=false);
    uint64_t getNextTimer();
    void listExpiredCb(std::vector<std::function<void()>>& cbs);
    bool hasTimer();
protected:
    virtual void OnTimerInsertedAtFront();
    void addTimer(std::shared_ptr<Timer> timer);
private:
    bool detectClockRollOver();
private:
    std::shared_mutex m_mutex;
    std::set<std::shared_ptr<Timer>,Timer::Comparator> m_timers;
    bool m_tickled=false;
    std::chrono::time_point<std::chrono::system_clock> m_previouseTime;
};

};