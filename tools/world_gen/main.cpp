// lcdlln_world_gen — CLI du générateur de monde (outil offline, sans GPU).
//
//   lcdlln_world_gen [--content <dir>] [--world <id>] [--output <dir>]
//                    [--cells N003_W009,N011_E008] [--dry-run]
//
// Code retour : 0 succès, 1 échec (message sur stderr). Les avertissements
// (route impossible, ville sans lot…) sont listés mais n'échouent pas.

#include "lib/WorldGen.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
	tools::world_gen::GenOptions opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--content" && i + 1 < argc) opt.contentRoot = argv[++i];
		else if (a == "--world" && i + 1 < argc) opt.worldId = argv[++i];
		else if (a == "--output" && i + 1 < argc) opt.outputRoot = argv[++i];
		else if (a == "--dry-run") opt.dryRun = true;
		else if (a == "--cells" && i + 1 < argc)
		{
			std::string list = argv[++i];
			size_t start = 0;
			while (start <= list.size())
			{
				const size_t comma = list.find(',', start);
				const std::string id = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				if (!id.empty()) opt.cells.push_back(id);
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
		}
		else if (a == "-h" || a == "--help")
		{
			std::printf("Usage: lcdlln_world_gen [--content game/data] [--world royaumes] [--output <dir>] [--cells N003_W009,...] [--dry-run]\n");
			return 0;
		}
		else
		{
			std::fprintf(stderr, "argument inconnu : %s\n", a.c_str());
			return 1;
		}
	}
	tools::world_gen::GenReport rep;
	std::string err;
	if (!tools::world_gen::RunWorldGen(opt, rep, err))
	{
		std::fprintf(stderr, "[world_gen] ECHEC : %s\n", err.c_str());
		return 1;
	}
	std::printf("[world_gen] monde '%s' genere%s : %llu cellule(s) de 10 km, %llu lieux proceduraux\n", opt.worldId.c_str(), opt.dryRun ? " (dry-run)" : "",
		static_cast<unsigned long long>(rep.cells), static_cast<unsigned long long>(rep.proceduralPlaces));
	std::printf("  decor instancie : %llu instances, %llu meshes\n",
		static_cast<unsigned long long>(rep.scatterInstances), static_cast<unsigned long long>(rep.scatterMeshes));
	std::printf("  batiments       : %llu (modeles parametriques : %llu)\n",
		static_cast<unsigned long long>(rep.buildings), static_cast<unsigned long long>(rep.houseTemplates));
	std::printf("  routes (continent) : %llu (%.1f km)\n", static_cast<unsigned long long>(rep.roads), rep.roadLengthM / 1000.0);
	std::printf("  eau (continent)    : %llu rivieres, %llu lacs\n", static_cast<unsigned long long>(rep.rivers), static_cast<unsigned long long>(rep.lakes));
	std::printf("  population      : %llu spawners (%llu PNJ d'ambiance, %llu groupes de creatures)\n",
		static_cast<unsigned long long>(rep.spawners), static_cast<unsigned long long>(rep.ambientNpcs), static_cast<unsigned long long>(rep.creatures));
	std::printf("  respawns        : %llu, PNJ statiques : %llu\n",
		static_cast<unsigned long long>(rep.respawnPoints), static_cast<unsigned long long>(rep.interactables));
	for (const std::string& f : rep.writtenFiles) std::printf("  ecrit : %s\n", f.c_str());
	for (const std::string& wmsg : rep.warnings) std::printf("  [avertissement] %s\n", wmsg.c_str());
	return 0;
}
