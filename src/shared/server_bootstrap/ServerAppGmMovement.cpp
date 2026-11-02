// ADM.1 — Commandes de déplacement des maîtres de jeu (shard).
//
// /tp, /goto, /summon, /recall, /back, /places, /mark, /marks, /gomark, /unmark,
// /forward, /up, /down, /ground, /spawn, /speed, /fly, /freeze, /unfreeze.
//
// Le shard n'a pas le terrain : une destination sans hauteur (/tp x z, lieu,
// /forward, /ground) part avec une hauteur très basse et le client recale le
// personnage au sol ; la hauteur de sa commande suivante est reprise telle quelle
// (MovementState::trustNextHeight). Les autres destinations (joueur, favori,
// /back, /up) gardent la hauteur connue, que le client relève au sol si besoin.
//
// Vitesse et vol : ConnectedClient::gm (GmPlayerState) ; RefreshMaxMoveSpeed
// applique le multiplicateur à la vitesse autoritaire, GmStateUpdate (kind 104)
// prévient le client (vitesse locale, vol, gel).

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Distance maximale d'un /forward, /up, /down (m).
		constexpr float kMaxStepMeters = 20000.0f;
		/// Favoris par personnage au plus.
		constexpr size_t kMaxBookmarksPerCharacter = 50u;
		/// Écart avec le joueur rejoint ou convoqué (m) : pas l'un dans l'autre.
		constexpr float kBesidePlayerMeters = 2.0f;
		/// Lieux par page de /places.
		constexpr size_t kPlacesPerPage = 15u;
	}

	// ---- ServerApp : état MJ vers le client ---------------------------------

	void ServerApp::SendGmState(const ConnectedClient& client)
	{
		GmStateUpdateMessage message{};
		message.clientId = client.clientId;
		message.flags = client.gm.Flags();
		message.speedMultiplier = client.gm.EffectiveSpeedMultiplier();
		if (!SendDatagram(client.endpoint, EncodeGmStateUpdate(message)))
		{
			LOG_WARN(Net, "[ServerApp] ADM.1 GmStateUpdate non envoyé (client_id={})", client.clientId);
			return;
		}
		LOG_INFO(Net, "[ServerApp] ADM.1 GmStateUpdate (client_id={}, flags=0x{:x}, vitesse x{:.2f})",
			client.clientId, message.flags, message.speedMultiplier);
	}

	// ---- Outils ---------------------------------------------------------------

	gm::GmSavedPosition GmCommandExecutor::SavedPositionOf(const ConnectedClient& client)
	{
		return gm::GmSavedPosition{ true, client.positionMetersX, client.positionMetersY, client.positionMetersZ,
			client.yawRadians };
	}

	float GmCommandExecutor::DistanceXZ(const ConnectedClient& a, const ConnectedClient& b)
	{
		const float dx = a.positionMetersX - b.positionMetersX;
		const float dz = a.positionMetersZ - b.positionMetersZ;
		return std::sqrt(dx * dx + dz * dz);
	}

	std::string GmCommandExecutor::FormatPosition(float x, float y, float z)
	{
		const std::string ys = y <= kGroundSnapY * 0.5f ? std::string("sol") : engine::admin::FormatFixed(y, 1);
		return engine::admin::FormatFixed(x, 1) + ", " + ys + ", " + engine::admin::FormatFixed(z, 1);
	}

	const gm::GmPlaceDirectory& GmCommandExecutor::Places()
	{
		if (!app.m_gmPlacesLoaded)
		{
			app.m_gmPlacesLoaded = true;
			// Villes de départ (toujours présentes sur un shard « Royaumes »).
			for (const engine::world::StartLocation& start : app.m_worldStarts.All())
			{
				gm::GmPlace p;
				p.id = start.cityId;
				p.name = start.cityName.empty() ? start.cityId : start.cityName;
				p.detail = "départ des " + start.race;
				p.kind = gm::GmPlaceKind::Start;
				p.x = static_cast<float>(start.x);
				p.z = static_cast<float>(start.z);
				if (!p.name.empty())
					app.m_gmPlaces.Add(std::move(p));
			}
			std::string error;
			const std::string world = app.m_config.GetString("world.active_zone", "");
			if (!world.empty() && !app.m_gmPlaces.LoadFromContent(app.m_config, world, error))
				LOG_WARN(Net, "[GmCommand] ADM.1 annuaire des lieux : {} ({} départ(s) seulement)", error, app.m_gmPlaces.Size());
			else
				LOG_INFO(Net, "[GmCommand] ADM.1 annuaire des lieux chargé ({} lieux)", app.m_gmPlaces.Size());
		}
		return app.m_gmPlaces;
	}

	bool GmCommandExecutor::Teleport(ConnectedClient& client, float x, float y, float z, bool groundSnap, bool saveBack)
	{
		if (!std::isfinite(x) || !std::isfinite(z) || !app.IsInsideGeneratedWorld(x, z))
		{
			Fail(GmCommandStatus::Usage, "Destination hors du monde jouable (" + FormatPosition(x, y, z) + ").");
			return false;
		}
		if (saveBack)
			client.gm.back = SavedPositionOf(client);
		client.positionMetersX = x;
		client.positionMetersY = groundSnap || !std::isfinite(y) ? kGroundSnapY : y;
		client.positionMetersZ = z;
		client.velocityMetersPerSecondX = 0.0f;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = 0.0f;
		// Comme un respawn : trace anti-triche remise à zéro, position imposée au client
		// (nouvelle époque de mouvement), hauteur de la commande suivante acceptée.
		app.m_antiCheat.Reset(client.persistenceCharacterKey);
		app.SendForcePosition(client, kForcePositionReasonTeleport);
		client.movement.trustNextHeight = true;
		app.UpdateClientInterest(client);
		LOG_INFO(Net, "[GmCommand] ADM.1 téléport (client_id={}, vers=({:.1f},{:.1f},{:.1f}), sol={})",
			client.clientId, x, client.positionMetersY, z, groundSnap ? "oui" : "non");
		return true;
	}

	void GmCommandExecutor::ApplyGmState(ConnectedClient& client)
	{
		app.RefreshMaxMoveSpeed(client);
		app.SendGmState(client);
	}

	// ---- Téléportation ---------------------------------------------------------

	void GmCommandExecutor::CmdTeleport()
	{
		if (!RequireActor())
			return;
		if (args.empty())
			return Usage("/tp <x> <z> | /tp <x> <y> <z> | /tp <lieu> | /tp @<joueur>");
		// Coordonnées : deux ou trois nombres.
		const auto a0 = engine::admin::ParseDouble(args[0]);
		const auto a1 = args.size() >= 2 ? engine::admin::ParseDouble(args[1]) : std::nullopt;
		if (a0 && a1)
		{
			const auto a2 = args.size() >= 3 ? engine::admin::ParseDouble(args[2]) : std::nullopt;
			const float x = static_cast<float>(*a0);
			const float y = a2 ? static_cast<float>(*a1) : kGroundSnapY;
			const float z = static_cast<float>(a2 ? *a2 : *a1);
			if (Teleport(*actor, x, y, z, !a2, true))
				Say("Téléporté en " + FormatPosition(x, y, z) + " (retour : /back).");
			return;
		}
		const std::string text = engine::admin::JoinArgs(args, 0);
		// « @nom » : un joueur, explicitement.
		const bool forcePlayer = !text.empty() && text.front() == '@';
		ConnectedClient* player = (args.size() == 1u || forcePlayer) ? FindPlayer(text) : nullptr;
		if (forcePlayer)
		{
			if (player == nullptr)
				return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + text.substr(1));
		}
		else
		{
			const auto places = Places().Search(text, 4u);
			const bool exactPlace = !places.empty()
				&& (gm::FoldPlaceText(places.front()->name) == gm::FoldPlaceText(text)
					|| gm::FoldPlaceText(places.front()->id) == gm::FoldPlaceText(text));
			if (!places.empty() && (exactPlace || player == nullptr))
			{
				const gm::GmPlace& p = *places.front();
				if (!Teleport(*actor, p.x, kGroundSnapY, p.z, true, true))
					return;
				Say("Téléporté vers " + p.name + " (" + p.detail + ") — " + FormatPosition(p.x, kGroundSnapY, p.z) + ".");
				if (places.size() > 1u)
				{
					std::string others = "Autres lieux proches du nom :";
					for (size_t i = 1; i < places.size(); ++i)
						others += (i == 1 ? " " : ", ") + places[i]->name;
					Say(others + " (/places " + text + ").");
				}
				return;
			}
			if (player == nullptr)
			{
				return Fail(GmCommandStatus::NotFound, "Ni lieu ni joueur ne correspond à « " + text + " »"
					+ (Places().Empty() ? " (annuaire des lieux indisponible sur ce shard)." : " — voir /places."));
			}
		}
		if (player == actor)
			return Fail(GmCommandStatus::Usage, "Vous êtes déjà là.");
		float dx = 0.0f;
		float dz = 0.0f;
		gm::ForwardOffset(player->yawRadians, -kBesidePlayerMeters, dx, dz);
		if (Teleport(*actor, player->positionMetersX + dx, player->positionMetersY, player->positionMetersZ + dz, false, true))
			Say("Téléporté auprès de " + NameOf(*player) + " (retour : /back).");
	}

	void GmCommandExecutor::CmdGoto()
	{
		if (args.empty())
			return Usage("/goto <joueur>");
		if (!args[0].empty() && args[0].front() != '@')
			args[0].insert(args[0].begin(), '@');
		args.resize(1);
		CmdTeleport();
	}

	void GmCommandExecutor::CmdSummon()
	{
		if (!RequireActor())
			return;
		if (args.empty())
			return Usage("/summon <joueur>");
		ConnectedClient* target = FindPlayer(args[0]);
		if (target == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		if (target == actor)
			return Fail(GmCommandStatus::Usage, "Vous ne pouvez pas vous convoquer vous-même.");
		target->gm.recall = SavedPositionOf(*target);
		float dx = 0.0f;
		float dz = 0.0f;
		gm::ForwardOffset(actor->yawRadians, kBesidePlayerMeters, dx, dz);
		if (!Teleport(*target, actor->positionMetersX + dx, actor->positionMetersY, actor->positionMetersZ + dz, false, true))
			return;
		NotifyTarget(*target, "Vous avez été convoqué par le maître de jeu " + (request.actorName.empty() ? std::string("?") : request.actorName) + ".");
		Say(NameOf(*target) + " convoqué devant vous (renvoi : /recall " + NameOf(*target) + ").");
	}

	void GmCommandExecutor::CmdRecall()
	{
		if (args.empty())
			return Usage("/recall <joueur>");
		ConnectedClient* target = FindPlayer(args[0]);
		if (target == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		const gm::GmSavedPosition saved = target->gm.recall.valid ? target->gm.recall : target->gm.back;
		if (!saved.valid)
			return Fail(GmCommandStatus::NotFound, NameOf(*target) + " n'a pas été déplacé par un maître de jeu.");
		target->gm.recall = gm::GmSavedPosition{};
		if (!Teleport(*target, saved.x, saved.y, saved.z, false, true))
			return;
		NotifyTarget(*target, "Un maître de jeu vous a renvoyé à votre position précédente.");
		Say(NameOf(*target) + " renvoyé en " + FormatPosition(saved.x, saved.y, saved.z) + ".");
	}

	void GmCommandExecutor::CmdBack()
	{
		if (!RequireActor())
			return;
		const gm::GmSavedPosition saved = actor->gm.back;
		if (!saved.valid)
			return Fail(GmCommandStatus::NotFound, "Aucune position précédente (aucun déplacement MJ depuis la connexion).");
		// La position quittée devient la nouvelle « précédente » : /back deux fois = aller-retour.
		if (Teleport(*actor, saved.x, saved.y, saved.z, false, true))
			Say("Retour en " + FormatPosition(saved.x, saved.y, saved.z) + ".");
	}

	void GmCommandExecutor::CmdPlaces()
	{
		size_t page = 1;
		std::vector<std::string> words = args;
		if (!words.empty())
		{
			if (const auto n = engine::admin::ParseInt(words.back()); n && *n > 0)
			{
				page = static_cast<size_t>(*n);
				words.pop_back();
			}
		}
		const std::string filter = engine::admin::JoinArgs(words, 0);
		const auto list = Places().List(filter);
		if (list.empty())
		{
			return Fail(GmCommandStatus::NotFound, Places().Empty()
				? std::string("Annuaire des lieux indisponible sur ce shard (places.json absent).")
				: "Aucun lieu ne contient « " + filter + " ».");
		}
		const size_t pages = (list.size() + kPlacesPerPage - 1u) / kPlacesPerPage;
		page = (std::min)(page, pages);
		Say(std::to_string(list.size()) + " lieu(x)" + (filter.empty() ? std::string() : " pour « " + filter + " »")
			+ " — page " + std::to_string(page) + "/" + std::to_string(pages) + " :");
		const size_t first = (page - 1u) * kPlacesPerPage;
		for (size_t i = first; i < list.size() && i < first + kPlacesPerPage; ++i)
		{
			const gm::GmPlace& p = *list[i];
			Say("  " + p.name + " (" + p.detail + ") — " + engine::admin::FormatFixed(p.x, 0) + ", "
				+ engine::admin::FormatFixed(p.z, 0));
		}
		if (page < pages)
			Say("Suite : /places " + (filter.empty() ? std::string() : filter + " ") + std::to_string(page + 1u)
				+ " — aller : /tp <lieu>");
	}

	// ---- Favoris -----------------------------------------------------------------

	void GmCommandExecutor::CmdMark()
	{
		if (!RequireActor())
			return;
		if (args.empty())
			return Usage("/mark <nom>");
		const std::string name = engine::admin::ToLowerAscii(engine::admin::TruncateUtf8(engine::admin::JoinArgs(args, 0), 32));
		auto& marks = app.m_gmBookmarks[actor->persistenceCharacterKey];
		if (marks.find(name) == marks.end() && marks.size() >= kMaxBookmarksPerCharacter)
			return Fail(GmCommandStatus::Denied, "Trop de favoris (50 au plus) : supprimez-en avec /unmark <nom>.");
		marks[name] = SavedPositionOf(*actor);
		Say("Favori « " + name + " » enregistré en " + FormatPosition(actor->positionMetersX, actor->positionMetersY,
			actor->positionMetersZ) + " (aller : /gomark " + name + ").");
	}

	void GmCommandExecutor::CmdMarks()
	{
		if (!RequireActor())
			return;
		const auto it = app.m_gmBookmarks.find(actor->persistenceCharacterKey);
		if (it == app.m_gmBookmarks.end() || it->second.empty())
			return Say("Aucun favori (créez-en avec /mark <nom>).");
		Say(std::to_string(it->second.size()) + " favori(s) :");
		for (const auto& [name, pos] : it->second)
		{
			float dist = 0.0f;
			{
				const float dx = pos.x - actor->positionMetersX;
				const float dz = pos.z - actor->positionMetersZ;
				dist = std::sqrt(dx * dx + dz * dz);
			}
			Say("  " + name + " — " + FormatPosition(pos.x, pos.y, pos.z) + " (à " + engine::admin::FormatFixed(dist, 0) + " m)");
		}
	}

	void GmCommandExecutor::CmdGoMark()
	{
		if (!RequireActor())
			return;
		if (args.empty())
			return Usage("/gomark <nom>");
		const std::string name = engine::admin::ToLowerAscii(engine::admin::TruncateUtf8(engine::admin::JoinArgs(args, 0), 32));
		const auto it = app.m_gmBookmarks.find(actor->persistenceCharacterKey);
		if (it == app.m_gmBookmarks.end() || it->second.find(name) == it->second.end())
			return Fail(GmCommandStatus::NotFound, "Favori inconnu : « " + name + " » (voir /marks).");
		const gm::GmSavedPosition pos = it->second.at(name);
		if (Teleport(*actor, pos.x, pos.y, pos.z, false, true))
			Say("Téléporté au favori « " + name + " ».");
	}

	void GmCommandExecutor::CmdUnmark()
	{
		if (!RequireActor())
			return;
		if (args.empty())
			return Usage("/unmark <nom>");
		const std::string name = engine::admin::ToLowerAscii(engine::admin::TruncateUtf8(engine::admin::JoinArgs(args, 0), 32));
		auto it = app.m_gmBookmarks.find(actor->persistenceCharacterKey);
		if (it == app.m_gmBookmarks.end() || it->second.erase(name) == 0u)
			return Fail(GmCommandStatus::NotFound, "Favori inconnu : « " + name + " ».");
		Say("Favori « " + name + " » supprimé.");
	}

	// ---- Déplacements relatifs ---------------------------------------------------

	void GmCommandExecutor::CmdForward()
	{
		if (!RequireActor())
			return;
		const auto meters = args.empty() ? std::nullopt : engine::admin::ParseDouble(args[0]);
		if (!meters || std::fabs(*meters) > kMaxStepMeters)
			return Usage("/forward <mètres> (entre -20000 et 20000)");
		float dx = 0.0f;
		float dz = 0.0f;
		gm::ForwardOffset(actor->yawRadians, static_cast<float>(*meters), dx, dz);
		// En vol, la hauteur est gardée ; à pied, recalage au sol.
		const bool snap = !actor->gm.flying;
		if (Teleport(*actor, actor->positionMetersX + dx, actor->positionMetersY, actor->positionMetersZ + dz, snap, true))
			Say("Avancé de " + engine::admin::FormatFixed(*meters, 0) + " m.");
	}

	void GmCommandExecutor::VerticalMove(float sign)
	{
		if (!RequireActor())
			return;
		const auto meters = args.empty() ? std::nullopt : engine::admin::ParseDouble(args[0]);
		if (!meters || *meters <= 0.0 || *meters > kMaxStepMeters)
			return Usage(sign > 0.0f ? "/up <mètres> (1 à 20000)" : "/down <mètres> (1 à 20000)");
		const float y = actor->positionMetersY + sign * static_cast<float>(*meters);
		if (Teleport(*actor, actor->positionMetersX, y, actor->positionMetersZ, false, true))
		{
			Say(std::string(sign > 0.0f ? "Monté" : "Descendu") + " de " + engine::admin::FormatFixed(*meters, 0)
				+ " m" + (sign > 0.0f && !actor->gm.flying ? " (sans /fly, vous allez tomber)." : "."));
		}
	}

	void GmCommandExecutor::CmdUp() { VerticalMove(1.0f); }
	void GmCommandExecutor::CmdDown() { VerticalMove(-1.0f); }

	void GmCommandExecutor::CmdGround()
	{
		if (!RequireActor())
			return;
		if (Teleport(*actor, actor->positionMetersX, kGroundSnapY, actor->positionMetersZ, true, true))
			Say("Recalé au sol.");
	}

	void GmCommandExecutor::CmdSpawn()
	{
		if (!RequireActor())
			return;
		const float x = actor->spawnPositionMetersX;
		const float y = actor->spawnPositionMetersY;
		const float z = actor->spawnPositionMetersZ;
		if (Teleport(*actor, x, y, z, false, true))
			Say("Retour au point d'entrée en monde (" + FormatPosition(x, y, z) + ").");
	}

	// ---- Vitesse, vol, gel ---------------------------------------------------------

	void GmCommandExecutor::CmdSpeed()
	{
		if (args.empty())
			return Usage("/speed <0.1 à 10> [joueur] | /speed reset [joueur]");
		ConnectedClient* target = TargetOrSelf(1);
		if (target == nullptr)
			return;
		float multiplier = 1.0f;
		if (!engine::admin::EqualsIgnoreCase(args[0], "reset") && !engine::admin::EqualsIgnoreCase(args[0], "normal"))
		{
			std::string text = args[0];
			if (!text.empty() && (text.front() == 'x' || text.front() == 'X'))
				text.erase(text.begin()); // « x3 » accepté
			const auto v = engine::admin::ParseDouble(text);
			multiplier = v ? gm::ClampSpeedMultiplier(*v) : -1.0f;
			if (multiplier <= 0.0f)
				return Usage("/speed <0.1 à 10> [joueur] | /speed reset [joueur]");
		}
		target->gm.speedMultiplier = multiplier;
		ApplyGmState(*target);
		const std::string value = "×" + engine::admin::FormatFixed(multiplier, multiplier < 1.0f ? 2 : 1);
		Say("Vitesse de " + NameOf(*target) + " : " + value + " (vitesse maximale "
			+ engine::admin::FormatFixed(target->maxMoveSpeed, 1) + " m/s).");
		NotifyTarget(*target, "Un maître de jeu a réglé votre vitesse à " + value + ".");
	}

	void GmCommandExecutor::CmdFly()
	{
		ConnectedClient* target = TargetOrSelf(1);
		if (target == nullptr)
			return;
		bool on = !target->gm.flying;
		if (!args.empty())
		{
			const auto parsed = engine::admin::ParseOnOff(args[0]);
			if (!parsed)
				return Usage("/fly [on|off] [joueur]");
			on = *parsed;
		}
		target->gm.flying = on;
		ApplyGmState(*target);
		Say(std::string("Vol libre ") + (on ? "activé" : "désactivé") + " pour " + NameOf(*target)
			+ (on ? " (Espace : monter, Accroupi : descendre)." : "."));
		NotifyTarget(*target, std::string("Un maître de jeu a ") + (on ? "activé" : "désactivé") + " votre vol libre.");
	}

	void GmCommandExecutor::CmdFreeze()
	{
		if (args.empty())
			return Usage("/freeze <joueur>");
		ConnectedClient* target = FindPlayer(args[0]);
		if (target == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		if (target == actor)
			return Fail(GmCommandStatus::Usage, "Vous ne pouvez pas vous geler vous-même.");
		target->gm.frozen = true;
		target->velocityMetersPerSecondX = 0.0f;
		target->velocityMetersPerSecondZ = 0.0f;
		ApplyGmState(*target);
		NotifyTarget(*target, "Vous avez été immobilisé par un maître de jeu.");
		Say(NameOf(*target) + " est gelé (libérer : /unfreeze " + NameOf(*target) + ").");
	}

	void GmCommandExecutor::CmdUnfreeze()
	{
		if (args.empty())
			return Usage("/unfreeze <joueur>");
		ConnectedClient* target = FindPlayer(args[0]);
		if (target == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		if (!target->gm.frozen)
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " n'est pas gelé.");
		target->gm.frozen = false;
		ApplyGmState(*target);
		NotifyTarget(*target, "Vous pouvez de nouveau vous déplacer.");
		Say(NameOf(*target) + " est libéré.");
	}
}
