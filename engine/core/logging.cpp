#include "logging.h"

namespace chonk {
namespace core {

void log(LogLevel level, const std::string& message) {
    const char* prefix = "";
    switch (level) {
        case LogLevel::DEBUG: prefix = "[DEBUG] "; break;
        case LogLevel::INFO: prefix = "[INFO] "; break;
        case LogLevel::WARNING: prefix = "[WARNING] "; break;
        case LogLevel::ERROR: prefix = "[ERROR] "; break;
    }
    std::cout << prefix << message << std::endl;
}

} // namespace core
} // namespace chonk

