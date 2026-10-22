// tools/world_gen — orchestration du pipeline de génération.

#include "WorldGen.h"
#include "WorldGenContext.h"

namespace tools::world_gen
{
	bool RunWorldGen(const GenOptions& opt, GenReport& outReport, std::string& err)
	{
		World w;
		w.contentRoot = opt.contentRoot;
		w.outputRoot = opt.outputRoot.empty() ? opt.contentRoot : opt.outputRoot;
		if (!LoadWorldMaster(opt.contentRoot, opt.worldId, w.master, err)) return false;

		// L'ordre compte : chaque étape s'appuie sur les champs des précédentes.
		BuildBaseTerrain(w);        // altitude + biomes
		FlattenSettlements(w);      // plateaux urbains (avant l'eau : les rivières traversent)
		BuildWater(w);              // rivières, lacs (creusement)
		BuildRoads(w);              // A* + aplanissement (évite l'eau, franchit les gués)
		if (!ImportLegacyContent(w, err)) return false; // occupe la place des objets historiques
		BuildSettlements(w);        // rues, bâtiments, remparts, mobilier, graphe PNJ
		BuildVegetation(w);         // décor naturel (respecte l'occupation)
		BuildPopulation(w);         // spawners, respawns, départs

		// Contrôles de cohérence bloquants.
		for (const CityDef& c : w.master.cities)
		{
			if (w.HeightAt(c.center) < w.master.scale.seaLevelM + 1.0)
			{
				err = "ville sous le niveau de la mer : " + c.id;
				return false;
			}
		}
		if (w.starts.empty()) { err = "aucun point de depart genere"; return false; }

		if (!opt.dryRun)
		{
			if (!WriteHouseTemplates(w, err)) return false;
			if (!WriteOutputs(w, err)) return false;
		}
		outReport = w.report;
		return true;
	}
}
