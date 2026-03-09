// Single translation unit that defines the Vulkan Memory Allocator implementation.
// Include path to vk_mem_alloc.h must be provided by CMake (VMA_SOURCE_DIR/include).
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
