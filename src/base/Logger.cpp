//
// Created by 33550 on 2026/10/21.
//

#include "base/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
    std::string timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
        localtime_r(&time , &local);

        char date[32]{};
        std::strftime(date , sizeof(date) , "%Y-%m-%d %H:%M:%S" , &local);

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::ostringstream ss;
        ss << date << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    const char* levelName(const Logger::Level level)
    {
        switch (level) {
            case Logger::Level::Info: return "INFO";
            case Logger::Level::Warn: return "WARN";
            case Logger::Level::Error: return "ERROR";
        }
        return "UNKNOWN";
    }
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

bool Logger::init(const std::string& path)
{
    std::lock_guard lock{m_mutex};
    m_file.open(path , std::ios::app);
    return m_file.is_open();
}

void Logger::write(const Level level , const std::string& message)
{
    std::ostringstream ss;
    ss << timestamp() << ' ' << levelName(level) << " [" << std::this_thread::get_id() << "] " << message << '\n';
    const std::string line = ss.str();

    std::lock_guard lock{m_mutex};
    if (m_file.is_open()) {
        m_file << line;
        m_file.flush(); // 立即落盘，进程崩了也不丢
    }
    std::cout << line << std::flush;
}
