#pragma once
#include "imodule_enumerator.h"
#include <memory>
#include "memory_accessor_factory.h"   // ¸´ÓÃ MemoryBackend Ã¶¾Ù

class ModuleEnumeratorFactory
{
public:
    static std::unique_ptr<IModuleEnumerator> create(MemoryBackend type);
};