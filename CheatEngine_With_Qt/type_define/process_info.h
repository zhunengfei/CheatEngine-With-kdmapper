#pragma once
#include <string>
#include <cstdint>

struct ProcessInfo
{
    uint32_t pid;
    std::string  name;
    std::string exePath;
    bool hasWindow = false;
};