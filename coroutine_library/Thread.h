#pragma once
#include<pthread.h>
#include<iostream>
#include<mutex>
#include<condition_variable>
#include<string>
#include<functional>

namespace sylar
{

class Semaphore
{
private:
    std::mutex mtx_;
    std::condition_variable cv;
    int count_;
public:
    explicit Semaphore(int count = 0):count_(count){}
    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        while(count_<=0)
        {
            cv.wait(lock);
        }
        count_--;
    }
    void signal()
    {   std::unique_lock<std::mutex> lock(mtx_);
        count_++;
        cv.notify_one();    
    }
};

class Thread
{
public:
    Thread(std::function<void()>cb,const std::string&name);
    ~Thread();

    pid_t getId() const { return m_id; };
    const std::string& getName() const { return m_name; };

    void join();
public:
    static pid_t GetThreadId();

    static pthread_t GetThis();

    static const std::string& GetName();

    static void setName(const std::string& name);

private:
    static void* run(void* arg);
private:
    pid_t m_id;
    pthread_t m_thread;    
    std::function<void()> m_cb;
    std::string m_name;
    Semaphore m_semaphore;
};

}