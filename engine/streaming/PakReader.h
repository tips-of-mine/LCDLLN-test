#pragma once

/**
 * @file PakReader.h
 * @brief Simple .pak format (header + entries + offsets) and streamable reader (M10.5).
 *
 * Format: magic (4), version (4), numEntries (4), then per-entry: name[64], offset (8), size (8).
 * Payloads at stored offsets. Header readable; compression (zstd/lz4) can be applied per entry later.
 */

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace engine::streaming {

/** @brief Pak file magic "PAK\0". */
constexpr uint32_t kPakMagic = 0x004B4150u;

/** @brief Current pak format version. */
constexpr uint32_t kPakVersion = 1u;

/** @brief Max length of entry name in pak (null-padded). */
constexpr size_t kPakEntryNameSize = 64u;

/**
 * @brief Single entry in a .pak file (name, file offset, size).
 */
struct PakEntry {
    char     name[kPakEntryNameSize] = {};
    uint64_t offset = 0;
    uint64_t size   = 0;

    /** @brief Returns entry name as null-terminated string. */
    [[nodiscard]] std::string Name() const;
};

/**
 * @brief Streamable .pak reader: open file, read header, read entry payloads on demand.
 */
class PakReader {
public:
    PakReader() = default;

    /**
     * @brief Opens a .pak file. Reads and validates header; entry table kept in memory.
     * @param path Path to .pak file (content-relative or absolute; caller resolves paths.content).
     * @return true on success, false on open/format error.
     */
    [[nodiscard]] bool Open(const std::string& path);

    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_fileHandle != nullptr; }

    /** @brief Number of entries in the pak. */
    [[nodiscard]] uint32_t GetNumEntries() const noexcept { return m_numEntries; }

    /**
     * @brief Gets entry by index. No bounds check; valid for index in [0, GetNumEntries()).
     */
    [[nodiscard]] const PakEntry& GetEntry(uint32_t index) const noexcept;

    /**
     * @brief Finds entry by name. Returns index or GetNumEntries() if not found.
     */
    [[nodiscard]] uint32_t FindEntry(const char* name) const noexcept;

    /**
     * @brief Reads full payload of an entry into buffer. Buffer must be at least entry.size bytes.
     * @param entryIndex Entry index from GetEntry/FindEntry.
     * @param buffer     Output buffer.
     * @param bufferSize Size of buffer (must be >= entry.size).
     * @return true if read succeeded, false on error.
     */
    [[nodiscard]] bool ReadEntry(uint32_t entryIndex, void* buffer, size_t bufferSize) const;

    /**
     * @brief Returns size of entry (for allocation). Returns 0 if index invalid.
     */
    [[nodiscard]] uint64_t GetEntrySize(uint32_t entryIndex) const noexcept;

private:
    std::FILE* m_file = nullptr;
    uint32_t m_numEntries = 0;
    std::vector<PakEntry> m_entries;
};

} // namespace engine::streaming
