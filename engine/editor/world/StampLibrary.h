#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::editor::world
{
	/// Entrée énumérée d'une bibliothèque de stamps. `name` est le stem du
	/// fichier (sans extension) pour l'affichage UI ; `path` est le chemin
	/// complet sur disque utilisé par `LoadStampPng16`.
	struct StampEntry
	{
		std::string name;
		std::filesystem::path path;
	};

	/// Énumère tous les fichiers `*.png` directement contenus dans `dir`
	/// (non récursif). Ne charge pas le pixel data — seul le nom + chemin est
	/// renvoyé. Si `dir` n'existe pas ou n'est pas un dossier, retourne un
	/// vecteur vide (silencieux : c'est le cas usuel quand l'utilisateur n'a
	/// pas encore créé `assets/editor/stamps/`).
	///
	/// L'ordre du retour suit l'itération de `std::filesystem` (généralement
	/// alphabétique sur Windows/NTFS, non garanti — l'UI doit trier elle-même
	/// si stable nécessaire).
	///
	/// \param dir Dossier à scanner pour les `*.png`.
	/// \return Liste des entrées trouvées (vide si dir absent).
	std::vector<StampEntry> EnumerateStampLibrary(const std::filesystem::path& dir);

	/// Charge un PNG 16-bit grayscale en heightmap normalisée. Utilise
	/// `stbi_load_16` avec `req_comp = 1` pour forcer le mono-canal (les PNG
	/// RGBA sont convertis en luminance par stb). Convertit chaque uint16 en
	/// float `[0..1]` via `val / 65535.0`. Échoue si l'image n'est pas carrée
	/// (heightmap rectangulaire non supportée — le tool stamp suppose un
	/// disque inscrit).
	///
	/// Effet de bord : aucun (pas de cache disque, pas de logger). Le caller
	/// est libre de logger `outError` côté shell.
	///
	/// \param path           Chemin du PNG 16-bit grayscale à charger.
	/// \param outHeights     Reçoit `outResolution²` floats dans [0..1].
	///                       Vidé puis rempli en cas de succès.
	/// \param outResolution  Côté de l'image (= largeur = hauteur).
	/// \param outError       Reçoit le message d'erreur stb si échec, sinon
	///                       chaîne vide.
	/// \return true si chargement OK, false sinon (et outError est renseigné).
	bool LoadStampPng16(const std::filesystem::path& path,
		std::vector<float>& outHeights,
		uint32_t& outResolution,
		std::string& outError);

	/// Helper pur (sans I/O disque) : convertit un buffer uint16 row-major
	/// `width × width` en floats `[0..1]`. Factorisée pour être testable
	/// indépendamment du loader stb (les tests évitent ainsi d'avoir à forger
	/// un PNG 16-bit valide en mémoire — voir TerrainStampTests).
	///
	/// \param src         Buffer source uint16 (longueur ≥ width²).
	/// \param width       Côté de la grille (carrée).
	/// \param outHeights  Reçoit width² floats dans [0..1].
	void ConvertUint16GrayscaleToHeights(const uint16_t* src,
		uint32_t width,
		std::vector<float>& outHeights);
}
