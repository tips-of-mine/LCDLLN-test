// ServerApp — liaison avec le master : courrier (débits, boîte aux lettres), commandes MJ relayées, boîte d'envoi, métriques de jeu, présence des joueurs.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	// ETAPE5.6 (chantier 5.2) — or joint à un courrier, côté shard.
	void ServerApp::EnqueueMailDebit(const engine::network::MailDebitPayload& debit)
	{
		std::lock_guard lock(m_masterInboxMutex);
		if (m_pendingMailDebits.size() < 1024u)
			m_pendingMailDebits.push_back(debit);
	}

	void ServerApp::EnqueueMailboxPoke(uint64_t characterId)
	{
		std::lock_guard lock(m_masterInboxMutex);
		if (m_pendingMailboxPokes.size() < 4096u)
			m_pendingMailboxPokes.push_back(characterId);
	}

	std::vector<engine::network::MailDebitResultPayload> ServerApp::TakeMasterOutbox()
	{
		std::lock_guard lock(m_masterInboxMutex);
		std::vector<engine::network::MailDebitResultPayload> out;
		out.swap(m_masterOutbox);
		return out;
	}

	// ADM.0 — commandes de jeu (MJ) relayées par le master.
	void ServerApp::EnqueueGmCommand(const engine::network::GmCommandRequestPayload& request)
	{
		std::lock_guard lock(m_masterInboxMutex);
		if (m_pendingGmCommands.size() < 256u)
		{
			m_pendingGmCommands.push_back(request);
			return;
		}
		// File pleine : réponse immédiate (l'appelant n'attend pas le délai du master).
		LOG_WARN(Net, "[ServerApp] ADM.0 file des commandes MJ pleine : commande '{}' refusée", request.command);
		engine::network::GmCommandResultPayload busy;
		busy.requestId = request.requestId;
		busy.actorAccountId = request.actorAccountId;
		busy.status = static_cast<uint8_t>(engine::network::GmCommandStatus::Failed);
		busy.lines.push_back("Le shard est saturé de commandes : réessayez dans quelques secondes.");
		m_gmCommandResults.push_back(std::move(busy));
	}

	std::vector<engine::network::GmCommandResultPayload> ServerApp::TakeGmCommandResults()
	{
		std::lock_guard lock(m_masterInboxMutex);
		std::vector<engine::network::GmCommandResultPayload> out;
		out.swap(m_gmCommandResults);
		return out;
	}

	void ServerApp::ProcessGmCommands()
	{
		PurgeDeadGmMobs(); // ADM.6 — mobs apparus par un MJ et morts depuis
		std::vector<engine::network::GmCommandRequestPayload> batch;
		{
			std::lock_guard lock(m_masterInboxMutex);
			const size_t n = (std::min)(m_pendingGmCommands.size(), kMaxGmCommandsPerTick);
			batch.assign(m_pendingGmCommands.begin(), m_pendingGmCommands.begin() + static_cast<std::ptrdiff_t>(n));
			m_pendingGmCommands.erase(m_pendingGmCommands.begin(), m_pendingGmCommands.begin() + static_cast<std::ptrdiff_t>(n));
		}
		for (const auto& request : batch)
		{
			engine::network::GmCommandResultPayload result;
			result.requestId = request.requestId;
			result.actorAccountId = request.actorAccountId;
			ExecuteGmCommand(request, result);
			std::lock_guard lock(m_masterInboxMutex);
			m_gmCommandResults.push_back(std::move(result));
		}
	}

	void ServerApp::ProcessMasterInbox()
	{
		ResolveUncertainMailDebits();
		ProcessGmCommands(); // ADM.0
		// Au plus 4 prélèvements par tick : chacun est une transaction synchrone (rare, mais
		// une rafale ne doit pas faire caler le tick). Le reste attend le tick suivant.
		constexpr size_t kMaxMailDebitsPerTick = 4u;
		std::vector<engine::network::MailDebitPayload> debits;
		std::vector<uint64_t> pokes;
		{
			std::lock_guard lock(m_masterInboxMutex);
			const size_t n = (std::min)(m_pendingMailDebits.size(), kMaxMailDebitsPerTick);
			debits.assign(m_pendingMailDebits.begin(), m_pendingMailDebits.begin() + static_cast<std::ptrdiff_t>(n));
			m_pendingMailDebits.erase(m_pendingMailDebits.begin(), m_pendingMailDebits.begin() + static_cast<std::ptrdiff_t>(n));
			pokes.swap(m_pendingMailboxPokes);
		}
		for (const auto& debit : debits)
		{
			engine::network::MailDebitResultPayload reply;
			reply.operationId = debit.operationId;
			reply.result = ProcessMailDebit(debit);
			if (reply.result == 0xFFu)
			{
				m_uncertainMailDebits.push_back(UncertainMailDebit{ debit, 0u });
				continue; // réponse différée
			}
			LOG_INFO(Net, "[ServerApp] ETAPE5.6 prélèvement courrier (operation={}, character_key={}, gold={}, result={})",
				debit.operationId, debit.characterId, debit.gold, static_cast<unsigned>(reply.result));
			{
				// Le paquet est construit par le thread principal du shard (ShardPayloads n'est
				// pas compilé dans le server_app WIN32).
				std::lock_guard lock(m_masterInboxMutex);
				m_masterOutbox.push_back(std::move(reply));
			}
		}
		for (uint64_t characterKey : pokes)
			RequestLiveMailboxClaim(characterKey);
		ApplyLiveMailboxResults();
		// FIX.4 — résultats des opérations d'amis faites en base (hors tick).
		ApplyFriendDbResults();
	}

	uint8_t ServerApp::ProcessMailDebit(const engine::network::MailDebitPayload& debit)
	{
		using engine::mail::MailDebitResult;
		ConnectedClient* client = FindConnectedClientByCharacterKey(debit.characterId);
		if (client == nullptr)
			return static_cast<uint8_t>(MailDebitResult::NotHere);
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr)
			return static_cast<uint8_t>(MailDebitResult::Unsupported);
		if (debit.gold == 0u || debit.gold > std::numeric_limits<uint32_t>::max())
			return static_cast<uint8_t>(MailDebitResult::Failed);
		// Les sauvegardes en file de ce personnage sont plus anciennes : elles passent avant.
		if (m_characterSaveQueue != nullptr && !m_characterSaveQueue->Flush(debit.characterId))
			return static_cast<uint8_t>(MailDebitResult::Failed);
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
			return static_cast<uint8_t>(MailDebitResult::Failed);
		(void)mysql_query(mysql, "SET TRANSACTION ISOLATION LEVEL READ COMMITTED");
		if (mysql_query(mysql, "START TRANSACTION") != 0)
			return static_cast<uint8_t>(MailDebitResult::Failed);
		auto rollback = [mysql](MailDebitResult r) {
			mysql_query(mysql, "ROLLBACK");
			return static_cast<uint8_t>(r);
		};
		// Verrou commun master / shard : la ligne du transfert, lue FOR UPDATE.
		auto* sel = cache->Acquire(mysql,
			"SELECT state, sender_character_id, gold FROM mail_transfers WHERE operation_id = ? FOR UPDATE");
		if (sel == nullptr || !sel->Bind(0, std::string_view(debit.operationId)) || !sel->Execute())
			return rollback(MailDebitResult::Failed);
		if (!sel->FetchRow())
			return rollback(MailDebitResult::Cancelled);
		const std::string state = sel->GetString(0);
		const uint64_t sender = sel->GetUInt64(1);
		const uint64_t gold = sel->GetUInt64(2);
		while (sel->FetchRow()) {}
		if (sender != debit.characterId || gold != debit.gold)
			return rollback(MailDebitResult::Failed);
		if (state == "locked")
			return rollback(MailDebitResult::Debited); // déjà prélevé : réponse idempotente
		if (state != "pending")
			return rollback(MailDebitResult::Cancelled); // annulé par le master avant nous
		if (client->gold < gold)
			return rollback(MailDebitResult::Insufficient);
		const uint32_t goldBefore = client->gold;
		client->gold = goldBefore - static_cast<uint32_t>(gold);
		auto* lock = cache->Acquire(mysql,
			"UPDATE mail_transfers SET state = 'locked' WHERE operation_id = ? AND state = 'pending'");
		const PersistedCharacterState persisted = BuildPersistedCharacterState(*client);
		const bool written = lock != nullptr && lock->Bind(0, std::string_view(debit.operationId)) && lock->Execute()
			&& lock->AffectedRows() == 1u;
		const CharacterDbSaveResult saved = written
			? WriteCharacterStateInTx(mysql, cache, persisted, client->yawRadians, client->ownerGeneration)
			: CharacterDbSaveResult::Failed;
		if (saved != CharacterDbSaveResult::Saved)
		{
			client->gold = goldBefore;
			return rollback(saved == CharacterDbSaveResult::RefusedNotOwner ? MailDebitResult::NotHere : MailDebitResult::Failed);
		}
		m_savedBlocks.Invalidate(debit.characterId); // ETAPE5.5 — écrit hors SaveCharacterToDb
		if (mysql_query(mysql, "COMMIT") != 0)
		{
			// Réponse du COMMIT perdue : la base seule sait si l'or est parti. L'or reste
			// retiré en mémoire et aucune réponse n'est envoyée ; ResolveUncertainMailDebits
			// relit l'état sur une connexion neuve aux ticks suivants : locked ⇒ Debited,
			// sinon l'or est rendu (jamais dupliqué, jamais perdu).
			mysql_query(mysql, "ROLLBACK");
			LOG_ERROR(Net, "[ServerApp] ETAPE5.6 COMMIT incertain (operation={}) : vérification différée", debit.operationId);
			return 0xFFu;
		}
		(void)SendWalletUpdate(*client);
		return static_cast<uint8_t>(MailDebitResult::Debited);
#else
		(void)client;
		return static_cast<uint8_t>(MailDebitResult::Unsupported);
#endif
	}

	void ServerApp::ResolveUncertainMailDebits()
	{
#if defined(SHARD_POSITION_DB)
		if (m_uncertainMailDebits.empty() || m_characterDbPool == nullptr)
			return;
		std::vector<UncertainMailDebit> still;
		for (UncertainMailDebit& u : m_uncertainMailDebits)
		{
			++u.attempts;
			std::string state;
			bool known = false;
			{
				auto guard = m_characterDbPool->Acquire(); // connexion neuve à chaque essai
				MYSQL* mysql = guard.get();
				auto* cache = guard.cache();
				auto* check = (mysql != nullptr && cache != nullptr)
					? cache->Acquire(mysql, "SELECT state FROM mail_transfers WHERE operation_id = ?") : nullptr;
				if (check != nullptr && check->Bind(0, std::string_view(u.debit.operationId)) && check->Execute())
				{
					known = true;
					if (check->FetchRow())
						state = check->GetString(0);
					while (check->FetchRow()) {}
				}
			}
			if (!known)
			{
				still.push_back(u); // base injoignable : on réessaie au tick suivant
				continue;
			}
			engine::network::MailDebitResultPayload reply;
			reply.operationId = u.debit.operationId;
			if (state == "locked" || state == "delivered" || state == "refunded")
			{
				reply.result = static_cast<uint8_t>(engine::mail::MailDebitResult::Debited);
			}
			else
			{
				// Le COMMIT n'a pas eu lieu : l'or revient à l'expéditeur (s'il est encore ici ;
				// sinon sa dernière sauvegarde validée contient déjà l'or d'avant).
				if (ConnectedClient* client = FindConnectedClientByCharacterKey(u.debit.characterId))
				{
					const uint64_t total = static_cast<uint64_t>(client->gold) + u.debit.gold;
					client->gold = static_cast<uint32_t>((std::min)(total, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
					(void)SendWalletUpdate(*client);
					SaveConnectedClient(*client, "mail_debit_reverted");
				}
				reply.result = static_cast<uint8_t>(engine::mail::MailDebitResult::Failed);
			}
			LOG_WARN(Net, "[ServerApp] ETAPE5.6 COMMIT incertain tranché (operation={}, state='{}', result={}, essais={})",
				u.debit.operationId, state, static_cast<unsigned>(reply.result), u.attempts);
			{
				// Le paquet est construit par le thread principal du shard (ShardPayloads n'est
				// pas compilé dans le server_app WIN32).
				std::lock_guard lock(m_masterInboxMutex);
				m_masterOutbox.push_back(std::move(reply));
			}
		}
		m_uncertainMailDebits.swap(still);
#endif
	}

		void ServerApp::RequestLiveMailboxClaim(uint64_t characterKey)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || FindConnectedClientByCharacterKey(characterKey) == nullptr)
			return; // hors ligne ici : crédité à la prochaine connexion
		auto job = [this, characterKey]() {
			std::vector<LiveMailboxRow> rows;
			auto guard = m_characterDbPool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			if (mysql == nullptr || cache == nullptr)
				return;
			auto* st = cache->Acquire(mysql,
				"SELECT id, gold, item_id, quantity FROM character_mailbox WHERE character_id = ? ORDER BY id LIMIT 256");
			if (st == nullptr || !st->Bind(0, characterKey) || !st->Execute())
				return;
			while (st->FetchRow())
			{
				LiveMailboxRow r;
				r.id = st->GetUInt64(0);
				r.gold = static_cast<uint32_t>(st->GetUInt64(1));
				r.itemId = static_cast<uint32_t>(st->GetUInt64(2));
				r.quantity = static_cast<uint32_t>(st->GetUInt64(3));
				rows.push_back(r);
			}
			std::lock_guard lock(m_liveMailboxMutex);
			m_liveMailboxResults.emplace_back(characterKey, std::move(rows));
		};
		// Lecture hors du tick, dans l'ordre des écritures de ce personnage (ETAPE2.7).
		if (m_characterSaveQueue == nullptr
			|| m_characterSaveQueue->EnqueueOrdered(characterKey, job) == CharacterSaveQueue::EnqueueResult::Rejected)
			job();
#else
		(void)characterKey;
#endif
	}

	void ServerApp::ApplyLiveMailboxResults()
	{
		std::vector<std::pair<uint64_t, std::vector<LiveMailboxRow>>> results;
		{
			std::lock_guard lock(m_liveMailboxMutex);
			results.swap(m_liveMailboxResults);
		}
		for (auto& [characterKey, rows] : results)
		{
			ConnectedClient* client = FindConnectedClientByCharacterKey(characterKey);
			if (client == nullptr)
				continue; // parti entre-temps : crédité à la prochaine connexion
			uint64_t goldAdded = 0;
			std::vector<ItemStack> itemsAdded;
			for (const LiveMailboxRow& row : rows)
			{
				// Déjà réclamée (à la connexion ou par un crédit précédent) : ignorée.
				if (std::find(client->mailboxClaimedIds.begin(), client->mailboxClaimedIds.end(), row.id)
					!= client->mailboxClaimedIds.end())
					continue;
				// Bourse pleine : la ligne n'est pas réclamée (elle reste pour plus tard) plutôt
				// que de détruire l'excédent.
				if (row.gold > 0u && static_cast<uint64_t>(client->gold) + row.gold > std::numeric_limits<uint32_t>::max())
					continue;
				client->mailboxClaimedIds.push_back(row.id);
				if (row.gold > 0u)
				{
					client->gold += row.gold;
					goldAdded += row.gold;
				}
				if (row.itemId != 0u && row.quantity != 0u)
				{
					AddItemToInventory(*client, ItemStack{ row.itemId, row.quantity });
					itemsAdded.push_back(ItemStack{ row.itemId, row.quantity });
				}
			}
			if (goldAdded == 0u && itemsAdded.empty())
				continue;
			LOG_INFO(Net, "[ServerApp] ETAPE5.6 courrier crédité en jeu (character_key={}, gold={}, objets={})",
				characterKey, goldAdded, itemsAdded.size());
			if (goldAdded > 0u)
				(void)SendWalletUpdate(*client);
			if (!itemsAdded.empty())
				(void)SendInventoryDelta(*client, itemsAdded);
			// La sauvegarde écrit la bourse et le sac ET supprime ces lignes, ensemble.
			SaveConnectedClient(*client, "mailbox_live");
		}
	}

	void ServerApp::PublishGameplayMetrics(size_t asleepMobs, const engine::world::PopulationCounts& mobsByClass)
	{
		ShardGameplayMetrics m;
		m.tick = m_currentTick;
		m.tickHz = static_cast<uint32_t>((std::max<int64_t>)(m_tickHz, 0));
		m.tickLastSeconds = m_lastTickSeconds;
		m.tickMaxSeconds = m_tickMaxSinceMetrics;
		m_tickMaxSinceMetrics = 0.0;
		m.tickLoad = m_loadShedder.SmoothedLoad();
		m.clients = static_cast<uint32_t>(m_clients.size());
		m.mobs = static_cast<uint32_t>(m_mobs.size());
		m.mobsAsleep = static_cast<uint32_t>(asleepMobs);
		m.mobsClassA = mobsByClass[engine::world::PopulationClassIndex(engine::world::PopulationClass::Interactive)];
		m.mobsClassB = mobsByClass[engine::world::PopulationClassIndex(engine::world::PopulationClass::Combat)];
		m.mobsClassC = mobsByClass[engine::world::PopulationClassIndex(engine::world::PopulationClass::Extra)];
		m.lootBags = static_cast<uint32_t>(m_lootBags.size());
		m.regionsWarm = static_cast<uint32_t>(m_regionLifecycle.WarmCount());
		m.regionsTracked = static_cast<uint32_t>(m_regionLifecycle.TrackedCount());
		m.pendingHellos = static_cast<uint32_t>(m_pendingHellos.size());
		m.pendingCharacterLoads = static_cast<uint32_t>(m_pendingCharacterLoads.size());
		if (m_characterSaveQueue != nullptr)
		{
			const auto stats = m_characterSaveQueue->GetStats();
			m.saveQueuePending = static_cast<uint32_t>(m_characterSaveQueue->PendingCount());
			m.saveQueueOverloaded = m_characterSaveQueue->IsOverloaded();
			m.saveQueueExecuted = stats.executed;
			m.saveQueueRejected = stats.rejected;
			m.saveQueueCoalesced = stats.coalesced;
		}
		m.sheddingActive = m_loadShedder.Active();
		m.sheddingEnterCount = m_loadShedder.EnterCount();
		m.stallCount = m_stallGuard.StallCount();
		m.saveBlocksWritten = m_saveBlocksWritten.load(std::memory_order_relaxed);
		m.saveBlocksSkipped = m_saveBlocksSkipped.load(std::memory_order_relaxed);
		m.savesWithoutWrite = m_savesWithoutWrite.load(std::memory_order_relaxed);
		m_metricsBoard.Publish(m, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	std::vector<engine::network::ShardPlayerPresence> ServerApp::GetPlayerPresenceSnapshot() const
	{
		// Source unique : ShardPresenceService (Niveau 1). Alimenté aux hooks
		// login/logout/zone ; thread-safe. On projette ses entrées sur le format wire.
		std::vector<engine::network::ShardPlayerPresence> out;
		for (const auto& e : m_shardPresence.Snapshot())
		{
			engine::network::ShardPlayerPresence p;
			p.accountId = e.accountId;
			p.characterId = e.characterId;
			p.level = e.level;
			p.zoneId = e.zoneId;
			out.push_back(p);
		}
		return out;
	}
}
