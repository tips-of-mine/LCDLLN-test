#pragma once

#include <cstdint>
#include <string_view>

namespace engine::render::gi
{
	/// M45.8 — Niveaux de qualité de la GI dynamique (DDGI), phase 3.
	///
	/// Remplace le simple booléen `gi.ddgi.enabled` par une échelle de qualité
	/// pilotée par la clé `gi.ddgi.quality`. Le DÉFAUT est `Off` : aucune DDGI
	/// dynamique, rendu STRICTEMENT identique au chemin probes statiques / IBL
	/// d'avant la GI dynamique.
	///
	/// Garantie de fallback (centrale au ticket) : `Off` ET `StaticProbes`
	/// résolvent tous deux `dynamic=false` => `m_ddgiEnabled=false` côté Engine
	/// => `useDdgi=0` côté LightingPass => rendu probes statiques inchangé. Seuls
	/// `DynamicLow` / `DynamicHigh` activent l'allocation du volume et la passe
	/// compute `DDGI_Update`.
	enum class DdgiQuality
	{
		Off,           ///< Pas de DDGI. Rendu identique à l'historique (défaut).
		StaticProbes,  ///< Fallback explicite probes statiques (= Off pour le rendu dynamique).
		DynamicLow,    ///< DDGI dynamique économique (amortissement fort, intensité réduite).
		DynamicHigh    ///< DDGI dynamique pleine qualité (amortissement faible, intensité 1.0).
	};

	/// Parse une chaîne de configuration en niveau de qualité DDGI.
	/// Insensible à la casse. Correspondances :
	///   "off"                          -> Off
	///   "static" / "static-probes"     -> StaticProbes
	///   "dynamic-low"                  -> DynamicLow
	///   "dynamic-high"                 -> DynamicHigh
	/// Toute valeur inconnue, vide ou non reconnue retombe sur `Off` (sécurité :
	/// une coquille de config ne doit jamais activer la DDGI par accident).
	/// \param s Chaîne brute issue de la config (peut être vide).
	/// \return Le niveau de qualité résolu (Off par défaut).
	DdgiQuality ParseDdgiQuality(std::string_view s);

	/// Paramètres effectifs dérivés d'un niveau de qualité.
	/// \note `updateDivisor` est garanti >= 1 par `ResolveDdgiQuality` (le shader
	///       l'utilise comme diviseur de modulo : 0 provoquerait une division par
	///       zéro côté GPU). L'appelant peut quand même reclamper par sécurité.
	struct DdgiQualitySettings
	{
		bool     dynamic;       ///< true => allouer le volume + enregistrer DDGI_Update + useDdgi=1.
		uint32_t updateDivisor; ///< Amortissement : 1 sonde sur N mise à jour par frame (>= 1).
		float    intensity;     ///< Facteur du terme indirect DDGI dans le lighting (0 = neutre).
	};

	/// Résout un niveau de qualité en paramètres concrets.
	///   Off          -> { dynamic=false, updateDivisor=4, intensity=0.0 }
	///   StaticProbes -> { dynamic=false, updateDivisor=4, intensity=0.0 }
	///   DynamicLow   -> { dynamic=true,  updateDivisor=8, intensity=0.5 }
	///   DynamicHigh  -> { dynamic=true,  updateDivisor=2, intensity=1.0 }
	/// `Off` et `StaticProbes` => `dynamic=false` => useDdgi=0 => rendu probes
	/// statiques inchangé (garantie de non-régression du ticket).
	/// \param q Niveau de qualité.
	/// \return Paramètres effectifs (`updateDivisor` toujours >= 1).
	DdgiQualitySettings ResolveDdgiQuality(DdgiQuality q);

	/// Nom lisible du niveau de qualité, pour les logs debug.
	/// \param q Niveau de qualité.
	/// \return Littéral statique ("off", "static-probes", "dynamic-low",
	///         "dynamic-high"). Jamais nullptr.
	const char* DdgiQualityName(DdgiQuality q);
} // namespace engine::render::gi
