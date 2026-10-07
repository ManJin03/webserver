# webserver

一个用 C++20 从零实现的 Linux Web 服务器。**单线程 epoll 负责 I/O，线程池负责业务**，中间用 `eventfd` 打通。

写它的目的是把网络编程里那些"知道概念但没写过"的东西真正实现一遍：非阻塞 accept、epoll 事件分发、TCP 半包粘包、HTTP 报文解析、收发分离、超时连接清理、I/O 线程与工作线程的协作。全部代码 1100 行左右，没有第三方依赖，只看 Linux 系统调用和 C++ 标准库。

## 现在能做什么

- 完整的 HTTP/1.1 请求解析：请求行、Header、按 `Content-Length` 收正文
- 路由分发：路径 → 处理函数，支持自定义状态码、Content-Type、兜底 404
- keep-alive 长连接、pipeline（一次收到多条请求）
- 尊重 `Connection: close`：客户端要求关闭时响应完就关连接
- 半包/粘包正确处理：数据没收全就继续攒，收全才处理
- 空闲连接自动清理（30 秒无数据）
- 业务逻辑在线程池里跑，耗时处理不会卡住事件循环

还没做：静态文件服务、路径参数（`/user/:id`）、HTTPS、Chunked 编码、`Transfer-Encoding`、真正的 `multipart` 解析。

## 特性

- 基于 `epoll` 的 I/O 多路复用（电平触发），单线程管理全部连接
- 全链路非阻塞：`socket` / `accept4` 都带 `SOCK_NONBLOCK`
- `SO_REUSEADDR`，重启不会 `Address already in use`
- 收发分离：一轮事件只做一件事，靠 `EPOLLIN` / `EPOLLOUT` 互切推进
- `timerfd` 定时器挂在 epoll 里，不需要额外的定时线程
- 线程池按 key 分队列，同一连接的请求串行处理，响应顺序不乱
- RAII 管理 fd：`unique_ptr` 托管连接，析构自动 `close`
- C++20，零依赖

## 编译与运行

依赖：Linux、CMake ≥ 3.10、支持 C++20 的编译器（GCC 10+ / Clang 12+）。

```bash
mkdir -p build && cd build
cmake ..
make
./webserver
```

```
Server listening on port 8888...
```

端口在 `src/main.cpp` 里改，`constexpr int PORT = 8888;`。

## 快速体验

```bash
curl -i http://127.0.0.1:8888/                  # 200 welcome
curl -i -d 'payload' http://127.0.0.1:8888/echo # 200，把请求正文原样回显
curl -i http://127.0.0.1:8888/hello             # 200，返回 HTML，正文里带你的 User-Agent
curl -i http://127.0.0.1:8888/nope              # 404
```

`/echo` 的完整报文：

```http
HTTP/1.1 200 OK
Content-Type: text/plain; charset=utf-8
Content-Length: 5
Connection: keep-alive

hello
```

想验证半包处理，用 `nc` 手敲，分几次发（每次回车后服务端不会立刻响应，直到报文完整）：

```
POST /echo HTTP/1.1
Host: 127.0.0.1
Content-Length: 5

world
```

也可以把报文拆得更碎——头和正文分两次发、正文分两次发，服务端都只会在收全之后回一次响应。

## 性能压测

仓库里带了 `WebBench`。现代发行版把 `rpc/types.h` 挪到了 `tirpc` 目录下，编译时补一个 include 路径即可：

```bash
cd WebBench
make webbench CFLAGS="-Wall -ggdb -W -O -I/usr/include/tirpc"
./webbench -c 100 -t 5 -2 http://127.0.0.1:8888/
```

`-2` 必须加：webbench 默认是 HTTP/0.9，报文只有一行 `GET /`，没有头部结束的空行，服务端会一直等下去（这恰好能验证半包不会误处理）。

### 测试结果

Release 构建（`-O2`），回环地址，webbench 每个请求新建一条短连接，每组跑 5 秒：

| 并发客户端 | 完成请求数 | 吞吐 | 失败 |
| --- | --- | --- | --- |
| 10 | 56,770 | 11.3k req/s | 0 |
| 100 | 99,654 | 19.9k req/s | 0 |
| 500 | 106,793 | 21.4k req/s | 0 |
| 1000 | 107,044 | 21.4k req/s | 0 |

四轮压测累计建立 37 万条连接，服务端日志里 **0 个 Bad request、0 个超时清理**，半包处理与连接回收在高压下没有出问题。

### 从数据里能看出什么

- **100 并发之后吞吐不再涨**，卡在 21k req/s 左右。瓶颈不在服务端逻辑，而在"每请求一条新 TCP 连接"——建连、关闭、TIME_WAIT 的内核开销占了大头。这也说明测试数据衡量的是短连接场景，换成 keep-alive 长连接会高得多（本机没有 ab/wrk，长连接场景未测）。
- **`MAXEVENTS` 不是瓶颈**：从 10 调到 1024，吞吐纹丝不动。事件循环本身跑得比网络栈快。
- **线程池 handoff 大约吃掉 12% 吞吐**：把业务逻辑改回直接在 I/O 线程执行（不投递线程池），100 并发下是 22.5k req/s，走线程池是 19.9k req/s。空业务时这笔开销是"白付"的，但它换来的是耗时业务不会阻塞事件循环——业务越重越划算。

### 压测暴露的一个真 bug

第一版压测结果是 `0 susceed, 0 failed`。查下来是服务端**不尊重 `Connection: close`**：webbench 客户端靠 `read` 返回 0（服务端关闭连接）来判断一个请求结束，而服务端一律回 `Connection: keep-alive`，客户端只能死等到 5 秒超时。

修法是让服务端正确处理连接语义：`HttpRequest::wantClose()` 判断请求是否要求关闭（`Connection: close`，或 HTTP/1.0 且没声明 keep-alive），`Router` 据此写响应头，`Connection` 记住"响应发完就关"，`EventLoop` 在发送阶段发现数据发完且要求关闭就摘除连接。修完立刻跑出了上面的数据。

## 目录结构

```
webserver/
├── CMakeLists.txt      # 构建配置
├── LICENSE             # MIT
└── src/
    ├── main.cpp        # 建路由表、装配 Server / EventLoop / ThreadPool
    ├── net/            # 网络层：只管 fd、事件与连接
    │   ├── Server.h/.cpp      # 监听 socket：socket / bind / listen / accept
    │   ├── EventLoop.h/.cpp   # epoll 事件循环、连接生命周期、超时清理
    │   └── Connection.h/.cpp  # 单条连接：读写缓冲区、解析调度、收发
    ├── http/           # 协议层：HTTP 报文解析与分发
    │   ├── HttpRequest.h/.cpp # 请求行与 Header 解析
    │   └── Router.h/.cpp      # 路由表：路径 → 处理函数
    └── base/           # 基础设施：与业务无关
        ├── Buffer.h/.cpp      # 读写双索引缓冲区
        ├── Timer.h/.cpp       # timerfd 周期定时器
        └── ThreadPool.h/.cpp  # 线程池
```

依赖方向是单向的：`base` 不依赖任何人 → `http` 依赖 `base` → `net` 依赖 `base` 和 `http` → `main` 组装全部。没有反向依赖，也没有环。

头文件一律以 `src` 为根引用（`#include "net/Server.h"`），CMake 里 `target_include_directories(webserver PRIVATE src)` 配好，不写 `../` 这种相对路径。

## 整体架构

```
                          ┌──────────────────────── 工作线程 ×4 ─────┐
                          │  ThreadPool                              │
                          │  task: Router::route() → Response         │
                          └───────────────┬──────────────────────────┘
                                          │ submitResponse(fd, data)
                                          │ + eventfd 唤醒
   ┌──────────────────────────────────────▼──────────────────────────┐
   │  I/O 线程（唯一）                                                │
   │                                                                  │
   │  EventLoop ── epoll_wait ──┬─ 监听 fd   → Server::accept()       │
   │                            ├─ 定时器 fd → checkTimeout()         │
   │                            ├─ eventfd   → handleWakeup()         │
   │                            └─ 连接 fd   → Connection             │
   │                                            ├─ handleRead()  → Buffer
   │                                            ├─ processInput() → HttpRequest
   │                                            └─ handleWrite() ← Buffer
   └──────────────────────────────────────────────────────────────────┘
```

### 核心类

| 类 | 职责 |
| --- | --- |
| `Server` | 持有监听 fd，负责 `socket` / `bind` / `listen`，`accept()` 返回新 `Connection` |
| `EventLoop` | 持有 epoll fd，循环 `epoll_wait` 并分发事件，用 `unordered_map<fd, unique_ptr<Connection>>` 管理全部连接 |
| `Connection` | 一条 TCP 连接：读写缓冲区 + 解析状态，`handleRead()` 收、`processInput()` 解析并投递任务、`handleWrite()` 发 |
| `Buffer` | 读写双索引缓冲区，为上层屏蔽 TCP 半包/粘包 |
| `HttpRequest` | 解析请求行与 Header，收不全时不消费缓冲区 |
| `Router` | 路由表，按 path 分发到处理函数，把 `Response` 拼成 HTTP 报文 |
| `Timer` | 封装 `timerfd`，到期触发超时清理 |
| `ThreadPool` | 固定线程数，按 key 分队列，跑业务逻辑 |

### 一次请求的完整旅程

以 `curl -d 'hello' http://127.0.0.1:8888/echo` 为例：

1. **accept** — 监听 fd 就绪，`EventLoop` 循环 `accept4()` 直到 `EAGAIN`，新连接注册 `EPOLLIN`，存入 `m_connections`
2. **收** — 连接 fd 就绪，`Connection::handleRead()` 用 `readv` 把数据读进读缓冲区，刷新 `lastActive`
3. **解析** — `processInput()` 调 `HttpRequest::parse()`：找 `\r\n\r\n`，找到才解析请求行和 Header；再按 `Content-Length` 确认正文收全。任一步没收全就返回 `Incomplete`，缓冲区一个字节不动，等下一次读事件
4. **投递** — 收全后把 `HttpRequest` 和正文 move 进一个任务，`pool.enqueue(fd, task)`，连接状态立即复位，I/O 线程继续收下一条
5. **处理** — 工作线程执行任务：`Router::route()` 查表、调 handler、拼出完整 HTTP 报文
6. **交回** — 工作线程调 `EventLoop::submitResponse(fd, data)`，数据存进 `m_pendingResponses`，写 `eventfd` 唤醒 I/O 线程
7. **发送** — I/O 线程被唤醒，`handleWakeup()` 把数据写进连接的写缓冲区，`epoll_ctl(MOD)` 切成 `EPOLLOUT`；下一轮 `epoll_wait` 返回后 `handleWrite()` 发出，发完切回 `EPOLLIN`

整个过程中 socket 只被 I/O 线程碰过，fd 的读写从来没有跨线程发生。

## 模块详解

### Buffer：为什么必须有它

TCP 是字节流，没有消息边界。你 `send("hello")`，对端可能收到 `"hel"` + `"lo"`；你连发两条，对端可能一次收到 `"ab"`。所以**一次 `read` 的返回值不能当成"一条消息"**。

`Buffer` 的做法是一块连续内存 + 两个下标，切成三段：

```
├─────────┬──────────────────┬─────────────────┤
 prepend   待处理数据(readable)  可写空间(writable)
           ↑                  ↑
        m_readIndex        m_writeIndex
```

- 读数据 = 推进 `m_writeIndex`
- 消费数据 = 推进 `m_readIndex`
- 全程不搬移字节，只有空间不足时才整理

开头空出的 8 字节 `BUFFER_CHEAP_PREPEND` 是预留位：将来要在响应前面回填内容（比如 chunked 的长度前缀）时，不用整体挪动数据。

**收数据用 `readv` 分散读**：

```cpp
vec[0].iov_base = beginWrite();  vec[0].iov_len = writable;
vec[1].iov_base = extrabuf;      vec[1].iov_len = sizeof(extrabuf);  // 64KB 栈上备用
const ssize_t n = readv(fd, vec, 2);
```

先填满缓冲区剩余空间，装不下的溢到栈上临时区，再 `append` 进去。好处是不会因为"缓冲区只剩 10 字节"就只读 10 字节（那样会触发大量无谓的 epoll 事件），也不会因为猜错缓冲区大小而丢数据。

**空间不足时三级策略**，能不扩容就不扩容：

1. 尾部空间够 → 直接写
2. 尾部不够但"尾部 + 已消费的垃圾"够 → 把待处理数据前移复用空间，只内存拷贝
3. 都不够 → 才 `resize()`

长连接会被反复读写，第 2 级策略让固定大小的缓冲区能一直循环使用，否则前面已消费的字节永远浪费掉。

**判断"收全了"是上层的责任**：`peek()` 偷看、`findCRLF()` 找分隔符，确认完整才 `retrieve()` 消费。没收到就什么都不动。

### HttpRequest：解析规则

```cpp
enum class ParseResult { Incomplete, Complete, Error };

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;

    ParseResult parse(Buffer& buf);
};
```

解析步骤：

1. 在 `buf.peek()` 上找 `\r\n\r\n`（头部结束标志），找不到 → `Incomplete`
2. 第一行按空白切出 `method` / `path` / `version`，切不满三个 → `Error`
3. 其余行按 `:` 拆键值对，key 转小写（`Host` 和 `host` 都能查到），value 去首尾空白
4. 全部成功后才 `buf.retrieve()`，连结尾空行一起取走，正文留在缓冲区

关键点在第 1 步和第 4 步：**半包时一个字节都不消费**。这样不需要保存中间状态机，下次读事件追加数据后从请求行重新解析即可，逻辑非常简单。

正文长度由 `Connection` 按 `Content-Length` 判定——`Buffer` 里可读字节数不够就继续等，够了才取走处理。

### Router：把业务和网络隔开

```cpp
struct Response
{
    int status{200};
    std::string body{};
    std::string contentType{"text/plain; charset=utf-8"};
};

using Handler = std::function<Response(const HttpRequest& , std::string_view)>;
```

处理函数的签名很克制：**给它请求和正文，它还你一个"要回什么"，全程碰不到 socket 和缓冲区**。状态行文本（`200 OK` / `404 Not Found` / `500` 等）由 `Router` 按状态码映射，`Content-Length` 和 `Connection` 头也不用业务操心。

注册方式：

```cpp
router->addRoute("/echo" , [] (const HttpRequest& , std::string_view body)
{
    return Response{200 , std::string{body}};
});

router->setDefaultHandler([] (const HttpRequest& request , std::string_view)
{
    return Response{404 , "404 Not Found: " + request.path};
});
```

因为 handler 能拿到完整的 `HttpRequest`，按 Header 分支、按 method 分支都可以在 handler 内部自己写，不需要改框架。

### Connection：一条连接的状态机

持有读缓冲区、写缓冲区、一个 `HttpRequest` 和 `bool m_headerParsed`。后两者是跨多次读事件保留的解析进度——半包场景下进度不会丢。

```cpp
ParseResult processInput(const Router& router , ThreadPool& pool , EventLoop& loop);
```

内部是循环：解析出一条完整请求就投递一个任务，直到数据不够（`Incomplete`）或报文非法（`Error`）。一次读事件里攒着的多条请求（pipeline）会被逐条处理。

### EventLoop：四类 fd 一个循环

epoll 里注册了四类 fd，靠 `data.fd` 区分：

| fd | 就绪时做什么 |
| --- | --- |
| 监听 fd | 循环 `accept4` 直到 `EAGAIN` |
| `timerfd` | 读掉计数，扫一遍连接清超时 |
| `eventfd` | 取回工作线程算好的响应 |
| 连接 fd | 按当前关注的事件进入接收或发送阶段 |

连接 fd 上的收发是两个阶段，靠 `epoll_ctl(MOD)` 互切：

```
接收阶段（EPOLLIN）
  └─ 循环 handleRead() + processInput()
     ├─ 收全了   → 投递任务给线程池
     ├─ 没收全   → 保持 EPOLLIN 等数据
     ├─ n == 0   → 对端关闭，摘除连接
     └─ Error    → 报文非法，摘除连接

发送阶段（EPOLLOUT）
  └─ handleWrite()
     ├─ 没发完 → 保持 EPOLLOUT，下一轮继续
     ├─ 发完了 → MOD 回 EPOLLIN
     └─ 出错   → 摘除连接
```

**注意不要注册成 `EPOLLIN | EPOLLOUT`**：电平触发下 socket 几乎总是可写，`EPOLLOUT` 会一直就绪导致空转。只在真有数据要发时才切过去，发完立刻切回来。

`MAXEVENTS` 是 1024，即一次 `epoll_wait` 最多取 1024 个事件。实测把它从 10 调到 1024 吞吐没有变化——事件批量不是这个模型的瓶颈。

### Timer：为什么用 timerfd

`timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)` 创建的是一个**可以被 epoll 监听的 fd**，到期就变可读。它天然融入事件循环，不需要额外的定时线程，也不需要给定时器加锁。

到期后必须把计数读掉，否则电平触发下会一直就绪、把 CPU 打满。

连接空闲时间用 `steady_clock` 记录而不是 `system_clock`——后者会被 NTP 校时或手动改时间影响，可能导致连接被误杀或永不超时。

### ThreadPool：按 key 分队列

```cpp
void enqueue(std::uint64_t key , std::function<void()> task);
```

每个工作线程一条自己的队列，`key % 线程数` 选队列。用 fd 当 key，于是**同一连接的所有任务落在同一个线程上，FIFO 执行**，keep-alive 和 pipeline 场景下响应顺序不会乱。这个设计还顺带减少了锁竞争——每个队列一把锁，而不是全局一把。

## 线程模型

I/O 线程只有一个，独占 epoll、监听 fd 和所有 `Connection`，只做三件事：收数据、解析头部、发数据。业务逻辑全在线程池里。

```
I/O 线程                              工作线程 ×4
handleRead + 解析头部
  └─ 请求收全 → pool.enqueue(fd, task)
                                      task: router.route() 算出响应
        submitResponse(fd, data) ←────┘   结果交回，不直接写 socket
  └─ appendOutput + MOD EPOLLOUT
handleWrite → 发送
```

三个必须说清楚的设计：

**工作线程不碰 socket。** 它算完响应后调 `submitResponse()`，把字节存进 `m_pendingResponses`（加锁），再写一个 `eventfd` 把阻塞在 `epoll_wait` 上的 I/O 线程唤醒，由 I/O 线程写缓冲区并发送。这样 fd 只被一个线程操作，既不用给 socket 加锁，也不会出现多线程并发 `write` 导致报文交错。

**任务里不带 `Connection` 指针。** 只带 fd 和解析好的请求。如果任务排队期间连接被超时清理或客户端断开，`handleWakeup()` 发现 `!m_connections.contains(fd)` 就直接丢弃结果，不会踩到已释放的对象。这是"跨线程回写"里最容易踩的坑。

**唤醒是必需的。** I/O 线程阻塞在 `epoll_wait(-1)` 上，没有新事件就不会醒。工作线程算完响应这件事本身不是 epoll 事件，所以必须靠 `eventfd` 制造一个。

实测：4 个并发的「耗时 1 秒」请求，总耗时 1021 ms，日志显示落在 4 个不同线程上——确实并行，且 I/O 线程没被卡住。

## 几个设计取舍

**为什么不用 `EPOLLET` 边缘触发？** 电平触发（默认）写代码更宽容：没读完的数据下一轮还会再通知一次。边缘触发要求每次必须读到 `EAGAIN` 为止，漏一次就永远不再通知，容易出隐蔽 bug。等项目跑稳了再切 ET 是更合理的顺序（已在待办里）。

**为什么不是每连接一线程？** 一万连接就一万线程，光栈内存和调度开销就撑不住。Reactor（epoll + 非阻塞）用少数线程扛大量连接，是 Linux 下高并发的标准答案。

**为什么业务要丢给线程池而不是直接在 I/O 线程跑？** 一个慢查询就会卡住整个事件循环，所有连接一起饿死。I/O 线程只做纯粹的数据搬运，把耗时逻辑隔离出去。

**为什么解析头部在 I/O 线程、业务在线程池？** 解析是纯 CPU 操作且很快，放在 I/O 线程能省掉一次跨线程传递；而且解析结果决定了"这条请求收没收全"，这是 I/O 层必须知道的信息。业务处理则完全不需要知道 socket 的存在，天然适合隔离。

## 待办

已完成：

- [x] HTTP/1.1 请求解析（请求行 + Header）与响应封装，正文按 `Content-Length` 收取
- [x] 路由模块：路径 → 处理函数，支持自定义状态码与 Content-Type
- [x] 收发分离：接收后切 `EPOLLOUT` 下一轮再发，未发完的数据续发
- [x] `timerfd` 定时器：每 5s 扫一遍，清掉空闲超过 30s 的连接
- [x] 线程池：业务在工作线程跑，按 fd 分队列保证同一连接的响应顺序

待办：

- [ ] 改用 `EPOLLET` 边缘触发，配合循环读写
- [ ] 用最小堆 / 时间轮管理定时器，避免每次全量扫描连接
- [ ] 静态文件服务、路径参数（`/user/:id`）、method 匹配
- [ ] 支持 `Transfer-Encoding: chunked`
- [ ] 优雅关闭线程池：目前 `startLoop()` 是死循环，退出路径还没打通

## 许可证

[MIT](LICENSE) © 2026 MJ
