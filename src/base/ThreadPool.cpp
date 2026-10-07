//
// Created by 33550 on 2026/10/7.
//

#include "base/ThreadPool.h"

#include <utility>

ThreadPool::ThreadPool(const std::size_t threadCount)
{
    const std::size_t count = threadCount == 0 ? 1 : threadCount;
    for (std::size_t i = 0 ; i < count ; i++) {
        m_queues.push_back(std::make_unique<TaskQueue>());
    }
    for (std::size_t i = 0 ; i < count ; i++) {
        m_workers.emplace_back([this , i] { workerLoop(i); });
    }
}

ThreadPool::~ThreadPool()
{
    m_stop.store(true);
    for (auto& queue : m_queues) {
        queue->cv.notify_all(); // 唤醒所有工作线程让它们退出
    }
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(const std::uint64_t key , std::function<void()> task)
{
    auto& queue = *m_queues[key % m_queues.size()];
    {
        std::lock_guard lock{queue.mutex};
        queue.tasks.push(std::move(task));
    }
    queue.cv.notify_one();
}

void ThreadPool::workerLoop(const std::size_t index)
{
    auto& queue = *m_queues[index];
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock{queue.mutex};
            queue.cv.wait(lock , [this , &queue] { return m_stop.load() || !queue.tasks.empty(); });
            if (queue.tasks.empty()) {
                return; // 停止且队列已排空
            }
            task = std::move(queue.tasks.front());
            queue.tasks.pop();
        }
        task();
    }
}
