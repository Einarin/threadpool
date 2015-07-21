/*
Copyright (c) 2015 Johnny Dickinson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <memory>
#include <queue>
#include <vector>
#include <functional>
#include <utility>
#include <iostream>
#include "Semaphore.h"

//use Standard library thread when available
#define USE_STD_THREAD

//Visual Studio before 2012 doesn't have std::thread
#ifdef _MSC_VER
#if _MSC_VER < 1700
#undef USE_STD_THREAD
#endif
//no version of MSVC has thread_local
#define thread_local __declspec(thread)
#endif

//GCC < 4.8 doesn't have thread_local
#ifdef __GNUC__
#if __GNUC__ < 4 || __GNUC__ == 4 && __GNUC_MINOR__ < 8
#define thread_local __thread
#endif
#endif

#ifdef USE_STD_THREAD
#include <thread>
#include <mutex>
#define MUTEX std::unique_ptr<std::mutex>
#define NEW_MUTEX(m) m = std::unique_ptr<std::mutex>(new std::mutex)
#define ACQUIRE_MUTEX(m) m->lock()
#define TRY_MUTEX(m) m->try_lock()
#define RELEASE_MUTEX(m) m->unlock()
#define THREAD_ID thread::id
#define LPVOID void*
using std::thread;
#else
//no STL support for concurrency
#ifdef _WIN32
#define WIN32_CONCURRENCY
#include "windows.h"
#define MUTEX CRITICAL_SECTION
//#define NEW_MUTEX CreateMutex(NULL,FALSE,NULL)
#define NEW_MUTEX(m) InitializeCriticalSection(&m)
//#define ACQUIRE_MUTEX(m) while(WaitForSingleObject(m,INFINITE)!=0) { }
#define ACQUIRE_MUTEX(m) EnterCriticalSection(&m)
//#define TRY_MUTEX(m) 0==WaitForSingleObject(m,10)
#define TRY_MUTEX(m) TryEnterCriticalSection(&m)
//#define RELEASE_MUTEX(m) ReleaseMutex(m)
#define RELEASE_MUTEX(m) LeaveCriticalSection(&m)
#define THREAD_ID DWORD
typedef HANDLE thread;
#endif
#endif

template<typename T>
class Future{
protected:
	struct Data{
	    Semaphore s;
	    bool complete;
	    T result;
	    Data():complete(false),s(0){
	    }
	};
	std::shared_ptr<Data> data;
public:
	Future():data(new Data())
	{}
	inline void set(T val){
		data->result = val;
		data->complete = true;
		data->s.post();
	}
	inline T operator=(T val){
		set(val);
		return val;
	}
	inline bool isDone(){
		return data->complete;
	}
	inline T wait(){
		while(!data->s.wait())
		{}
		data->s.post();
		return data->result;
	}
	inline operator T(){
		return wait();
	}
};

template<typename T>
class Future<Future<T>>{
	struct Data{
		Semaphore s;
		bool m_set;
		Future<T> child;
		Data():m_set(false),s(0){
		}
	};
	std::shared_ptr<Data> data;
public:
	Future():data(new Data)
	{}
	inline bool isDone(){
		return data->m_set && data->child.isDone();
	}
	inline void set(const Future<T>& result)
	{
		data->child = result;
		data->m_set = true;
		data->s.post();
	}
	inline Future<T> wait(){
		while(!data->s.wait())
		{}
		data->s.post();
		return data->child;
	}
	inline operator Future<T>(){
		return wait();
	}
};

template<>
class Future<void>{
	struct Data{
	Semaphore s;
	bool complete;
	Data():complete(false),s(0){
	}
	};
	std::shared_ptr<Data> data;
public:
	Future():data(new Data())
	{}
	inline bool isDone(){
		return data->complete;
	}
	inline void set(void)
	{
		data->complete = true;
		data->s.post();
	}
	inline void wait(){
		while(!data->s.wait())
		{}
		data->s.post();
	}
};

struct WorkerThreadData{
public:
	thread mthread;
	THREAD_ID threadId;
	MUTEX mutex;
	bool blocked;
	std::queue<std::function<void()>> workData; 
	WorkerThreadData(): blocked(false){
		NEW_MUTEX(mutex);
	}
#if _MSC_VER <= 1800
	WorkerThreadData(WorkerThreadData&& rhs) : mthread(std::move(rhs.mthread))
												   , threadId(std::move(rhs.threadId))
												   , mutex(std::move(rhs.mutex))
												   , blocked(std::move(rhs.blocked))
												   , workData(std::move(rhs.workData))
	{	}
#else
	WorkerThreadData(WorkerThreadData&& rhs) = default;
#endif
#if _MSC_VER < 1700
private:
	WorkerThreadData(const WorkerThreadData & rhs);
#else
	WorkerThreadData(const WorkerThreadData & rhs) = delete;
#endif
};

struct DispatchData{
	std::vector<WorkerThreadData> workerThreads;
	std::queue<std::function<void()>> dispatchQueue;
	MUTEX dispatchMutex;
	Semaphore dispatchSemaphore;
	bool stop;
	DispatchData():dispatchSemaphore(0)
	{}
};

template<typename T>
#ifdef USE_STD_THREAD
void awaitWorkerThreadProc(void* lpParameter){
#else
#ifdef _WIN32
DWORD WINAPI awaitWorkerThreadProc(LPVOID lpParameter){
#else
#error "Need thread invocation signature"
#endif
#endif
	std::pair<std::shared_ptr<DispatchData>,Future<T>>* stuff = (std::pair<std::shared_ptr<DispatchData>,Future<T>>*)lpParameter;
	//DispatchData* data = stuff->first;
	while(true/*!stuff->second.complete()*/){
		ACQUIRE_MUTEX(stuff->first->dispatchMutex);
		if(!stuff->first->dispatchQueue.empty()){
			std::function<void()> workUnit = stuff->first->dispatchQueue.front();
			stuff->first->dispatchQueue.pop();
			RELEASE_MUTEX(stuff->first->dispatchMutex);
			workUnit();
		} else {
			RELEASE_MUTEX(stuff->first->dispatchMutex);
			break; //no work to do, let this thread die
		}	
	}
	delete stuff;
#ifndef USE_STD_THREAD
	return 0;
#endif
}

class ThreadPool{
private:
	std::shared_ptr<DispatchData> sharedState;
public:
	ThreadPool();
	ThreadPool(int numberOfThreads);
	~ThreadPool();
	bool working(){
		return sharedState->dispatchQueue.size() > 0;
	}
	void async(std::function<void()> workUnit);
	template<typename T>
	Future<T> async(std::function<T()> func){
		Future<T> f;
		async([f,func]()mutable{
			f.set(func());
		});
		return f;
	}
	/*template<typename T>
	Future<Future<T>> async(std::function<Future<T>()> func){
		Future<T> f;
		queue([f,func]()mutable{
			Future<T> result = func();
			f.set(result);
		});
		return f;
	}*/
	template<typename T>
	T await(Future<T> result){
		static thread_local int depth;
		depth++; //track recursion depth
		//TODO: put upper bound on recursion depth to prevent stack overflow (doesn't work yet)
		if(depth >=100){ 
			std::cout << "Warning: worker thread stack has become excessive" << std::endl;
		}
			while(!result.isDone() && !sharedState->stop){
				//do some other work while we wait	
				if(TRY_MUTEX(sharedState->dispatchMutex)){
					if(!sharedState->dispatchQueue.empty()){
						std::function<void()> workUnit = sharedState->dispatchQueue.front();
						sharedState->dispatchQueue.pop();
						RELEASE_MUTEX(sharedState->dispatchMutex);
						workUnit();
					} else {
						RELEASE_MUTEX(sharedState->dispatchMutex);
					}
				}
			}
		/*} else {
			std::cout << "Warning: worker thread stack has become excessive" << std::endl;
			//spawn a new worker thread so we don't blow out our stack
			//auto data = make_pair(&sharedState,result);
			std::pair<std::shared_ptr<DispatchData>,Future<T>>* data = new std::pair<std::shared_ptr<DispatchData>,Future<T>>;
			data->first = sharedState;
			data->second = result;
			#ifdef WIN32_CONCURRENCY
				thread worker = CreateThread(NULL,0,awaitWorkerThreadProc<T>,(LPVOID)data,0,NULL);
			#else
				thread worker(awaitWorkerThreadProc<T>,(LPVOID)data);
				worker.detach();
			#endif
			//block on the return value rather than the other thread
			//WaitForSingleObject(thread,INFINITE);
		}*/
		depth--;
		//because result.done() is true it should be done now
		return result.wait();
	}
};

template<>
Future<void> ThreadPool::async<void>(std::function<void()> func);

class WorkQueue{
protected:
	std::queue<std::function<void()>> workQueue;
	MUTEX queueMutex;
public:
	WorkQueue();
	void async(std::function<void()> workUnit);
	template<typename T>
	Future<T> async(std::function<T()> func){
		Future<T> f;
		async([f,func]()mutable{
			f.set(func());
		});
		return f;
	}

	template<typename T>
	T await(Future<T> result) {
		while (!result.isDone()) {
			processQueueUnit();
		}
		return result.wait();
	}
	
	bool processQueueUnit(); //returns true if work was done
};

template<>
Future<void> WorkQueue::async<void>(std::function<void()> func);

#ifdef TEST

int fib(ThreadPool& pool, int x){
	if(x < 2) {
		return 1;
	}
	return pool.await(pool.async<int>([&pool,x]()->int{return fib(pool, x-1);}))
		+ pool.await(pool.async<int>([&pool,x]()->int{return fib(pool, x-2);}));
}

bool testThreadpool(std::ostream& out){
	WorkQueue mainQueue;
	ThreadPool pool1(1);
	ThreadPool pool2(32);

	out << "Testing thread pool creation and deletion" << std::endl;
	ThreadPool* pool3 = new ThreadPool();
	delete pool3;
	MUTEX blockStart;
	NEW_MUTEX(blockStart);
	ACQUIRE_MUTEX(blockStart);
	int count = 0;
	int count2 = 0;
	bool success = true;
	out << "test serial execution and basic use of Futures" << std::endl;
	Future<bool> result1 = pool1.async<bool>([&blockStart,&out,&count]()->bool{
		ACQUIRE_MUTEX(blockStart);
		out << "result 1" << std::endl;
		count++;
		RELEASE_MUTEX(blockStart);
		return count == 1;
	});
	pool1.async([&out,&count2](){
		out << "result 2" << std::endl;
		count2++;
	});
	Future<void> result3 = pool1.async<void>([&out,&count2](){
		out << "result 3" << std::endl;
		count2++;
	});
	success &= (count == 0);
	success &= (count2 == 0);
	out << "start: " << (success ? "true" : "false") << std::endl;
	RELEASE_MUTEX(blockStart);
	success &= result1.wait();
	success &= (count == 1);
	result3.wait();
	success &= (count2 == 2);
	out << "test parallel execution with an abusive workload and implicit conversion of Futures" << std::endl;
	const int fibDepth = 20;
	Future<int> output[fibDepth];
	for (int i = 0; i < fibDepth; i++) {
		output[i] = pool2.async<int>([&pool2, i]()->int {return fib(pool2, i); });
	}
	int correctVals[] = { 1,1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181,6765};
	for (int i = 0; i < fibDepth; i++) {
		//out << "status: " << (success ? "true" : "false") << std::endl;
		out << i << " = " << /*pool2.await(output[i])*/output[i] << std::endl;
		success &= (output[i].wait() == correctVals[i]);
	}

	out << "test WorkQueue and nested Futures" << std::endl;
	int val = 0;
	Future<Future<bool>> result = pool2.async<Future<bool>>([&]()->Future<bool>{
		return mainQueue.async<bool>([&](){
			val++;
			return true;
		});
	});
	success &= mainQueue.await(result);
	success = (val == 1);

	out << "finish: " << (success ? "true" : "false") << std::endl;
	return success;
}
#endif
