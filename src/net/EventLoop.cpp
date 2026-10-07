//
// Created by 33550 on 2026/10/5.
//

#include "net/EventLoop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <utility>

EventLoop::EventLoop()
{
    m_socket = epoll_create1(0);
    if (m_socket < 0) {
        perror("epoll_create1");
        return;
    }

    // 定时器也是 epoll 里的一个 fd，到期时和其他事件一起被 epoll_wait 返回
    epoll_event timerEvent{};
    timerEvent.events = EPOLLIN;
    timerEvent.data.fd = m_timer.getFd();
    if (epoll_ctl(m_socket , EPOLL_CTL_ADD , m_timer.getFd() , &timerEvent) < 0) {
        perror("epoll_ctl add timer");
    }

    // 工作线程算完响应后写这个 fd，把阻塞在 epoll_wait 上的 I/O 线程唤醒
    m_wakeupFd = eventfd(0 , EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0) {
        perror("eventfd");
        return;
    }
    epoll_event wakeupEvent{};
    wakeupEvent.events = EPOLLIN;
    wakeupEvent.data.fd = m_wakeupFd;
    if (epoll_ctl(m_socket , EPOLL_CTL_ADD , m_wakeupFd , &wakeupEvent) < 0) {
        perror("epoll_ctl add wakeup");
    }
}

EventLoop::~EventLoop()
{
    if (m_socket > 0) {
        close(m_socket);
    }
    if (m_wakeupFd > 0) {
        close(m_wakeupFd);
    }
}

int EventLoop::setServer(const std::shared_ptr<Server>& server)
{
    m_connections.clear();
    m_server = server;
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server->getSocket();
    if (epoll_ctl(m_socket , EPOLL_CTL_ADD , server->getSocket() , &event) < 0) {
        perror("epoll_ctl");
        return 1;
    }
    return 0;
}

void EventLoop::updateEvent(const int fd , const std::uint32_t events) const
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (epoll_ctl(m_socket , EPOLL_CTL_MOD , fd , &event) < 0) {
        perror("epoll_ctl mod");
    }
}

void EventLoop::removeConnection(const int fd)
{
    epoll_ctl(m_socket , EPOLL_CTL_DEL , fd , nullptr);
    m_connections.erase(fd);
}

void EventLoop::submitResponse(const int fd , const std::string& data)
{
    {
        std::lock_guard lock{m_pendingMutex};
        m_pendingResponses[fd].append(data);
    }
    // 唤醒 I/O 线程；写失败（EAGAIN）也没关系，说明已经有未处理的唤醒了
    constexpr std::uint64_t one = 1;
    write(m_wakeupFd , &one , sizeof(one));
}

void EventLoop::handleWakeup()
{
    std::uint64_t count{0};
    while (read(m_wakeupFd , &count , sizeof(count)) > 0) {}

    std::unordered_map<int,std::string> responses;
    {
        std::lock_guard lock{m_pendingMutex};
        responses.swap(m_pendingResponses);
    }

    for (auto& [fd , data] : responses) {
        if (!m_connections.contains(fd)) {
            continue; // 连接已经被关掉，算好的响应直接丢弃
        }
        m_connections[fd]->appendOutput(std::move(data));
        updateEvent(fd , EPOLLOUT); // 下一轮把响应发出去
    }
}

void EventLoop::checkTimeout()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_connections.begin() ; it != m_connections.end() ;) {
        if (now - it->second->lastActive() > CONNECTION_TIMEOUT) {
            printf("Connection timeout: fd %d\n" , it->first);
            epoll_ctl(m_socket , EPOLL_CTL_DEL , it->first , nullptr);
            it = m_connections.erase(it); // Connection 析构时自动 close
        }
        else {
            ++it;
        }
    }
}

void EventLoop::startLoop()
{
    while (true) {
        epoll_event events[MAXEVENTS];
        //获取就绪套接字数
        const int nfds = epoll_wait(m_socket , events , MAXEVENTS , -1);
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }
        for (int i = 0 ; i < nfds ; i++) {
            //获取所有有新输入的套接字文件描述符
            const int fd = events[i].data.fd;
            //定时器到期，清掉空闲连接
            if (fd == m_timer.getFd()) {
                m_timer.onTick();
                checkTimeout();
                continue;
            }
            //工作线程算完了响应，取回来准备发送
            if (fd == m_wakeupFd) {
                handleWakeup();
                continue;
            }
            //服务端有新连接
            if (fd == m_server->getSocket()) {
                while (true) {
                    auto conn = m_server->accept();
                    if (conn == nullptr) {
                        break;
                    }
                    //将新连接注册到epoll中，新连接先只关注可读
                    epoll_event event{};
                    event.events = EPOLLIN;
                    event.data.fd = conn->getSocket();
                    if (epoll_ctl(m_socket , EPOLL_CTL_ADD , conn->getSocket() , &event) < 0) {
                        perror("epoll_ctl add conn");
                    }
                    else {
                        printf("New connection at fd %d\n" , conn->getSocket());
                        m_connections.emplace(conn->getSocket() , std::move(conn));
                    }
                }
                continue;
            }

            //发送阶段：上一轮收完数据后切到 EPOLLOUT，这一轮把响应发出去
            if (events[i].events & EPOLLOUT) {
                const auto sent = m_connections[fd]->handleWrite();
                if (sent < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue; // 内核发送缓冲区满了，保持 EPOLLOUT 等下一轮
                    }
                    perror("write");
                    removeConnection(fd);
                    continue;
                }
                // 没发完继续关注可写；发完了按是否需要关闭连接分别处理
                if (m_connections[fd]->hasPendingWrite()) {
                    updateEvent(fd , EPOLLOUT);
                }
                else if (m_connections[fd]->shouldClose()) {
                    // 响应了 Connection: close，发完就关
                    removeConnection(fd);
                }
                else {
                    updateEvent(fd , EPOLLIN);
                }
                continue;
            }

            //接收阶段：把数据收进读缓冲区并解析，响应留到下一轮发
            if (events[i].events & EPOLLIN) {
                while (true) {
                    if (const auto n = m_connections[fd]->handleRead() ; n > 0) {
                        // 解析请求并填好响应，非法报文直接关闭连接
                        if (m_connections[fd]->processInput(*m_router , *m_pool , *this) == ParseResult::Error) {
                            printf("Bad request: fd %d\n" , fd);
                            removeConnection(fd);
                            break;
                        }
                    }
                    else if (n == 0) {
                        //连接关闭，移除该连接
                        printf("Connection closed: fd %d\n" , fd);
                        removeConnection(fd);
                        break;
                    }
                    else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 数据读完了
                        }
                        perror("read");
                        removeConnection(fd);
                        break;
                    }
                }
                // 有响应要发就切到 EPOLLOUT，下一轮 epoll_wait 返回后再发送
                if (m_connections.contains(fd) && m_connections[fd]->hasPendingWrite()) {
                    updateEvent(fd , EPOLLOUT);
                }
            }
        }
    }
}
