#ifndef _THREAD_POOL
#define _THREAD_POOL

#include <iostream>
#include <queue>
#include <exception>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include "locker.h"

using namespace std;

/*线程池中的线程在任务队列为空时等待任务的到来，有任务时则依次获取任务*/


template<class type>
class threadpool {
private:
	int thread_num; 
	pthread_t *all_threads;  //线程id数组指针
	queue<type *> task_que;  //任务/线程队列
	mutex_locker que_mutex_locker;  //互斥锁
	cond_locker que_cond_locker;    //条件变量
	bool is_stop;
	
public:
	threadpool(int _thread_num = 20);
	~threadpool(void);
	
	bool append_task(type *_task);  //添加任务
	void start(void);  //线程池运行函数
	void stop(void);
	
private:
	static void *worker(void *_arg);  //thread work function
	void run(void);  //线程运行函数
	type *getTask(void);
};

template<class type>
threadpool<type>::threadpool(int _thread_num) : thread_num(_thread_num), is_stop(false), all_threads(NULL) {
	if(thread_num <= 0)
		cout << "tnread_num == 0!\n";
	
	all_threads = new pthread_t[thread_num];
	if(all_threads == NULL)
		cout << "create pthread_t[] error!\n";
}

template<class type>
threadpool<type>::~threadpool(void) {
	delete[] all_threads;
	stop();
}

template<class type>
void threadpool<type>::stop(void) {
	is_stop = true;
	que_cond_locker.broadcast();
}

template<class type>
void threadpool<type>::start(void) {
	for(int i = 0; i<thread_num; ++i) {
		if(pthread_create(all_threads+i, NULL, worker, this) != 0) {
			delete[] all_threads;  //创建线程失败，释放所有资源并抛出异常
			throw exception();
		}
		
		if(pthread_detach(all_threads[i])) {
			delete[] all_threads;  //脱离线程失败，释放所有资源并抛出异常
			throw exception();
		}
	}
}

template<class type>
bool threadpool<type>::append_task(type *task) {
	bool is_empty = task_que.empty();
	
	//添加任务/线程进入队列
	que_mutex_locker.mutex_lock();
	//cout << "thread_num: " << thread_num << endl;
	if(task_que.size() == thread_num)
		return false;
	task_que.push(task);
	que_mutex_locker.mutex_unlock();
	//为空时则唤醒一个等待任务的线程
	if(is_empty)
		que_cond_locker.signal();
	
	return true;
}

template<class type>
void *threadpool<type>::worker(void *_arg) {
	threadpool *pool = (threadpool *)_arg;
	pool->run();
	return pool;
}

template<class type>
type* threadpool<type>::getTask(void) {
	type *task = NULL;
	
	que_mutex_locker.mutex_lock();
	if(!task_que.empty()) {
		task = task_que.front();
		task_que.pop();
	}
	que_mutex_locker.mutex_unlock();
	
	return task;
}

template<class type>
void threadpool<type>::run(void) {
	while(!is_stop) {
		//等待任务的到来
		type *task = getTask();
		if(task == NULL)  
			que_cond_locker.wait();
		else {
			task->doit();
			delete task;  //在main函数中new
		}
	}
}


#endif