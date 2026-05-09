#include "engine/world/PakReader.h"

namespace engine::world
{
	bool PakReader::Open(const std::string& path)
	{
		Close();
		m_file.open(path, std::ios::binary);
		if (!m_file)
			return false;
		uint32_t magic = 0;
		uint32_t version = 0;
		m_file.read(reinterpret_cast<char*>(&magic), 4);
		m_file.read(reinterpret_cast<char*>(&version), 4);
		m_file.read(reinterpret_cast<char*>(&m_numEntries), 4);
		if (!m_file || magic != kPakMagic || version != kPakVersion)
		{
			Close();
			return false;
		}
		m_entries.resize(m_numEntries);
		for (uint32_t i = 0; i < m_numEntries; ++i)
		{
			m_file.read(reinterpret_cast<char*>(&m_entries[i].offset), 8);
			m_file.read(reinterpret_cast<char*>(&m_entries[i].size), 8);
		}
		if (!m_file)
		{
			Close();
			return false;
		}
		m_path = path;
		return true;
	}

	void PakReader::Close()
	{
		m_file.close();
		m_entries.clear();
		m_numEntries = 0;
		m_path.clear();
	}

	bool PakReader::IsOpen() const
	{
		return m_file.is_open();
	}

	uint64_t PakReader::GetEntryOffset(uint32_t index) const
	{
		if (index >= m_numEntries)
			return 0;
		return m_entries[index].offset;
	}

	uint64_t PakReader::GetEntrySize(uint32_t index) const
	{
		if (index >= m_numEntries)
			return 0;
		return m_entries[index].size;
	}

	size_t PakReader::ReadEntry(uint32_t index, void* outBuffer, size_t bufferSize) const
	{
		if (index >= m_numEntries || outBuffer == nullptr || !m_file)
			return 0;
		const uint64_t offset = m_entries[index].offset;
		const uint64_t size = m_entries[index].size;
		if (bufferSize < size)
			return 0;
		m_file.seekg(static_cast<std::streamoff>(offset));
		if (!m_file)
			return 0;
		m_file.read(reinterpret_cast<char*>(outBuffer), static_cast<std::streamsize>(size));
		return m_file.gcount() > 0 ? static_cast<size_t>(m_file.gcount()) : 0;
	}

	std::vector<uint8_t> PakReader::ReadEntry(uint32_t index) const
	{
		if (index >= m_numEntries)
			return {};
		const uint64_t size = m_entries[index].size;
		std::vector<uint8_t> out(static_cast<size_t>(size));
		const size_t read = ReadEntry(index, out.data(), out.size());
		if (read != out.size())
			return {};
		return out;
	}
}
