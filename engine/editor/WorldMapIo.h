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

	/// Copie le heightmap + écrit \c zone.meta (en-tête versionné) + manifeste JSON runtime sous \c zones/<zone_id>/.
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
