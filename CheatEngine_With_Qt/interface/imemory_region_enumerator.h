#pragma once
#include <vector>
#include "memory_region.h"

class IMemoryRegionEnumerator
{
public:
    virtual ~IMemoryRegionEnumerator() = default;
    virtual std::vector<MemoryRegion> enumerate() = 0;
};