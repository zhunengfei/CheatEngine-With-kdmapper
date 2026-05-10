#pragma once
#include <vector>
#include "memory_region.h"
#include "scan_data_stream_define.h"

class IMemoryRegionEnumerator
{
public:
    virtual ~IMemoryRegionEnumerator() = default;
    virtual std::vector<MemoryRegion> enumerate() = 0;
    virtual std::vector<MemoryRegion> enumerate(const ScanRequest& req) = 0;
};