cmake_minimum_required(VERSION 3.10)

project(webserver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(webserver
        main.cpp
        Server.cpp
        Server.h
        Connection.cpp
        Connection.h
        EventLoop.cpp
        EventLoop.h
        Buffer.cpp
        Buffer.h
)
