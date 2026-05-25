#include "src/shared/core/ServerEndpoints.h"

#include "src/shared/core/Config.h"

#include <array>
#include <fstream>
#include <string>
#include <system_error>

namespace engine::core
{
	namespace
	{
		/// Unique source de vérité des endpoints client. Pour ajouter/modifier une
		/// URL, éditer CETTE table : génération du fichier, backfill runtime et
		/// documentation du `.ini` en découlent automatiquement.
		///
		/// Note : ce sont les **défauts de repli** (régénération si le fichier est
		/// absent). L'environnement réellement livré se règle dans `config/server.ini`,
		/// que l'opérateur édite dans l'artifact — c'est lui qui fait foi au runtime.
		constexpr std::array<ServerEndpointDef, 6> kServerEndpointDefs = {{
			{"client.master_tcp_host",
			 "10.0.4.133",
			 "Hote/IP du master pour la connexion TCP (AUTH, liste des royaumes)."},
			{"client.master_port",
			 "3840",
			 "Port TCP du master."},
			{"client.master_https_host",
			 "10.0.4.133",
			 "Hote/IP du master pour les acces HTTPS (portail embarque)."},
			{"client.master_embedded_http_origin",
			 "http://10.0.4.133:3842",
			 "Origine HTTP du tunnel embarque du master (schema://hote:port)."},
			{"client.status_api_url",
			 "http://10.0.4.133:3842/status",
			 "URL absolue de la sonde /status (sert aussi a deriver l'hote master)."},
			{"client.web_portal_reset_url",
			 "https://lcdlln-portal.tips-of-mine.com/password-recovery",
			 "URL du portail web de recuperation de mot de passe."},
		}};

		/// Renvoie la partie « section » d'une clé (avant le premier point) et
		/// place le reste (suffixe à écrire dans le `.ini`) dans \p suffix.
		std::string_view SplitSection(std::string_view fullKey, std::string_view& suffix)
		{
			const auto dot = fullKey.find('.');
			if (dot == std::string_view::npos)
			{
				suffix = fullKey;
				return {};
			}
			suffix = fullKey.substr(dot + 1);
			return fullKey.substr(0, dot);
		}

		/// Sérialise la table en contenu `.ini` (sections + commentaires + clés).
		std::string RenderIni()
		{
			std::string out;
			out += "; Endpoints serveur du client LCDLLN — genere automatiquement si absent.\n";
			out += "; Editez ces valeurs pour pointer le client vers prod / preprod / dev,\n";
			out += "; puis relancez le jeu. Source unique : aucune recompilation necessaire.\n";
			out += "; (L'adresse du shard n'est PAS ici : elle est annoncee par le master.)\n";

			std::string_view currentSection;
			bool sectionOpen = false;
			for (const auto& def : kServerEndpointDefs)
			{
				std::string_view suffix;
				const std::string_view section = SplitSection(def.key, suffix);
				if (!sectionOpen || section != currentSection)
				{
					out += '\n';
					out += '[';
					out += section;
					out += "]\n";
					currentSection = section;
					sectionOpen = true;
				}
				out += "; ";
				out += def.comment;
				out += '\n';
				out += suffix;
				out += " = ";
				out += def.defaultValue;
				out += '\n';
			}
			return out;
		}
	}

	std::span<const ServerEndpointDef> ServerEndpointDefs()
	{
		return kServerEndpointDefs;
	}

	bool EnsureServerEndpointsFile(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (std::filesystem::exists(path, ec))
		{
			return true;
		}

		const std::filesystem::path parent = path.parent_path();
		if (!parent.empty())
		{
			std::filesystem::create_directories(parent, ec);
		}

		std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out.is_open())
		{
			return false;
		}
		out << RenderIni();
		return out.good();
	}

	void ApplyServerEndpointDefaults(Config& cfg)
	{
		for (const auto& def : kServerEndpointDefs)
		{
			Config::Value value{std::string(def.defaultValue)};
			if (auto scalar = Config::ParseScalar(def.defaultValue))
			{
				value = std::move(*scalar);
			}
			cfg.SetDefault(def.key, std::move(value));
		}
	}
}
