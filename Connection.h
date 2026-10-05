//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_CONNECTION_H
#define WEBSERVER_CONNECTION_H

#include "Buffer.h"

#include <netinet/in.h>
#include <unistd.h>

class Connection
{
    int m_socket{-1};
    sockaddr_in m_addr{};
    Buffer m_readBuf{};
    Buffer m_writeBuf{};

public:
    explicit Connection(const int socket , const sockaddr_in addr)
        : m_socket{socket}, m_addr{addr} {};

    ~Connection()
    {
        if (m_socket > 0)
            close(m_socket);
    }

    [[nodiscard]] int getSocket() const { return m_socket; }

    [[nodiscard]] Buffer& getReadBuf() { return m_readBuf; }

    [[nodiscard]] Buffer& getWriteBuf() { return m_writeBuf; }

    // 把本次可读的数据全部累积进读缓冲区，返回本次读到的字节数
    ssize_t handleRead();

    // 把读缓冲区里未消费的数据写回对端，返回写出的字节数（可能少于待发数据）
    ssize_t handleWrite();
};


#endif //WEBSERVER_CONNECTION_H
