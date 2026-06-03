/// @file src/shared/core/Config.h
/// @brief Système de configuration hiérarchique pour le MMORPG.
///
/// Résolution des valeurs (ordre de priorité décroissant) :
///   1. Ligne de commande CLI  (--clé=valeur, priorité maximale)
///   2. Endpoints serveur éditables (config/server.ini, à côté de l'exe)
///   3. Fichier JSON/INI chargé via LoadFromFile() (config.json)
///   4. Valeurs par défaut déclarées avec SetDefault() / table des endpoints
///
/// Utilisation typique (serveur ou client) :
/// @code
///   auto cfg = engine::core::Config::Load("config.json", argc, argv);
///   int port = cfg.GetInt("client.master_port", 7000);
///   bool debug = cfg.GetBool("log.console", true);
/// @endcode
///
/// Thread-safety : aucune. Toutes les lectures/écritures doivent se faire
/// depuis le même thread (généralement le thread principal au démarrage).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace engine::core
{
	/// Gestionnaire de configuration unique par processus.
	/// Supporte les formats JSON et INI. Les clés hiérarchiques utilisent
	/// le point comme séparateur (ex. "client.master_port").
	class Config final
	{
	public:
		/// Type discriminé pouvant représenter toutes les valeurs scalaires supportées.
		/// std::monostate indique une clé déclarée mais sans valeur.
		using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

		/// Create an empty config (use `SetDefault` to seed defaults).
		Config() = default;

		/// Load config from a JSON/INI file (if present) and apply CLI overrides (`--key=value`).
		static Config Load(std::string_view filePath, int argc, char** argv);

		/// Set a default value (used if no file/override sets the key).
		void SetDefault(std::string_view key, Value value);

		/// Load values from a JSON/INI file (returns false if file is missing/unreadable).
		bool LoadFromFile(std::string_view filePath);

		/// Écrit toutes les valeurs scalaires courantes dans un fichier JSON
		/// **plat** (clés pointées au niveau racine, valeurs scalaires). Conçu
		/// pour round-tripper avec \ref LoadFromFile (qui aplatit les objets en
		/// clés pointées : un objet plat reste donc identique). Les clés sans
		/// valeur (`std::monostate`) sont ignorées.
		/// \return false si le fichier n'est pas ouvrable en écriture.
		/// Effet de bord : écriture disque (tronque le fichier existant).
		/// Sous-projet 1 (éditeur) : sert à persister préférences + layout.
		bool SaveToFile(std::string_view filePath) const;

		/// Fusionne un JSON aplati comme valeurs par défaut (sans écraser les clés déjà chargées).
		bool MergeDefaultsFromJsonFile(std::string_view filePath);

		/// Apply CLI overrides of the form `--key=value` (highest priority).
		void ApplyCli(int argc, char** argv);

		/// True if the key exists after merges.
		bool Has(std::string_view key) const;

		/// Get a string value or return `fallback` if missing/not convertible.
		std::string GetString(std::string_view key, std::string_view fallback = {}) const;

		/// Hôte TCP maître : \c client.master_host, puis \c client.master_tcp_host, puis hôte extrait de \c client.status_api_url,
		/// sinon \p fallback. Ces clés proviennent de \c config/server.ini (voir ServerEndpoints).
		std::string GetEffectiveMasterHost(std::string_view fallback = "localhost") const;

		/// Get an int64 value or return `fallback` if missing/not convertible.
		int64_t GetInt(std::string_view key, int64_t fallback = 0) const;

		/// Get a double value or return `fallback` if missing/not convertible.
		double GetDouble(std::string_view key, double fallback = 0.0) const;

		/// Get a bool value or return `fallback` if missing/not convertible.
		bool GetBool(std::string_view key, bool fallback = false) const;

		/// Set a value explicitly (used by parsers and CLI overrides).
		void SetValue(std::string_view key, Value value);

		/// Parse a string as a scalar value (used by INI/CLI parsers).
		static std::optional<Value> ParseScalar(std::string_view text);

		/// Collecte les paires (suffixe, valeur) pour les clés \c prefix + "." + suffixe (valeur string uniquement).
		/// Le suffixe ne doit pas contenir de point (un seul niveau sous le préfixe). Ex. préfixe \c "log.subsystem_files" et clé fichier \c "log.subsystem_files.Smtp".
		std::unordered_map<std::string, std::string> GetStringMapUnderPrefix(std::string_view prefix) const;

	private:
		static std::string ToOwnedKey(std::string_view key);

		std::unordered_map<std::string, Value> m_values;
	};
}

