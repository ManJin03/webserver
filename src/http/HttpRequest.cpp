//
// Created by 33550 on 2026/10/5.
//

#include "http/HttpRequest.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace
{
    constexpr std::string_view CRLF{"\r\n"};
    constexpr std::string_view HEAD_END{"\r\n\r\n"};

    std::string_view trim(std::string_view str)
    {
        constexpr std::string_view blank{" \t"};
        const auto begin = str.find_first_not_of(blank);
        if (begin == std::string_view::npos) { return {}; }
        const auto end = str.find_last_not_of(blank);
        return str.substr(begin , end - begin + 1);
    }

    std::string toLower(std::string_view str)
    {
        std::string result{str};
        std::transform(result.begin() , result.end() , result.begin() ,
                       [] (const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }
}

ParseResult HttpRequest::parse(Buffer& buf)
{
    const std::string_view view{buf.peek() , buf.readableBytes()};
    // 头部以空行结束，找不到 "\r\n\r\n" 说明报文还没收全
    const auto headEnd = view.find(HEAD_END);
    if (headEnd == std::string_view::npos) { return ParseResult::Incomplete; }

    const std::string_view head = view.substr(0 , headEnd);

    // 第一行是请求行：METHOD PATH VERSION
    auto lineEnd = head.find(CRLF);
    std::istringstream requestLine{std::string{head.substr(0 , lineEnd)}};
    if (!(requestLine >> method >> path >> version)) { return ParseResult::Error; }

    headers.clear();
    // 其余各行是 Header：Name: value
    while (lineEnd != std::string_view::npos) {
        const auto lineBegin = lineEnd + CRLF.size();
        lineEnd = head.find(CRLF , lineBegin);
        const std::string_view line = head.substr(lineBegin ,
                                                  lineEnd == std::string_view::npos
                                                      ? std::string_view::npos
                                                      : lineEnd - lineBegin);
        if (line.empty()) { continue; }

        const auto colon = line.find(':');
        if (colon == std::string_view::npos) { return ParseResult::Error; }
        headers.emplace(toLower(trim(line.substr(0 , colon))) ,
                        trim(line.substr(colon + 1)));
    }

    // 解析成功才消费缓冲区，连结尾的空行一起取走，正文留给后续处理
    buf.retrieve(headEnd + HEAD_END.size());
    return ParseResult::Complete;
}

bool HttpRequest::wantClose() const
{
    if (const auto it = headers.find("connection") ; it != headers.end()) {
        const std::string value = toLower(it->second);
        if (value.find("close") != std::string::npos) { return true; }
        if (value.find("keep-alive") != std::string::npos) { return false; }
    }
    // HTTP/1.1 默认持久连接，更早的版本默认发完就关
    return version != "HTTP/1.1";
}
