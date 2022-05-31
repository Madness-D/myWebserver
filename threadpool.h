#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include"locker.h"
#include<cstdio>
#include<exception>

//线程池类，定义为模板，提高复用性
//T为任务类
template<typename T>
class threadpool{
public:
    threadpool(int thread_number=8,int max_requests=10000);
    ~threadpool();
    bool append(T* request);//添加任务

private:
/*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();

private:
    //线程池数量 
    int m_thread_number;

    //线程池数组，大小为m_thread_number
    pthread_t * m_threads;

    //请求队列允许的最大待处理请求数
    int m_max_requests;

    //请求队列, 存放任务
    std::list< T*> m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量，用于判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};

//构造函数
template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(NULL) {

        if(thread_number<=0 || max_requests<=0) {
            throw std::exception();
        }

        //申请创建线程池数组
        m_threads=new pthread_t[m_thread_number];
        if(!m_threads){
            throw std::exception();
        }

        //创建thread_number个线程，并设置为脱离线程
        for(int i=0;i<thread_number;++i){
            printf("create the %dth thread\n",i);
            if(pthread_create(m_threads+i,NULL,worker,this)!=0){
                delete []m_threads; 
                throw std::exception();
            }
            
            if(pthread_detach(m_threads[i])){
                delete [] m_threads;
                throw std::exception();
            }
        }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop=true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    //需要对队列上锁，因为该队列被所有线程共享
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){ //超出最大请求数，加不进去了
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool=(threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){

    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        request->process();
    }
}

#endif