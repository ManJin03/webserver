# webserver

一个用 C++ 从零实现的 Linux 高性能 Web 服务器，基于 **epoll + 非阻塞 I/O** 的单线程事件驱动模型。

项目处于早期阶段：网络框架（监听、连接管理、事件循环）已经跑通，目前对客户端数据做 **echo 回显** 处理，HTTP 协议解析尚未实现。

## 特性

- 基于 `epoll` 的 I/O 多路复用，单线程支撑大量并发连接
- 全链路非阻塞：`socket`/`accept4` 均带 `SOCK_NONBLOCK`
- 监听套接字设置 `SO_REUSEADDR`，避免重启时 `Address already in use`
- 连接由 `std::unique_ptr` 托管，RAII 自动关闭 fd，无裸指针泄漏
- `Buffer` 读写双索引缓冲区：一次读不完的消息留在缓冲区里累积，配合 `findCRLF()` 判断消息是否收全
- C++20，代码量小（约 400 行），适合作为网络编程的学习样例

## 目录结构

```
webserver/
├── main.cpp          # 程序入口：创建 Server 与 EventLoop 并启动循环
├── Server.h/.cpp     # 监听Socket：socket / bind / listen / accept
├── EventLoop.h/.cpp  # epoll 事件循环：事件注册、分发、连接生命周期管理
├── Connection.h/.cpp # 单条 TCP 连接：读写缓冲区与读写处理
├── CMakeLists.txt    # 构建配置
└── LICENSE           # MIT
```

## 编译与运行

依赖：Linux（epoll）、CMake ≥ 3.10、支持 C++20 的编译器（GCC 10+ / Clang 12+）。

```bash
mkdir -p build && cd build
cmake ..
make
./webserver
```

启动后输出：

```
Server listening on port 8888...
```

端口号在 `main.cpp` 中修改：

```cpp
constexpr int PORT = 8888;
```

## 测试

用 `nc` 连接，输入任意内容，服务端会原样回显：

```bash
nc 127.0.0.1 8888
hello        # 输入
hello        # 服务端回显
```

也可以用 `telnet 127.0.0.1 8888`。用 `curl` 的话会收到被原样回显的 HTTP 请求报文（因为目前没有 HTTP 解析与响应封装）。

## 架构说明

### 三个核心类

| 类 | 职责 |
| --- | --- |
| `Server` | 持有监听 fd，负责 `socket` / `bind` / `listen`，`accept()` 返回一个新的 `Connection` |
| `EventLoop` | 持有 epoll fd，把监听 fd 加入 epoll，循环 `epoll_wait` 并分发事件，用 `unordered_map<fd, unique_ptr<Connection>>` 管理全部连接 |
| `Connection` | 封装一条 TCP 连接，持有读写缓冲区，`handleRead()` / `handleWrite()` 完成数据收发 |

### 事件循环流程

```
main:  创建 Server(8888) → 创建 EventLoop → 注册监听 fd → listen → startLoop()

startLoop():
  epoll_wait 阻塞等待就绪事件
    ├─ 就绪的是监听 fd  → 循环 accept 直到 EAGAIN
    │                     新连接注册 EPOLLIN 并存入 m_connections
    └─ 就绪的是连接 fd  → 循环 handleRead()
                           ├─ n > 0  : handleWrite() 回显
                           ├─ n == 0 : 对端关闭，摘除 fd 并销毁 Connection
                           └─ n < 0  : 读到 EAGAIN 则本轮结束；出错则摘除 fd
```

监听 fd 就绪后用 `while(true)` 一次性把积压的连接全部 accept 完，是 Reactor 模式里常见的写法。

## 待办

- [ ] HTTP/1.1 请求解析（请求行、Header、Body）与响应封装
- [ ] 改用 `EPOLLET` 边缘触发，配合循环读写
- [ ] 定时器 + 心跳，清理超时空闲连接
- [ ] 引入线程池，把请求处理与 I/O 线程分离
- [ ] 静态文件服务与路由

## 许可证

[MIT](LICENSE) © 2026 MJ
