//
// Created by 33550 on 2026/10/5.
//

#include "Buffer.h"

#include <algorithm>
#include <string_view>
#include <sys/uio.h>

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
