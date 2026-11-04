// ADM.6 — Commandes de monde des maîtres de jeu (shard) : PNJ / mobs, nettoyage,
// réapparitions, carte à découvrir.
//
// /npc add|delete|move|info, /npcsearch, /cleanup, /respawn, /revealmap.
//
// Un mob apparu par /npc add est HORS spawner (MobEntity::gmSpawned) : il garde
// sa position comme poste (laisse de 40 m), donne butin et expérience comme les
// autres, et disparaît à sa mort sans réapparaître. /cleanup les retire. Un mob de
// spawner supprimé par /npc delete réapparaît après le délai de son spawner.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shardd/gameplay/gm/GmPlaceDirectory.h"
#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Mobs au plus par /npc add.
		constexpr int64_t kMaxSpawnCount = 25;
	}

	std::vector<const CreatureArchetype*> GmCommandExecutor::FindArchetypes(std::string_view query, size_t maxResults) const
	{
		std::vector<const CreatureArchetype*> out;
		if (const auto id = engine::admin::ParseInt(query); id && *id > 0)
		{
			if (const CreatureArchetype* a = app.m_archetypeLibrary.Find(static_cast<uint32_t>(*id)))
				out.push_back(a);
			return out;
		}
		const std::string folded = gm::FoldPlaceText(query);
		if (folded.empty())
			return out;
		std::vector<std::pair<int, const CreatureArchetype*>> scored;
		for (const auto& [id, a] : app.m_archetypeLibrary.All())
		{
			(void)id;
			const std::string name = gm::FoldPlaceText(a.name);
			const int score = name == folded ? 0 : name.rfind(folded, 0) == 0 ? 1 : name.find(folded) != std::string::npos ? 2 : -1;
			if (score >= 0)
				scored.emplace_back(score, &a);
		}
		std::sort(scored.begin(), scored.end(), [](const auto& l, const auto& r) {
			return l.first != r.first ? l.first < r.first : l.second->archetypeId < r.second->archetypeId;
		});
		for (const auto& [score, a] : scored)
		{
			(void)score;
			if (out.size() >= maxResults)
				break;
			out.push_back(a);
		}
		return out;
	}

	MobEntity* GmCommandExecutor::TargetMob()
	{
		if (!RequireActor())
			return nullptr;
		MobEntity* mob = app.FindMobByEntityId(actor->lastTargetEntityId);
		if (mob == nullptr)
			Fail(GmCommandStatus::NotFound, "Aucun mob ciblé (attaquez ou visez un mob d'abord).");
		return mob;
	}

	// Vrai si la hauteur de l'appelant n'est pas encore connue (téléport « au sol »
	// en attente du premier mouvement client) : un mob posé là tomberait à -100 km.
	bool GmCommandExecutor::ActorHeightUnknown()
	{
		if (actor != nullptr && actor->positionMetersY <= kGroundSnapY * 0.5f)
		{
			Fail(GmCommandStatus::Failed, "Hauteur inconnue (téléport en cours) : bougez d'un pas puis réessayez.");
			return true;
		}
		return false;
	}

	void GmCommandExecutor::CmdNpc()
	{
		const std::string sub = args.empty() ? std::string() : engine::admin::ToLowerAscii(args[0]);
		if (sub == "add" || sub == "spawn")
		{
			if (!RequireActor() || ActorHeightUnknown())
				return;
			if (args.size() < 2u)
				return Usage("/npc add <archétype|n°> [nombre 1-25]");
			const auto found = FindArchetypes(args[1], 6u);
			if (found.empty())
				return Fail(GmCommandStatus::NotFound, "Archétype inconnu : « " + args[1] + " » (voir /npcsearch).");
			if (found.size() > 1u && gm::FoldPlaceText(found[0]->name) != gm::FoldPlaceText(args[1]))
			{
				std::string list = "Plusieurs archétypes correspondent :";
				for (const CreatureArchetype* a : found)
					list += " " + a->name + " (#" + std::to_string(a->archetypeId) + ")";
				return Fail(GmCommandStatus::Usage, list + " — précisez.");
			}
			int64_t count = 1;
			if (args.size() >= 3u)
			{
				const auto n = engine::admin::ParseInt(args[2]);
				if (!n || *n < 1 || *n > kMaxSpawnCount)
					return Usage("/npc add <archétype|n°> [nombre 1-25]");
				count = *n;
			}
			// En cercle à 3 m devant l'appelant (un seul : droit devant).
			size_t spawned = 0;
			for (int64_t i = 0; i < count; ++i)
			{
				const float angle = actor->yawRadians + (count > 1 ? static_cast<float>(i) * 6.2831853f / static_cast<float>(count) : 0.0f);
				const float radius = count > 1 ? 4.0f : 3.0f;
				float dx = 0.0f;
				float dz = 0.0f;
				gm::ForwardOffset(angle, radius, dx, dz);
				if (app.SpawnGmMob(found[0]->archetypeId, actor->zoneId, actor->positionMetersX + dx, actor->positionMetersY,
					actor->positionMetersZ + dz) != 0u)
					++spawned;
			}
			if (spawned == 0u)
				return Fail(GmCommandStatus::Failed, "Apparition impossible (grille de la zone indisponible).");
			return Say(std::to_string(spawned) + " × " + found[0]->name + " (#" + std::to_string(found[0]->archetypeId) + ", niv. "
				+ std::to_string(found[0]->level) + ") apparu(s) — retirer : /cleanup.");
		}
		if (sub == "delete" || sub == "remove" || sub == "del")
		{
			MobEntity* mob = TargetMob();
			if (mob == nullptr)
				return;
			const std::string label = MobLabel(*mob);
			const bool fromSpawner = !mob->gmSpawned && !mob->isDynamicEventMob && mob->owningSpawnerIndex < app.m_spawners.size();
			if (!app.RemoveMobByGm(mob->entityId))
				return Fail(GmCommandStatus::Failed, "Suppression impossible.");
			return Say(label + " supprimé" + (fromSpawner ? " (il réapparaîtra au délai de son spawner)." : "."));
		}
		if (sub == "move" || sub == "here")
		{
			MobEntity* mob = TargetMob();
			if (mob == nullptr || ActorHeightUnknown())
				return;
			if (CellGrid* grid = app.GetOrCreateZoneGrid(mob->zoneId))
			{
				CellCoord cell{};
				mob->positionMetersX = actor->positionMetersX;
				mob->positionMetersY = actor->positionMetersY;
				mob->positionMetersZ = actor->positionMetersZ;
				mob->homePositionMetersX = mob->positionMetersX;
				mob->homePositionMetersY = mob->positionMetersY;
				mob->homePositionMetersZ = mob->positionMetersZ;
				(void)grid->UpsertEntity(mob->entityId, mob->positionMetersX, mob->positionMetersZ, cell);
			}
			return Say(MobLabel(*mob) + " déplacé à votre position (nouveau poste).");
		}
		if (sub == "info")
			return CmdTarget();
		Usage("/npc add <archétype> [nombre] | /npc delete | /npc move | /npc info");
	}

	void GmCommandExecutor::CmdNpcSearch()
	{
		if (args.empty())
			return Usage("/npcsearch <mot|n°>");
		const std::string query = engine::admin::JoinArgs(args, 0);
		const auto found = FindArchetypes(query, 25u);
		if (found.empty())
			return Fail(GmCommandStatus::NotFound, "Aucun archétype ne correspond à « " + query + " » ("
				+ std::to_string(app.m_archetypeLibrary.Count()) + " archétypes chargés).");
		Say(std::to_string(found.size()) + " archétype(s) pour « " + query + " » :");
		for (const CreatureArchetype* a : found)
		{
			Say("  #" + std::to_string(a->archetypeId) + " " + a->name + " — niv. " + std::to_string(a->level)
				+ (a->ambient ? " (PNJ d'ambiance)" : "") + (a->attackable ? "" : ", invulnérable"));
		}
	}

	void GmCommandExecutor::CmdCleanup()
	{
		// « [rayon] » : sans rayon, tous les mobs MJ du shard ; avec, ceux à moins de N m.
		float radius = -1.0f;
		if (!args.empty())
		{
			const auto r = engine::admin::ParseDouble(args[0]);
			if (!r || *r <= 0.0)
				return Usage("/cleanup [rayon en mètres]");
			if (!RequireActor())
				return;
			radius = static_cast<float>(*r);
		}
		std::vector<EntityId> toRemove;
		for (const EntityId id : app.m_gmMobIds)
		{
			const MobEntity* mob = app.FindMobByEntityId(id);
			if (mob == nullptr)
				continue;
			if (radius > 0.0f)
			{
				const float dx = mob->positionMetersX - actor->positionMetersX;
				const float dz = mob->positionMetersZ - actor->positionMetersZ;
				if (std::sqrt(dx * dx + dz * dz) > radius)
					continue;
			}
			toRemove.push_back(id);
		}
		for (const EntityId id : toRemove)
			(void)app.RemoveMobByGm(id);
		Say(std::to_string(toRemove.size()) + " mob(s) apparu(s) par un MJ retiré(s)"
			+ (radius > 0.0f ? " dans un rayon de " + engine::admin::FormatFixed(radius, 0) + " m." : std::string(" (tout le shard).")));
	}

	void GmCommandExecutor::CmdRespawn()
	{
		if (!RequireActor())
			return;
		float radius = 100.0f;
		if (!args.empty())
		{
			const auto r = engine::admin::ParseDouble(args[0]);
			if (!r || *r <= 0.0 || *r > 2000.0)
				return Usage("/respawn [rayon en mètres, 1 à 2000]");
			radius = static_cast<float>(*r);
		}
		size_t slots = 0;
		size_t spawners = 0;
		for (SpawnerRuntimeState& s : app.m_spawners)
		{
			if (!engine::world::cells::SameMap(s.definition.zoneId, actor->zoneId))
				continue;
			const float dx = s.definition.positionMetersX - actor->positionMetersX;
			const float dz = s.definition.positionMetersZ - actor->positionMetersZ;
			if (std::sqrt(dx * dx + dz * dz) > radius)
				continue;
			bool touched = false;
			for (SpawnerSlotState& slot : s.slots)
			{
				if (slot.mobEntityId == 0u && slot.nextRespawnTick > app.m_currentTick)
				{
					slot.nextRespawnTick = app.m_currentTick; // réapparition au prochain passage
					++slots;
					touched = true;
				}
			}
			if (touched)
				++spawners;
		}
		Say(std::to_string(slots) + " réapparition(s) avancée(s) sur " + std::to_string(spawners) + " spawner(s) dans un rayon de "
			+ engine::admin::FormatFixed(radius, 0) + " m (effet dès que le spawner est actif).");
	}

	void GmCommandExecutor::CmdRevealMap()
	{
		// « [all] [joueur] » : la cellule courante, ou toutes les cellules générées.
		bool all = false;
		if (!args.empty() && engine::admin::EqualsIgnoreCase(args[0], "all"))
		{
			all = true;
			args.erase(args.begin());
		}
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		if (!app.m_cellWorld)
			return Fail(GmCommandStatus::Unavailable, "Carte à découvrir indisponible (monde historique).");
		namespace cells = engine::world::cells;
		int revealed = 0;
		size_t cellCount = 0;
		if (all)
		{
			for (const cells::CellCoord& c : app.m_worldStarts.GeneratedCells())
			{
				revealed += target->mapDiscovery.RevealWholeCell(cells::CellZoneId(c));
				++cellCount;
			}
		}
		else
		{
			// Seconde carte : la carte à découvrir ne couvre que le continent.
			if (ServerApp::ClientCellMap(*target) != cells::CellMap::Royaumes)
				return Fail(GmCommandStatus::Unavailable, "Carte à découvrir indisponible aux Enfers (seul le continent se découvre) ; /revealmap all révèle le continent.");
			revealed = target->mapDiscovery.RevealWholeCell(
				cells::CellZoneId(cells::CellFromPosition(target->positionMetersX, target->positionMetersZ)));
			cellCount = 1;
		}
		app.SaveConnectedClient(*target, "gm_revealmap");
		Say(std::to_string(revealed) + " case(s) révélée(s) sur " + std::to_string(cellCount) + " cellule(s) pour "
			+ NameOf(*target) + " (visible à la prochaine ouverture de la carte).");
		NotifyTarget(*target, "Un maître de jeu a révélé votre carte : rouvrez-la (touche M).");
	}
}
