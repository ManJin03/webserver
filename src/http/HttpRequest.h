//
// Created by 33550 on 2026/10/5.
//

#ifndef WEBSERVER_HTTPREQUEST_H
#define WEBSERVER_HTTPREQUEST_H

#include "base/Buffer.h"

#include <string>
#include <unordered_map>

enum class ParseResult
{
    Incomplete , // 数据没收全，等待下一次读事件继续收
    Complete , // 请求行与头部解析完成
    Error // 报文格式非法
};

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;

    std::unordered_map<std::string,std::string> headers;

    // 从 Buffer 里解析请求行与头部。数据没收全时返回 Incomplete 且一个字节都不消费，
    // 这样下一次读事件追加数据后可以从头重新解析；解析成功才把已消费的头部取走。
    ParseResult parse(Buffer& buf);
};


#endif //WEBSERVER_HTTPREQUEST_H
