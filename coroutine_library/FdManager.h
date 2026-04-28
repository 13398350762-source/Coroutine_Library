#pragma once
#include"Thread.h"
#include<shared_mutex>
#include<memory>

namespace sylar
{

class FdCtx
{
private:
    bool m_isInit=false;
    bool m_isSocket=false;
    bool m_sysNonblock=false;
    bool m_userNonblock=false;
    bool m_isClosed=false;
    int m_fd;
    uint64_t m_recvTimeout=(uint64_t)-1;
    uint64_t m_sendTimeout=(uint64_t)-1;
public:
    FdCtx(int fd);
    ~FdCtx();

    bool init();
    bool isInit(){return m_isInit;}
    bool isSocket(){return m_isSocket;}
    bool isClosed(){return m_isClosed;}

    void setSysNonBlock(bool v){m_sysNonblock=v;}
    bool getSysNonBlock(){return m_sysNonblock;}
    
    void setUserNonBlock(bool v){m_userNonblock=v;}
    bool getUserNonBlock(){return m_userNonblock;}

    void setTimeout(int type,uint64_t time);
    uint64_t getTimeout(int type);
};

class FdManager
{
public:
    FdManager();
    std::shared_ptr<FdCtx> get(int fd,bool auto_create=false);
    void del(int fd);
private:
    std::shared_mutex m_mutex;
    std::vector<std::shared_ptr<FdCtx>> m_datas;
};

template<typename T>
class Sigleton
{
private:
    static T* instance;
    static std::mutex mutex;
    Sigleton();
    ~Sigleton();
    Sigleton(const Sigleton&)=delete;
    Sigleton& operator=(const Sigleton&)=delete;
public:
    static T* getInstance()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(instance==nullptr)
        {
            instance=new T();
        }
        return instance;
    }

    static void destroyInstance()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(instance)
        {
            delete instance;
            instance=nullptr;
        }
    }

};

using FdMgr=Sigleton<FdManager>;

}