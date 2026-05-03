#pragma once
#include "imemory_region_enumerator.h"
#include <vector>

class Win32MemoryRegionEnumerator : public IMemoryRegionEnumerator
{
public:
    std::vector<MemoryRegion> enumerate() override;
};