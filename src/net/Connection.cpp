//
// Created by 33550 on 2026/10/5.
//

#include "net/Connection.h"
#include "net/EventLoop.h"
#include "http/Router.h"
#include "base/ThreadPool.h"

#include <optional>
#include <string>
#include <utility>

namespace
{
    // 取 Content-Length；缺失或不是合法数字时返回 nullopt（当作没有正文）
    std::optional<std::size_t> contentLength(const HttpRequest& request)
    {
        const auto it = request.headers.find("content-length");
        if (it == request.headers.end()) { return std::nullopt; }
        try {
            return std::stoull(it->second);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
}

ssize_t Connection::handleRead()
{
    updateLastActive();
    return m_readBuf.readFd(m_socket);
}

ParseResult Connection::processInput(const Router& router , ThreadPool& pool , EventLoop& loop)
{
    // 一次读事件里可能攒着多条请求（pipeline），收全一条就处理一条
    while (true) {
        if (!m_headerParsed) {
            if (const auto result = m_request.parse(m_readBuf) ; result != ParseResult::Complete) {
                return result; // Incomplete 继续等数据，Error 交给上层关闭连接
            }
            m_headerParsed = true;
        }

        // 头部收全了还要等正文收全，否则继续等下一次读事件
        const std::size_t bodyLen = contentLength(m_request).value_or(0);
        if (m_readBuf.readableBytes() < bodyLen) { return ParseResult::Incomplete; }

        std::string body = m_readBuf.retrieveAsString(bodyLen);
        // 请求已经收全，把请求移出去交给工作线程，连接状态立刻复位等下一条请求
        HttpRequest request = std::move(m_request);
        m_request = HttpRequest{};
        m_headerParsed = false;

        // 按 fd 投递：同一连接的任务落在同一个工作线程上，保证响应顺序
        const int fd = m_socket;
        pool.enqueue(fd , [&router , &loop , fd , req = std::move(request) , data = std::move(body)]
        {
            Buffer out;
            router.route(req , data , out);
            loop.submitResponse(fd , out.retrieveAllAsString()); // 结果交回 I/O 线程发送
        });
    }
}

ssize_t Connection::handleWrite()
{
    const std::size_t bytes = m_writeBuf.readableBytes();
    if (bytes == 0) { return 0; }

    const auto sent = write(m_socket , m_writeBuf.peek() , bytes);
    if (sent > 0) {
        // 未发完的数据留在写缓冲区，等下一次可写事件
        m_writeBuf.retrieve(static_cast<std::size_t>(sent));
    }
    return sent;
}
