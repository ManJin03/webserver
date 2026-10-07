cmake_minimum_required(VERSION 3.10)

project(webserver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(webserver
        src/main.cpp
        # 网络层：监听、连接、事件循环
        src/net/Server.cpp
        src/net/Server.h
        src/net/Connection.cpp
        src/net/Connection.h
        src/net/EventLoop.cpp
        src/net/EventLoop.h
        # 协议层：请求解析与路由
        src/http/HttpRequest.cpp
        src/http/HttpRequest.h
        src/http/Router.cpp
        src/http/Router.h
        # 基础设施：缓冲区与定时器
        src/base/Buffer.cpp
        src/base/Buffer.h
        src/base/Timer.cpp
        src/base/Timer.h
)

# 头文件一律以 src 为根引用，如 #include "net/Server.h"
target_include_directories(webserver PRIVATE src)
