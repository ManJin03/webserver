//
// Created by 33550 on 2026/10/7.
//

#ifndef WEBSERVER_THREADPOOL_H
#define WEBSERVER_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 固定线程数的线程池。每个工作线程一条自己的任务队列，
// 按 key 把任务分到固定线程：同一个 key 的任务一定串行执行，
// 用它保证一条连接上的多条请求按顺序处理。
class ThreadPool
{
    struct TaskQueue
    {
        std::queue<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable cv;
    };

    std::vector<std::unique_ptr<TaskQueue>> m_queues;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{false};

public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());

    ~ThreadPool();

    // 投递任务；相同 key 的任务落在同一线程上，按投递顺序执行
    void enqueue(std::uint64_t key , std::function<void()> task);

private:
    void workerLoop(std::size_t index);
};


#endif //WEBSERVER_THREADPOOL_H
