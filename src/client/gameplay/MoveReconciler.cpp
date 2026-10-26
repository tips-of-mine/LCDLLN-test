// ETAPE3.3 (chantier 4.1) — recalage du client sur la position officielle du shard.
// Voir MoveReconciler.h.

#include "src/client/gameplay/MoveReconciler.h"

namespace engine::gameplay
{
	void MoveReconciler::RecordCommand(uint32_t sequence, double x, double z)
	{
		if (!m_commands.empty() && sequence <= m_commands.back().sequence)
			return; // numéro non croissant : ignoré
		m_commands.push_back(Sent{ sequence, x, z });
		while (m_commands.size() > kMaxCommands)
			m_commands.pop_front();
	}

	bool MoveReconciler::ApplyCorrection(uint32_t sequence, double officialX, double officialZ, Offset& out)
	{
		out = Offset{};
		bool found = false;
		while (!m_commands.empty() && m_commands.front().sequence <= sequence)
		{
			if (m_commands.front().sequence == sequence)
			{
				out.dx = officialX - m_commands.front().x;
				out.dz = officialZ - m_commands.front().z;
				found = true;
			}
			m_commands.pop_front();
		}
		if (!found)
			return false;
		for (Sent& later : m_commands)
		{
			later.x += out.dx;
			later.z += out.dz;
		}
		return true;
	}
}
