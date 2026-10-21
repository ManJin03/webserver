//
// Created by 33550 on 2026/10/21.
//

#ifndef WEBSERVER_LOGGER_H
#define WEBSERVER_LOGGER_H

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

// 运行日志：多线程共用一个实例，按 "时间 级别 [线程] 内容" 追加到日志文件，
// 同时在控制台留一份。没 init 或打开失败时只写控制台。
class Logger
{
public:
    enum class Level
    {
        Info ,
        Warn ,
        Error
    };

    static Logger& instance();

    // 以追加方式打开日志文件，返回是否打开成功
    bool init(const std::string& path);

    // 内容可以是任意能流输出的东西，拼成一行再落盘
    template <typename... Args>
    void log(const Level level , Args&&... args)
    {
        std::ostringstream ss;
        (ss << ... << std::forward<Args>(args));
        write(level , ss.str());
    }

private:
    Logger() = default;

    void write(Level level , const std::string& message);

    std::ofstream m_file;
    std::mutex m_mutex;
};

#define LOG_INFO(...) Logger::instance().log(Logger::Level::Info , __VA_ARGS__)
#define LOG_WARN(...) Logger::instance().log(Logger::Level::Warn , __VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().log(Logger::Level::Error , __VA_ARGS__)

#endif //WEBSERVER_LOGGER_H
