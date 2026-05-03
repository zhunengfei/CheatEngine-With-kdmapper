#pragma once
#include "iprocess_enumerator.h"
#include <memory>
#include "memory_accessor_factory.h"   // ∏¥”√ MemoryBackend

class ProcessEnumeratorFactory
{
public:
    static std::unique_ptr<IProcessEnumerator> create(MemoryBackend type);
};