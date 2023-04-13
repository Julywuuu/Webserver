#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include "locker.h"
#include <list>
#include<exception>
#include<cstdio>

// 线程池
template<typename T>
class threadpool
{
private:
    /* data */
    
    int m_thread_number;            // 线程的数量
    pthread_t * m_threads;          // 线程池数组

    int m_max_requests;             // 等待队列最大值
    std::list<T*> m_workqueue;      // 等待队列

    locker m_queuelocker;           // 互斥锁

    sem m_queuestat;                // 等待队列的信号量

    bool m_stop;                    // 是否停止线程
public:
    threadpool(int m_thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);
    void run();
private:
    static void* worker(void* args);    //创建线程时使用的第三个参数，必须是静态变量
};


// 构造函数
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL) {
    // 参数必须大于0
    if(thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    // 创建线程池数组资源池, 并将线程都设置脱离
    m_threads = new pthread_t[thread_number];
    if(!m_threads) {
        throw std::exception();
    }
    for(int i=0; i<thread_number; i++) {
        printf("creating the %d th thread!\n", i);
        if(pthread_create(m_threads+i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0) {  
            delete[] m_threads;
            throw std::exception();
        }     
    }

    // 创建等待队列
    // std::list<T> m_workqueue(max_requests);              不需要初始化！list有自己的构造函数

}

// 析构函数
template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    // 需要加锁，因为等待队列被所有的线程共享
    m_queuelocker.lock();
    // 等待队列满了
    if(this->m_workqueue.size() >= this->m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    // 没满的话
    this->m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();         // 信号量+1 表示等待队列中的成员+1
    return true;
}

template<typename T> 
void* threadpool<T>::worker(void* args){
    threadpool* pool = (threadpool*) args;
    pool->run();
    return pool;
}


// 启动线程池
template<typename T> 
void threadpool<T>::run() {
    // 是否停止
    while(!m_stop) {
        // 等待队列信号量如果为0，就阻塞、 在此期间，CPU可以运行其他可执行的线程或进程
        // 这里wait必须在lock之前：类似生产消费模型，如果队列中为空了，run先上锁，会导致阻塞在这里。也没法append进新的任务。形成死锁！
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        // 从等待队列中取出要运行的成员，取完就给队列解锁
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request) {
            continue;
        }
        // 调用运行成员要处理的程序
        request->process();
    }
}


#endif