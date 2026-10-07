//
// Created by 33550 on 2026/10/7.
//

#ifndef WEBSERVER_TIMER_H
#define WEBSERVER_TIMER_H

#include <chrono>

// 基于 timerfd 的周期定时器：到期后 fd 变可读，可以直接挂进 epoll 一起等
class Timer
{
    int m_fd{-1};

public:
    explicit Timer(std::chrono::seconds interval);

    ~Timer();

    [[nodiscard]] int getFd() const { return m_fd; }

    // 到期后必须把 fd 上的计数读掉，否则电平触发下会一直就绪
    void onTick() const;
};


#endif //WEBSERVER_TIMER_H
