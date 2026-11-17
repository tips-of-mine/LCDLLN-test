// ServerApp — bateaux : flotte, embarquement / débarquement, position des passagers.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	// -------------------------------------------------------------------------
	// v1.0.125 — bateaux des lignes maritimes (décision du 15/11/2026 : « à bord en temps
	// réel », LORE_BD §16.51). Horaire pur (BoatFleet) ; les passagers marchent sur le pont
	// dans le repère du bateau, le shard en déduit leur position monde à chaque tick.
	// -------------------------------------------------------------------------

	void ServerApp::InitBoats()
	{
		m_boatFleet.Build(m_seaLines, m_config.GetDouble("boats.speed_mps", 8.0));
		m_boatEntityIds.clear();
		m_boatPoses.assign(m_boatFleet.Count(), BoatPose{});
		m_boatClockSec = 0.0;
		for (size_t b = 0; b < m_boatFleet.Count(); ++b)
		{
			m_boatEntityIds.push_back(m_nextServerEntityId++);
			m_boatPoses[b] = m_boatFleet.Evaluate(b, m_boatClockSec);
			if (CellGrid* grid = GetOrCreateZoneGrid(engine::world::cells::MapIdOf(m_boatFleet.LineOf(b).map)))
			{
				CellCoord cell{};
				(void)grid->UpsertEntity(m_boatEntityIds[b], static_cast<float>(m_boatPoses[b].x),
					static_cast<float>(m_boatPoses[b].z), cell);
			}
			LOG_INFO(Net, "[ServerApp] Bateau {} de la ligne {} (entity_id={}, cycle {:.0f} s)",
				m_boatFleet.RankOf(b), m_boatFleet.LineOf(b).id, m_boatEntityIds[b], m_boatFleet.CycleSeconds(b));
		}
		if (m_boatFleet.Count() > 0u)
			LOG_INFO(Net, "[ServerApp] Bateaux : {} sur les lignes maritimes", m_boatFleet.Count());
	}

	int32_t ServerApp::FindBoatByEntityId(EntityId entityId) const
	{
		for (size_t b = 0; b < m_boatEntityIds.size(); ++b)
			if (m_boatEntityIds[b] == entityId) return static_cast<int32_t>(b);
		return -1;
	}

	void ServerApp::UpdatePassengerWorldPosition(ConnectedClient& client)
	{
		if (client.boatIndex < 0 || static_cast<size_t>(client.boatIndex) >= m_boatPoses.size())
			return;
		const BoatPose& p = m_boatPoses[static_cast<size_t>(client.boatIndex)];
		double wx = 0.0, wz = 0.0;
		BoatFleet::LocalToWorld(p, client.movement.x, client.movement.z, wx, wz);
		client.positionMetersX = static_cast<float>(wx);
		client.positionMetersY = static_cast<float>(p.y) + client.movement.y;
		client.positionMetersZ = static_cast<float>(wz);
		client.velocityMetersPerSecondX = std::sin(p.yawRad) * p.speed;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = std::cos(p.yawRad) * p.speed;
	}

	void ServerApp::EmitBoatAboard(ConnectedClient& client)
	{
		if (client.boatIndex < 0 || static_cast<size_t>(client.boatIndex) >= m_boatPoses.size())
			return;
		const BoatPose& p = m_boatPoses[static_cast<size_t>(client.boatIndex)];
		BoatAboardMessage message{};
		message.clientId = client.clientId;
		message.moveEpoch = client.moveEpoch;
		message.boatEntityId = m_boatEntityIds[static_cast<size_t>(client.boatIndex)];
		message.localX = client.movement.x;
		message.localY = client.movement.y;
		message.localZ = client.movement.z;
		message.localYaw = std::remainder(client.yawRadians - p.yawRad, 6.28318530718f);
		if (!SendDatagram(client.endpoint, EncodeBoatAboard(message)))
		{
			LOG_WARN(Net, "[ServerApp] BoatAboard send failed (client_id={})", client.clientId);
			return;
		}
		client.lastForcePositionSentMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void ServerApp::DisembarkAtPonton(ConnectedClient& client, size_t stop, std::string_view notice)
	{
		if (client.boatIndex < 0 || static_cast<size_t>(client.boatIndex) >= m_boatPoses.size())
			return;
		const SeaLine& line = m_boatFleet.LineOf(static_cast<size_t>(client.boatIndex));
		const SeaLineStop& st = line.stops[(std::min)(stop, line.stops.size() - 1)];
		client.boatIndex = -1;
		client.positionMetersX = static_cast<float>(st.x);
		client.positionMetersY = static_cast<float>(st.y);
		client.positionMetersZ = static_cast<float>(st.z);
		client.velocityMetersPerSecondX = 0.0f;
		client.velocityMetersPerSecondY = 0.0f;
		client.velocityMetersPerSecondZ = 0.0f;
		client.yawRadians = (st.yawDeg + 180.0f) * 3.14159265f / 180.0f; // dos à la mer
		SendForcePosition(client, kForcePositionReasonTeleport);
		UpdateClientInterest(client);
		if (!notice.empty())
			SendChatSystemNotice(client, std::string(notice));
		LOG_INFO(Net, "[ServerApp] Passager debarque (client_id={}, ligne={}, escale={})", client.clientId, line.id, st.cityId);
	}

	void ServerApp::TickBoats()
	{
		if (m_boatFleet.Count() == 0u)
			return;
		m_boatClockSec += 1.0 / static_cast<double>((std::max)(static_cast<unsigned>(m_tickHz), 1u));
		for (size_t b = 0; b < m_boatFleet.Count(); ++b)
		{
			m_boatPoses[b] = m_boatFleet.Evaluate(b, m_boatClockSec);
			if (CellGrid* grid = GetOrCreateZoneGrid(engine::world::cells::MapIdOf(m_boatFleet.LineOf(b).map)))
			{
				CellCoord cell{};
				(void)grid->UpsertEntity(m_boatEntityIds[b], static_cast<float>(m_boatPoses[b].x),
					static_cast<float>(m_boatPoses[b].z), cell);
			}
		}
		for (ConnectedClient& client : m_clients)
		{
			if (client.boatIndex < 0)
				continue;
			if (static_cast<size_t>(client.boatIndex) >= m_boatPoses.size())
			{
				client.boatIndex = -1;
				continue;
			}
			const BoatPose& p = m_boatPoses[static_cast<size_t>(client.boatIndex)];
			if (p.departures > client.boatDeparturesPaid)
			{
				// Départ d'un quai : l'étape se paie (sinon le passager reste à terre).
				const SeaLine& line = m_boatFleet.LineOf(static_cast<size_t>(client.boatIndex));
				const uint32_t price = line.priceGoldPerLeg;
				std::string err;
				if (price == 0u || (client.gold >= price && TrySubtractCurrency(client.clientId, kCurrencyGold, price, err)))
				{
					client.boatDeparturesPaid = p.departures;
					SendChatSystemNotice(client, std::format("Le navire appareille pour {} ({} or pour l'étape).",
						line.stops[p.stop].name, price));
				}
				else
				{
					DisembarkAtPonton(client, p.fromStop,
						std::format("Sans les {} or de l'étape, le capitaine vous fait descendre à quai.", price));
					continue;
				}
			}
			UpdatePassengerWorldPosition(client);
			UpdateClientInterest(client);
		}
	}

	void ServerApp::HandleBoatBoardRequest(const Endpoint& endpoint, const BoatBoardRequestMessage& request)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr || client->clientId != request.clientId)
		{
			LOG_WARN(Net, "[ServerApp] BoatBoardRequest ignored (unknown endpoint or client_id mismatch)");
			return;
		}
		const int32_t b = FindBoatByEntityId(request.boatEntityId);
		if (b < 0)
		{
			SendChatSystemNotice(*client, "Ce navire n'est plus là.");
			return;
		}
		const BoatPose& p = m_boatPoses[static_cast<size_t>(b)];
		const SeaLine& line = m_boatFleet.LineOf(static_cast<size_t>(b));
		if (request.board == 0u)
		{
			if (client->boatIndex != b)
				return;
			if (!p.docked)
			{
				SendChatSystemNotice(*client, "Le navire est en mer : attendez l'escale pour débarquer.");
				return;
			}
			DisembarkAtPonton(*client, p.stop, std::format("Vous débarquez à {}.", line.stops[p.stop].name));
			return;
		}
		if (client->boatIndex >= 0)
			return; // déjà à bord
		if ((client->stateFlags & kEntityStateDead) != 0u || client->stats.currentHealth == 0u)
		{
			SendChatSystemNotice(*client, "Le capitaine ne prend pas les morts à bord.");
			return;
		}
		if (!engine::world::cells::IsCellZoneId(client->zoneId) || ClientCellMap(*client) != line.map)
			return;
		if (!p.docked)
		{
			SendChatSystemNotice(*client, "Le navire n'est pas à quai.");
			return;
		}
		const double dist = std::hypot(client->positionMetersX - p.x, client->positionMetersZ - p.z);
		if (!(dist <= BoatFleet::kBoardRadiusM))
		{
			SendChatSystemNotice(*client, "Approchez-vous du quai pour embarquer.");
			return;
		}
		// Place sur le pont : le point du pont le plus proche du joueur, 0,5 m en retrait du bord.
		double lx = 0.0, lz = 0.0;
		BoatFleet::WorldToLocal(p, client->positionMetersX, client->positionMetersZ, lx, lz);
		float deckX = std::clamp(static_cast<float>(lx), -BoatFleet::kDeckHalfX + 0.5f, BoatFleet::kDeckHalfX - 0.5f);
		float deckZ = std::clamp(static_cast<float>(lz), BoatFleet::kDeckMinZ + 0.5f, BoatFleet::kDeckMaxZ - 0.5f);
		(void)BoatFleet::ClampToDeck(deckX, deckZ);
		client->boatIndex = b;
		client->boatDeparturesPaid = p.departures;
		engine::movement::ResetMovement(client->movement, deckX, BoatFleet::kDeckY + BoatFleet::kStandHeight, deckZ);
		++client->moveEpoch;
		client->lastForcePositionReason = kForcePositionReasonTeleport;
		UpdatePassengerWorldPosition(*client);
		UpdateClientInterest(*client);
		EmitBoatAboard(*client);
		SendChatSystemNotice(*client, std::format("Vous montez à bord ({}). Chaque étape se paie {} or au départ ; "
			"départ dans {} s.", line.name, line.priceGoldPerLeg, static_cast<int>(std::ceil(p.secondsLeft))));
		LOG_INFO(Net, "[ServerApp] Passager a bord (client_id={}, ligne={}, bateau={}, escale={})",
			client->clientId, line.id, m_boatFleet.RankOf(static_cast<size_t>(b)), line.stops[p.stop].cityId);
	}
}
