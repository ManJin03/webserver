//
// Created by 33550 on 2026/10/5.
//

#include "net/Server.h"

#include <cstdio>

Server::Server(int port) : m_port{port}
{
    m_socket = socket(AF_INET , SOCK_STREAM | SOCK_NONBLOCK , 0);
    if (m_socket < 0) {
        perror("Error creating socket");
        return;
    }
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    m_addr.sin_port = htons(port);
    constexpr int opt = 1;
    setsockopt(m_socket , SOL_SOCKET , SO_REUSEADDR , &opt , sizeof(opt));
}

int Server::start()
{
    if (m_socket < 0) {
        perror("Error creating socket");
        return -1;
    }
    if (bind(m_socket , reinterpret_cast<sockaddr*>(&m_addr) , sizeof(m_addr)) < 0) {
        perror("bind");
    }
    if (listen(m_socket , SOMAXCONN) < 0) {
        perror("listen");
    }
    printf("Server listening on port %d...\n" , m_port);
    return 0;
}

std::unique_ptr<Connection> Server::accept() const
{
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    const int confd = accept4(m_socket , reinterpret_cast<sockaddr*>(&addr) ,
                              &addr_len ,SOCK_NONBLOCK);
    if (confd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return nullptr; }
        perror("accept");
    }
    return std::make_unique<Connection>(confd , addr);
}
