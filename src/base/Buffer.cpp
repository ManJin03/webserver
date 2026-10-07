//
// Created by 33550 on 2026/10/5.
//

#include "base/Buffer.h"

#include <algorithm>
#include <string_view>
#include <sys/uio.h>

const char* Buffer::findCRLF() const
{
    constexpr std::string_view crlf{"\r\n"};
    const std::string_view view{peek() , readableBytes()};
    if (const auto pos = view.find(crlf) ; pos != std::string_view::npos) {
        return peek() + pos;
    }
    return nullptr;
}

void Buffer::retrieve(std::size_t len)
{
    if (len >= readableBytes()) {
        retrieveAll();
        return;
    }
    m_readIndex += len;
}

void Buffer::retrieveUntil(const char* end)
{
    retrieve(static_cast<std::size_t>(end - peek()));
}

void Buffer::retrieveAll()
{
    m_readIndex = BUFFER_CHEAP_PREPEND;
    m_writeIndex = BUFFER_CHEAP_PREPEND;
}

std::string Buffer::retrieveAsString(std::size_t len)
{
    len = std::min(len , readableBytes());
    std::string result{peek() , len};
    retrieve(len);
    return result;
}

std::string Buffer::retrieveAllAsString()
{
    return retrieveAsString(readableBytes());
}

void Buffer::append(const char* data , std::size_t len)
{
    ensureWritableBytes(len);
    std::copy_n(data , len , beginWrite());
    hasWritten(len);
}

void Buffer::ensureWritableBytes(std::size_t len)
{
    if (writableBytes() >= len) { return; }
    if (writableBytes() + prependableBytes() >= len + BUFFER_CHEAP_PREPEND) {
        // 把待处理数据前移，腾出尾部空间
        std::copy(m_buffer.begin() + static_cast<long>(m_readIndex) ,
                  m_buffer.begin() + static_cast<long>(m_writeIndex) ,
                  m_buffer.begin() + static_cast<long>(BUFFER_CHEAP_PREPEND));
        m_writeIndex = BUFFER_CHEAP_PREPEND + readableBytes();
        m_readIndex = BUFFER_CHEAP_PREPEND;
    }
    else {
        m_buffer.resize(m_writeIndex + len);
    }
}

ssize_t Buffer::readFd(int fd)
{
    // 栈上备用空间：缓冲区剩余空间不够时，多余数据先落到这里再 append，
    // 避免一次 read 因为缓冲区小而丢数据，也避免频繁扩容
    char extrabuf[65536];
    const std::size_t writable = writableBytes();

    iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    const ssize_t n = readv(fd , vec , 2);
    if (n < 0) { return n; }

    if (static_cast<std::size_t>(n) <= writable) {
        hasWritten(static_cast<std::size_t>(n));
    }
    else {
        hasWritten(writable);
        append(extrabuf , static_cast<std::size_t>(n) - writable);
    }
    return n;
}
