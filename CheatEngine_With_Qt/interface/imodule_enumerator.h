#pragma once
#include <vector>
#include <cstdint>
#include "module_info.h"

class IModuleEnumerator
{
public:
    virtual ~IModuleEnumerator() = default;
    virtual std::vector<ModuleInfo> enumerate(uint32_t pid) = 0;
};