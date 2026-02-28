/**
 * @file PakReader.cpp
 * @brief Streamable .pak reader: header + entries + on-demand payload read (M10.5).
 */

#include "engine/streaming/PakReader.h"

#include <algorithm>
#include <cstring>

namespace engine::streaming {

std::string PakEntry::Name() const {
    const char* end = std::find(name, name + kPakEntryNameSize, '\0');
    return std::string(name, static_cast<size_t>(end - name));
}

bool PakReader::Open(const std::string& path) {
    Close();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0, numEntries = 0;
    if (std::fread(&magic, 1, sizeof(magic), f) != sizeof(magic) ||
        std::fread(&version, 1, sizeof(version), f) != sizeof(version) ||
        std::fread(&numEntries, 1, sizeof(numEntries), f) != sizeof(numEntries)) {
        std::fclose(f);
        return false;
    }
    if (magic != kPakMagic || version != kPakVersion) {
        std::fclose(f);
        return false;
    }
    m_entries.resize(numEntries);
    for (uint32_t i = 0; i < numEntries; ++i) {
        PakEntry& e = m_entries[i];
        if (std::fread(e.name, 1, kPakEntryNameSize, f) != kPakEntryNameSize ||
            std::fread(&e.offset, 1, sizeof(e.offset), f) != sizeof(e.offset) ||
            std::fread(&e.size, 1, sizeof(e.size), f) != sizeof(e.size)) {
            std::fclose(f);
            return false;
        }
    }
    m_file = f;
    m_numEntries = numEntries;
    return true;
}

void PakReader::Close() noexcept {
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_numEntries = 0;
    m_entries.clear();
}

const PakEntry& PakReader::GetEntry(uint32_t index) const noexcept {
    return m_entries[index];
}

uint32_t PakReader::FindEntry(const char* name) const noexcept {
    if (!name) return m_numEntries;
    size_t len = std::strlen(name);
    if (len >= kPakEntryNameSize) return m_numEntries;
    for (uint32_t i = 0; i < m_numEntries; ++i) {
        if (std::strncmp(m_entries[i].name, name, kPakEntryNameSize) == 0)
            return i;
    }
    return m_numEntries;
}

bool PakReader::ReadEntry(uint32_t entryIndex, void* buffer, size_t bufferSize) const {
    if (entryIndex >= m_numEntries || !m_file || !buffer) return false;
    const PakEntry& e = m_entries[entryIndex];
    if (e.size > bufferSize) return false;
    if (e.size == 0) return true;
    if (std::fseek(m_file, static_cast<long>(e.offset), SEEK_SET) != 0)
        return false;
    if (std::fread(buffer, 1, static_cast<size_t>(e.size), m_file) != static_cast<size_t>(e.size))
        return false;
    return true;
}

uint64_t PakReader::GetEntrySize(uint32_t entryIndex) const noexcept {
    if (entryIndex >= m_numEntries) return 0;
    return m_entries[entryIndex].size;
}

} // namespace engine::streaming
