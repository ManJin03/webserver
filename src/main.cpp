//
// Created by 33550 on 2026/10/1.
//

#include "net/EventLoop.h"
#include "http/Router.h"
#include "base/ThreadPool.h"

constexpr int PORT = 8888;
// 工作线程数：业务处理在这里跑，I/O 线程只负责收发
constexpr std::size_t WORKER_THREADS = 4;

int main()
{
    // 路由表：每个路径注册自己的处理方式
    const auto router = std::make_shared<Router>();
    router->addRoute("/" , [] (const HttpRequest& , std::string_view)
    {
        return Response{200 , "welcome"};
    });
    router->addRoute("/echo" , [] (const HttpRequest& , std::string_view body)
    {
        return Response{200 , std::string{body}}; // 把请求正文原样回显
    });
    router->addRoute("/hello" , [] (const HttpRequest& request , std::string_view)
    {
        const auto it = request.headers.find("user-agent");
        const std::string ua = it == request.headers.end() ? "unknown" : it->second;
        Response resp{200 , "<h1>hello</h1><p>ua: " + ua + "</p>"};
        resp.contentType = "text/html; charset=utf-8";
        return resp;
    });
    router->setDefaultHandler([] (const HttpRequest& request , std::string_view)
    {
        return Response{404 , "404 Not Found: " + request.path};
    });

    const auto pool = std::make_shared<ThreadPool>(WORKER_THREADS);

    const auto server = std::make_shared<Server>(PORT);
    const auto loop = std::make_unique<EventLoop>();
    loop->setServer(server);
    loop->setRouter(router);
    loop->setThreadPool(pool);
    server->start();
    loop->startLoop();
    return 0;
}
