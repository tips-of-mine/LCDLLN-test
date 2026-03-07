#include "ManifestParser.h"

#include <sstream>
#include <cctype>

namespace tools::hlod_builder
{
	std::vector<ChunkInput> ParseManifest(const std::string& contentDirDefault, const std::string& text, std::string& contentDirOut)
	{
		contentDirOut = contentDirDefault;
		std::vector<ChunkInput> chunks;
		std::istringstream iss(text);
		std::string line;
		ChunkInput* current = nullptr;

		while (std::getline(iss, line))
		{
			// Trim
			while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
				line.pop_back();
			size_t start = 0;
			while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
				++start;
			line = line.substr(start);
			if (line.empty() || line[0] == '#')
				continue;

			std::istringstream ls(line);
			std::string cmd;
			ls >> cmd;
			if (cmd == "content")
			{
				ls >> contentDirOut;
				continue;
			}
			if (cmd == "chunk")
			{
				ChunkInput ch;
				ls >> ch.chunkX >> ch.chunkZ;
				chunks.push_back(std::move(ch));
				current = &chunks.back();
				continue;
			}
			if (cmd == "mesh" && current != nullptr)
			{
				Instance inst;
				ls >> inst.meshPath >> inst.materialId >> inst.x >> inst.y >> inst.z;
				current->instances.push_back(std::move(inst));
			}
		}
		return chunks;
	}
}
