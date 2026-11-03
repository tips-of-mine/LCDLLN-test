#pragma once
// ADM.1 — Annuaire des lieux du monde pour les commandes de déplacement (/tp <lieu>,
// /places). Côté shard.
//
// Sources (même monde que world.active_zone) :
//   - `world/<monde>/places.json` (généré par tools/world_gen) : villes, lieux
//     remarquables, régions (centre du rectangle) ;
//   - les villes de départ de start_locations.json (toujours présentes sur un
//     shard « Royaumes ») si places.json manque.
// La recherche ignore la casse et les accents français (« Lhynn » trouve
// « L'hynn », « foret » trouve « Forêt ») : nom exact d'abord, puis identifiant,
// puis début de nom, puis nom contenant le texte.
//
// Module pur (Config + FileSystem) : testé par gm_place_directory_tests.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }

namespace engine::server::gm
{
	/// Genre d'un lieu (ordre = priorité à score égal).
	enum class GmPlaceKind : uint8_t
	{
		City = 0,
		Poi = 1,
		Region = 2,
		Start = 3,
	};

	struct GmPlace
	{
		std::string id;
		std::string name;
		std::string detail;      ///< genre affiché (« capitale », « ruine », « région »…).
		GmPlaceKind kind = GmPlaceKind::City;
		float x = 0.0f;          ///< position monde (m).
		float z = 0.0f;
	};

	/// Texte replié pour la recherche : minuscules ASCII, accents français retirés,
	/// apostrophes, tirets et espaces supprimés (« L'Hynn-Sud » → « lhynnsud »).
	std::string FoldPlaceText(std::string_view text);

	class GmPlaceDirectory final
	{
	public:
		/// Lit places.json depuis le texte (remplace le contenu). \return false si le
		/// JSON est invalide ou ne contient aucun lieu.
		bool LoadFromText(std::string_view json, std::string& outError);

		/// Charge `world/<worldId>/places.json` (paths.content). \return false si absent
		/// ou invalide (annuaire vide : seuls les départs ajoutés restent).
		/// Effet de bord : lecture disque.
		bool LoadFromContent(const engine::core::Config& config, std::string_view worldId, std::string& outError);

		/// Ajoute un lieu (villes de départ, tests).
		void Add(GmPlace place) { m_places.push_back(std::move(place)); }

		/// Lieux correspondant à \p query, du plus pertinent au moins pertinent (au plus
		/// \p maxResults). Vide si aucun.
		std::vector<const GmPlace*> Search(std::string_view query, size_t maxResults = 10u) const;

		/// Tous les lieux dont le nom contient \p filter (vide = tous), triés par nom.
		std::vector<const GmPlace*> List(std::string_view filter) const;

		size_t Size() const { return m_places.size(); }
		bool Empty() const { return m_places.empty(); }

	private:
		std::vector<GmPlace> m_places;
	};
}
