// ADM.2 — Commandes d'information exécutées par le shard.
//
// /pos (alias /gps) et /where sont ouvertes aux joueurs pour eux-mêmes ; viser un
// autre joueur (/pos <joueur>) demande le rôle maître de jeu. /info, /target,
// /zone et /nearby sont réservées aux maîtres de jeu.
//
// Repère du monde : +X = est, +Z = sud (carte du monde) ; l'orientation d'un
// personnage suit la convention du client (avant = (sin yaw, cos yaw)).

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		/// Cap en degrés (0 = nord, sens horaire) et point cardinal pour un yaw client.
		std::string HeadingOf(float yawRadians)
		{
			// Avant = (sin yaw, cos yaw) en (X, Z) ; nord = -Z.
			const float dx = std::sin(yawRadians);
			const float dz = std::cos(yawRadians);
			float deg = std::atan2(dx, -dz) * 180.0f / kPi;
			if (deg < 0.0f)
				deg += 360.0f;
			static const char* kNames[] = { "nord", "nord-est", "est", "sud-est", "sud", "sud-ouest", "ouest", "nord-ouest" };
			const int sector = static_cast<int>(std::floor((deg + 22.5f) / 45.0f)) % 8;
			return engine::admin::FormatFixed(deg, 0) + "° (" + kNames[sector] + ")";
		}

		/// Nom lisible d'un état d'IA de mob.
		const char* AiStateName(MobAiState state)
		{
			switch (state)
			{
			case MobAiState::Idle: return "au repos";
			case MobAiState::Patrol: return "en patrouille";
			case MobAiState::Aggro: return "en combat";
			case MobAiState::Return: return "retour au poste";
			}
			return "?";
		}
	}

	std::string GmCommandExecutor::CellLabel(float x, float z) const
	{
		if (!app.m_cellWorld)
			return "monde historique";
		return engine::world::cells::LoreCellId(engine::world::cells::CellFromPosition(x, z));
	}

	std::string GmCommandExecutor::MobLabel(const MobEntity& mob) const
	{
		const CreatureArchetype* arch = app.m_archetypeLibrary.Find(mob.archetypeId);
		std::string label = arch != nullptr && !arch->name.empty() ? arch->name : "mob " + std::to_string(mob.archetypeId);
		if (arch != nullptr)
			label += " niv. " + std::to_string(arch->level);
		return label + " #" + std::to_string(mob.entityId);
	}

	void GmCommandExecutor::CmdPos()
	{
		ConnectedClient* target = nullptr;
		if (!args.empty())
		{
			if (!HasRole(gm::GmRole::GameMaster))
				return Fail(GmCommandStatus::Denied, "Voir la position d'un autre joueur est réservé aux maîtres de jeu.");
			target = FindPlayer(args[0]);
			if (target == nullptr)
				return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		}
		else
		{
			if (!RequireActor())
				return;
			target = actor;
		}
		const float x = target->positionMetersX;
		const float y = target->positionMetersY;
		const float z = target->positionMetersZ;
		Say((target == actor ? std::string("Position") : "Position de " + NameOf(*target)) + " : "
			+ FormatPosition(x, y, z) + " — orientation " + HeadingOf(target->yawRadians));
		std::string where = "Cellule " + CellLabel(x, z) + ", zone " + std::to_string(target->zoneId);
		if (const gm::GmPlace* region = Places().RegionAt(x, z))
			where += ", région " + region->name;
		float dist = 0.0f;
		if (const gm::GmPlace* near = Places().Nearest(x, z, dist))
			where += " — " + near->name + " à " + engine::admin::FormatFixed(dist, 0) + " m";
		Say(where + ".");
	}

	void GmCommandExecutor::CmdWhere()
	{
		if (!RequireActor())
			return;
		const float x = actor->positionMetersX;
		const float z = actor->positionMetersZ;
		float dist = 0.0f;
		const gm::GmPlace* near = Places().Nearest(x, z, dist);
		const gm::GmPlace* region = Places().RegionAt(x, z);
		if (near == nullptr && region == nullptr)
			return Say("Lieu inconnu (annuaire des lieux indisponible sur ce shard) — cellule " + CellLabel(x, z) + ".");
		std::string line;
		if (near != nullptr)
		{
			// Direction du lieu depuis le joueur (nord = -Z).
			float deg = std::atan2(near->x - x, -(near->z - z)) * 180.0f / kPi;
			if (deg < 0.0f)
				deg += 360.0f;
			static const char* kNames[] = { "au nord", "au nord-est", "à l'est", "au sud-est", "au sud", "au sud-ouest", "à l'ouest", "au nord-ouest" };
			const int sector = static_cast<int>(std::floor((deg + 22.5f) / 45.0f)) % 8;
			line = dist < 30.0f ? "Vous êtes à " + near->name + " (" + near->detail + ")"
				: near->name + " (" + near->detail + ") est à " + engine::admin::FormatFixed(dist, 0) + " m " + kNames[sector];
		}
		if (region != nullptr)
			line += (line.empty() ? "Région : " : " — région ") + region->name;
		Say(line + ".");
	}

	void GmCommandExecutor::CmdInfo()
	{
		if (args.empty())
			return Usage("/info <joueur>");
		ConnectedClient* t = FindPlayer(args[0]);
		if (t == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		Say(NameOf(*t) + " (P" + std::to_string(t->clientId) + ", personnage #" + std::to_string(t->persistenceCharacterKey)
			+ ", compte #" + std::to_string(t->accountId) + ")");
		Say("Niveau " + std::to_string(t->level) + " (" + std::to_string(t->experiencePoints) + " XP) — "
			+ (t->classId.empty() ? std::string("classe ?") : t->classId) + ", " + (t->factionId.empty() ? std::string("faction ?") : t->factionId)
			+ ", " + (t->raceId.empty() ? std::string("race ?") : t->raceId) + (t->gender.empty() ? std::string() : ", " + t->gender));
		Say("Vie " + std::to_string(t->stats.currentHealth) + "/" + std::to_string(t->stats.maxHealth) + ", ressource "
			+ std::to_string(t->currentResource) + "/" + std::to_string(t->maxResource)
			+ ((t->stateFlags & kEntityStateDead) != 0u ? " — MORT" : "") + " — or " + std::to_string(t->gold)
			+ ", " + std::to_string(t->inventory.size()) + " pile(s) au sac");
		Say("Position " + FormatPosition(t->positionMetersX, t->positionMetersY, t->positionMetersZ) + " — cellule "
			+ CellLabel(t->positionMetersX, t->positionMetersZ) + ", zone " + std::to_string(t->zoneId)
			+ (t->guildId != 0u ? ", guilde #" + std::to_string(t->guildId) : std::string()));
		std::string gmLine = "État MJ :";
		if (!t->gm.IsActive())
			gmLine += " aucun";
		else
		{
			if (std::fabs(t->gm.speedMultiplier - 1.0f) > 1e-4f) gmLine += " vitesse ×" + engine::admin::FormatFixed(t->gm.speedMultiplier, 1);
			if (t->gm.flying) gmLine += " vol";
			if (t->gm.frozen) gmLine += " gelé";
			if (t->gm.invincible) gmLine += " invincible";
			if (t->gm.invisible) gmLine += " invisible";
			if (t->gm.noCost) gmLine += " sans-coût";
			if (t->gm.mounted) gmLine += " monté";
		}
		Say(gmLine + ".");
	}

	void GmCommandExecutor::CmdTarget()
	{
		if (!RequireActor())
			return;
		const EntityId id = actor->lastTargetEntityId;
		if (id == 0u)
			return Fail(GmCommandStatus::NotFound, "Aucune cible (attaquez ou visez une entité d'abord).");
		if (const MobEntity* mob = app.FindMobByEntityId(id))
		{
			const float dx = mob->positionMetersX - actor->positionMetersX;
			const float dz = mob->positionMetersZ - actor->positionMetersZ;
			Say("Cible : " + MobLabel(*mob) + " — " + AiStateName(mob->aiState)
				+ (mob->ambient ? " (PNJ d'ambiance)" : "") + (mob->attackable ? "" : ", invulnérable"));
			Say("Vie " + std::to_string(mob->stats.currentHealth) + "/" + std::to_string(mob->stats.maxHealth)
				+ " — à " + engine::admin::FormatFixed(std::sqrt(dx * dx + dz * dz), 1) + " m — "
				+ FormatPosition(mob->positionMetersX, mob->positionMetersY, mob->positionMetersZ)
				+ (mob->owningSpawnerIndex < app.m_spawners.size()
					? " — spawner " + app.m_spawners[mob->owningSpawnerIndex].definition.spawnerId
					: std::string()));
			return;
		}
		if (ConnectedClient* player = app.FindClientByEntityId(id))
		{
			args = { NameOf(*player) };
			return CmdInfo();
		}
		Fail(GmCommandStatus::NotFound, "La cible #" + std::to_string(id) + " n'existe plus.");
	}

	void GmCommandExecutor::CmdZone()
	{
		if (!RequireActor())
			return;
		size_t players = 0;
		size_t mobs = 0;
		size_t combat = 0;
		for (const ConnectedClient& c : app.m_clients)
		{
			if (c.zoneId == actor->zoneId)
				++players;
		}
		for (const MobEntity& m : app.m_mobs)
		{
			if (m.zoneId == actor->zoneId)
			{
				++mobs;
				if (m.aiState == MobAiState::Aggro)
					++combat;
			}
		}
		size_t spawners = 0;
		size_t activeSpawners = 0;
		for (const SpawnerRuntimeState& s : app.m_spawners)
		{
			if (s.definition.zoneId != actor->zoneId && !(app.m_cellWorld
				&& engine::world::cells::SameMap(s.definition.zoneId, actor->zoneId)))
				continue;
			++spawners;
			if (s.isActive)
				++activeSpawners;
		}
		Say("Zone " + std::to_string(actor->zoneId) + " — cellule " + CellLabel(actor->positionMetersX, actor->positionMetersZ)
			+ (app.m_cellWorld ? " (monde en cellules de 10 km)" : ""));
		Say(std::to_string(players) + " joueur(s), " + std::to_string(mobs) + " mob(s) dont " + std::to_string(combat)
			+ " en combat — spawners actifs " + std::to_string(activeSpawners) + "/" + std::to_string(spawners) + ".");
	}

	void GmCommandExecutor::CmdNearby()
	{
		if (!RequireActor())
			return;
		float radius = 50.0f;
		if (!args.empty())
		{
			const auto r = engine::admin::ParseDouble(args[0]);
			if (!r || *r <= 0.0 || *r > 1000.0)
				return Usage("/nearby [rayon en mètres, 1 à 1000]");
			radius = static_cast<float>(*r);
		}
		struct Row
		{
			float dist;
			std::string text;
		};
		std::vector<Row> rows;
		const float ax = actor->positionMetersX;
		const float az = actor->positionMetersZ;
		auto distTo = [&](float x, float z) {
			const float dx = x - ax;
			const float dz = z - az;
			return std::sqrt(dx * dx + dz * dz);
		};
		for (const ConnectedClient& c : app.m_clients)
		{
			if (&c == actor || !engine::world::cells::SameMap(c.zoneId, actor->zoneId))
				continue;
			const float d = distTo(c.positionMetersX, c.positionMetersZ);
			if (d <= radius)
				rows.push_back({ d, "joueur " + NameOf(c) + " niv. " + std::to_string(c.level) });
		}
		for (const MobEntity& m : app.m_mobs)
		{
			if (m.pendingDespawn || !engine::world::cells::SameMap(m.zoneId, actor->zoneId))
				continue;
			const float d = distTo(m.positionMetersX, m.positionMetersZ);
			if (d <= radius)
				rows.push_back({ d, MobLabel(m) + ((m.stateFlags & kEntityStateDead) != 0u ? " (mort)" : "") });
		}
		for (const LootBagEntity& b : app.m_lootBags)
		{
			if (!engine::world::cells::SameMap(b.zoneId, actor->zoneId))
				continue;
			const float d = distTo(b.positionMetersX, b.positionMetersZ);
			if (d <= radius)
				rows.push_back({ d, "sac de butin #" + std::to_string(b.entityId) + " (" + std::to_string(b.items.size()) + " objet(s))" });
		}
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.dist < b.dist; });
		if (rows.empty())
			return Say("Rien dans un rayon de " + engine::admin::FormatFixed(radius, 0) + " m.");
		Say(std::to_string(rows.size()) + " entité(s) dans un rayon de " + engine::admin::FormatFixed(radius, 0) + " m"
			+ (rows.size() > 20u ? " (20 plus proches) :" : " :"));
		for (size_t i = 0; i < rows.size() && i < 20u; ++i)
			Say("  " + engine::admin::FormatFixed(rows[i].dist, 1) + " m — " + rows[i].text);
	}
}
