# webserver

一个用 C++ 从零实现的 Linux 高性能 Web 服务器，基于 **epoll + 非阻塞 I/O** 的单线程事件驱动模型。

项目处于早期阶段：网络框架（监听、连接管理、事件循环）与 HTTP 请求解析已经跑通，目前会把请求的 **Body 原样回显** 成 `200 OK` 响应，路由与静态文件尚未实现。

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
├── CMakeLists.txt      # 构建配置
├── LICENSE             # MIT
└── src/
    ├── main.cpp        # 程序入口：建路由表、创建 Server 与 EventLoop 并启动循环
    ├── net/            # 网络层：只管 fd、事件与连接，不关心协议内容
    │   ├── Server.h/.cpp      # 监听 socket：socket / bind / listen / accept
    │   ├── EventLoop.h/.cpp   # epoll 事件循环：事件注册、分发、连接生命周期与超时清理
    │   └── Connection.h/.cpp  # 单条 TCP 连接：读写缓冲区、请求解析与收发
    ├── http/           # 协议层：HTTP 报文的解析与分发
    │   ├── HttpRequest.h/.cpp # 请求行与 Header 解析，配合 Buffer 处理半包
    │   └── Router.h/.cpp      # 路由表：路径 → 处理函数，未命中走兜底 handler
    └── base/           # 基础设施：与业务无关的可复用组件
        ├── Buffer.h/.cpp      # 读写双索引缓冲区：自动扩容、从 fd 读满数据
        └── Timer.h/.cpp       # 基于 timerfd 的周期定时器
```

头文件一律以 `src` 为根引用（`#include "net/Server.h"`、`#include "base/Buffer.h"`），
CMake 里用 `target_include_directories(webserver PRIVATE src)` 配好，不需要相对路径 `../`。

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

## 注册路由

在 `main.cpp` 里给路径挂一个 lambda 就行，处理函数只负责"回什么"，不碰 socket：

```cpp
const auto router = std::make_shared<Router>();

router->addRoute("/echo" , [](const HttpRequest& , std::string_view body)
{
    return Response{200 , std::string{body}};   // 回显请求正文
});

router->addRoute("/hello" , [](const HttpRequest& request , std::string_view)
{
    Response resp{200 , "<h1>hello</h1>"};
    resp.contentType = "text/html; charset=utf-8";
    return resp;
});

router->setDefaultHandler([](const HttpRequest& request , std::string_view)
{
    return Response{404 , "404 Not Found: " + request.path};
});

loop->setRouter(router);
```

`Response` 的三个字段：`status`（默认 200）、`body`、`contentType`（默认 `text/plain; charset=utf-8`）。状态行文本由 `Router` 按状态码映射，重复的 `Content-Length` / `Connection` 头不用操心。

## 测试

用 `curl` 打几个注册过的路径：

```bash
curl -i http://127.0.0.1:8888/            # 200 welcome
curl -i -d 'payload' http://127.0.0.1:8888/echo   # 200 payload
curl -i http://127.0.0.1:8888/hello       # 200，返回 HTML，含 User-Agent
curl -i http://127.0.0.1:8888/nope        # 404
```

回显示例：

```bash
curl -i -d 'hello' http://127.0.0.1:8888/echo
```

```http
HTTP/1.1 200 OK
Content-Type: text/plain; charset=utf-8
Content-Length: 5
Connection: keep-alive

hello
```

不带正文的 `GET` 返回 `Content-Length: 0`。也可以直接 `nc 127.0.0.1 8888` 手敲报文，以空行结束：

```
GET /hello HTTP/1.1
Host: 127.0.0.1

```

头部或正文没发完（比如只发一半）时服务端不会响应，等数据收全后才回一次——这正是 `Buffer` 累积数据的作用。

## 架构说明

### 核心类

| 类 | 职责 |
| --- | --- |
| `Server` | 持有监听 fd，负责 `socket` / `bind` / `listen`，`accept()` 返回一个新的 `Connection` |
| `EventLoop` | 持有 epoll fd，把监听 fd 加入 epoll，循环 `epoll_wait` 并分发事件，用 `unordered_map<fd, unique_ptr<Connection>>` 管理全部连接 |
| `Connection` | 封装一条 TCP 连接，持有读写缓冲区与 `HttpRequest`，`handleRead()` 收数据、`processInput()` 解析并生成响应、`handleWrite()` 发送 |
| `Buffer` | 读写双索引缓冲区，为上层屏蔽 TCP 半包/粘包 |
| `Router` | 路由表，按 `path` 分发到注册的处理函数，把产出的 `Response` 拼成 HTTP 报文 |
| `Timer` | 封装 `timerfd`，周期到期时触发 `EventLoop::checkTimeout()` 清理空闲连接 |
| `HttpRequest` | 解析请求行与 Header，收不全时不消费缓冲区 |

### 事件循环流程

```
main:  创建 Server(8888) → 创建 EventLoop → 注册监听 fd → listen → startLoop()

startLoop():
  epoll_wait 阻塞等待就绪事件
    ├─ 就绪的是监听 fd  → 循环 accept 直到 EAGAIN
    │                     新连接注册 EPOLLIN 并存入 m_connections
    └─ 就绪的是连接 fd → 接收阶段（EPOLLIN）
                           └─ 循环 handleRead() + processInput() 解析请求、填好响应
                              ├─ 有响应待发 : MOD 成 EPOLLOUT，下一轮再发送
                              ├─ 还没收全   : 保持 EPOLLIN 继续等数据
                              ├─ n == 0     : 对端关闭，摘除 fd 并销毁 Connection
                              ├─ n < 0      : EAGAIN 本轮结束；出错则摘除 fd
                              └─ Error      : 报文非法，摘除 fd
       下一轮就绪的是同一个 fd（EPOLLOUT）→ 发送阶段
                           └─ handleWrite() 把写缓冲区里的数据发出去
                              ├─ 没发完 : 保持 EPOLLOUT，再等下一轮
                              ├─ 发完了 : MOD 回 EPOLLIN，继续收下一条请求
                              └─ 出错   : 摘除 fd
```

收和发拆成两个阶段：一轮 `epoll_wait` 里只做一件事，响应攒在写缓冲区里，靠 `EPOLLIN` / `EPOLLOUT` 互切推进。好处是发送不会被接收拖住，写缓冲区没发完的数据能在后续轮次续发，不会丢。

`processInput()` 内部循环处理，一次读事件里攒着的多条请求（pipeline）会被逐条解析、逐条填入写缓冲区。

监听 fd 就绪后用 `while(true)` 一次性把积压的连接全部 accept 完，是 Reactor 模式里常见的写法。

## 待办

- [x] HTTP/1.1 请求解析（请求行 + Header）与响应封装；Body 按 `Content-Length` 收取
- [x] 收发分离：接收后切 `EPOLLOUT`，下一轮再发；未发完的数据留到后续轮次续发
- [x] `timerfd` 定时器：每 5s 扫一遍连接，清掉空闲超过 30s 的连接
- [ ] 改用 `EPOLLET` 边缘触发，配合循环读写
- [ ] 用最小堆 / 时间轮管理定时器，避免每次全量扫描连接
- [ ] 引入线程池，把请求处理与 I/O 线程分离
- [x] 路由模块：路径 → 处理函数，支持自定义响应状态码与 Content-Type
- [ ] 静态文件服务、路径参数（`/user/:id`）与方法匹配（GET/POST 分发到不同 handler）

## 许可证

[MIT](LICENSE) © 2026 MJ
