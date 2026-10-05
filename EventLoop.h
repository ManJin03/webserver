//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_EVENTLOOP_H
#define WEBSERVER_EVENTLOOP_H

#include "Server.h"

#include <unordered_map>


constexpr int MAXEVENTS = 10;

class EventLoop
{
    int m_socket{-1};
    std::shared_ptr<Server> m_server{nullptr};
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

    void startLoop();
};


#endif //WEBSERVER_EVENTLOOP_H
