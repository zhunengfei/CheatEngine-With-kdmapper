#pragma once
#include <cstdint>
#include <string>

struct ModuleInfo
{
    std::string name;
    uint64_t base;
    uint64_t size;
};