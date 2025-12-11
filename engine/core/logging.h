#pragma once

#include <iostream>
#include <string>

namespace chonk {
namespace core {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

void log(LogLevel level, const std::string& message);

#define LOG_DEBUG(msg) chonk::core::log(chonk::core::LogLevel::DEBUG, msg)
#define LOG_INFO(msg) chonk::core::log(chonk::core::LogLevel::INFO, msg)
#define LOG_WARNING(msg) chonk::core::log(chonk::core::LogLevel::WARNING, msg)
#define LOG_ERROR(msg) chonk::core::log(chonk::core::LogLevel::ERROR, msg)

} // namespace core
} // namespace chonk

