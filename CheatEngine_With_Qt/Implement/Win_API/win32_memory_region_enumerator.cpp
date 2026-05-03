#include "win32_memory_region_enumerator.h"
#include "process_manager.h"
#include <Windows.h>

std::vector<MemoryRegion> Win32MemoryRegionEnumerator::enumerate()
{
    std::vector<MemoryRegion> regions;

    auto mem = ProcessManager::instance().memory();

    // ⭐ 防御1：没附加进程
    if (!mem)
        return regions;

    HANDLE hProcess = reinterpret_cast<HANDLE>(
        ProcessManager::instance().memory()->nativeHandle()
        );
    // ⭐ 防御2：handle无效
    if (!hProcess)
        return regions;

    MEMORY_BASIC_INFORMATION mbi;

    uint64_t addr = 0;

    while (VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)))
    {
        // 只扫描可读区域（CE核心逻辑）
        if (mbi.State == MEM_COMMIT &&
            !(mbi.Protect & PAGE_NOACCESS) &&
            !(mbi.Protect & PAGE_GUARD))
        {
            regions.push_back({
                (uint64_t)mbi.BaseAddress,
                mbi.RegionSize
                });
        }

        addr += mbi.RegionSize;
    }

    return regions;
}