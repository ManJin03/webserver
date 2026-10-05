//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_EVENTLOOP_H
#define WEBSERVER_EVENTLOOP_H

#include "Server.h"

#include <cstdint>
#include <unordered_map>


constexpr int MAXEVENTS = 10;

class Router;

class EventLoop
{
    int m_socket{-1};
    std::shared_ptr<Server> m_server{nullptr};
    std::shared_ptr<Router> m_router{nullptr};
    std::unordered_map<int,std::unique_ptr<Connection>> m_connections;

public:
    explicit EventLoop();

    ~EventLoop()
    {
        if (m_socket > 0) {
            close(m_socket);
        }
    };

    int setServer(const std::shared_ptr<Server>& server);

    void setRouter(const std::shared_ptr<Router>& router) { m_router = router; }

    void startLoop();

private:
    // 切换某个连接关注的事件（收数据和发数据两个阶段互切）
    void updateEvent(int fd , std::uint32_t events);

    // 从 epoll 摘除并销毁连接
    void removeConnection(int fd);
};


#endif //WEBSERVER_EVENTLOOP_H
