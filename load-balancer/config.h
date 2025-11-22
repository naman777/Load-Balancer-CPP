#pragma once
#include "LoadBalancer.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct LBConfig {
    int port                  = 8080;
    std::vector<int> backends = {8001, 8002, 8003};
    std::vector<int> weights  = {};  // empty = all weight 1
    Algorithm algo            = Algorithm::LEAST_CONNECTIONS;
    size_t threads            = 16;
    int max_conn              = 0;   // 0 = unlimited
};

// Parses a simple "key = value" config file.
// Blank lines and lines starting with '#' are ignored.
inline LBConfig load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open config: " + path);

    auto trim = [](std::string s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    };

    LBConfig cfg;
    std::string line;
    while (std::getline(f, line)) {
        if (auto p = line.find('#'); p != std::string::npos) line.erase(p);
        line = trim(line);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "port") {
            cfg.port = std::stoi(val);
        } else if (key == "backends") {
            cfg.backends.clear();
            std::stringstream ss(val);
            std::string tok;
            while (std::getline(ss, tok, ','))
                cfg.backends.push_back(std::stoi(trim(tok)));
        } else if (key == "algo") {
            if (val == "rr")      cfg.algo = Algorithm::ROUND_ROBIN;
            else if (val == "ih") cfg.algo = Algorithm::IP_HASH;
            else if (val == "rh") cfg.algo = Algorithm::RENDEZVOUS;
            else if (val == "lc") cfg.algo = Algorithm::LEAST_CONNECTIONS;
            else throw std::runtime_error("Unknown algo in config: " + val);
        } else if (key == "weights") {
            cfg.weights.clear();
            std::stringstream ss(val);
            std::string tok;
            while (std::getline(ss, tok, ','))
                cfg.weights.push_back(std::stoi(trim(tok)));
        } else if (key == "threads") {
            cfg.threads = static_cast<size_t>(std::stoi(val));
        } else if (key == "max_conn") {
            cfg.max_conn = std::stoi(val);
        }
    }
    return cfg;
}
