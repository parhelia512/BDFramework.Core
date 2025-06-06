#include "il2cpp-config.h"

#if IL2CPP_TARGET_POSIX || IL2CPP_TARGET_N3DS && !IL2CPP_TINY_WITHOUT_DEBUGGER

#include "os/Memory.h"
#include <stdint.h>
#include <stdlib.h>

namespace il2cpp
{
namespace os
{
namespace Memory
{
    void* AlignedAlloc(size_t size, size_t alignment)
    {
#if IL2CPP_TARGET_ANDROID || IL2CPP_TARGET_PSP2
        return memalign(alignment, size);
#else
        void* ptr = NULL;
        alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
        posix_memalign(&ptr, alignment, size);
        return ptr;
#endif
    }

    void* AlignedReAlloc(void* memory, size_t newSize, size_t alignment)
    {
        void* newMemory = realloc(memory, newSize);

        // Fast path: realloc returned aligned memory
        if ((reinterpret_cast<uintptr_t>(newMemory) & (alignment - 1)) == 0)
            return newMemory;

        // Slow path: realloc returned non-aligned memory
        void* alignedMemory = AlignedAlloc(newSize, alignment);
        memcpy(alignedMemory, newMemory, newSize);
        free(newMemory);
        return alignedMemory;
    }

    void AlignedFree(void* memory)
    {
        free(memory);
    }
}
}
}

#endif
