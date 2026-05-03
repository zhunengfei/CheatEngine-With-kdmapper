#pragma once
#include "imemory_accessor.h"
#include <memory>

enum class MemoryBackend
{
    Win32,
    // ‘§¡Ù£∫DbkDriver, ...
};

class MemoryAccessorFactory
{
public:
    static std::unique_ptr<IMemoryAccessor> create(MemoryBackend type);
};