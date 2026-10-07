//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_BUFFER_H
#define WEBSERVER_BUFFER_H

#include <algorithm>
#include <string>
#include <vector>

constexpr std::size_t BUFFER_INITIAL_SIZE = 1024;
constexpr std::size_t BUFFER_CHEAP_PREPEND = 8;

// 读写缓冲区：readIndex 之前是已读数据，[readIndex, writeIndex) 是待处理数据，
// writeIndex 之后是可写空间。TCP 是字节流，一次 read 不保证拿到完整消息，
// 因此把数据先累积在 Buffer 里，由上层判断是否已经收全。
class Buffer
{
    std::vector<char> m_buffer;
    std::size_t m_readIndex{BUFFER_CHEAP_PREPEND};
    std::size_t m_writeIndex{BUFFER_CHEAP_PREPEND};

public:
    explicit Buffer(std::size_t initialSize = BUFFER_INITIAL_SIZE)
        : m_buffer(initialSize + BUFFER_CHEAP_PREPEND) {}

    [[nodiscard]] std::size_t readableBytes() const { return m_writeIndex - m_readIndex; }
    [[nodiscard]] std::size_t writableBytes() const { return m_buffer.size() - m_writeIndex; }
    [[nodiscard]] std::size_t prependableBytes() const { return m_readIndex; }

    [[nodiscard]] const char* peek() const { return m_buffer.data() + m_readIndex; }

    // 在待处理数据中查找 "\r\n"，找不到返回 nullptr
    [[nodiscard]] const char* findCRLF() const;

    void retrieve(std::size_t len);

    // 消费 [peek(), end) 区间的数据
    void retrieveUntil(const char* end);

    void retrieveAll();

    [[nodiscard]] std::string retrieveAsString(std::size_t len);

    [[nodiscard]] std::string retrieveAllAsString();

    void append(const char* data , std::size_t len);

    void append(const std::string& str) { append(str.data() , str.size()); }

    void ensureWritableBytes(std::size_t len);

    [[nodiscard]] char* beginWrite() { return m_buffer.data() + m_writeIndex; }

    void hasWritten(std::size_t len)
    {
        m_writeIndex += std::min(len , writableBytes());
    }

    // 从 fd 读一次数据写入缓冲区，返回读到的字节数；出错返回 -1（errno 由 readv 设置）
    ssize_t readFd(int fd);
};


#endif //WEBSERVER_BUFFER_H
