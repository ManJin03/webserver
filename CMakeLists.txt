cmake_minimum_required(VERSION 3.10)

project(webserver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 未指定构建类型时默认 Release，否则压测跑的是 -O0 的代码
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

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
        # 基础设施：缓冲区、定时器与线程池
        src/base/Buffer.cpp
        src/base/Buffer.h
        src/base/Timer.cpp
        src/base/Timer.h
        src/base/ThreadPool.cpp
        src/base/ThreadPool.h
)

find_package(Threads REQUIRED)
target_link_libraries(webserver PRIVATE Threads::Threads)

# 头文件一律以 src 为根引用，如 #include "net/Server.h"
target_include_directories(webserver PRIVATE src)
