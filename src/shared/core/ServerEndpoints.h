/// @file src/shared/core/ServerEndpoints.h
/// @brief Source unique des endpoints serveur consommés par le client.
///
/// Le client doit savoir où joindre le **master** (le shard est ensuite annoncé
/// par le master au runtime, voir ServerListPayloads). Ces endpoints vivent dans
/// un fichier éditable embarqué dans l'artifact (`config/server.ini`, à côté de
/// l'exe) afin de basculer prod / préprod / dev **sans recompiler** : il suffit
/// d'éditer le fichier puis de relancer le jeu.
///
/// Pour **ajouter ou modifier une URL**, éditer uniquement la table
/// `ServerEndpointDefs()` dans ServerEndpoints.cpp : elle pilote à la fois la
/// génération du fichier par défaut, le backfill runtime et la documentation
/// écrite dans le `.ini`. C'est l'unique endroit à faire vivre.

#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace engine::core
{
	class Config;

	/// Description d'un endpoint serveur côté client.
	struct ServerEndpointDef
	{
		std::string_view key;          ///< Clé de config complète, ex. "client.master_tcp_host".
		std::string_view defaultValue; ///< Valeur par défaut (génération du fichier + backfill runtime).
		std::string_view comment;      ///< Ligne de documentation écrite au-dessus de la clé dans le `.ini`.
	};

	/// Table figée des endpoints client — unique source de vérité.
	/// Étendre cette table (dans le .cpp) pour faire vivre la liste d'URL.
	std::span<const ServerEndpointDef> ServerEndpointDefs();

	/// Crée le fichier d'endpoints avec les valeurs par défaut s'il est **absent**.
	/// No-op si le fichier existe déjà (les éditions de l'opérateur sont préservées).
	///
	/// \param path Chemin cible (typiquement `<dossier_exe>/config/server.ini`).
	/// \return true si le fichier existe désormais (déjà présent ou créé), false si
	///         la création a échoué (dossier en lecture seule, etc.).
	///
	/// Effet de bord : écrit sur disque et crée le dossier parent si nécessaire.
	bool EnsureServerEndpointsFile(const std::filesystem::path& path);

	/// Renseigne dans \p cfg, via SetDefault (sans écraser ce qui est déjà chargé),
	/// toute clé d'endpoint encore absente, à partir de la table.
	///
	/// Garantit qu'aucune clé n'est manquante au runtime même si le fichier sur
	/// disque est ancien et ne contient pas une URL ajoutée depuis (forward-compat).
	void ApplyServerEndpointDefaults(Config& cfg);
}
