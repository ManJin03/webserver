//
// Created by 33550 on 2026/10/6.
//

#include "http/Router.h"

namespace
{
    std::string_view statusText(const int status)
    {
        switch (status) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }
}

void Router::route(const HttpRequest& request , std::string_view body , Buffer& out) const
{
    const auto it = m_routes.find(request.path);
    const Response resp = it != m_routes.end()
                              ? it->second(request , body)
                              : m_default(request , body);

    out.append("HTTP/1.1 " + std::to_string(resp.status) + " " + std::string{statusText(resp.status)} + "\r\n");
    out.append("Content-Type: " + resp.contentType + "\r\n");
    out.append("Content-Length: " + std::to_string(resp.body.size()) + "\r\n");
    out.append(request.wantClose() ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
    out.append("\r\n");
    out.append(resp.body);
}
