#ifndef _LOKER_H
#define _LOKER_H

#include <iostream>
#include <stdio.h>
#include <semaphore.h>

using namespace std;

class sem_loker {
private:
	sem_t m_sem;
	
public:
	sem_loker(void) {
		if(sem_init(&m_sem, 0, 0) != 0)
			printf("semaphore init faild!\n");
	}
	~sem_loker(void) {
		sem_destroy(&m_sem);
	}
	
	bool wait(void) {
		return (sem_wait(&m_sem) == 0);
	}
	
	bool add(void) {
		return (sem_post(&m_sem) == 0);
	}
};

class mutex_locker {
private:
	pthread_mutex_t m_mutex;
	
public:
	mutex_locker(void) {
		if(pthread_mutex_init(&m_mutex, NULL) != 0)
			printf("pthread_mutex_init faild\n");
	}
	~mutex_locker(void) {
		pthread_mutex_destroy(&m_mutex);
	}
	
	bool mutex_lock(void) {
		return (pthread_mutex_lock(&m_mutex) == 0);
    }
    bool mutex_unlock(void) {
		return (pthread_mutex_unlock(&m_mutex) == 0);
    }
};

class cond_locker {
private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
	
public:
	cond_locker(void) {
		if(pthread_mutex_init(&m_mutex, NULL) != 0)
			printf("mutex init error");
		if(pthread_cond_init(&m_cond, NULL) != 0)
		{   //条件变量初始化是被，释放初始化成功的mutex
			pthread_mutex_destroy(&m_mutex);
			printf("cond init error");
		}
	}
	~cond_locker() {
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cond);
    }
	
	bool wait(void) {
		int res = 0;
		
		pthread_mutex_lock(&m_mutex);
		res = pthread_cond_wait(&m_cond, &m_mutex);
		pthread_mutex_unlock(&m_mutex);
		
		return (res == 0);
	}
	bool signal(void) {
		return (pthread_cond_signal(&m_cond) == 0);
	}
	//唤醒所有等待条件的线程
	bool broadcast(void) {
		return (pthread_cond_broadcast(&m_cond) == 0);
	}
};





#endif