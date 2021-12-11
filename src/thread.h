#pragma once
#include <pthread.h>

struct Mutex{
	pthread_mutex_t mutex;

	Mutex(){
		mutex = PTHREAD_MUTEX_INITIALIZER;
	}

	int lock(){
		return pthread_mutex_lock(&mutex);
	}

	int unlock(){
		return pthread_mutex_unlock(&mutex);
	}
};

struct Cond{
	pthread_cond_t cond;

	Cond(){
		cond = PTHREAD_COND_INITIALIZER;
	}

	Cond(int timer){
		pthread_condattr_t attr;

		pthread_condattr_init(&attr);
		pthread_condattr_setclock(&attr, timer);
		pthread_cond_init(&cond, &attr);
	}

	int wait(Mutex& mutex){
		return pthread_cond_wait(&cond, &mutex.mutex);
	}

	int wait(Mutex& mutex, timespec& abstime){
		return pthread_cond_timedwait(&cond, &mutex.mutex, &abstime);
	}

	int signal(){
		return pthread_cond_signal(&cond);
	}

	int broadcast(){
		return pthread_cond_broadcast(&cond);
	}
};

struct Thread{
	typedef void (*start_func)(void*);

	static void* start(void* thread){
		Thread* t = (Thread*)thread;

		t -> func(t -> arg);

		return nullptr;
	}

	pthread_t thread;
	start_func func;
	void* arg;

	Thread(){
		func = nullptr;
		arg = nullptr;
	}

	Thread(start_func f, void* a){
		func = f;
		arg = a;
	}

	int start(){
		return pthread_create(&thread, nullptr, start, this);
	}

	int detach(){
		return pthread_detach(thread);
	}

	int join(){
		return pthread_join(thread, nullptr);
	}
};