#pragma once
#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

inline void log(const char* level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::cout << "[" << ts << "] [" << level << "] " << msg << "\n";
}

inline void log_info(const std::string& m) { log("INFO ", m); }
inline void log_warn(const std::string& m) { log("WARN ", m); }
inline void log_err (const std::string& m) { log("ERROR", m); }
