#pragma once

/// @file engine/core/LogConfig.h
/// @brief M45 — Pont Config → LogSettings : factorise la lecture de toutes les
/// clés \c log.* (filtres bitmask, fichiers spécialisés GM/Char/DBError/Packet/Custom,
/// couleurs console, seuil fichier distinct) entre les binaires master et shard.

#include "engine/core/Log.h"

#include <string_view>

namespace engine::core
{
	class Config;

	/// Construit un \c LogSettings complet à partir de la configuration chargée.
	/// Toutes les clés \c log.* sont optionnelles ; les défauts reproduisent
	/// le comportement antérieur (Info, console, rotation 10 MB, 7 fichiers retenus).
	/// \param cfg Configuration déjà chargée (Config::Load).
	/// \param defaultLogFile Nom du fichier principal si \c log.file n'est pas défini
	///        (typiquement \c "engine.log" pour le master, \c "shard.log" pour le shard).
	/// \return Structure \c LogSettings prête à passer à \c Log::Init.
	LogSettings BuildLogSettingsFromConfig(const Config& cfg, std::string_view defaultLogFile);
}
