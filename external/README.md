# External dependencies

## VulkanMemoryAllocator (VMA)

The build uses [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (v3.0.1). If the header is present here, it is used instead of FetchContent (recommended for CI and offline builds).

### Option A: Git submodule (recommended for CI)

From the repository root:

```bash
git submodule add https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator external/VulkanMemoryAllocator
cd external/VulkanMemoryAllocator && git checkout v3.0.1 && cd ../..
```

Then in CI, before configure:

```bash
git submodule update --init external/VulkanMemoryAllocator
cd external/VulkanMemoryAllocator && git checkout v3.0.1 && cd ../..
```

### Option B: FetchContent

If `external/VulkanMemoryAllocator/include/vk_mem_alloc.h` is not present, CMake will fetch VMA at configure time (requires network and Git).
