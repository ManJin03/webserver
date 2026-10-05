//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_CONNECTION_H
#define WEBSERVER_CONNECTION_H

#include <string>
#include <unistd.h>
#include <netinet/in.h>

constexpr int BUFFER_SIZE = 1024;

class Connection
{
    int m_socket{-1};
    sockaddr_in m_addr{};
    std::string m_readBuf{};
    std::string m_writeBuf{};

public:
    explicit Connection(const int socket , const sockaddr_in addr)
        : m_socket{socket}, m_addr{addr}, m_readBuf(BUFFER_SIZE , '\0'), m_writeBuf(BUFFER_SIZE , '\0') {};

    ~Connection()
    {
        if (m_socket > 0)
            close(m_socket);
    }

    [[nodiscard]] int getSocket() const { return m_socket; }

    ssize_t handleRead();

    void handleWrite() const;
};


#endif //WEBSERVER_CONNECTION_H
