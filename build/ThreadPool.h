#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool
{
	void init(int threads);
public:
	static ThreadPool &getInstance();
	ThreadPool();
	template<class F, class... Args>
	auto enqueue(F&& f, Args&&... args)
		->std::future<typename std::result_of<F(Args...)>::type>;
	~ThreadPool();
private:
	// need to keep track of threads so we can join them
	std::vector< std::thread > workers;
	// the task queue
	std::queue< std::function<void()> > tasks;

	// synchronization
	std::mutex queue_mutex;
	bool stop;
};
// the constructor just launches some amount of workers
void ThreadPool::init(int threads)
{
	if (threads < 1)
	{
		threads = 1;
	}
	stop = false;
	for (size_t i = 0; i < threads; ++i)
		workers.emplace_back(
			[this]
	{
		for (;;)
		{
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(this->queue_mutex);
				if (this->tasks.empty())
				{
					if (this->stop)
						return;
					else
						goto _sleep;
				}
				task = std::move(this->tasks.front());
				this->tasks.pop();
			}
			task();
			continue;
		_sleep:
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	);
}

#if !defined (_WIN32) && !defined (_WIN64)
#ifndef LINUX
#define LINUX
#endif
#include <sysconf.h>  
#else
#ifndef WINDOWS
#define WINDOWS
#endif
#include <windows.h>  
#endif  
inline unsigned getCoreCount()
{
	unsigned count = 1; // 至少一个  
#if defined (LINUX)  
	count = sysconf(_SC_NPROCESSORS_CONF);
#elif defined (WINDOWS)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	count = si.dwNumberOfProcessors;
#endif
	return count;
}

ThreadPool & ThreadPool::getInstance()
{
	static ThreadPool tp;
	return tp;
}

inline ThreadPool::ThreadPool()
{
	init(getCoreCount()-1);
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type>
{
	using return_type = typename std::result_of<F(Args...)>::type;
	auto task = std::make_shared< std::packaged_task<return_type()> >(
		std::bind(std::forward<F>(f), std::forward<Args>(args)...)
		);

	std::future<return_type> res = task->get_future();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		tasks.emplace([task]() { (*task)(); });
	}
	return res;
}
// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		stop = true;
	}
	for (std::thread &worker : workers)
		worker.join();
}