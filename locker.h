#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h> 
#include<exception>
#include<semaphore.h>

//线程同步机制封装类

//互斥锁
class locker{
public:
    locker(){ //构造
        if(pthread_mutex_init(&m_mutex,NULL)!=0){
            throw std::exception(); //初始化失败则抛出异常
        }
    }

    ~locker(){ //析构
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){ //封装，判断是否已上锁
        return pthread_mutex_lock(&m_mutex)==0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }

    pthread_mutex_t * get(){
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


//条件变量类
class cond{
public:
    cond(){//构造
        if(pthread_cond_init(&m_cond,NULL)!=0){
            throw std::exception(); //初始化失败则抛出异常
        }
    }

    ~cond(){
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *mutex){//⭐这里不太一样，采用的视频里面的写法
        return pthread_cond_wait(&m_cond,mutex) == 0;
    }
   
    bool timewait(pthread_mutex_t *mutex,struct timespec t){ //⭐这里不太一样
        return pthread_cond_timedwait(&m_cond,mutex,&t) == 0;
    }

    bool signal(){//唤醒单个线程
        return pthread_cond_signal(&m_cond)==0;
    }

    bool broadcast(){ //唤醒所有线程
        return pthread_cond_broadcast(&m_cond)==0;
    }

private:
    pthread_cond_t  m_cond;
};

//信号量类
class sem{
public:
    sem(){
        if (sem_init(&m_sem,0,0)!=0){ //设置初始值为0，第二个参数0表示是否与其他进程共享
            throw std::exception();
        }
    }

    ~sem(){
        sem_destroy(&m_sem);
    }

    bool wait(){ //等待信号量
        return sem_wait(&m_sem)==0;
    }

    bool post(){ //增加信号量
        return sem_post(&m_sem)==0;
    }

private:
    sem_t m_sem;
};

#endif