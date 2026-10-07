//
// Created by 33550 on 2026/10/5.
//

#include "net/Connection.h"
#include "http/Router.h"

#include <optional>
#include <string>

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

ParseResult Connection::processInput(const Router& router)
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

        const std::string body = m_readBuf.retrieveAsString(bodyLen);
        router.route(m_request , body , m_writeBuf);

        // 复位，准备解析该连接上的下一条请求
        m_request = HttpRequest{};
        m_headerParsed = false;
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
