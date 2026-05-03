#include "process_enumerator_factory.h"
#include "win32_process_enumerator.h"   // Win32  µœ÷

std::unique_ptr<IProcessEnumerator> ProcessEnumeratorFactory::create(MemoryBackend type)
{
    switch (type)
    {
    case MemoryBackend::Win32:
        return std::make_unique<Win32ProcessEnumerator>();
        // Œ¥¿¥£∫case MemoryBackend::DbkDriver: ...
    default:
        return nullptr;
    }
}