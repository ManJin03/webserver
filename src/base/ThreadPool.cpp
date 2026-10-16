//
// Created by 33550 on 2026/10/7.
//

#include "base/ThreadPool.h"

#include <utility>

ThreadPool::ThreadPool(const std::size_t threadCount)
{
    const std::size_t count = threadCount == 0 ? 1 : threadCount;
    try {
        for (std::size_t i = 0 ; i < count ; i++) {
            m_workers.emplace_back([this] { workerLoop(); });
        }
    } catch (...) {
        shutdown(); // 线程没起全：先收掉已经起来的，否则它们会用到半构造的对象
        throw;
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

bool ThreadPool::enqueue(const std::uint64_t key , std::function<void()> task)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stop || m_size >= MAX_TASK_COUNT) {
            return false; // 已停止或积压满了，交给调用方降级
        }
        KeyState& state = m_keys[key];
        if (state.busy) {
            state.waiting.push_back({key , std::move(task)}); // 前一条还没跑完，排队
        }
        else {
            state.busy = true;
            m_ready.push_back({key , std::move(task)});
        }
        ++m_size;
    }
    m_cv.notify_one();
    return true;
}

void ThreadPool::workerLoop()
{
    while (true) {
        Task task;
        {
            std::unique_lock lock{m_mutex};
            m_cv.wait(lock , [this] { return m_stop || !m_ready.empty(); });
            if (m_ready.empty()) {
                return; // 停止且就绪队列已排空
            }
            task = std::move(m_ready.front());
            m_ready.pop_front();
            --m_size;
        }
        try {
            task.fn();
        } catch (...) {
            // 业务代码抛异常不能把工作线程带走
        }
        releaseKey(task.key); // 抛了异常也要放下一个同 key 的任务进去
    }
}

void ThreadPool::releaseKey(const std::uint64_t key)
{
    {
        std::lock_guard lock{m_mutex};
        const auto it = m_keys.find(key);
        auto& waiting = it->second.waiting;
        if (!waiting.empty()) {
            m_ready.push_back(std::move(waiting.front()));
            waiting.pop_front();
        }
        else {
            m_keys.erase(it);
        }
    }
    m_cv.notify_one();
}

void ThreadPool::shutdown()
{
    {
        std::lock_guard lock{m_mutex}; // 在锁里置停止位，唤醒才不会丢
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
