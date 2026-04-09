#pragma once

#include "engine/editor/WorldMapEditDocument.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::core
{
	class Config;
}

namespace engine::editor
{
	/// État éditeur (carte, chemins, brosses, grille) — logique sans ImGui.
	class WorldEditorSession final
	{
	public:
		WorldEditorSession();

		WorldMapEditDocument& MutableDoc() { return m_doc; }
		const WorldMapEditDocument& Doc() const { return m_doc; }

		std::string& Status() { return m_status; }
		const std::string& Status() const { return m_status; }

		void SetStatus(std::string_view message);

		/// Fichier JSON d’édition absolu (vide si jamais sauvegardé).
		const std::string& EditFileAbsolutePath() const { return m_editJsonAbsolutePath; }
		void SetEditFileAbsolutePath(std::string path) { m_editJsonAbsolutePath = std::move(path); }

		// Tampons pour l’UI (ImGui InputText)
		std::array<char, 128>& BufZoneId() { return m_bufZoneId; }
		std::array<char, 32>& BufSize() { return m_bufSize; }
		std::array<char, 32>& BufSeed() { return m_bufSeed; }
		std::array<char, 512>& BufLoadPath() { return m_bufLoadPath; }
		std::array<char, 512>& BufSavePath() { return m_bufSavePath; }
		std::array<char, 512>& BufPngPath() { return m_bufPngPath; }
		std::array<char, 160>& BufTexrName() { return m_bufTexrName; }
		std::array<char, 512>& BufAudioSrc() { return m_bufAudioSrc; }
		std::array<char, 256>& BufAudioDest() { return m_bufAudioDest; }

		bool& ShowGrid() { return m_showGrid; }
		float& GridCellMeters() { return m_gridCellMeters; }
		float& BrushRadius() { return m_brushRadius; }
		float& BrushStrength() { return m_brushStrength; }
		int& BrushOp() { return m_brushOp; } // 0 raise, 1 lower, 2 smooth, 3 flatten

		/// Crée une carte plate sous \c world_editor/maps/<id>/ (content).
		bool ActionNewMap(const engine::core::Config& cfg);

		bool ActionSaveEditJson(const engine::core::Config& cfg);
		bool ActionLoadEditJson(const engine::core::Config& cfg);
		bool ActionExportRuntime(const engine::core::Config& cfg);
		bool ActionImportTexture(const engine::core::Config& cfg);
		bool ActionImportAudio(const engine::core::Config& cfg);

		void SyncBuffersFromDoc();
		void SyncDocIdFromBuffer();

		/// Demande un rechargement du terrain GPU (après nouvelle carte / chargement JSON).
		void RequestTerrainGpuReload();
		/// \return true une fois par demande consommée (pour l’Engine).
		bool ConsumeTerrainGpuReloadRequest();

	private:
		WorldMapEditDocument m_doc;
		std::string m_editJsonAbsolutePath;
		std::string m_status;

		std::array<char, 128> m_bufZoneId{};
		std::array<char, 32> m_bufSize{};
		std::array<char, 32> m_bufSeed{};
		std::array<char, 512> m_bufLoadPath{};
		std::array<char, 512> m_bufSavePath{};
		std::array<char, 512> m_bufPngPath{};
		std::array<char, 160> m_bufTexrName{};
		std::array<char, 512> m_bufAudioSrc{};
		std::array<char, 256> m_bufAudioDest{};

		bool m_showGrid = true;
		float m_gridCellMeters = 8.f;
		float m_brushRadius = 10.f;
		float m_brushStrength = 0.1f;
		int m_brushOp = 0;

		bool m_terrainGpuReloadRequested = false;
	};

} // namespace engine::editor
