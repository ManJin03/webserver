//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_SERVER_H
#define WEBSERVER_SERVER_H

#include "Connection.h"

#include <memory>

class Server
{
    int m_socket{-1};
    int m_port{-1};
    sockaddr_in m_addr{};

public:
    explicit Server(int port);

    ~Server()
    {
        if (m_socket > 0)
            close(m_socket);
    };

    [[nodiscard]] int getSocket() const { return m_socket; };

    int start();

    [[nodiscard]] std::unique_ptr<Connection> accept() const;
};


#endif //WEBSERVER_SERVER_H
