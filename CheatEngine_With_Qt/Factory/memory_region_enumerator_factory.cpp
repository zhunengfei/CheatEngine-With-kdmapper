#include "memory_region_enumerator_factory.h"


std::unique_ptr<IMemoryRegionEnumerator> MemoryRegionEnumeratorFactory::create(MemoryBackend type)
{
    switch (type)
    {
    case MemoryBackend::Win32:
        return std::make_unique<Win32MemoryRegionEnumerator>();
        // Î´À´£ºcase MemoryBackend::DbkDriver: ...
    default:
        return nullptr;
    }
}