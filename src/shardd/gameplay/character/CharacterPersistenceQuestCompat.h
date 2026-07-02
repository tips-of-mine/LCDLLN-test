#pragma once
// SP1 — Table de compat des statuts de quête persistés. L'ancien format
// (formatVersion 0) sérialisait 0=Locked/1=Active/2=Completed ; le nouveau
// (formatVersion >= 1) sérialise directement l'enum QuestStatus (0..4).

#include "src/shardd/gameplay/quest/QuestRuntime.h"  // engine::server::QuestStatus

#include <cstdint>

namespace engine::server
{
	/// Convertit une valeur de statut persistée en QuestStatus.
	/// \param persistedValue valeur brute lue du fichier de personnage.
	/// \param formatVersion 0 = ancien schéma (0/1/2), >=1 = enum direct.
	/// \return QuestStatus mappé ; Locked pour toute valeur hors plage.
	QuestStatus MapPersistedQuestStatus(int64_t persistedValue, uint32_t formatVersion);
}
