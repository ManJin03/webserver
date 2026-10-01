//
// Created by 33550 on 2026/10/1.
//
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

constexpr int PORT = 8888;
constexpr int MAXEVENTS = 10;
constexpr int BUFSIZE = 1024;

int main()
{
    //TCP监听套接字
    const int server_fd = socket(AF_INET , SOCK_STREAM | SOCK_NONBLOCK , 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    setsockopt(server_fd , SOL_SOCKET , SO_REUSEADDR , &opt , sizeof(opt));
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd , reinterpret_cast<sockaddr*>(&server_addr) , sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_fd , SOMAXCONN) < 0) {
        perror("listen");
        exit(1);
    }
    const int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        exit(1);
    }
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd , EPOLL_CTL_ADD , server_fd , &event) < 0) {
        perror("epoll_ctl");
        exit(1);
    }
    printf("Server listening on port %d...\n" , PORT);
    while (true) {
        epoll_event events[MAXEVENTS];
        const int nfds = epoll_wait(epoll_fd , events , MAXEVENTS , -1);
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }
        for (int i = 0 ; i < nfds ; i++) {
            const int fd = events[i].data.fd;
            if (fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    const int confd = accept(server_fd , reinterpret_cast<sockaddr*>(&client_addr) , &client_len);
                    if (confd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
                        perror("accept");
                        break;
                    }
                    fcntl(confd ,F_SETFL , fcntl(confd ,F_GETFL , 0) | O_NONBLOCK);
                    event.events = EPOLLIN;
                    event.data.fd = confd;
                    if (epoll_ctl(epoll_fd , EPOLL_CTL_ADD , confd , &event) < 0) {
                        perror("epoll_ctl add conn");
                        close(confd);
                    }
                    else {
                        printf("New connection at fd %d\n" , confd);
                    }
                }
            }
            else {
                std::string buf(BUFSIZE , '\0');
                while (true) {
                    const auto n = read(fd , buf.data() , BUFSIZE);
                    if (n > 0) {
                        write(fd , buf.data() , n);
                    }
                    else if (n == 0) {
                        printf("Connection closed: fd %d\n" , fd);
                        epoll_ctl(epoll_fd , EPOLL_CTL_DEL , fd , nullptr);
                        close(fd);
                        break;
                    }
                    else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 数据读完了
                        }
                        perror("read");
                        epoll_ctl(epoll_fd , EPOLL_CTL_DEL , fd , nullptr);
                        close(fd);
                        break;
                    }
                }
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
    return 0;
}
