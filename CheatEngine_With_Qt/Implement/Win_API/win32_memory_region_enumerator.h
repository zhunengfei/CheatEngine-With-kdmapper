#pragma once
#include "imemory_region_enumerator.h"
#include "scan_data_stream_define.h"
#include <Windows.h>
#include <vector>

class Win32MemoryRegionEnumerator : public IMemoryRegionEnumerator
{
public:
    std::vector<MemoryRegion> enumerate() override;
	std::vector<MemoryRegion> enumerate(const ScanRequest& req) override;
private:
    // 辅助函数：判断内存页保护属性
    inline bool isWritable(DWORD protect) {
        return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }

    inline bool isExecutable(DWORD protect) {
        return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    }
};