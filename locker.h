#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 互斥锁
class locker
{
private:
    /* data */
    pthread_mutex_t m_mutex;
public:
    locker(/* args */);
    ~locker();
    bool lock();
    bool unlock();
    pthread_mutex_t get();
};

locker::locker(/* args */)
{   
    int ret = pthread_mutex_init(&this->m_mutex, NULL);
    if(ret != 0) {
        throw std::exception();
    }
}

locker::~locker()
{
    int ret = pthread_mutex_destroy(&this->m_mutex);
    if(ret != 0) {
        throw std::exception();
    }
}

bool locker::lock(){
    return pthread_mutex_lock(&this->m_mutex) == 0;
}

bool locker::unlock(){
    return pthread_mutex_unlock(&this->m_mutex) == 0;
}

pthread_mutex_t locker::get(){
    return this->m_mutex;
}

// 条件变量
class cond{
private: 
    pthread_cond_t p_cond;
public:
    cond();
    ~cond();
    bool wait(pthread_mutex_t* mutex);
    bool timewait(pthread_mutex_t *mutex, const struct timespec *abstime);
    bool signal();
    bool broadcast();
};

cond::cond(){
    pthread_cond_init(&p_cond, NULL);
    if(pthread_cond_init(&p_cond, NULL) != 0){
        throw std::exception();
    }
}
cond::~cond(){
    pthread_cond_destroy(&p_cond);
}

bool cond::wait(pthread_mutex_t* mutex){
    return pthread_cond_wait(&p_cond, mutex) == 0;
}

bool cond::timewait(pthread_mutex_t *mutex, const struct timespec* abstime){
    return pthread_cond_timedwait(&p_cond, mutex, abstime) == 0;
}

bool cond::signal(){
    return pthread_cond_signal(&p_cond) == 0;
}

bool cond::broadcast(){
    return pthread_cond_broadcast(&p_cond) == 0;
}

// 信号量
class sem{
private:
    sem_t m_sem;
public:
    sem();
    sem(int num);
    ~sem();
    bool wait();
    bool timedwait(const struct timespec *abs_timeout);
    bool post();
};

sem::sem(){
    if(sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();
    }
}
sem::sem(int num) {
    if(sem_init(&m_sem, 0, num) != 0){
        throw std::exception();
    }
}

sem::~sem(){
    sem_destroy(&m_sem);
}

bool sem::wait() {
    return sem_wait(&m_sem) == 0;
}

bool sem::timedwait(const struct timespec *abs_timeout) {
    return sem_timedwait(&m_sem, abs_timeout) == 0;
}

bool sem::post(){
    return sem_post(&m_sem) == 0;
}

#endif