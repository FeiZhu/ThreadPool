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
public:
    explicit ThreadPool(size_t);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    size_t size() const;

    ThreadPool() = delete;
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool& operator=(const ThreadPool &) = delete;
    ThreadPool& operator=(ThreadPool &&) = delete;
private:
    std::vector<std::thread>          m_workers;         // need to keep track of threads so we can join them
    std::queue<std::function<void()>> m_tasks;           // the task queue
    
    // synchronization
    std::mutex                        m_queue_mutex;
    std::condition_variable           m_condition;
    bool                              m_stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   m_stop(false)
{
    for(size_t i = 0;i<threads;++i)
        m_workers.emplace_back(
            [this]
            {
                while(true) //loop until quit
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->m_queue_mutex);
                        this->m_condition.wait(lock,
                            [this]{ return this->m_stop || !this->m_tasks.empty(); }); //wait until task queue is not empty
                        if(this->m_stop && this->m_tasks.empty())
                            return;
                        task = std::move(this->m_tasks.front());   //take one task
                        this->m_tasks.pop();
                    }

                    task();   //do the task
                }
            }
        );
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for(std::thread &worker: m_workers)
        worker.join();
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
        std::lock_guard<std::mutex> lock(m_queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(m_stop)
            throw std::runtime_error("enqueue on m_stopped ThreadPool");

        m_tasks.emplace([task](){ (*task)(); });
    }
    m_condition.notify_one();
    return res;
}

inline size_t ThreadPool::size() const
{
    return m_workers.size();
}