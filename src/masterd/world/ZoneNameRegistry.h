#pragma once

// Registre zoneId(numérique) → nom de zone lisible, chargé depuis les fichiers de
// carte (manifestes de zone) côté master pour la présence enrichie (web-portal).
//
// Le master monte les mêmes `game/data` que le shard ; il lit donc les manifestes
// directement et résout le nom de région dans /online-accounts, sans transport
// shard→master supplémentaire ni changement de protocole.
//
// Format attendu dans chaque `<zonesRoot>/<dossier>/runtime_manifest.json` :
//   { "zone_numeric_id": <entier = le zoneId runtime>, "display_name": "<nom lisible>" , ... }
// Les deux champs sont OPTIONNELS : une zone sans ces champs est simplement absente
// du registre (le portail retombe alors sur « Zone N »). Aucun nom n'est inventé.

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server
{
	class ZoneNameRegistry
	{
	public:
		/// Scanne \p zonesRoot (ex. "game/data/zones") : pour chaque sous-dossier
		/// contenant un runtime_manifest.json avec `zone_numeric_id` (>0) ET
		/// `display_name` (non vide), enregistre la correspondance. Idempotent
		/// (remplace le contenu). Tolérant : dossier absent / manifeste illisible
		/// → entrée ignorée. Retourne le nombre d'entrées chargées.
		size_t Load(const std::string& zonesRoot);

		/// Nom lisible pour \p zoneId, ou chaîne vide si inconnu.
		std::string NameFor(uint32_t zoneId) const;

		/// Nombre d'entrées connues.
		size_t Size() const { return m_byId.size(); }

	private:
		std::unordered_map<uint32_t, std::string> m_byId;
	};
}
