#pragma once
// FIX.10 — Perception (rayon de vue) du personnage local, d'après le classeur de
// game design LCDLLN_Faction_Races_Classes.xlsx (onglets « Paramètres » et
// « Tableau »), transcrit dans game/data/gameplay/perception.json.
//
// Rayon (m) = (base_level1_m + per_level_m × (niveau − 1))
//             × profile_mult[profil(classe)] × race_mult[race] × sex_mult[profil][sexe]
// arrondi au dixième, comme dans le classeur.

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::client
{
	class PerceptionModel final
	{
	public:
		/// Charge `gameplay/perception.json` depuis `paths.content`. Politique tolérante :
		/// fichier absent ou invalide => modèle non chargé (IsLoaded() faux, l'appelant
		/// garde son rayon par défaut) + LOG_WARN. Main thread, au démarrage.
		bool Init(const engine::core::Config& config);

		/// Variante testable sans I/O. \return faux (et \p outError) si le JSON est
		/// invalide ou si base_level1_m <= 0.
		bool LoadFromText(std::string_view jsonText, std::string& outError);

		/// Vrai après un chargement réussi.
		bool IsLoaded() const { return m_loaded; }

		/// Rayon de perception (m) d'un personnage.
		/// \param level   niveau (borné à [1, max_level]).
		/// \param classId id de classe du jeu (races/classes.json : warrior, hunter…).
		/// \param raceId  id de race du jeu (humains, elfes, orcs, nains, demons…).
		/// \param gender  "male" / "female" (autre valeur => "male").
		float RadiusM(uint32_t level, std::string_view classId, std::string_view raceId,
			std::string_view gender) const;

		/// Part du rayon (0..1) formant le « cœur du cercle » où le niveau des créatures
		/// est révélé (classeur, onglet Code couleur : ½ rayon).
		float HalfCircleRatio() const { return m_halfCircleRatio; }

	private:
		engine::core::Config m_data;
		bool m_loaded = false;
		float m_halfCircleRatio = 0.5f;
	};
}
