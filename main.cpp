//
// Created by 33550 on 2026/10/1.
//

#include "EventLoop.h"

constexpr int PORT = 8888;

int main()
{
    const auto server = std::make_shared<Server>(PORT);
    const auto loop = std::make_unique<EventLoop>();
    loop->setServer(server);
    server->start();
    loop->startLoop();
    return 0;
}
