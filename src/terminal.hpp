#pragma once
#include "clicalc/engine.hpp"
#include <filesystem>
#include <string>

namespace clicalc::cli {
void clear_screen();
class Terminal {
public:
    Terminal(Engine& engine, std::filesystem::path history);
    ~Terminal();
    bool read(std::string& line);
private:
    std::filesystem::path history_;
};
}
