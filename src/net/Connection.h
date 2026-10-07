//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_CONNECTION_H
#define WEBSERVER_CONNECTION_H

#include "base/Buffer.h"
#include "http/HttpRequest.h"

#include <chrono>
#include <netinet/in.h>
#include <string>
#include <unistd.h>

class Router;
class ThreadPool;
class EventLoop;

class Connection
{
    int m_socket{-1};
    sockaddr_in m_addr{};
    Buffer m_readBuf{};
    Buffer m_writeBuf{};
    HttpRequest m_request{};
    bool m_headerParsed{false};
    std::chrono::steady_clock::time_point m_lastActive{std::chrono::steady_clock::now()};

public:
    explicit Connection(const int socket , const sockaddr_in addr)
        : m_socket{socket}, m_addr{addr} {};

    ~Connection()
    {
        if (m_socket > 0)
            close(m_socket);
    }

    [[nodiscard]] int getSocket() const { return m_socket; }

    // 最近一次有数据到达的时间，定时器据此判断连接是否空闲超时
    [[nodiscard]] std::chrono::steady_clock::time_point lastActive() const { return m_lastActive; }

    void updateLastActive() { m_lastActive = std::chrono::steady_clock::now(); }

    [[nodiscard]] Buffer& getReadBuf() { return m_readBuf; }

    [[nodiscard]] Buffer& getWriteBuf() { return m_writeBuf; }

    // 把本次可读的数据全部累积进读缓冲区，返回本次读到的字节数
    ssize_t handleRead();

    // 解析读缓冲区里的 HTTP 请求，把业务处理丢给线程池，
    // 算好的响应由工作线程交回 EventLoop 发送。
    // 头部或正文没收全返回 Incomplete（继续等下一次读事件），
    // 报文非法返回 Error（上层应关闭连接）。
    ParseResult processInput(const Router& router , ThreadPool& pool , EventLoop& loop);

    // I/O 线程专用：把工作线程算好的响应放进写缓冲区
    void appendOutput(std::string data) { m_writeBuf.append(data); }

    // 把读缓冲区里待发的数据发给对端，返回写出的字节数（可能少于待发数据）
    ssize_t handleWrite();

    [[nodiscard]] bool hasPendingWrite() const { return m_writeBuf.readableBytes() > 0; }
};


#endif //WEBSERVER_CONNECTION_H
