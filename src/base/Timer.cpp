//
// Created by 33550 on 2026/10/7.
//

#include "base/Timer.h"

#include <cstdint>
#include <cstdio>
#include <sys/timerfd.h>
#include <unistd.h>

Timer::Timer(const std::chrono::seconds interval)
{
    m_fd = timerfd_create(CLOCK_MONOTONIC , TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_fd < 0) {
        perror("timerfd_create");
        return;
    }

    itimerspec spec{};
    spec.it_value.tv_sec = interval.count();     // 首次到期时间
    spec.it_interval.tv_sec = interval.count();  // 之后的周期
    if (timerfd_settime(m_fd , 0 , &spec , nullptr) < 0) {
        perror("timerfd_settime");
    }
}

Timer::~Timer()
{
    if (m_fd > 0) {
        close(m_fd);
    }
}

void Timer::onTick() const
{
    std::uint64_t expirations{0};
    // 读到 EAGAIN 为止，把累计的到期次数全部清掉
    while (read(m_fd , &expirations , sizeof(expirations)) > 0) {}
}
