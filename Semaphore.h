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
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#define semaphore_t HANDLE
#else
#include <semaphore.h>
#define semaphore_t sem_t
#endif
#include <iostream>
class Semaphore{
protected:
    semaphore_t s;
    int id;
public:
    Semaphore(int initialCount){
    static int num;
        #ifdef _WIN32
            s = CreateSemaphore(NULL,0,initialCount,NULL);
        #else
            sem_init(&s,0,initialCount);
        #endif
        id = num;
        num++;
    }
    ~Semaphore(){
        #ifdef _WIN32
            CloseHandle(s);
        #else
            sem_destroy(&s);
        #endif
    }
    inline bool wait(){
        #ifdef _WIN32
            return 0==WaitForSingleObject(s,INFINITE);
        #else
            //return 0==sem_wait(&s);
	    int result = sem_wait(&s);
	    if(result != 0){
	    	std::cout << "sem_wait failed: ";
		switch(errno){
		case EINTR:
			std::cout << "interrupted";
			break;
			case EINVAL:
			std::cout << "not a valid semaphore";
			break;
			case EAGAIN:
			std::cout << "would block";
			break;
			default:
			std::cout << errno;
		}
		return false;
	    }
	    return true;
        #endif
    }
    inline void post(){
        #ifdef _WIN32
            ReleaseSemaphore(s,1,NULL);
        #else
            sem_post(&s);
        #endif
        //std::cout << "posting 1 to sem " << id << std::endl;
    }
    void post(int count){
        #ifdef _WIN32
            ReleaseSemaphore(s,count,NULL);
        #else
            for(int i=0;i<count;i++){
                sem_post(&s);
            }
        #endif
        //std::cout << "posting " << count << " to sem " << id << std::endl;
    }
};

