#pragma once
// CMANGOS.30 (Phase 5.30a) — CinematicSequence : timeline de keyframes
// (camera position + look-at + sons) déclenchée par event narratif.
// Header-only.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::cinematics
{
	using SequenceId = uint32_t;

	struct CameraKeyframe
	{
		uint64_t tsMs;            ///< absolu depuis le début de la séquence
		float    posX, posY, posZ;
		float    lookX, lookY, lookZ;
		std::string soundCue;     ///< vide si pas de son à ce keyframe
	};

	struct CinematicSequence
	{
		SequenceId id = 0;
		std::vector<CameraKeyframe> keyframes;
	};

	/// Interpole linéairement entre 2 keyframes successifs au temps \p tMs.
	/// Retourne false si tMs est hors de la séquence ou pas assez de keyframes.
	struct InterpolatedFrame
	{
		float    posX, posY, posZ;
		float    lookX, lookY, lookZ;
	};

	inline bool SampleAt(const CinematicSequence& seq, uint64_t tMs, InterpolatedFrame& out)
	{
		if (seq.keyframes.size() < 2) return false;
		if (tMs < seq.keyframes.front().tsMs) return false;
		if (tMs > seq.keyframes.back().tsMs) return false;

		for (size_t i = 0; i + 1 < seq.keyframes.size(); ++i)
		{
			const auto& a = seq.keyframes[i];
			const auto& b = seq.keyframes[i + 1];
			if (tMs >= a.tsMs && tMs <= b.tsMs)
			{
				const auto dt = b.tsMs - a.tsMs;
				const float t = (dt == 0) ? 0.0f : static_cast<float>(tMs - a.tsMs) / static_cast<float>(dt);
				out.posX  = a.posX  + (b.posX  - a.posX)  * t;
				out.posY  = a.posY  + (b.posY  - a.posY)  * t;
				out.posZ  = a.posZ  + (b.posZ  - a.posZ)  * t;
				out.lookX = a.lookX + (b.lookX - a.lookX) * t;
				out.lookY = a.lookY + (b.lookY - a.lookY) * t;
				out.lookZ = a.lookZ + (b.lookZ - a.lookZ) * t;
				return true;
			}
		}
		return false;
	}
}
