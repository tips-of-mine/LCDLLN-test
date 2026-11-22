#pragma once
// Atelier décors (décision du 22/11/2026) — enseignes de service.
//
// Les panneaux posés par tools/world_gen devant les bâtiments de service portent un
// pictogramme peint, sans texte (meshes/props/Sign_Service_<service>.gltf). Le client
// affiche le nom du service, traduit dans la langue du joueur (clé de catalogue
// « service.<service> »), quand le curseur passe sur le panneau ou quand on clique dessus.
// Fonctions pures (testées par service_signs_tests).

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Identifiant du service d'un chemin de mesh d'enseigne
	/// (« meshes/props/Sign_Service_forgeron.gltf » → « forgeron », LOD compris :
	/// « …_forgeron_LOD1.gltf » → « forgeron ») ; chaîne vide si ce n'est pas une enseigne.
	inline std::string ServiceKeyFromMeshPath(std::string_view path)
	{
		constexpr std::string_view kPrefix = "Sign_Service_";
		const size_t slash = path.find_last_of("/\\");
		std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
		if (file.substr(0, kPrefix.size()) != kPrefix) return {};
		file.remove_prefix(kPrefix.size());
		const size_t dot = file.find_last_of('.');
		if (dot != std::string_view::npos) file = file.substr(0, dot);
		for (std::string_view suffix : { std::string_view("_LOD1"), std::string_view("_LOD2"), std::string_view("_BB") })
			if (file.size() > suffix.size() && file.substr(file.size() - suffix.size()) == suffix)
				file.remove_suffix(suffix.size());
		for (char c : file)
			if (!((c >= 'a' && c <= 'z') || c == '_')) return {};
		return std::string(file);
	}

	/// Clé de catalogue de langue du nom d'un service (« service.forgeron »).
	inline std::string ServiceNameKey(std::string_view serviceKey)
	{
		return "service." + std::string(serviceKey);
	}

	/// Enseigne projetée à l'écran pour le test de survol.
	struct ServiceSignScreen
	{
		float x = 0.0f;       ///< pixels écran
		float y = 0.0f;
		float radiusPx = 0.0f; ///< rayon de prise du panneau (pixels)
		float distM = 0.0f;   ///< distance caméra (m), départage les panneaux superposés
	};

	/// Rayon de prise (pixels) d'un panneau de ~0,64 m vu à \p distM mètres, pour une
	/// fenêtre de \p viewportH pixels de haut et un champ vertical \p fovYRad (radians).
	/// Borné à [14, 120] px pour rester cliquable de loin sans masquer l'écran de près.
	inline float ServiceSignPickRadiusPx(float distM, float viewportH, float fovYRad)
	{
		const float t = fovYRad > 0.01f ? std::tan(fovYRad * 0.5f) : 0.7f;
		const float d = distM > 0.5f ? distM : 0.5f;
		float r = 0.36f * viewportH / (2.0f * d * t);
		if (r < 14.0f) r = 14.0f;
		if (r > 120.0f) r = 120.0f;
		return r;
	}

	/// Indice du panneau sous le curseur (\p mx, \p my), le plus proche de la caméra
	/// en cas de recouvrement ; -1 si aucun.
	inline int PickServiceSign(const std::vector<ServiceSignScreen>& signs, float mx, float my)
	{
		int best = -1;
		for (size_t i = 0; i < signs.size(); ++i)
		{
			const float dx = signs[i].x - mx, dy = signs[i].y - my;
			if (dx * dx + dy * dy > signs[i].radiusPx * signs[i].radiusPx) continue;
			if (best < 0 || signs[i].distM < signs[static_cast<size_t>(best)].distM) best = static_cast<int>(i);
		}
		return best;
	}
}
