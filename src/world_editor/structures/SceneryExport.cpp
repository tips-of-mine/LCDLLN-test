#include "src/world_editor/structures/SceneryExport.h"

#include <cstdio>
#include <sstream>

#include "src/world_editor/structures/BuildingInstantiate.h" // RotateYaw

namespace engine::editor::world::structures
{
	std::vector<SceneryEntry> BuildSceneryEntries(const BuildingPreset& preset,
		float pivotX, float pivotZ, float groupYawDeg)
	{
		std::vector<SceneryEntry> out;
		out.reserve(preset.elements.size());
		for (const BuildingPresetElement& e : preset.elements)
		{
			const engine::math::Vec3 r = RotateYaw(e.offset, groupYawDeg);
			SceneryEntry s;
			s.meshPath = e.meshPath;
			s.x = pivotX + r.x;
			s.z = pivotZ + r.z;
			s.yawDeg = groupYawDeg + e.yawDeg;
			s.scale = e.scale;
			s.collisionRadius = e.collisionRadius;
			s.solid = e.solid;
			out.push_back(std::move(s));
		}
		return out;
	}

	std::string SerializeSceneryEntries(const std::vector<SceneryEntry>& entries,
		int startIndex)
	{
		std::string out;
		char buf[512];
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const SceneryEntry& s = entries[i];
			std::snprintf(buf, sizeof(buf),
				"            \"%d\": { \"mesh\": \"%s\", \"x\": %.2f, \"z\": %.2f, "
				"\"yaw_deg\": %.1f, \"scale\": %.2f, \"collision_radius\": %.2f, \"solid\": %s },\n",
				startIndex + static_cast<int>(i), s.meshPath.c_str(), s.x, s.z,
				s.yawDeg, s.scale, s.collisionRadius, s.solid ? "true" : "false");
			out += buf;
		}
		return out;
	}

	bool SpliceSceneryBlock(const std::string& configText,
		const std::string& newBlock, std::string& out, std::string& err)
	{
		const std::string startMark = "\"_comment_auberge\"";
		const std::string endMark = "\"_comment_auberge_end\"";
		const size_t s = configText.find(startMark);
		const size_t e = configText.find(endMark);
		if (s == std::string::npos || e == std::string::npos || e < s)
		{
			err = "SceneryExport: sentinelles _comment_auberge[_end] introuvables";
			return false;
		}
		// Reculer au début de ligne du marqueur de début (préserve l'indentation
		// amont) et au début de ligne du marqueur de fin (la ligne endMark reste
		// intacte, recollée telle quelle après le bloc régénéré).
		size_t lineStart = configText.rfind('\n', s);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		size_t endLineStart = configText.rfind('\n', e);
		endLineStart = (endLineStart == std::string::npos) ? 0 : endLineStart + 1;
		out = configText.substr(0, lineStart) + newBlock + configText.substr(endLineStart);
		return true;
	}

	std::string SpliceInnRespawn(const std::string& respawnText, uint32_t zoneId,
		float x, float z)
	{
		char line[128];
		std::snprintf(line, sizeof(line), "%u inn %.1f 1.5 %.1f", zoneId, x, z);
		std::istringstream in(respawnText);
		std::string row;
		std::string out;
		bool replaced = false;
		const std::string prefix = std::to_string(zoneId) + " inn ";
		while (std::getline(in, row))
		{
			if (row.rfind(prefix, 0) == 0)
			{
				out += line; out += "\n"; replaced = true;
			}
			else { out += row; out += "\n"; }
		}
		if (!replaced) { out += line; out += "\n"; }
		return out;
	}
}
