#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::core
{
	class Config;
}

namespace engine::editor
{
	struct WorldMapEditDocument;

	/// Écrit le JSON d’édition (indentation 2 espaces).
	bool SaveEditDocumentJson(const std::filesystem::path& absolutePath, const WorldMapEditDocument& doc, std::string& outError);

	/// Lit le JSON d’édition (champs reconnus uniquement).
	bool LoadEditDocumentJson(const std::filesystem::path& absolutePath, WorldMapEditDocument& doc, std::string& outError);

	/// Fichier .r16h (magic HAMP) rempli d’une valeur constante.
	bool WriteFlatHeightmapR16h(const std::filesystem::path& absolutePath, uint32_t width, uint32_t height, uint16_t normalizedHeight,
		std::string& outError);

	/// Fichier SLAP (magic « SLAP ») : herbe 100 %, dimensions \p width × \p height.
	bool WriteDefaultTerrainSplatSlap(const std::filesystem::path& absolutePath, uint32_t width, uint32_t height, std::string& outError);

	/// Copie le heightmap, les textures listées (\c textureAssets) vers \c zones/<zone_id>/exported_textures/,
	/// écrit \c zone.meta (en-tête versionné seul) + \c runtime_manifest.json + \c layout_from_editor.json (instances éditeur → schéma \c zone_builder) sous \c zones/<zone_id>/.
	bool ExportRuntimeBundle(const engine::core::Config& cfg, const WorldMapEditDocument& doc, std::string& outError);

	/// PNG → .texr (magic TEXR, RGBA8) sous \c textures/<relativeDest> (relatif au content).
	bool ImportPngToTexr(const engine::core::Config& cfg, const std::filesystem::path& pngAbsolutePath, std::string_view texrRelativeToTextures,
		bool srgb, std::string& outError);

	/// Copie fichier audio vers \c audio/<relativeDest> (relatif au content). Aucune lecture.
	bool ImportAudioFile(const engine::core::Config& cfg, const std::filesystem::path& srcAbsolutePath, std::string_view destRelativeToAudio,
		std::string& outError);

	/// Normalise un identifiant de zone (a-z, 0-9, _).
	std::string SanitizeZoneId(std::string_view raw);

} // namespace engine::editor
