//
// Created by 33550 on 2026/10/6.
//

#ifndef WEBSERVER_ROUTER_H
#define WEBSERVER_ROUTER_H

#include "Buffer.h"
#include "HttpRequest.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

// 处理函数产出的结果，由 Router 负责拼成完整的 HTTP 响应
struct Response
{
    int status{200};
    std::string body{};
    std::string contentType{"text/plain; charset=utf-8"};
};

class Router
{
public:
    // 参数是解析好的请求与请求正文，返回值只描述"回什么"，不碰 socket
    using Handler = std::function<Response(const HttpRequest& , std::string_view)>;

    // 注册某个路径的处理方式，重复注册同一路径会被覆盖
    void addRoute(std::string path , Handler handler)
    {
        m_routes.insert_or_assign(std::move(path) , std::move(handler));
    }

    // 注册兜底处理方式，未命中的路径走这里，默认 404
    void setDefaultHandler(Handler handler)
    {
        m_default = std::move(handler);
    }

    // 按请求路径分发，把响应写入输出缓冲区
    void route(const HttpRequest& request , std::string_view body , Buffer& out) const;

private:
    std::unordered_map<std::string,Handler> m_routes;
    Handler m_default{
        [] (const HttpRequest& , std::string_view)
        {
            return Response{404 , "404 Not Found"};
        }
    };
};


#endif //WEBSERVER_ROUTER_H
