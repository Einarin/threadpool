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

#include "ThreadPool.h"

//global threadpools
ThreadPool CpuPool;
ThreadPool IoPool;
WorkQueue glQueue;

//black magic to name threads in Visual Studio
#ifdef WIN32 
#ifdef _DEBUG
#include <sstream>
const DWORD MS_VC_EXCEPTION=0x406D1388;
int workernum = 0;
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
   DWORD dwType; // Must be 0x1000.
   LPCSTR szName; // Pointer to name (in user addr space).
   DWORD dwThreadID; // Thread ID (-1=caller thread).
   DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

void SetThreadName( DWORD dwThreadID, const char* threadName)
{
   THREADNAME_INFO info;
   info.dwType = 0x1000;
   info.szName = threadName;
   info.dwThreadID = dwThreadID;
   info.dwFlags = 0;

   __try
   {
      RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
   }
   __except(EXCEPTION_EXECUTE_HANDLER)
   {
   }
}
#endif
#endif

//DWORD WINAPI DispatchThreadProc(LPVOID lpParameter);
#ifdef USE_STD_THREAD
void WorkerThreadProc(void* lpParameter){
#else
DWORD WINAPI WorkerThreadProc(LPVOID lpParameter){
#endif
	std::shared_ptr<DispatchData>* ptr = (std::shared_ptr<DispatchData>*)lpParameter;
	std::shared_ptr<DispatchData> data = *ptr;
	bool run = true;
	while(run){
		ACQUIRE_MUTEX(data->dispatchMutex);
		if(!data->dispatchQueue.empty()){
			std::function<void()> workUnit = data->dispatchQueue.front();
			data->dispatchQueue.pop();
			RELEASE_MUTEX(data->dispatchMutex);
			workUnit();
		} else {
			//only check if thread should end if there's no work to do
			run = !data->stop;
			RELEASE_MUTEX(data->dispatchMutex);
			if(run){
				data->dispatchSemaphore.wait();
			}
		}
		
	}
#ifndef USE_STD_THREAD
	return 0;
#endif
}

template<>
Future<void> ThreadPool::async<void>(std::function<void()> func){
	Future<void> f;
	async([f,func]()mutable{
		func();
		f.set();
	});
	return f;
}

ThreadPool::ThreadPool():sharedState(new DispatchData){
#ifdef _WIN32
	SYSTEM_INFO sysinfo;
	GetSystemInfo( &sysinfo );

	int numberOfThreads = sysinfo.dwNumberOfProcessors;
#else
	int numberOfThreads = std::thread::hardware_concurrency();
	numberOfThreads = numberOfThreads == 0 ? 4 : numberOfThreads;
#endif
	sharedState->stop = false;
	sharedState->workerThreads.resize(numberOfThreads);

	NEW_MUTEX(sharedState->dispatchMutex);
	//sharedState->dispatchSemaphore = CreateSemaphore(NULL,0,numberOfThreads,NULL);

	for(int i=0;i<numberOfThreads;i++)
	{
		WorkerThreadData& current = sharedState->workerThreads[i];
		#ifdef _DEBUG
			std::stringstream ss;
			ss << "Thread Pool Worker " << workernum++;
			std::string tmp = ss.str();
		#endif
		#ifndef USE_STD_THREAD
			current.mthread = CreateThread(NULL,0,WorkerThreadProc,(LPVOID)&sharedState,0,&current.threadId);
			
#ifdef _DEBUG
			SetThreadName(current.threadId,tmp.c_str());
			#endif
		#else
			current.mthread = thread(WorkerThreadProc,(void*)new std::shared_ptr<DispatchData>(sharedState));
			current.threadId = current.mthread.get_id();
			current.mthread.detach();
			#ifdef _DEBUG
				SetThreadName(GetThreadId(current.mthread.native_handle()), tmp.c_str());
			#endif
		#endif
	}
	//sharedState.roundRobinIndex=0;
	
	//sharedState.dispatchSemaphore = CreateSemaphore(NULL,0,
	//sharedState.queuingThread = CreateThread(NULL,0,DispatchThreadProc,(LPVOID)&sharedState,0,NULL);
}

ThreadPool::ThreadPool(int numberOfThreads):sharedState(new DispatchData){
	sharedState->stop = false;
	sharedState->workerThreads.resize(numberOfThreads);

	NEW_MUTEX(sharedState->dispatchMutex);
	//sharedState->dispatchSemaphore = CreateSemaphore(NULL,0,numberOfThreads,NULL);

	for(int i=0;i<numberOfThreads;i++)
	{
		WorkerThreadData& current = sharedState->workerThreads[i];
		#ifdef _DEBUG
			std::stringstream ss;
			ss << "Thread Pool Worker " << workernum++;
			std::string tmp = ss.str();
		#endif
		#ifndef USE_STD_THREAD
			current.mthread = CreateThread(NULL,0,WorkerThreadProc,(LPVOID)&sharedState,0,&current.threadId);
			
			#ifdef _DEBUG
				SetThreadName(current.threadId,tmp.c_str());
			#endif
		#else
			current.mthread = thread(WorkerThreadProc,(void*)new std::shared_ptr<DispatchData>(sharedState));
			current.threadId = current.mthread.get_id();
			current.mthread.detach();
			#ifdef _DEBUG
				SetThreadName(GetThreadId(current.mthread.native_handle()), tmp.c_str());
			#endif
		#endif
	}
	//sharedState.roundRobinIndex=0;
	
	//sharedState.dispatchSemaphore = CreateSemaphore(NULL,0,
	//sharedState.queuingThread = CreateThread(NULL,0,DispatchThreadProc,(LPVOID)&sharedState,0,NULL);
	
}

ThreadPool::~ThreadPool(){
	int numThreads = sharedState->workerThreads.size();
	ACQUIRE_MUTEX(sharedState->dispatchMutex);
	sharedState->stop = true; //tell the worker threads to stop
	///ReleaseSemaphore(sharedState->dispatchSemaphore,numThreads,NULL); //wake all worker threads
	sharedState->dispatchSemaphore.post(numThreads);
	RELEASE_MUTEX(sharedState->dispatchMutex);
	
	#ifndef USE_STD_THREAD
	HANDLE* lpHandles = new HANDLE[numThreads];
	for(int i=0; i<numThreads;i++){
		lpHandles[i] = sharedState->workerThreads[i].mthread;
	}
	//Don't wait for threads to end before returning
	//sharedState is a shared_ptr so it will persist until all threads end
	//WaitForMultipleObjects(numThreads,lpHandles,TRUE,1000);
	for(int i=0; i<numThreads;i++){
		CloseHandle(lpHandles[i]);
	}
	delete[] lpHandles;
	#else
	for(int i=0; i<numThreads;i++){
		//sharedState.workerThreads[i].thread.join();
	}
	#endif
}

void ThreadPool::async(std::function<void()> workUnit){
	ACQUIRE_MUTEX(sharedState->dispatchMutex);
	sharedState->dispatchQueue.push(workUnit);
	RELEASE_MUTEX(sharedState->dispatchMutex);
	//ReleaseSemaphore(sharedState->dispatchSemaphore,1,NULL);
	sharedState->dispatchSemaphore.post();
	//ResumeThread(sharedState.queuingThread);
}

WorkQueue::WorkQueue(){
	NEW_MUTEX(queueMutex);
}

void WorkQueue::async(std::function<void()> workUnit){
	ACQUIRE_MUTEX(queueMutex);
	workQueue.push(workUnit);
	RELEASE_MUTEX(queueMutex);
}

bool WorkQueue::processQueueUnit(){
	ACQUIRE_MUTEX(queueMutex);
	if(!workQueue.empty()){
		std::function<void()> workUnit = workQueue.front();
		workQueue.pop();
		RELEASE_MUTEX(queueMutex);
		workUnit();
		return true;
	} else {
		RELEASE_MUTEX(queueMutex);
		return false;
	}
}

template<>
Future<void> WorkQueue::async<void>(std::function<void()> func){
	Future<void> f;
	async([f,func]()mutable{
		func();
		f.set();
	});
	return f;
}

/*DWORD WINAPI DispatchThreadProc(LPVOID lpParameter){
	bool run = true;
	DispatchData* data = (DispatchData*)lpParameter;
    int length = 1;
	while(run){
		WaitForSingleObject(data->dispatchMutex,INFINITE);
		if(!data->dispatchQueue.empty()){
			std::function<void()> workUnit = data->dispatchQueue.front();
			data->dispatchQueue.pop();
			ReleaseMutex(data->dispatchMutex);
			//find a worker thread for this work unit
            int start = data->roundRobinIndex;
            
			for(;;){
				//we wait up to 10 ms for a worker thread to release it's mutex, before skipping over it
				DWORD result = WaitForSingleObject(data->workerThreads[data->roundRobinIndex].mutex,INFINITE);
                if(result == WAIT_OBJECT_0 && data->workerThreads[data->roundRobinIndex].workData.size() < length){ //success
					data->workerThreads[data->roundRobinIndex].workData.push(workUnit);
					ReleaseMutex(data->workerThreads[data->roundRobinIndex].mutex);
					ResumeThread(data->workerThreads[data->roundRobinIndex].thread);
					break;
				} else { //not a suitable thread
					ReleaseMutex(data->workerThreads[data->roundRobinIndex].mutex);
                    data->roundRobinIndex++;
				    data->roundRobinIndex %= data->workerThreads.size();
                    if(start == data->roundRobinIndex){//we checked every thread, relax selection criteria
                        length++;
                    }
				}
			}
            data->roundRobinIndex++;
			data->roundRobinIndex %= data->workerThreads.size();
		} else {
			ReleaseMutex(data->dispatchMutex);
            length = 1; //reset expected queue length
			SuspendThread(data->queuingThread);
		}
	}
	return 0;
}*/
