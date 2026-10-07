//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_EVENTLOOP_H
#define WEBSERVER_EVENTLOOP_H

#include "net/Server.h"
#include "base/Timer.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>


constexpr int MAXEVENTS = 10;
// 定时器每隔多久检查一次
constexpr std::chrono::seconds TIMER_INTERVAL{5};
// 连接空闲超过这么久就清理掉
constexpr std::chrono::seconds CONNECTION_TIMEOUT{30};

class Router;
class ThreadPool;

class EventLoop
{
    int m_socket{-1};
    int m_wakeupFd{-1};
    std::shared_ptr<Server> m_server{nullptr};
    std::shared_ptr<Router> m_router{nullptr};
    std::shared_ptr<ThreadPool> m_pool{nullptr};
    std::unordered_map<int,std::unique_ptr<Connection>> m_connections;
    Timer m_timer{TIMER_INTERVAL};
    // 工作线程算好的响应先存在这里，由 I/O 线程取走发送
    std::unordered_map<int,std::string> m_pendingResponses;
    std::mutex m_pendingMutex;

public:
    explicit EventLoop();

    ~EventLoop();

    int setServer(const std::shared_ptr<Server>& server);

    void setRouter(const std::shared_ptr<Router>& router) { m_router = router; }

    void setThreadPool(const std::shared_ptr<ThreadPool>& pool) { m_pool = pool; }

    // 工作线程算完响应后调用：把响应交给 I/O 线程，并唤醒事件循环
    void submitResponse(int fd , const std::string& data);

    void startLoop();

private:
    // 切换某个连接关注的事件（收数据和发数据两个阶段互切）
    void updateEvent(int fd , std::uint32_t events) const;

    // 从 epoll 摘除并销毁连接
    void removeConnection(int fd);

    // 定时检查，清掉空闲超时的连接
    void checkTimeout();

    // 被工作线程唤醒：把算好的响应写进对应连接的写缓冲区，并切到 EPOLLOUT
    void handleWakeup();
};


#endif //WEBSERVER_EVENTLOOP_H
