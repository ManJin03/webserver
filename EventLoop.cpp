//
// Created by 33550 on 2026/10/5.
//

#include "EventLoop.h"

#include <sys/epoll.h>

EventLoop::EventLoop()
{
    m_socket = epoll_create1(0);
    if (m_socket < 0) {
        perror("epoll_create1");
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
            //服务端有新连接
            if (fd == m_server->getSocket()) {
                while (true) {
                    auto conn = m_server->accept();
                    if (conn == nullptr) {
                        break;
                    }
                    //将新连接注册到epoll中
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
            }
            //现有连接有新的输入
            else {
                while (true) {
                    if (const auto n = m_connections[fd]->handleRead() ; n > 0) {
                        m_connections[fd]->handleWrite();
                    }
                    else if (n == 0) {
                        //连接关闭，移除该连接
                        printf("Connection closed: fd %d\n" , fd);
                        epoll_ctl(m_socket , EPOLL_CTL_DEL , fd , nullptr);
                        m_connections.erase(fd);
                        break;
                    }
                    else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 数据读完了
                        }
                        perror("read");
                        epoll_ctl(m_socket , EPOLL_CTL_DEL , fd , nullptr);
                        m_connections.erase(fd);
                        break;
                    }
                }
            }
        }
    }
}
