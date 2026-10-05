//
// Created by 33550 on 2026/10/5.
//

#include "Connection.h"


ssize_t Connection::handleRead()
{
    m_readBuf.clear();
    m_readBuf.resize(BUFFER_SIZE , '\0');
    const auto bytes = recv(m_socket , m_readBuf.data() , m_readBuf.size() , 0);
    return bytes;
}

void Connection::handleWrite() const
{
    write(m_socket , m_readBuf.data() , m_readBuf.size());
}
