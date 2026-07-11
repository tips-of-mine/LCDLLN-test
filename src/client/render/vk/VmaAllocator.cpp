// Single translation unit that defines the Vulkan Memory Allocator implementation.
// Include path to vk_mem_alloc.h must be provided by CMake (VMA_SOURCE_DIR/include).
#include <cstdio>

#if defined(_MSC_VER)
#pragma warning(push, 0) // en-tete tiers : silence les warnings
#endif
#include "vk_mem_alloc.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
