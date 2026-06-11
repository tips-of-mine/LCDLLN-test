#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace engine::editor::world::zone_paths
{
	/// Lot B3 (audit 2026-06-10 §4.2) — Conventions de chemins disque
	/// namespacés par zone pour les documents de l'éditeur monde.
	///
	/// Historique : les chunks terrain/splat et les fichiers d'instances
	/// (eau, mesh inserts, portails de donjon) étaient écrits dans des
	/// chemins « plats » (`chunks/chunk_X_Z/…`, `instances/water.bin`)
	/// sans identifiant de zone : deux cartes différentes s'écrasaient
	/// mutuellement au save. Ces helpers centralisent la convention
	/// namespacée et sa rétro-compatibilité :
	///   - ÉCRITURE : toujours le chemin namespacé
	///     (`chunks/zone_<zoneId>/chunk_X_Z/…`, `instances/zone_<zoneId>/…`)
	///     dès que `zoneId` est non vide.
	///   - LECTURE : d'abord le chemin namespacé ; s'il n'existe pas,
	///     fallback sur l'ancien chemin plat (migration douce des cartes
	///     créées avant le namespacing — elles sont ré-écrites au format
	///     namespacé au prochain save).
	///   - `zoneId` vide = comportement legacy intégral (chemins plats),
	///     conservé pour les tests unitaires et le boot de l'éditeur avant
	///     le chargement d'une première carte.
	///
	/// `zoneId` doit être DÉJÀ sanitizé (`SanitizeZoneId` : a-z, 0-9, _) —
	/// aucun échappement supplémentaire n'est fait ici.
	///
	/// Contraintes thread : fonctions pures sans état global ; seule
	/// `ResolveInstancesFileForRead` fait une E/S (un `exists()`).

	/// Rôle : répertoire legacy (plat, pré-namespacing) du chunk
	/// `(chunkX, chunkZ)` : `<contentRoot>/chunks/chunk_<x>_<z>`.
	inline std::filesystem::path LegacyChunkDir(
		const std::string& contentRoot, int chunkX, int chunkZ)
	{
		return std::filesystem::path(contentRoot) / "chunks"
			/ ("chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ));
	}

	/// Rôle : répertoire namespacé par zone du chunk `(chunkX, chunkZ)` :
	/// `<contentRoot>/chunks/zone_<zoneId>/chunk_<x>_<z>`. Si \p zoneId est
	/// vide, retombe sur le chemin legacy plat (cf. bloc de doc ci-dessus).
	inline std::filesystem::path ZoneChunkDir(
		const std::string& contentRoot, const std::string& zoneId,
		int chunkX, int chunkZ)
	{
		if (zoneId.empty()) return LegacyChunkDir(contentRoot, chunkX, chunkZ);
		return std::filesystem::path(contentRoot) / "chunks" / ("zone_" + zoneId)
			/ ("chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ));
	}

	/// Rôle : chemin legacy (plat) d'un fichier d'instances :
	/// `<contentRoot>/instances/<filename>`.
	inline std::filesystem::path LegacyInstancesFile(
		const std::string& contentRoot, const char* filename)
	{
		return std::filesystem::path(contentRoot) / "instances" / filename;
	}

	/// Rôle : chemin namespacé par zone d'un fichier d'instances :
	/// `<contentRoot>/instances/zone_<zoneId>/<filename>`. Si \p zoneId est
	/// vide, retombe sur le chemin legacy plat. À utiliser pour toute
	/// ÉCRITURE (jamais de fallback en écriture).
	inline std::filesystem::path ZoneInstancesFile(
		const std::string& contentRoot, const std::string& zoneId,
		const char* filename)
	{
		if (zoneId.empty()) return LegacyInstancesFile(contentRoot, filename);
		return std::filesystem::path(contentRoot) / "instances"
			/ ("zone_" + zoneId) / filename;
	}

	/// Rôle : résolution LECTURE d'un fichier d'instances avec fallback
	/// legacy : retourne le chemin namespacé s'il existe sur disque, sinon
	/// l'ancien chemin plat (qu'il existe ou non — l'appelant gère le cas
	/// « fichier absent » comme avant, i.e. document vide sans erreur).
	/// Effet de bord : un test `exists()` disque sur le chemin namespacé.
	inline std::filesystem::path ResolveInstancesFileForRead(
		const std::string& contentRoot, const std::string& zoneId,
		const char* filename)
	{
		const std::filesystem::path namespaced =
			ZoneInstancesFile(contentRoot, zoneId, filename);
		if (zoneId.empty()) return namespaced; // déjà le chemin legacy
		std::error_code ec;
		if (std::filesystem::exists(namespaced, ec)) return namespaced;
		return LegacyInstancesFile(contentRoot, filename);
	}
}
