//
// Created by 33550 on 2026/10/5.
//

#include "Connection.h"

ssize_t Connection::handleRead()
{
    return m_readBuf.readFd(m_socket);
}

ssize_t Connection::handleWrite()
{
    const auto bytes = write(m_socket , m_readBuf.peek() , m_readBuf.readableBytes());
    if (bytes > 0) {
        m_readBuf.retrieve(static_cast<std::size_t>(bytes));
    }
    return bytes;
}
