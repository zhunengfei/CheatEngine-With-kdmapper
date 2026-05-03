#pragma once
#include "imemory_region_enumerator.h"
#include <memory>
#include "memory_accessor_factory.h"   // ¸´ÓÃ MemoryBackend Ã¶¾Ù
#include "win32_memory_region_enumerator.h"


class MemoryRegionEnumeratorFactory
{
public:
    static std::unique_ptr<IMemoryRegionEnumerator> create(MemoryBackend type);
};