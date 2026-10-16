//
// Created by 33550 on 2026/10/7.
//

#ifndef WEBSERVER_THREADPOOL_H
#define WEBSERVER_THREADPOOL_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// 积压上限：达到后 enqueue 返回 false，由调用方降级
constexpr std::size_t MAX_TASK_COUNT{1024};

// 固定线程数的线程池。工作线程共用一条就绪队列，谁空闲谁取任务，
// 不会被某一条队列上的慢任务堵住。
// 同一个 key 的任务按投递顺序串行执行（下一条要等上一条跑完才进就绪队列），
// 用它保证一条连接上的多条请求按顺序处理。
class ThreadPool
{
    struct Task
    {
        std::uint64_t key{0};
        std::function<void()> fn;
    };

    // 同一个 key 的排队情况：busy 为真说明该 key 还有任务没跑完
    struct KeyState
    {
        bool busy{false};
        std::deque<Task> waiting;
    };

    std::deque<Task> m_ready; // 可以被任意工作线程取走的任务
    std::unordered_map<std::uint64_t,KeyState> m_keys;
    std::size_t m_size{0}; // 就绪 + 排队的任务总数
    std::vector<std::thread> m_workers;
    std::mutex m_mutex; // 同时保护上面这些成员和 m_stop
    std::condition_variable m_cv;
    bool m_stop{false};

public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());

    ~ThreadPool();

    // 投递任务；相同 key 的任务按投递顺序串行执行。
    // 线程池已停止或积压达到上限时返回 false，任务不会被执行
    bool enqueue(std::uint64_t key , std::function<void()> task);

private:
    void workerLoop();

    // 把 key 上排队的下一条任务放进就绪队列，没有就清掉这个 key 的排队状态
    void releaseKey(std::uint64_t key);

    void shutdown();
};


#endif //WEBSERVER_THREADPOOL_H
