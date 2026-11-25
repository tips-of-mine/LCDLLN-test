// ServerApp — connexion des joueurs : chargement / sauvegarde du personnage en base, Hello (UDP chiffré), entrées et corrections de mouvement, canaux fiables, sessions UDP.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

// R1-A — JSON embarqués (générés au build) pour le moteur de stats des personnages.
// Symboles inline constexpr kCharacterStatsJson / kFactionsJson, atteignables via
// ${LCDLLN_GEN_DIR} sur l'include path des cibles server_app / shard_app.
#include "CharacterStatsData.h"
#include "FactionsData.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	bool ServerApp::LoadSpawnFromDb(uint64_t characterId, float& x, float& y, float& z, float& yawDeg,
		std::string& outName, std::string& outGender, uint32_t& outLevel,
		std::string& outFactionId, std::string& outClassId, std::string& outRaceId)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || characterId == 0u)
		{
			return false;
		}
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			return false;
		}
		// TD.5 — on fetch aussi `name` pour le SnapshotEntity (plaque de nom).
		// TD.6 — et `gender` (migration 0067) pour le rendu de l'avatar distant.
		// Présence enrichie — `level` (migration 0004) pour la présence web-portal.
		// N1-I : prepared statement (bind characterId). Floats lus via GetString + strtof.
		// R1-A — `faction_str`/`class_str` (cf. migrations 0040/0033) ajoutés EN FIN de SELECT
		// (indices 7/8) pour ne pas décaler les indices des colonnes existantes (0..6).
		// Alimentent le moteur de stats (PV à l'enter-world).
		// Monde « Royaumes » — `race_str` (migration 0033) ajouté EN FIN (indice 9) :
		// ville de départ quand la position sauvegardée est hors du monde généré.
		auto* stmt = cache->Acquire(mysql,
			"SELECT spawn_x, spawn_y, spawn_z, spawn_yaw_deg, name, gender, level, faction_str, class_str, race_str FROM characters "
			"WHERE id = ? AND deleted_at IS NULL");
		if (!stmt || !stmt->Bind(0, characterId) || !stmt->Execute() || !stmt->FetchRow())
		{
			return false;
		}
		x = std::strtof(stmt->GetString(0).c_str(), nullptr);
		y = std::strtof(stmt->GetString(1).c_str(), nullptr);
		z = std::strtof(stmt->GetString(2).c_str(), nullptr);
		yawDeg = std::strtof(stmt->GetString(3).c_str(), nullptr);
		outName = stmt->GetString(4);
		outGender = stmt->GetString(5);
		outLevel = static_cast<uint32_t>(std::strtoul(stmt->GetString(6).c_str(), nullptr, 10));
		if (outLevel == 0u)
			outLevel = 1u; // garde-fou : niveau minimal 1.
		outFactionId = stmt->GetString(7);
		outClassId = stmt->GetString(8);
		outRaceId = stmt->GetString(9);
		return true;
#else
		(void)characterId; (void)x; (void)y; (void)z; (void)yawDeg; (void)outName; (void)outGender; (void)outLevel;
		(void)outFactionId; (void)outClassId; (void)outRaceId;
		return false;
#endif
	}

#if defined(SHARD_POSITION_DB)
	ServerApp::CharacterDbSaveResult ServerApp::WriteCharacterStateInTx(MYSQL* mysql,
		db::SqlPreparedStatementCache* cache, const PersistedCharacterState& state,
		float yawRadians, uint64_t ownerGeneration, uint32_t blocks) const
	{
		// ETAPE5.5 — un état restauré s'écrit toujours en entier (niveau qui redescend).
		if (state.restoreSnapshotId != 0u)
			blocks = kSaveBlocksAll;
		// ETAPE2.4 — corps commun aux sauvegardes simples et aux transactions à deux
		// personnages : l'appelant ouvre la transaction, appelle cette fonction pour
		// chaque personnage, puis valide (Saved) ou annule (tout autre résultat).
		bool ok = true;
		// 0. ETAPE1.2 — contrôle de propriété : la ligne `characters` est verrouillée
		// (FOR UPDATE) pendant toute la transaction, donc le master ne peut pas
		// réattribuer le personnage entre ce contrôle et le COMMIT. owner_shard_id NULL
		// (verrou libéré, shard déclaré mort) est lu 0. Personnage absent → refus.
		{
			auto* own = cache->Acquire(mysql,
				"SELECT IFNULL(owner_shard_id, 0), owner_generation FROM characters "
				"WHERE id = ? AND deleted_at IS NULL FOR UPDATE");
			if (own == nullptr || !own->Bind(0, state.characterKey) || !own->Execute())
			{
				LOG_WARN(Net, "[ServerApp] WriteCharacterStateInTx: ownership query failed (character_key={})",
					state.characterKey);
				return CharacterDbSaveResult::Failed;
			}
			if (!own->FetchRow())
			{
				return CharacterDbSaveResult::RefusedNotOwner;
			}
			const auto check = db::CheckCharacterOwnership(static_cast<uint32_t>(own->GetUInt64(0)),
				own->GetUInt64(1), m_shardId, ownerGeneration);
			if (!db::IsWriteAllowed(check))
			{
				return CharacterDbSaveResult::RefusedNotOwner;
			}
		}
		// 0 bis. ETAPE1.2 — position de réapparition + niveau : écrits par le shard
		// propriétaire (le master ne les écrit plus pour un personnage en jeu). Colonnes
		// lues au Hello par LoadSpawnFromDb.
		if ((blocks & kSaveBlockPosition) != 0u) // ETAPE5.5
		{
			// ETAPE2.6 — sauvegarde d'un état restauré : le niveau de l'instantané est
			// écrit tel quel (il peut redescendre) ; sinon il ne fait que monter.
			auto* pos = cache->Acquire(mysql, state.restoreSnapshotId != 0u
				? "UPDATE characters SET spawn_x = ?, spawn_y = ?, spawn_z = ?, spawn_yaw_deg = ?, "
				  "level = IF(? > 0, ?, level) WHERE id = ?"
				: "UPDATE characters SET spawn_x = ?, spawn_y = ?, spawn_z = ?, spawn_yaw_deg = ?, "
				  "level = GREATEST(level, ?) WHERE id = ?");
			const size_t keyIndex = state.restoreSnapshotId != 0u ? 6u : 5u;
			ok = pos != nullptr
				&& pos->Bind(0, static_cast<double>(state.positionMetersX))
				&& pos->Bind(1, static_cast<double>(state.positionMetersY))
				&& pos->Bind(2, static_cast<double>(state.positionMetersZ))
				&& pos->Bind(3, static_cast<double>(yawRadians * 57.2957795f))
				&& pos->Bind(4, state.level)
				&& (state.restoreSnapshotId == 0u || pos->Bind(5, state.level))
				&& pos->Bind(keyIndex, state.characterKey)
				&& pos->Execute();
		}
		// 0 ter. ETAPE2.1 — progression (zone, XP, PV, quêtes, barres, métiers…) :
		// une ligne character_progress par personnage. Sa présence indique au
		// chargement que le personnage est entièrement en base (plus de .ini).
		if (ok && (blocks & kSaveBlockProgress) != 0u) // ETAPE5.5
		{
			const std::string payload = SerializeCharacterProgressPayload(state);
			auto* prog = cache->Acquire(mysql,
				"INSERT INTO character_progress (character_id, format_version, zone_id, experience_points, "
				"current_health, max_health, chat_moderator_role, progress_payload) VALUES (?, 1, ?, ?, ?, ?, ?, ?) "
				"ON DUPLICATE KEY UPDATE format_version = VALUES(format_version), zone_id = VALUES(zone_id), "
				"experience_points = VALUES(experience_points), current_health = VALUES(current_health), "
				"max_health = VALUES(max_health), chat_moderator_role = VALUES(chat_moderator_role), "
				"progress_payload = VALUES(progress_payload)");
			ok = prog != nullptr
				&& prog->Bind(0, state.characterKey)
				&& prog->Bind(1, state.zoneId)
				&& prog->Bind(2, state.experiencePoints)
				&& prog->Bind(3, state.stats.currentHealth)
				&& prog->Bind(4, state.stats.maxHealth)
				&& prog->Bind(5, static_cast<uint32_t>(state.chatModeratorRole ? 1u : 0u))
				&& prog->Bind(6, std::string_view(payload))
				&& prog->Execute();
		}
		// 0 quater. ETAPE2.1 — courrier fusionné à la connexion : les lignes lues
		// (mailboxClaimedIds) disparaissent avec l'écriture de la bourse et du sac
		// qui les contiennent désormais. Les lignes arrivées depuis restent.
		for (size_t i = 0; ok && i < state.mailboxClaimedIds.size(); ++i)
		{
			auto* claim = cache->Acquire(mysql,
				"DELETE FROM character_mailbox WHERE character_id = ? AND id = ?");
			ok = claim != nullptr
				&& claim->Bind(0, state.characterKey)
				&& claim->Bind(1, state.mailboxClaimedIds[i])
				&& claim->Execute();
		}
		// 0 sexies. ETAPE2.6 — instantané restauré à la connexion : la demande est
		// marquée appliquée avec l'écriture de l'état restauré (sans effet ensuite).
		if (ok && state.restoreSnapshotId != 0u)
		{
			auto* applied = cache->Acquire(mysql,
				"UPDATE character_restore_requests SET applied_at = UTC_TIMESTAMP() "
				"WHERE character_id = ? AND snapshot_id = ? AND applied_at IS NULL");
			ok = applied != nullptr && applied->Bind(0, state.characterKey)
				&& applied->Bind(1, state.restoreSnapshotId) && applied->Execute();
		}
		// 0 quinquies. ETAPE2.5 — carte découverte : fusion OU des cases révélées
		// pendant la session avec le masque en base (une case découverte ne redevient
		// jamais cachée). Écriture seulement si le masque grandit.
		for (size_t i = 0; ok && i < state.mapDiscoveryCells.size(); ++i)
		{
			const uint32_t cellZoneId = state.mapDiscoveryCells[i].first;
			engine::world::discovery::CellBitmap merged;
			auto* sel = cache->Acquire(mysql,
				"SELECT bits FROM character_map_discovery WHERE character_id = ? AND cell_zone_id = ? FOR UPDATE");
			ok = sel != nullptr && sel->Bind(0, state.characterKey) && sel->Bind(1, cellZoneId) && sel->Execute();
			if (!ok)
			{
				break;
			}
			bool existed = false;
			if (sel->FetchRow())
			{
				const std::string bits = sel->GetString(0);
				existed = merged.Assign(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bits.data()), bits.size()));
				while (sel->FetchRow()) {}
			}
			const bool grew = merged.Merge(state.mapDiscoveryCells[i].second);
			if ((existed && !grew) || merged.Empty())
			{
				continue;
			}
			const auto& bytes = merged.Bytes();
			auto* up = cache->Acquire(mysql,
				"INSERT INTO character_map_discovery (character_id, cell_zone_id, bits) VALUES (?, ?, ?) "
				"ON DUPLICATE KEY UPDATE bits = VALUES(bits)");
			ok = up != nullptr && up->Bind(0, state.characterKey) && up->Bind(1, cellZoneId)
				&& up->BindBlob(2, bytes.data(), bytes.size()) && up->Execute();
		}
		// 0 sexies bis. Lore v1.0.114 — cimetières visités et lieux secrets découverts
		// pendant la session (INSERT IGNORE : rejouer une ligne déjà écrite est sans effet),
		// puis l'exploit des 666 cimetières une fois atteint.
		for (size_t i = 0; ok && i < state.graveyardVisits.size(); ++i)
		{
			auto* ins = cache->Acquire(mysql,
				"INSERT IGNORE INTO character_graveyard_visits (character_id, graveyard_id) VALUES (?, ?)");
			ok = ins != nullptr && ins->Bind(0, state.characterKey)
				&& ins->Bind(1, std::string_view(state.graveyardVisits[i])) && ins->Execute();
		}
		for (size_t i = 0; ok && i < state.secretDiscoveries.size(); ++i)
		{
			auto* ins = cache->Acquire(mysql,
				"INSERT IGNORE INTO character_secret_discoveries (character_id, place_id) VALUES (?, ?)");
			ok = ins != nullptr && ins->Bind(0, state.characterKey)
				&& ins->Bind(1, std::string_view(state.secretDiscoveries[i])) && ins->Execute();
		}
		if (ok && state.grantPilgrimExploit)
		{
			auto* grant = cache->Acquire(mysql,
				"INSERT IGNORE INTO character_exploit_unlocks (character_id, exploit_id, source_detail) "
				"SELECT ?, id, 'graveyards_visited' FROM exploits WHERE code = ? AND is_active = 1");
			ok = grant != nullptr && grant->Bind(0, state.characterKey)
				&& grant->Bind(1, kPilgrimExploitCode) && grant->Execute();
		}
		// 1. Wallet : upsert des 4 devises (ids alignés sur PlayerWalletService :
		// 1=gold, 2=honor, 3=badges, 4=premium). Les 4 lignes sont TOUJOURS écrites
		// (même à 0) : elles servent de sentinelle d'existence à LoadCharacterFromDb.
		// Le stmt est ré-Acquire à chaque itération : le cache fait Reset() au hit
		// (état libmysql propre pour la ré-exécution).
		const std::pair<uint32_t, uint32_t> currencies[] = {
			{ 1u, state.gold }, { 2u, state.honor },
			{ 3u, state.badges }, { 4u, state.premiumCurrency } };
		// ETAPE5.5 — bourse inchangée depuis le dernier COMMIT : ses 4 lignes existent
		// déjà (la première sauvegarde d'une session est toujours complète).
		for (const auto& currency : currencies)
		{
			if (!ok || (blocks & kSaveBlockWallet) == 0u)
			{
				break;
			}
			auto* stmt = cache->Acquire(mysql,
				"INSERT INTO player_wallet (player_id, currency_id, amount) VALUES (?, ?, ?) "
				"ON DUPLICATE KEY UPDATE amount = VALUES(amount)");
			if (stmt == nullptr || !stmt->Bind(0, state.characterKey)
				|| !stmt->Bind(1, currency.first)
				|| !stmt->Bind(2, static_cast<uint64_t>(currency.second))
				|| !stmt->Execute())
			{
				ok = false;
				break;
			}
		}
		// 2. Inventaire : remplacement intégral (DELETE + INSERT par pile). Les sacs
		// sont petits (dizaines de piles) — le coût reste marginal par save.
		if (ok && (blocks & kSaveBlockInventory) != 0u) // ETAPE5.5
		{
			auto* del = cache->Acquire(mysql,
				"DELETE FROM character_inventory WHERE character_id = ?");
			ok = del != nullptr && del->Bind(0, state.characterKey) && del->Execute();
			for (size_t i = 0; ok && i < state.inventory.size(); ++i)
			{
				auto* ins = cache->Acquire(mysql,
					"INSERT INTO character_inventory (character_id, slot_index, item_id, quantity) "
					"VALUES (?, ?, ?, ?)");
				ok = ins != nullptr && ins->Bind(0, state.characterKey)
					&& ins->Bind(1, static_cast<uint32_t>(i))
					&& ins->Bind(2, state.inventory[i].itemId)
					&& ins->Bind(3, state.inventory[i].quantity)
					&& ins->Execute();
			}
		}
		// 3. Équipement porté : remplacement intégral, une ligne par slot occupé
		// (slot_id = valeur EquipmentSlot 1..kEquipSlotCount).
		if (ok && (blocks & kSaveBlockEquipment) != 0u) // ETAPE5.5
		{
			auto* del = cache->Acquire(mysql,
				"DELETE FROM character_equipment WHERE character_id = ?");
			ok = del != nullptr && del->Bind(0, state.characterKey) && del->Execute();
			for (size_t slot = 1; ok && slot < state.equipment.size(); ++slot)
			{
				if (state.equipment[slot] == 0u)
				{
					continue;
				}
				auto* ins = cache->Acquire(mysql,
					"INSERT INTO character_equipment (character_id, slot_id, item_id) "
					"VALUES (?, ?, ?)");
				ok = ins != nullptr && ins->Bind(0, state.characterKey)
					&& ins->Bind(1, static_cast<uint32_t>(slot))
					&& ins->Bind(2, state.equipment[slot])
					&& ins->Execute();
			}
		}
		return ok ? CharacterDbSaveResult::Saved : CharacterDbSaveResult::Failed;
	}
#endif

	ServerApp::CharacterDbSaveResult ServerApp::SaveCharacterToDb(const PersistedCharacterState& state,
		float yawRadians, uint64_t ownerGeneration)
	{
		// ETAPE2.2 — plusieurs threads écrivent désormais en même temps : un
		// interblocage InnoDB (1213) ou un délai de verrou (1205) fait échouer la
		// transaction sans rien écrire. On la rejoue (3 essais) tant que la connexion
		// a été obtenue ; base injoignable = échec immédiat (pas d'attente répétée).
		// ETAPE5.5 (chantier 3.5) — blocs à écrire : ceux dont l'empreinte a changé depuis
		// le dernier COMMIT (tous si inconnue, si c'est le tour d'une écriture complète ou si
		// le différentiel est désactivé).
		const std::string progressPayload = SerializeCharacterProgressPayload(state);
		const SaveBlockHashes hashes = ComputeSaveBlockHashes(state, yawRadians, progressPayload);
		SavePlan plan;
		if (m_differentialSaves && state.restoreSnapshotId == 0u && state.characterKey != 0u)
			plan = m_savedBlocks.Plan(state.characterKey, hashes);
		else
			m_savedBlocks.Invalidate(state.characterKey); // écriture complète hors référence
		const uint32_t blocks = plan.blocks;
#if defined(SHARD_POSITION_DB)
		// Rien à écrire (aucun bloc modifié, ni courrier réclamé, ni carte, ni restauration) :
		// pas de transaction du tout, donc pas de contrôle de propriété non plus (rien n'est
		// écrit : un shard qui ne serait plus propriétaire n'abîme rien).
		if (blocks == 0u && state.mailboxClaimedIds.empty() && state.mapDiscoveryCells.empty()
			&& state.graveyardVisits.empty() && state.secretDiscoveries.empty() && !state.grantPilgrimExploit
			&& m_characterDbPool != nullptr && state.characterKey != 0u)
		{
			m_saveBlocksSkipped.fetch_add(CountSaveBlocks(kSaveBlocksAll), std::memory_order_relaxed);
			m_savesWithoutWrite.fetch_add(1u, std::memory_order_relaxed);
			m_savedBlocks.OnCommitted(state.characterKey, hashes, 0u, plan.version);
			return CharacterDbSaveResult::Saved;
		}
#endif
		CharacterDbSaveResult result = CharacterDbSaveResult::Failed;
		for (int attempt = 1; attempt <= 3; ++attempt)
		{
			bool connected = false;
			result = SaveCharacterToDbOnce(state, yawRadians, ownerGeneration, blocks, connected);
			if (result == CharacterDbSaveResult::Saved)
			{
				m_savedBlocks.OnCommitted(state.characterKey, hashes, blocks, plan.version);
				m_saveBlocksWritten.fetch_add(CountSaveBlocks(blocks), std::memory_order_relaxed);
				m_saveBlocksSkipped.fetch_add(CountSaveBlocks(kSaveBlocksAll) - CountSaveBlocks(blocks), std::memory_order_relaxed);
				return result;
			}
			if (result != CharacterDbSaveResult::Failed || !connected)
			{
				// Refus, absence de base, ou échec sans connexion : l'empreinte validée n'est
				// plus sûre, la prochaine sauvegarde sera complète.
				m_savedBlocks.Invalidate(state.characterKey);
				return result;
			}
			if (attempt < 3)
			{
				LOG_WARN(Net, "[ServerApp] SaveCharacterToDb: transaction failed, retry {} (character_key={})",
					attempt, state.characterKey);
				std::this_thread::sleep_for(std::chrono::milliseconds(20 * attempt));
			}
		}
		m_savedBlocks.Invalidate(state.characterKey); // ETAPE5.5 — COMMIT peut-être incertain
		return result;
	}

	ServerApp::CharacterDbSaveResult ServerApp::SaveCharacterToDbOnce(const PersistedCharacterState& state,
		float yawRadians, uint64_t ownerGeneration, uint32_t blocks, bool& outConnected)
	{
		outConnected = false;
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || state.characterKey == 0u)
		{
			return CharacterDbSaveResult::SkippedNoDb;
		}
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			// ETAPE2.1 — base configurée mais injoignable : échec (pas de repli
			// fichier, il ne serait jamais relu une fois le personnage en base).
			return CharacterDbSaveResult::Failed;
		}
		outConnected = true;
		// ETAPE2.2 — READ COMMITTED pour cette transaction : pas de verrous
		// d'intervalle (gap locks) entre personnages voisins lors des DELETE +
		// INSERT du sac et de l'équipement, donc beaucoup moins d'interblocages
		// entre threads d'écriture. Le contrôle de propriété garde son FOR UPDATE.
		(void)mysql_query(mysql, "SET TRANSACTION ISOLATION LEVEL READ COMMITTED");
		// Transaction : tout l'état part ENSEMBLE — un lecteur concurrent (autre
		// shard, outil web) ne voit jamais un état mixte, et un échec au milieu
		// laisse la base sur la sauvegarde précédente.
		if (mysql_query(mysql, "START TRANSACTION") != 0)
		{
			LOG_WARN(Net, "[ServerApp] SaveCharacterToDb: START TRANSACTION failed (character_key={})",
				state.characterKey);
			return CharacterDbSaveResult::Failed;
		}
		const CharacterDbSaveResult result = WriteCharacterStateInTx(mysql, cache, state, yawRadians, ownerGeneration, blocks);
		if (result == CharacterDbSaveResult::Saved)
		{
			if (mysql_query(mysql, "COMMIT") != 0)
			{
				LOG_WARN(Net, "[ServerApp] SaveCharacterToDb: COMMIT failed (character_key={})",
					state.characterKey);
				mysql_query(mysql, "ROLLBACK");
				return CharacterDbSaveResult::Failed;
			}
			return CharacterDbSaveResult::Saved;
		}
		mysql_query(mysql, "ROLLBACK");
		if (result == CharacterDbSaveResult::Failed)
		{
			LOG_WARN(Net, "[ServerApp] SaveCharacterToDb: rollback (character_key={})", state.characterKey);
		}
		return result;
#else
		(void)state;
		(void)yawRadians;
		(void)ownerGeneration;
		(void)blocks;
		return CharacterDbSaveResult::SkippedNoDb;
#endif
	}

	ServerApp::CharacterDbLoadResult ServerApp::LoadCharacterFromDb(uint64_t characterKey, PersistedCharacterState& state)
	{
		// ETAPE5.5 — personnage (re)chargé : la base a pu changer depuis (autre shard,
		// restauration, courrier) ; la première sauvegarde de la session sera complète.
		m_savedBlocks.Invalidate(characterKey);
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || characterKey == 0u)
		{
			return CharacterDbLoadResult::NotFound;
		}
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: base injoignable (character_key={})", characterKey);
			return CharacterDbLoadResult::Error;
		}
		state.characterKey = characterKey;
		// ETAPE2.6 — restauration demandée par un MJ (/restore côté master) : l'état
		// complet vient de l'instantané, à la place de l'état courant. Le courrier en
		// attente (plus bas) reste dû. La sauvegarde suivante marque la demande appliquée.
		{
			auto* rst = cache->Acquire(mysql,
				"SELECT r.snapshot_id, s.payload FROM character_restore_requests r "
				"JOIN character_snapshots s ON s.id = r.snapshot_id AND s.character_id = r.character_id "
				"WHERE r.character_id = ? AND r.applied_at IS NULL");
			if (rst == nullptr || !rst->Bind(0, characterKey) || !rst->Execute())
			{
				LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: character_restore_requests illisible (migration 0080 appliquée ?) (character_key={})",
					characterKey);
				return CharacterDbLoadResult::Error;
			}
			if (rst->FetchRow())
			{
				const uint64_t snapshotId = rst->GetUInt64(0);
				engine::core::Config snapshot;
				snapshot.LoadFromIniString(rst->GetString(1));
				while (rst->FetchRow()) {}
				// Instantané illisible (vide, tronqué) : ne rien charger plutôt que de
				// repartir de valeurs par défaut que la sauvegarde suivante écrirait.
				if (!snapshot.Has("character.zone_id") || !snapshot.Has("inventory.count") || !snapshot.Has("quests.count"))
				{
					LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: snapshot unreadable, restore refused (character_key={}, snapshot_id={})",
						characterKey, snapshotId);
					return CharacterDbLoadResult::Error;
				}
				ReadCharacterStateFromConfig(snapshot, characterKey, state);
				state.mailboxGold = 0;
				state.mailboxItems.clear();
				state.restoreSnapshotId = snapshotId;
				LOG_WARN(Net, "[ServerApp] Character restored from snapshot (character_key={}, snapshot_id={})",
					characterKey, snapshotId);
			}
		}
		const bool restoring = state.restoreSnapshotId != 0u;
		// ETAPE2.1 — progression : la présence de la ligne signifie que le personnage
		// est entièrement en base (le .ini n'est alors plus lu).
		bool progressFound = false;
		{
			auto* prog = cache->Acquire(mysql,
				"SELECT zone_id, experience_points, current_health, max_health, chat_moderator_role, "
				"progress_payload FROM character_progress WHERE character_id = ?");
			if (prog == nullptr || !prog->Bind(0, characterKey) || !prog->Execute())
			{
				LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: character_progress illisible (migration 0078 appliquée ?) (character_key={})",
					characterKey);
				return CharacterDbLoadResult::Error;
			}
			if (!restoring && prog->FetchRow())
			{
				progressFound = true;
				state.zoneId = static_cast<uint32_t>(prog->GetUInt64(0));
				state.experiencePoints = static_cast<uint32_t>(prog->GetUInt64(1));
				state.stats.currentHealth = static_cast<uint32_t>(prog->GetUInt64(2));
				state.stats.maxHealth = static_cast<uint32_t>(prog->GetUInt64(3));
				state.chatModeratorRole = prog->GetUInt64(4) != 0u;
				ApplyCharacterProgressPayload(prog->GetString(5), state);
			}
		}
		// Bourse : écrite à CHAQUE save DB (4 lignes, même à 0). Sans ligne de
		// progression, une bourse présente = personnage sauvé avant ETAPE2.1
		// (bourse, sac, équipement en base, le reste encore dans le .ini).
		bool walletFound = false;
		auto* wallet = cache->Acquire(mysql,
			"SELECT currency_id, amount FROM player_wallet WHERE player_id = ?");
		if (wallet == nullptr || !wallet->Bind(0, characterKey) || !wallet->Execute())
		{
			LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: player_wallet illisible (character_key={})", characterKey);
			return CharacterDbLoadResult::Error;
		}
		{
			while (wallet->FetchRow())
			{
				if (restoring)
				{
					continue; // ETAPE2.6 — la bourse vient de l'instantané
				}
				walletFound = true;
				const uint32_t currencyId = static_cast<uint32_t>(wallet->GetUInt64(0));
				const uint32_t amount = static_cast<uint32_t>(wallet->GetUInt64(1));
				switch (currencyId)
				{
					case 1u: state.gold = amount; break;
					case 2u: state.honor = amount; break;
					case 3u: state.badges = amount; break;
					case 4u: state.premiumCurrency = amount; break;
					default: break; // devise inconnue (future) : ignorée.
				}
			}
		}
		if (walletFound)
		{
			// Inventaire (ordre stable par slot_index — reconstruit le vector du sac).
			auto* inv = cache->Acquire(mysql,
				"SELECT item_id, quantity FROM character_inventory WHERE character_id = ? "
				"ORDER BY slot_index");
			if (inv == nullptr || !inv->Bind(0, characterKey) || !inv->Execute())
			{
				LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: character_inventory illisible (character_key={})", characterKey);
				return CharacterDbLoadResult::Error;
			}
			{
				state.inventory.clear();
				while (inv->FetchRow())
				{
					ItemStack stack{};
					stack.itemId = static_cast<uint32_t>(inv->GetUInt64(0));
					stack.quantity = static_cast<uint32_t>(inv->GetUInt64(1));
					state.inventory.push_back(stack);
				}
			}
			// Équipement porté (slots hors bornes ignorés — schéma futur plus large).
			auto* equip = cache->Acquire(mysql,
				"SELECT slot_id, item_id FROM character_equipment WHERE character_id = ?");
			if (equip == nullptr || !equip->Bind(0, characterKey) || !equip->Execute())
			{
				LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: character_equipment illisible (character_key={})", characterKey);
				return CharacterDbLoadResult::Error;
			}
			{
				state.equipment.fill(0u);
				while (equip->FetchRow())
				{
					const size_t slot = static_cast<size_t>(equip->GetUInt64(0));
					if (slot >= 1u && slot < state.equipment.size())
					{
						state.equipment[slot] = static_cast<uint32_t>(equip->GetUInt64(1));
					}
				}
			}
		}
		// ETAPE2.1 — courrier en attente : ajouté au courrier déjà présent dans
		// \p state (cas d'un import .ini), fusionné ensuite par
		// MergePersistedMailboxIntoState. Les lignes lues sont supprimées par la
		// sauvegarde suivante (mailboxClaimedIds).
		{
			auto* mail = cache->Acquire(mysql,
				"SELECT id, gold, item_id, quantity FROM character_mailbox WHERE character_id = ? "
				"ORDER BY id LIMIT 256");
			if (mail == nullptr || !mail->Bind(0, characterKey) || !mail->Execute())
			{
				LOG_ERROR(Net, "[ServerApp] LoadCharacterFromDb: character_mailbox illisible (character_key={})", characterKey);
				return CharacterDbLoadResult::Error;
			}
			{
				while (mail->FetchRow())
				{
					state.mailboxClaimedIds.push_back(mail->GetUInt64(0));
					const uint64_t gold = static_cast<uint64_t>(state.mailboxGold) + mail->GetUInt64(1);
					state.mailboxGold = static_cast<uint32_t>(
						std::min<uint64_t>(gold, std::numeric_limits<uint32_t>::max()));
					ItemStack item{};
					item.itemId = static_cast<uint32_t>(mail->GetUInt64(2));
					item.quantity = static_cast<uint32_t>(mail->GetUInt64(3));
					if (item.itemId != 0u && item.quantity != 0u)
					{
						state.mailboxItems.push_back(item);
					}
				}
			}
		}
		if (progressFound || restoring)
		{
			return CharacterDbLoadResult::Full;
		}
		return walletFound ? CharacterDbLoadResult::WalletOnly : CharacterDbLoadResult::NotFound;
#else
		(void)characterKey; (void)state;
		return CharacterDbLoadResult::NotFound;
#endif
	}

	void ServerApp::HandleHello(const Endpoint& endpoint, uint64_t helloNonce, bool authenticatedBySession)
	{
		// R1-A — charge les tables de stats une seule fois (lazy). FromEmbedded renvoie
		// directement un std::optional ; en cas d'échec on logue une fois et on garde nullopt
		// (les PV par défaut s'appliqueront → pas de régression).
		if (!m_statsTablesLoadAttempted)
		{
			m_statsTablesLoadAttempted = true;
			m_statsTables = engine::server::gameplay::CharacterStatsTables::FromEmbedded(
				engine::server::gameplay::kCharacterStatsJson,
				engine::server::gameplay::kFactionsJson);
			if (!m_statsTables)
			{
				LOG_WARN(Net,
					"[ServerApp] R1-A : moteur de stats indisponible (FromEmbedded a échoué) — "
					"PV par défaut conservés à l'enter-world.");
			}
		}

		// Phase 3.7.5 — tentativeCharacterKey élargi à uint64. Fallback sur m_nextClientId
		// (uint32) si le client n'a pas envoyé de character_id ; cast widening explicite.
		const uint64_t tentativeCharacterKey = helloNonce != 0u
			? helloNonce
			: static_cast<uint64_t>(m_nextClientId);
		if (m_bannedCharacterKeys.find(tentativeCharacterKey) != m_bannedCharacterKeys.end())
		{
			LOG_WARN(Net,
				"[ServerApp] Hello rejected: banned character_key={} (endpoint={})",
				tentativeCharacterKey,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		// ETAPE3.1 — pas de repli vers le clair : si la clé de session UDP de ce personnage
		// est connue, seul un Hello chiffré avec elle ouvre la session. Sinon un tiers
		// qui connaît le character_id contournerait le chiffrement.
		if (!authenticatedBySession && m_admittedRegistry != nullptr)
		{
			const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
			if (m_admittedRegistry->AdmittedUdpSessionKey(tentativeCharacterKey, nowMs).has_value())
			{
				LOG_WARN(Net,
					"[ServerApp] Hello en clair refusé : le personnage a une clé de session UDP (character_key={}, endpoint={})",
					tentativeCharacterKey,
					UdpTransport::EndpointToString(endpoint));
				return;
			}
		}

		// TA.3 — Gate de session : si un registre d'admission est branché (shard Linux),
		// on n'accepte le Hello que pour un character_id authentifié par un ticket validé
		// (cf. ShardTicketHandshakeHandler). Empêche un client d'usurper le personnage
		// d'autrui en envoyant un nonce arbitraire. character_id=0 n'est jamais admis.
		// Registre absent (build Windows historique) → pas de gate (compat).
		if (m_admittedRegistry != nullptr)
		{
			const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
			if (!m_admittedRegistry->IsAdmitted(tentativeCharacterKey, nowMs))
			{
				// TA.3 — race condition Hello/Admit : l'admission est peut-être en route
				// (master vient de pousser kOpcodeMasterToShardAdmitCharacter, mais le
				// Hello UDP est arrivé avant). On met en attente plutôt que de drop, et
				// DrainPendingHellos (appelé à chaque TickOnce) re-essaiera dès que
				// l'admission sera vue.
				// Dédup par character_key : un client qui spam Hello pour le même perso
				// n'écrase que sa propre entrée (et le timestamp est rafraîchi).
				// ETAPE3.1 — seules les entrées en clair sont dédupliquées ici : un Hello en
				// clair ne doit jamais modifier une poignée de main chiffrée en attente
				// (sinon un tiers redirigerait la session vers son propre point de
				// terminaison). File bornée (kMaxPendingHellos).
				bool found = false;
				for (PendingHello& entry : m_pendingHellos)
				{
					if (entry.characterKey == tentativeCharacterKey && entry.sealedHello.empty())
					{
						entry.endpoint = endpoint;
						entry.arrivedAtMs = nowMs;
						found = true;
						break;
					}
				}
				if (!found)
				{
					if (m_pendingHellos.size() >= kMaxPendingHellos)
					{
						LOG_WARN(Net, "[ServerApp] Hello ignoré : file d'attente d'admission pleine ({} entrées)",
							m_pendingHellos.size());
						return;
					}
					m_pendingHellos.push_back(PendingHello{ endpoint, tentativeCharacterKey, nowMs, {} });
				}
				LOG_INFO(Net,
					"[ServerApp] Hello mis en attente d'admission (character_key={}, endpoint={}, pending={})",
					tentativeCharacterKey,
					UdpTransport::EndpointToString(endpoint),
					m_pendingHellos.size());
				return;
			}
		}

		ConnectedClient* existingClient = FindClient(endpoint);
		if (existingClient != nullptr)
		{
			// ETAPE3.1 — un point de terminaison déjà lié à un personnage ne sert jamais
			// à un autre : sans ce contrôle, un Hello d'un autre personnage depuis la même
			// adresse (usurpée) détournerait le client existant.
			if (existingClient->persistenceCharacterKey != tentativeCharacterKey)
			{
				LOG_WARN(Net,
					"[ServerApp] Hello refusé : adresse déjà utilisée par un autre personnage (endpoint={}, client_id={}, demandé={})",
					UdpTransport::EndpointToString(endpoint), existingClient->clientId, tentativeCharacterKey);
				return;
			}
			existingClient->helloNonce = helloNonce;
			existingClient->lastUdpActivityAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
			LOG_INFO(Net, "[ServerApp] Handshake refresh (client_id={}, endpoint={}, nonce={})",
				existingClient->clientId,
				UdpTransport::EndpointToString(endpoint),
				helloNonce);
			(void)SendWelcome(*existingClient);
			SendDynamicEventBootstrap(*existingClient);
			SendQuestStateBootstrap(*existingClient);
			(void)SendWalletUpdate(*existingClient);
			return;
		}

		// ETAPE2.7 — lecture de l'état hors du tick. Avec la file de sauvegarde, la
		// lecture part sur le thread du personnage, APRÈS ses sauvegardes en attente
		// (relecture juste après écriture garantie sans attendre) ; le tick reprend le
		// Hello quand la lecture est terminée (DrainCompletedCharacterLoads). Sans
		// file (pas de base, shard Windows) ou personnage sans identifiant : lecture
		// immédiate, comme avant.
		if (m_characterSaveQueue != nullptr && m_characterSaveQueue->IsRunning() && helloNonce != 0u)
		{
			const uint64_t characterKey = helloNonce;
			const auto pendingIt = m_pendingCharacterLoads.find(characterKey);
			if (pendingIt != m_pendingCharacterLoads.end())
			{
				pendingIt->second.endpoint = endpoint; // Hello répété pendant la lecture
				pendingIt->second.helloNonce = helloNonce;
				return;
			}
			m_pendingCharacterLoads[characterKey] = PendingCharacterLoad{ endpoint, helloNonce };
			const auto job = [this, characterKey]() {
				auto data = std::make_shared<LoadedCharacterData>(LoadCharacterForHello(characterKey));
				std::lock_guard<std::mutex> lock(m_completedCharacterLoadsMutex);
				m_completedCharacterLoads.emplace_back(characterKey, std::move(data));
			};
			if (m_characterSaveQueue->EnqueueOrdered(characterKey, job) != CharacterSaveQueue::EnqueueResult::Rejected)
			{
				LOG_INFO(Net, "[ServerApp] Hello: character load queued (character_key={}, endpoint={})",
					characterKey, UdpTransport::EndpointToString(endpoint));
				return;
			}
			// File pleine : lecture immédiate, après les écritures en cours du personnage.
			m_pendingCharacterLoads.erase(characterKey);
			if (!m_characterSaveQueue->Flush(characterKey, std::chrono::milliseconds(2000)))
			{
				LOG_WARN(Net, "[ServerApp] Hello refused: pending save still running (character_key={}, queue_pending={})",
					characterKey, m_characterSaveQueue->PendingCount());
				return;
			}
		}
		const uint64_t helloCharacterKey = helloNonce != 0u ? helloNonce : static_cast<uint64_t>(m_nextClientId);
		LoadedCharacterData loaded = LoadCharacterForHello(helloCharacterKey);
		FinishHello(endpoint, helloNonce, loaded);
	}

	ServerApp::LoadedCharacterData ServerApp::LoadCharacterForHello(uint64_t characterKey)
	{
		// ETAPE2.7 — n'utilise que le pool (thread-safe) : appelable depuis un thread
		// de la file de sauvegarde.
		LoadedCharacterData data{};
		data.dbLoad = LoadCharacterFromDb(characterKey, data.dbState);
		if (data.dbLoad != CharacterDbLoadResult::Error)
		{
			data.spawnLoaded = LoadSpawnFromDb(characterKey, data.spawnX, data.spawnY, data.spawnZ, data.spawnYawDeg,
				data.name, data.gender, data.level, data.factionId, data.classId, data.raceId);
			data.discoveryLoaded = LoadDiscoveryFromDb(characterKey, data); // lore v1.0.114
		}
		return data;
	}

	bool ServerApp::LoadDiscoveryFromDb(uint64_t characterKey, LoadedCharacterData& data)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr || characterKey == 0u)
		{
			return false;
		}
		auto guard = m_characterDbPool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (mysql == nullptr || cache == nullptr)
		{
			return false;
		}
		{
			auto* sel = cache->Acquire(mysql,
				"SELECT graveyard_id FROM character_graveyard_visits WHERE character_id = ?");
			if (sel == nullptr || !sel->Bind(0, characterKey) || !sel->Execute())
			{
				LOG_WARN(Net, "[ServerApp] Exploration : character_graveyard_visits illisible (migration 0086 appliquée ?) "
					"(character_key={}) — suivi désactivé pour la session", characterKey);
				return false;
			}
			while (sel->FetchRow())
				data.visitedGraveyards.push_back(sel->GetString(0));
		}
		{
			auto* sel = cache->Acquire(mysql,
				"SELECT place_id FROM character_secret_discoveries WHERE character_id = ?");
			if (sel == nullptr || !sel->Bind(0, characterKey) || !sel->Execute())
			{
				LOG_WARN(Net, "[ServerApp] Exploration : character_secret_discoveries illisible (character_key={})", characterKey);
				return false;
			}
			while (sel->FetchRow())
				data.discoveredSecretPlaces.push_back(sel->GetString(0));
		}
		{
			auto* sel = cache->Acquire(mysql,
				"SELECT COUNT(*) FROM character_exploit_unlocks u JOIN exploits e ON e.id = u.exploit_id "
				"WHERE u.character_id = ? AND e.code = ?");
			if (sel == nullptr || !sel->Bind(0, characterKey) || !sel->Bind(1, kPilgrimExploitCode) || !sel->Execute())
			{
				LOG_WARN(Net, "[ServerApp] Exploration : character_exploit_unlocks illisible (character_key={})", characterKey);
				return false;
			}
			if (sel->FetchRow())
			{
				data.pilgrimExploitUnlocked = sel->GetUInt64(0) != 0u;
				while (sel->FetchRow()) {}
			}
		}
		return true;
#else
		(void)characterKey; (void)data;
		return false;
#endif
	}

	void ServerApp::DrainCompletedCharacterLoads()
	{
		std::vector<std::pair<uint64_t, std::shared_ptr<LoadedCharacterData>>> completed;
		{
			std::lock_guard<std::mutex> lock(m_completedCharacterLoadsMutex);
			completed.swap(m_completedCharacterLoads);
		}
		for (auto& [characterKey, data] : completed)
		{
			const auto pendingIt = m_pendingCharacterLoads.find(characterKey);
			if (pendingIt == m_pendingCharacterLoads.end())
			{
				continue; // annulée entre-temps
			}
			const PendingCharacterLoad pending = pendingIt->second;
			// L'état a pu changer pendant la lecture : bannissement, admission retirée
			// ou déplacée par le master (les sauvegardes seraient toutes refusées), ou
			// joueur déjà accepté sur cet endpoint.
			bool admitted = true;
			if (m_admittedRegistry != nullptr)
			{
				const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
				admitted = m_admittedRegistry->IsAdmitted(characterKey, nowMs);
			}
			if (!admitted
				|| m_bannedCharacterKeys.find(characterKey) != m_bannedCharacterKeys.end()
				|| FindClient(pending.endpoint) != nullptr)
			{
				m_pendingCharacterLoads.erase(pendingIt);
				LOG_WARN(Net, "[ServerApp] Hello dropped after load (character_key={}, endpoint={}, admitted={})",
					characterKey, UdpTransport::EndpointToString(pending.endpoint), admitted);
				continue;
			}
			// Même personnage encore connecté depuis une autre adresse (reconnexion
			// après changement de port) : l'ancienne session est fermée (sauvegarde de
			// départ), puis l'état est relu APRÈS cette sauvegarde.
			if (ConnectedClient* previous = FindConnectedClientByCharacterKey(characterKey))
			{
				LOG_INFO(Net, "[ServerApp] Hello: closing previous session before reload (character_key={}, old_client_id={})",
					characterKey, previous->clientId);
				DisconnectConnectedClient(previous->clientId, "reconnect");
				const auto job = [this, characterKey]() {
					auto reloaded = std::make_shared<LoadedCharacterData>(LoadCharacterForHello(characterKey));
					std::lock_guard<std::mutex> lock(m_completedCharacterLoadsMutex);
					m_completedCharacterLoads.emplace_back(characterKey, std::move(reloaded));
				};
				if (m_characterSaveQueue != nullptr
					&& m_characterSaveQueue->EnqueueOrdered(characterKey, job) != CharacterSaveQueue::EnqueueResult::Rejected)
				{
					continue; // l'entrée en attente reste : la relecture la reprendra
				}
				m_pendingCharacterLoads.erase(pendingIt);
				continue; // file indisponible : le client renverra un Hello
			}
			m_pendingCharacterLoads.erase(pendingIt);
			FinishHello(pending.endpoint, pending.helloNonce, *data);
		}
	}

	void ServerApp::FinishHello(const Endpoint& endpoint, uint64_t helloNonce, LoadedCharacterData& loaded)
	{
		const uint64_t helloCharacterKey = helloNonce != 0u ? helloNonce : static_cast<uint64_t>(m_nextClientId);
		// ETAPE2.1 — l'état en base est lu AVANT de créer le joueur. Base configurée
		// mais illisible (connexion perdue, migration 0078 absente) → Hello refusé,
		// le client réessaiera : repartir du .ini (plus écrit depuis ETAPE2.1) ou des
		// valeurs par défaut ferait écraser la vraie progression par la sauvegarde
		// suivante.
		if (loaded.dbLoad == CharacterDbLoadResult::Error)
		{
			LOG_ERROR(Net, "[ServerApp] Hello refused: character state unreadable in DB (character_key={}, endpoint={})",
				helloCharacterKey, UdpTransport::EndpointToString(endpoint));
			return;
		}
		const CharacterDbLoadResult dbLoad = loaded.dbLoad;
		PersistedCharacterState& dbState = loaded.dbState;

		ConnectedClient client{};
		client.endpoint = endpoint;
		client.clientId = m_nextClientId++;
		client.zoneId = 1;
		client.entityId = static_cast<EntityId>(client.clientId);
		client.helloNonce = helloNonce;
		// Phase 3.7.5 — fallback uint32 clientId widened to uint64.
		client.persistenceCharacterKey = helloNonce != 0u
			? helloNonce
			: static_cast<uint64_t>(client.clientId);
		client.stats.currentHealth = kDefaultPlayerHealth;
		client.stats.maxHealth = kDefaultPlayerHealth;
		client.combat = BuildDefaultCombatComponent(m_tickHz, kDefaultPlayerDamage);
		// TA.3 — initialise le timestamp d'activite UDP au Hello (premier signe de vie).
		client.lastUdpActivityAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		ConnectedClient& acceptedClient = m_clients.emplace_back(std::move(client));
		// TG.2 — insertion incrémentale dans les 3 index O(1). push_back garantit que les
		// indices existants restent valides (la nouvelle entrée est en queue).
		const size_t acceptedIndex = m_clients.size() - 1u;
		m_clientIndexByPackedEndpoint[PackEndpointKey(acceptedClient.endpoint)] = acceptedIndex;
		m_clientIndexByClientId[acceptedClient.clientId] = acceptedIndex;
		m_clientIndexByEntityId[acceptedClient.entityId] = acceptedIndex;
		// ETAPE2.1 — la base est la seule source de vérité. Ordre de lecture :
		//   1. base (progression, bourse, sac, équipement, courrier) ;
		//   2. fichier .ini seulement si la base n'a pas de ligne de progression
		//      (personnage créé avant ETAPE2.1, ou shard sans base) : import une fois,
		//      la sauvegarde suivante écrit tout en base et le .ini n'est plus relu ;
		//   3. si la base n'avait que bourse/sac/équipement (Roadmap-10), ces trois
		//      domaines restent ceux de la base, par-dessus le fichier.
		PersistedCharacterState persistedState{};
		bool fileLoaded = false;
		if (dbLoad == CharacterDbLoadResult::Full)
		{
			persistedState = std::move(dbState);
		}
		else
		{
			fileLoaded = m_characterPersistence.LoadCharacter(acceptedClient.persistenceCharacterKey, persistedState);
			if (dbLoad == CharacterDbLoadResult::WalletOnly)
			{
				persistedState.gold = dbState.gold;
				persistedState.honor = dbState.honor;
				persistedState.badges = dbState.badges;
				persistedState.premiumCurrency = dbState.premiumCurrency;
				persistedState.inventory = std::move(dbState.inventory);
				persistedState.equipment = dbState.equipment;
			}
			// Courrier en base (déposé par un shard) ajouté au courrier du fichier.
			const uint64_t mailGold = static_cast<uint64_t>(persistedState.mailboxGold) + dbState.mailboxGold;
			persistedState.mailboxGold = static_cast<uint32_t>(
				std::min<uint64_t>(mailGold, std::numeric_limits<uint32_t>::max()));
			persistedState.mailboxItems.insert(persistedState.mailboxItems.end(),
				dbState.mailboxItems.begin(), dbState.mailboxItems.end());
			persistedState.mailboxClaimedIds = dbState.mailboxClaimedIds;
		}
		const bool dbLoaded = dbLoad != CharacterDbLoadResult::NotFound || !persistedState.mailboxClaimedIds.empty();
		LOG_INFO(Net,
			"[ServerApp] Character state source (character_key={}, db={}, file={}, gold={}, inventory_items={}, mailbox_rows={})",
			acceptedClient.persistenceCharacterKey,
			dbLoad == CharacterDbLoadResult::Full ? "full" : (dbLoad == CharacterDbLoadResult::WalletOnly ? "wallet_only" : "none"),
			fileLoaded ? "imported" : "none",
			persistedState.gold,
			persistedState.inventory.size(),
			persistedState.mailboxClaimedIds.size());
		acceptedClient.mailboxClaimedIds = persistedState.mailboxClaimedIds;
		acceptedClient.restoreSnapshotId = persistedState.restoreSnapshotId; // ETAPE2.6
		bool mailboxMergedAtLogin = false;
		if (fileLoaded || dbLoaded)
		{
			const bool hadMailbox =
				persistedState.mailboxGold != 0u || !persistedState.mailboxItems.empty();
			MergePersistedMailboxIntoState(persistedState);
			acceptedClient.zoneId = persistedState.zoneId;
			acceptedClient.positionMetersX = persistedState.positionMetersX;
			acceptedClient.positionMetersY = persistedState.positionMetersY;
			acceptedClient.positionMetersZ = persistedState.positionMetersZ;
			acceptedClient.experiencePoints = persistedState.experiencePoints;
			acceptedClient.gold = persistedState.gold;
			acceptedClient.honor = persistedState.honor;
			acceptedClient.badges = persistedState.badges;
			acceptedClient.premiumCurrency = persistedState.premiumCurrency;
			acceptedClient.stats = persistedState.stats;
			acceptedClient.inventory = persistedState.inventory;
			acceptedClient.equipment = persistedState.equipment; // Chantier 2 SP-A
			acceptedClient.questStates = persistedState.questStates;
		acceptedClient.chatIgnoredDisplayNames = std::move(persistedState.chatIgnoredDisplayNames);
		acceptedClient.chatModeratorRole = persistedState.chatModeratorRole;
		/// M36.2 — restore known professions.
		acceptedClient.professions = std::move(persistedState.professions);
		/// Grimoire — restore action bar layout (10 slots).
		acceptedClient.actionBarLayout = std::move(persistedState.actionBarLayout);
		// Roadmap-3 (2026-07-19) — ceinture persistée.
		acceptedClient.beltLayout = std::move(persistedState.beltLayout);
		// Anniversaires SP3 (2026-07-18) — expirations des gâteaux possédés
		// (la purge de minuit tourne dans TickCakeBuffs).
		acceptedClient.cakeExpiresAtMsUtcByItemId = std::move(persistedState.cakeExpiresAtMsUtcByItemId);
		/// SP-B — restore known class skills.
		acceptedClient.knownSkillIds = std::move(persistedState.knownSkillIds);
		acceptedClient.hasReplicatedState = true;
			LOG_INFO(Net,
				"[ServerApp] Character state restored (client_id={}, character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={}, moderator={})",
				acceptedClient.clientId,
				acceptedClient.persistenceCharacterKey,
				acceptedClient.zoneId,
				acceptedClient.inventory.size(),
				acceptedClient.questStates.size(),
				acceptedClient.chatIgnoredDisplayNames.size(),
				acceptedClient.chatModeratorRole ? "true" : "false");
			// Sauvegarde du courrier fusionné : plus bas, une fois la génération du
			// verrou de propriété et la position de la base connues (ETAPE2.1).
			mailboxMergedAtLogin = hadMailbox;
		}
		else
		{
			acceptedClient.hasReplicatedState = true;
			LOG_INFO(Net,
				"[ServerApp] Character state defaults active (client_id={}, character_key={})",
				acceptedClient.clientId,
				acceptedClient.persistenceCharacterKey);
		}

		// Retour joueur 2026-07-21 — tout personnage COMMENCE avec une ceinture :
		// si aucune ceinture n'est portée NI possédée en sac (perso neuf, ou
		// ceinture perdue/vendue), la « Ceinture usée » (4 slots) est équipée
		// d'office. Avant les envois d'état initiaux (équipement/ceinture),
		// qui reflètent donc l'état corrigé.
		EnsureStarterBelt(acceptedClient);

		// TA.4 — pont position : la table `characters` (écrite par le master via
		// CharacterSavePositionHandler) fait foi pour le spawn. On surcharge la position
		// issue du fichier/défauts par spawn_x/y/z de la DB. Sans pool (build Windows ou
		// DB non configurée) ou personnage absent, LoadSpawnFromDb renvoie false et on
		// conserve la position fichier. Avant UpdateClientInterest (qui place l'AoI).
		{
			// ETAPE2.7 — lu avec le reste de l'état (thread de la file ou dans le tick
			// sans file), cf. LoadCharacterForHello.
			const float dbX = loaded.spawnX, dbY = loaded.spawnY, dbZ = loaded.spawnZ, dbYawDeg = loaded.spawnYawDeg;
			std::string dbName = std::move(loaded.name);
			std::string dbGender = std::move(loaded.gender);
			const uint32_t dbLevel = loaded.level;
			std::string dbFactionId = std::move(loaded.factionId);
			std::string dbClassId = std::move(loaded.classId);
			std::string dbRaceId = std::move(loaded.raceId);
			if (loaded.spawnLoaded)
			{
				// ETAPE2.6 — personnage restauré : la position est celle de l'instantané.
				if (acceptedClient.restoreSnapshotId == 0u)
				{
					acceptedClient.positionMetersX = dbX;
					acceptedClient.positionMetersY = dbY;
					acceptedClient.positionMetersZ = dbZ;
					acceptedClient.yawRadians = dbYawDeg * 0.01745329252f; // deg -> rad
				}
				// TD.5 — nom du personnage destiné aux plaques de nom des avatars distants.
				acceptedClient.characterName = std::move(dbName);
				// TD.6 — genre du personnage destiné au rendu des avatars distants.
				acceptedClient.gender = std::move(dbGender);
				// Présence enrichie — niveau (table characters) reporté au master via heartbeat.
				acceptedClient.level = dbLevel;
				// ETAPE2.6 — restauration : le niveau est celui de l'instantané.
				if (acceptedClient.restoreSnapshotId != 0u && persistedState.level > 0u)
				{
					acceptedClient.level = persistedState.level;
				}
				// R1-A — faction/classe pour le moteur de stats (PV à l'enter-world).
				acceptedClient.factionId = std::move(dbFactionId);
				acceptedClient.classId = std::move(dbClassId);
				acceptedClient.raceId = std::move(dbRaceId);
				LOG_INFO(Net,
					"[ServerApp] Spawn position chargee depuis la DB (character_key={}, name=\"{}\", gender=\"{}\", pos=({:.2f}, {:.2f}, {:.2f}))",
					acceptedClient.persistenceCharacterKey, acceptedClient.characterName, acceptedClient.gender, dbX, dbY, dbZ);
			}
			// TD.5/TD.6 — fallback nom/genre : en mode no-DB (deploiement sans MySQL cote shard),
			// LoadSpawnFromDb renvoie false et characterName/gender restent vides → le client
			// retomberait sur le fallback "P<clientId>" + skin male par defaut. Le master nous a
			// pousse nom et genre via kOpcodeMasterToShardAdmitCharacter, on les recupere du
			// registre d'admission.
			if (m_admittedRegistry != nullptr)
			{
				const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
				// Présence unifiée : résout le compte propriétaire (clé de ShardPresenceService).
				acceptedClient.accountId = m_admittedRegistry->AdmittedAccountId(
					acceptedClient.persistenceCharacterKey, nowMs);
				// Roadmap-7 — guilde du compte (poussée par le master dans le push
				// d'admission ; 0 = sans guilde). Sert au partage du buff gâteau.
				acceptedClient.guildId = m_admittedRegistry->AdmittedGuildId(
					acceptedClient.persistenceCharacterKey, nowMs);
				// ETAPE1.2 — génération du verrou de propriété (0 = master legacy).
				acceptedClient.ownerGeneration = m_admittedRegistry->AdmittedOwnerGeneration(
					acceptedClient.persistenceCharacterKey, nowMs);
				if (acceptedClient.characterName.empty())
				{
					std::string admittedName = m_admittedRegistry->AdmittedCharacterName(
						acceptedClient.persistenceCharacterKey, nowMs);
					if (!admittedName.empty())
					{
						acceptedClient.characterName = std::move(admittedName);
						LOG_INFO(Net,
							"[ServerApp] Nom personnage recupere du registre d'admission (character_key={}, name=\"{}\")",
							acceptedClient.persistenceCharacterKey, acceptedClient.characterName);
					}
				}
				if (acceptedClient.gender.empty())
				{
					std::string admittedGender = m_admittedRegistry->AdmittedGender(
						acceptedClient.persistenceCharacterKey, nowMs);
					if (!admittedGender.empty())
					{
						acceptedClient.gender = std::move(admittedGender);
						LOG_INFO(Net,
							"[ServerApp] Genre personnage recupere du registre d'admission (character_key={}, gender=\"{}\")",
							acceptedClient.persistenceCharacterKey, acceptedClient.gender);
					}
				}
			}
		}

		// Level-up runtime : le niveau persisté (fichier) préserve les level-ups gagnés en
		// jeu tant que la colonne `level` de la table `characters` (master) n'est pas
		// synchronisée. On préfère le plus haut des deux (DB vs fichier). Placé AVANT le bloc
		// R1-A pour que les PV soient calculés à partir du niveau final.
		if (persistedState.level > 0u)
		{
			acceptedClient.level = std::max(acceptedClient.level, persistedState.level);
		}

		// R1-A — PV calculés depuis le moteur de stats (faction/classe/sexe/niveau). Placé
		// après le merge de l'état persisté (acceptedClient.stats fait foi) ET après la
		// résolution niveau/faction/classe (LoadSpawnFromDb ci-dessus). Le resolver clamp
		// les PV courants persistés sur le nouveau max. Faction/classe vides (char legacy ou
		// mode no-DB) → resolved=false → PV par défaut conservés (pas de régression).
		if (m_statsTables)
		{
			const engine::server::gameplay::Sex sex =
				(acceptedClient.gender == "female") ? engine::server::gameplay::Sex::Female
				                                    : engine::server::gameplay::Sex::Male;
			const engine::server::gameplay::SpawnHealth sh = engine::server::gameplay::ResolveSpawnHealth(
				*m_statsTables, acceptedClient.factionId, acceptedClient.classId,
				sex, acceptedClient.level, acceptedClient.stats.currentHealth);
			if (sh.resolved)
			{
				acceptedClient.stats.maxHealth = sh.maxHealth;
				acceptedClient.stats.currentHealth = sh.currentHealth;
				// Chantier 2 SP-A — le bonus PV d'équipement élargit le pool de PV max
				// (les PV courants persistés restent clampés dessous).
				const int32_t hpBonus = SumEquipmentBonus(acceptedClient).hp;
				if (hpBonus != 0)
				{
					const int64_t widened = static_cast<int64_t>(acceptedClient.stats.maxHealth) + hpBonus;
					acceptedClient.stats.maxHealth = widened < 0 ? 0u : static_cast<uint32_t>(widened);
					if (acceptedClient.stats.currentHealth > acceptedClient.stats.maxHealth)
					{
						acceptedClient.stats.currentHealth = acceptedClient.stats.maxHealth;
					}
				}
				LOG_INFO(Net,
					"[ServerApp] R1-A enter-world stats (client_id={}, faction={}, class={}, level={}) -> maxHealth={}, currentHealth={}",
					acceptedClient.clientId,
					acceptedClient.factionId,
					acceptedClient.classId,
					acceptedClient.level,
					sh.maxHealth,
					sh.currentHealth);
			}
			// Combat SP2 — injecte aussi les stats offensives calculées (damage,
			// portée, précision, critique) dans le composant combat, à la place
			// des constantes MVP. Faction/classe vides → ComputeStats nullopt →
			// le composant MVP est conservé (pas de régression mode no-DB).
			auto derived = engine::server::gameplay::ComputeStats(
				*m_statsTables, acceptedClient.factionId, acceptedClient.classId,
				sex, acceptedClient.level);
			if (derived)
			{
				// Chantier 2 SP-A — bonus d'equipement dans le combat/ressource au spawn.
				ApplyEquipmentBonus(acceptedClient, *derived);
				ApplyDerivedCombatStats(acceptedClient, *derived);
				// Combat SP3 — profil de classe (kit de sorts) + ressource runtime.
				const auto factionIt = m_statsTables->factions.find(acceptedClient.factionId);
				if (factionIt != m_statsTables->factions.end())
				{
					const auto classIt = factionIt->second.classesById.find(acceptedClient.classId);
					if (classIt != factionIt->second.classesById.end())
					{
						acceptedClient.profileId = classIt->second.profile;
					}
				}
				acceptedClient.maxResource = derived->resource;
				acceptedClient.currentResource = acceptedClient.maxResource;
				LOG_INFO(Net,
					"[ServerApp] SP2 enter-world combat stats (client_id={}, damage={}, range={:.1f}, accuracy={:.1f}, crit={:.1f}%x{:.2f}, profile={}, resource={})",
					acceptedClient.clientId,
					acceptedClient.combat.damagePerHit,
					acceptedClient.combat.attackRangeMeters,
					acceptedClient.combat.accuracy,
					acceptedClient.combat.critRate,
					acceptedClient.combat.critMult,
					acceptedClient.profileId.empty() ? "<none>" : acceptedClient.profileId.c_str(),
					acceptedClient.maxResource);
			}
		}

		std::vector<QuestProgressDelta> questSyncDeltas;
		// EXT-2 — rejoue les resets temporels (répétables/quotidiennes/cooldown)
		// AVANT la sync : une quête reset repasse Locked, que SyncQuestStates
		// repromeut à Offered dans la même passe. L'état final est transmis au
		// client par SendQuestStateBootstrap (plus bas), pas par ces deltas.
		(void)m_questRuntime.ApplyRepeatResets(
			acceptedClient.questStates, NowUnixEpochMsUtc(), questSyncDeltas);
		if (m_questRuntime.SyncQuestStates(acceptedClient.questStates, questSyncDeltas))
		{
			if (!questSyncDeltas.empty())
			{
				LOG_INFO(Net, "[ServerApp] Quest state bootstrap updated (client_id={}, deltas={})",
					acceptedClient.clientId,
					questSyncDeltas.size());
			}
		}
		else
		{
			LOG_WARN(Net, "[ServerApp] Quest state bootstrap skipped: runtime sync failed (client_id={})",
				acceptedClient.clientId);
		}

		// Monde « Royaumes » — position hors des cellules générées (ou héritée du
		// repère local de feyhin) : replacée au point de départ de la race. Le
		// client en est informé par un ForcePosition juste après le Welcome.
		// ETAPE2.1 — la zone vient du fichier importé ou de character_progress.
		const bool relocatedAtLogin = NormalizeCellWorldLoginPosition(acceptedClient,
			(fileLoaded || dbLoad == CharacterDbLoadResult::Full)
				? std::optional<uint32_t>(persistedState.zoneId) : std::nullopt);

		// Combat SP2 — mémorise la position d'entrée en monde comme point de
		// réapparition (la position finale est résolue à ce stade : persistance
		// fichier, pont DB et défauts ont tous été appliqués).
		acceptedClient.spawnPositionMetersX = acceptedClient.positionMetersX;
		acceptedClient.spawnPositionMetersY = acceptedClient.positionMetersY;
		acceptedClient.spawnPositionMetersZ = acceptedClient.positionMetersZ;

		// ETAPE2.1 — courrier fusionné : sauvé maintenant que la génération du verrou
		// (1.2) et la position réelle sont posées (avant, la sauvegarde était refusée,
		// ou écrivait la position par défaut). Supprime les lignes de courrier lues.
		if (mailboxMergedAtLogin || acceptedClient.restoreSnapshotId != 0u)
		{
			SaveConnectedClient(acceptedClient,
				acceptedClient.restoreSnapshotId != 0u ? "snapshot_restore" : "mailbox_merge_login");
			LOG_INFO(Net, "[ServerApp] Mailbox merged / snapshot restored at login (client_id={}, snapshot_id={})",
				acceptedClient.clientId, acceptedClient.restoreSnapshotId);
			// ETAPE2.6 — l'instantané (déjà copié dans la sauvegarde ci-dessus) ne doit
			// plus marquer de demande : une nouvelle demande posée pendant la session
			// s'appliquera à la prochaine connexion, pas à la prochaine sauvegarde.
			acceptedClient.restoreSnapshotId = 0;
		}

		// ETAPE3.3 — position officielle de départ des règles de mouvement, et vitesse
		// maximale autoritaire (stats dérivées). Époque 0 : celle du client au départ.
		engine::movement::ResetMovement(acceptedClient.movement, acceptedClient.positionMetersX,
			acceptedClient.positionMetersY, acceptedClient.positionMetersZ);
		RefreshMaxMoveSpeed(acceptedClient);

		UpdateClientInterest(acceptedClient);
		LOG_INFO(Net, "[ServerApp] Client accepted (client_id={}, entity_id={}, zone_id={}, hp={}, endpoint={}, total_clients={})",
			acceptedClient.clientId,
			acceptedClient.entityId,
			acceptedClient.zoneId,
			acceptedClient.stats.currentHealth,
			UdpTransport::EndpointToString(endpoint),
			m_clients.size());
		(void)SendWelcome(acceptedClient);
		// Seconde carte (Les Enfers) — le client entre toujours en monde sur le continent
		// (il ne connaît que la position de characters.spawn_*) : un personnage d'une
		// autre carte lui est annoncé par un ZoneChange (zone = cellule des Enfers), PUIS
		// sa position est imposée (ForcePosition) — le client change de carte à ce moment.
		const bool otherMapAtLogin = m_cellWorld && ClientCellMap(acceptedClient) != engine::world::cells::CellMap::Royaumes;
		if (otherMapAtLogin)
		{
			SendMapZoneChange(acceptedClient);
			LOG_INFO(Net, "[ServerApp] Entree en monde sur la carte '{}' (client_id={}, zone_id={})",
				engine::world::cells::WorldIdOf(ClientCellMap(acceptedClient)), acceptedClient.clientId, acceptedClient.zoneId);
		}
		if (relocatedAtLogin || otherMapAtLogin)
		{
			// Mouvement client-autoritaire : sans ce message, le client garderait sa
			// position (hors monde) et l'imposerait au premier Input.
			m_antiCheat.Reset(acceptedClient.persistenceCharacterKey);
			SendForcePosition(acceptedClient, kForcePositionReasonTeleport);
		}
		if (relocatedAtLogin)
		{
			// Sauvegarde immédiate : le fichier personnage porte désormais la zone de la
			// cellule (plus de relocalisation au prochain login, même après un crash).
			SaveConnectedClient(acceptedClient, "world_relocation");
		}
		(void)SendPlayerStats(acceptedClient);
		(void)SendPlayerXpUpdate(acceptedClient); // PR-C — sync initial barre d'XP
		(void)SendActionBarLayout(acceptedClient); // Grimoire — layout persisté (ou vide)
		(void)SendBeltLayout(acceptedClient); // Roadmap-3 — ceinture persistée (ou vide)
		(void)SendClassProgression(acceptedClient); // SP-B — progression par-classe persistée (ou vide)
		SendDynamicEventBootstrap(acceptedClient);
		SendQuestStateBootstrap(acceptedClient);
		(void)SendWalletUpdate(acceptedClient);
		// Loot réel (2026-07-19) — resynchronise l'inventaire ET l'équipement
		// PERSISTÉS à l'entrée en monde. Avant ce fix, le serveur rechargeait
		// bien les deux depuis le fichier personnage mais n'en envoyait rien :
		// au relog, le sac paraissait vide jusqu'au premier delta (loot,
		// achat, équipement…).
		(void)SendInventoryDelta(acceptedClient,
			std::span<const ItemStack>(acceptedClient.inventory.data(), acceptedClient.inventory.size()));
		(void)SendEquipmentUpdate(acceptedClient);
		// Lore v1.0.114 — exploration du personnage (cimetières visités, lieux secrets
		// découverts) : suivie seulement dans le monde en cellules, avec une base lisible.
		if (m_cellWorld && loaded.discoveryLoaded)
		{
			const bool catchUp = acceptedClient.discovery.Load(m_discoveryCatalog, loaded.visitedGraveyards,
				loaded.discoveredSecretPlaces, loaded.pilgrimExploitUnlocked);
			SendDiscoveryState(acceptedClient);
			if (catchUp)
			{
				// Tous les cimetières visités mais exploit absent (sauvegarde perdue) :
				// débloqué maintenant, écrit par la sauvegarde ci-dessous.
				DiscoveryEvents events;
				events.exploitUnlocked = true;
				SendDiscoveryEvents(acceptedClient, events);
				SaveConnectedClient(acceptedClient, "pilgrim_exploit_catch_up");
			}
		}
		OnClientLogin(acceptedClient);
	}

	void ServerApp::HandleInput(const Endpoint& endpoint, const InputMessage& input)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != input.clientId)
		{
			LOG_WARN(Net, "[ServerApp] Input ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				input.clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		// TA.3 — ce client est vivant (même si la commande est ensuite ignorée).
		client->lastUdpActivityAtMs = nowMs;

		// ETAPE3.3 — commande partie avant le dernier téléport : ignorée (le client ne
		// connaissait pas encore sa nouvelle position). Si le client reste dans l'ancienne
		// époque (ForcePosition perdu ou non envoyé), la position imposée est renvoyée,
		// au plus une fois toutes les 500 ms, SANS nouvelle époque.
		if (input.moveEpoch != client->moveEpoch)
		{
			if ((nowMs - client->lastForcePositionSentMs) >= 500u)
			{
				EmitForcePosition(*client, client->lastForcePositionReason);
			}
			return;
		}

		// v1.0.125 — à bord d'un bateau : la commande est dans le REPÈRE DU BATEAU (marche
		// sur le pont). Mêmes règles de mouvement (vitesse, saut, budget de temps), puis le
		// point est ramené sur le pont ; la position monde suit la pose du bateau.
		if (client->boatIndex >= 0 && static_cast<size_t>(client->boatIndex) < m_boatPoses.size())
		{
			engine::movement::MoveCommand deckCommand{};
			deckCommand.sequence = input.inputSequence;
			deckCommand.clientTimeMs = input.clientTimeMs;
			deckCommand.lastJumpMs = input.lastJumpMs;
			deckCommand.lastDodgeMs = input.lastDodgeMs;
			deckCommand.x = input.positionMetersX;
			deckCommand.y = input.positionMetersY;
			deckCommand.z = input.positionMetersZ;
			const engine::movement::MoveResult deckResult =
				engine::movement::ApplyMoveCommand(client->movement, deckCommand, client->maxMoveSpeed, nowMs);
			if (deckResult.verdict == engine::movement::MoveVerdict::Stale)
				return;
			float deckX = client->movement.x, deckZ = client->movement.z;
			const bool offDeck = BoatFleet::ClampToDeck(deckX, deckZ);
			client->movement.x = deckX;
			client->movement.z = deckZ;
			client->movement.y = std::clamp(client->movement.y, BoatFleet::kDeckY + BoatFleet::kStandHeight,
				BoatFleet::kDeckY + BoatFleet::kStandHeight + 2.0f);
			client->lastInputSequence = input.inputSequence;
			if (std::isfinite(input.yawRadians))
				client->yawRadians = input.yawRadians;
			client->animationState = input.animationState;
			client->hasReplicatedState = true;
			UpdatePassengerWorldPosition(*client);
			if (deckResult.verdict == engine::movement::MoveVerdict::Corrected || offDeck)
				SendMoveCorrection(*client, input.inputSequence);
			UpdateClientInterest(*client);
			return;
		}

		// Monde « Royaumes » — mur invisible côté serveur : une position dans une cellule
		// non générée (ou NaN) est refusée ; le client est recalé (ForcePosition,
		// ≤ 1 / 500 ms) sur la dernière position officielle.
		if (!IsInsideGeneratedWorld(input.positionMetersX, input.positionMetersZ, ClientCellMap(*client)))
		{
			LOG_DEBUG(Net, "[ServerApp] Input rejete : hors du monde genere (client_id={}, pos=({:.1f},{:.1f}))",
				client->clientId, input.positionMetersX, input.positionMetersZ);
			// ETAPE3.3 — la position officielle ne change pas : pas de nouvelle époque ni
			// de budget de temps remis à neuf (sinon chaque refus offrirait la marge du
			// départ, cf. docs/architecture/etape3_reseau.md §3.3).
			if ((nowMs - client->lastForcePositionSentMs) >= 500u)
			{
				EmitForcePosition(*client, kForcePositionReasonTeleport);
			}
			return;
		}

		// ETAPE3.3 (chantier 4.1) — le shard tranche avec les règles de mouvement
		// partagées : budget de temps (horloge client jamais plus rapide que la sienne),
		// distance horizontale (vitesse autoritaire du personnage, esquive), hauteur
		// (pente, saut). Remplace le contrôle 13 m/s de l'anti-triche (AntiCheatGameplay)
		// pour les joueurs.
		engine::movement::MoveCommand command{};
		command.sequence = input.inputSequence;
		command.clientTimeMs = input.clientTimeMs;
		command.lastJumpMs = input.lastJumpMs;
		command.lastDodgeMs = input.lastDodgeMs;
		command.x = input.positionMetersX;
		command.y = input.positionMetersY;
		command.z = input.positionMetersZ;
		const engine::movement::MoveResult result =
			engine::movement::ApplyMoveCommand(client->movement, command, client->maxMoveSpeed, nowMs);
		if (result.verdict == engine::movement::MoveVerdict::Stale)
		{
			return; // en retard, en double ou rejouée
		}

		client->lastInputSequence = input.inputSequence;
		client->positionMetersX = client->movement.x;
		client->positionMetersY = client->movement.y;
		client->positionMetersZ = client->movement.z;
		if (std::isfinite(input.yawRadians))
			client->yawRadians = input.yawRadians; // TC.1 : orientation fournie par le client
		client->animationState = input.animationState; // TD.8 : état d'animation reporté par le client
		client->velocityMetersPerSecondX = result.velocityX;
		client->velocityMetersPerSecondY = result.velocityY;
		client->velocityMetersPerSecondZ = result.velocityZ;
		client->hasReplicatedState = true;

		if (result.verdict == engine::movement::MoveVerdict::Corrected)
		{
			++m_antiCheatViolations;
			LOG_DEBUG(AntiCheat,
				"[ServerApp] Mouvement hors règles, position ramenée (client_id={}, seq={}, demandée=({:.2f},{:.2f}), officielle=({:.2f},{:.2f}), max={:.2f} m)",
				client->clientId, input.inputSequence, input.positionMetersX, input.positionMetersZ,
				client->positionMetersX, client->positionMetersZ, result.maxDistanceMeters);
			SendMoveCorrection(*client, input.inputSequence);
		}

		MaybeApplyZoneTransition(*client);
		// Seconde carte (Les Enfers) — portails entre cartes (juste après les transitions).
		MaybeApplyPortal(*client);
		// Lore v1.0.140 — portes du rocher de l'Oracle (passages conditionnels, même carte).
		MaybeApplyDoorPassage(*client);
		LOG_DEBUG(Net, "[ServerApp] Input accepted (client_id={}, seq={}, pos=({:.2f}, {:.2f}))",
			client->clientId,
			client->lastInputSequence,
			client->positionMetersX,
			client->positionMetersZ);
		UpdateClientInterest(*client);
	}

	void ServerApp::SendMoveCorrection(ConnectedClient& client, uint32_t inputSequence)
	{
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		if (nowMs - client.lastMoveCorrectionSentMs < kMoveCorrectionIntervalMs)
			return; // la position officielle est déjà en route ; la prochaine suivra
		MoveCorrectionMessage message{};
		message.clientId = client.clientId;
		message.moveEpoch = client.moveEpoch;
		message.inputSequence = inputSequence;
		message.positionX = client.positionMetersX;
		message.positionY = client.positionMetersY;
		message.positionZ = client.positionMetersZ;
		if (client.boatIndex >= 0)
		{
			// v1.0.125 — à bord : position officielle dans le repère du bateau.
			message.positionX = client.movement.x;
			message.positionY = client.movement.y;
			message.positionZ = client.movement.z;
		}
		message.yawRadians = client.yawRadians;
		if (SendDatagram(client.endpoint, EncodeMoveCorrection(message)))
			client.lastMoveCorrectionSentMs = nowMs;
	}

	void ServerApp::ResetClientMovement(ConnectedClient& client)
	{
		// v1.0.125 — toute position imposée (téléport, réapparition, débarquement) laisse à terre.
		client.boatIndex = -1;
		engine::movement::ResetMovement(client.movement, client.positionMetersX, client.positionMetersY,
			client.positionMetersZ);
		++client.moveEpoch;
	}

	void ServerApp::RefreshMaxMoveSpeed(ConnectedClient& client)
	{
		// ADM.1 — /speed, monture, gel : multiplicateur de MJ appliqué en dernier.
		const float gmMultiplier = client.gm.EffectiveSpeedMultiplier();
		client.movement.verticalFree = client.gm.flying && !client.gm.frozen;
		client.maxMoveSpeed = 13.0f * gmMultiplier; // défaut du client tant que les stats manquent
		if (!m_statsTables || client.factionId.empty() || client.classId.empty())
			return;
		const engine::server::gameplay::Sex sex =
			(client.gender == "female") ? engine::server::gameplay::Sex::Female
			                            : engine::server::gameplay::Sex::Male;
		auto d = engine::server::gameplay::ComputeStats(
			*m_statsTables, client.factionId, client.classId, sex, client.level);
		if (!d)
			return;
		ApplyEquipmentBonus(client, *d);
		ApplyAuraStatModifiers(client.auras, *d);
		const float fastest = (std::max)({ d->speedWalk, d->speedRun, d->speedSprint });
		if (std::isfinite(fastest) && fastest > 0.1f)
			client.maxMoveSpeed = fastest * gmMultiplier;
	}

	void ServerApp::HandleGoodbye(const Endpoint& endpoint, uint32_t clientId)
	{
		// Départ propre : on évince immédiatement le client (retrait grille + despawn au
		// prochain RefreshReplication + OnClientLogout + persistance), au lieu d'attendre
		// EvictIdleClients. Évite l'avatar « fantôme » d'un joueur déconnecté.
		const ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			// Endpoint inconnu (Goodbye en double, ou déjà évincé) : rien à faire.
			return;
		}
		// Défense : un Goodbye dont le clientId ne correspond pas à l'endpoint est ignoré
		// (paquet usurpé / obsolète). clientId==0 (client legacy) → on fait confiance à l'endpoint.
		if (clientId != 0u && client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] Goodbye ignoré : client_id mismatch (endpoint={}, attendu={}, reçu={})",
				UdpTransport::EndpointToString(endpoint), client->clientId, clientId);
			return;
		}
		const uint32_t resolvedClientId = client->clientId;
		LOG_INFO(Net, "[ServerApp] Goodbye reçu — éviction immédiate (client_id={}, endpoint={})",
			resolvedClientId, UdpTransport::EndpointToString(endpoint));
		DisconnectConnectedClient(resolvedClientId, "client_goodbye");
	}

	void ServerApp::DrainPendingHellos()
	{
		if (m_pendingHellos.empty() || m_admittedRegistry == nullptr)
			return;
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		// On copie les entrées candidates (admises ou expirées) puis on rebuild la liste
		// en gardant uniquement les non-admises non-expirées. Cas attendu : 0-2 entrées,
		// donc le coût est négligeable.
		std::vector<PendingHello> toReplay;
		std::vector<PendingHello> stillPending;
		toReplay.reserve(m_pendingHellos.size());
		stillPending.reserve(m_pendingHellos.size());
		for (const PendingHello& entry : m_pendingHellos)
		{
			if (nowMs - entry.arrivedAtMs > kPendingHelloTtlMs)
			{
				LOG_WARN(Net,
					"[ServerApp] Hello en attente expire (character_key={}, endpoint={}, age_ms={})",
					entry.characterKey,
					UdpTransport::EndpointToString(entry.endpoint),
					nowMs - entry.arrivedAtMs);
				continue; // drop expired
			}
			// FIX.2 — un Hello déjà refusé avec la clé admise n'est rejoué qu'une fois
			// cette clé remplacée (nouvelle admission du master) : sinon il échouerait
			// encore, à chaque tick, jusqu'à expirer.
			bool keyChanged = true;
			if (entry.rejectedWithKey.has_value())
			{
				const auto currentKey = m_admittedRegistry->AdmittedUdpSessionKey(entry.characterKey, nowMs);
				keyChanged = currentKey.has_value() && *currentKey != *entry.rejectedWithKey;
			}
			if (m_admittedRegistry->IsAdmitted(entry.characterKey, nowMs) && keyChanged)
			{
				toReplay.push_back(entry);
			}
			else
			{
				stillPending.push_back(entry);
			}
		}
		m_pendingHellos = std::move(stillPending);
		// Retraite hors-boucle pour eviter de re-entrer dans HandleHello qui pourrait
		// re-buffer l'entree si on echoue une 2e fois (cas pathologique : admit revoque
		// entre temps). Le rebuild de m_pendingHellos est deja figé avant les Replay.
		for (const PendingHello& entry : toReplay)
		{
			LOG_INFO(Net,
				"[ServerApp] Retraite Hello en attente (character_key={}, endpoint={}, age_ms={})",
				entry.characterKey,
				UdpTransport::EndpointToString(entry.endpoint),
				nowMs - entry.arrivedAtMs);
			// ETAPE3.1 — poignée de main chiffrée mise en attente : rouverte maintenant
			// que l'admission (et donc la clé) est arrivée.
			if (!entry.sealedHello.empty())
			{
				ProcessSecureHandshake(entry.endpoint, entry.sealedHello);
				continue;
			}
			HandleHello(entry.endpoint, entry.characterKey, false);
		}
	}

	bool ServerApp::SendDatagram(const Endpoint& endpoint, std::span<const std::byte> packet)
	{
		// ETAPE3.4 (chantier 4.4) — types qui ne doivent pas se perdre : canal fiable du
		// client (numéro, renvoi jusqu'à l'accusé, livraison dans l'ordre).
		engine::server::MessageKind kind{};
		if (engine::server::PeekMessageKind(packet, kind) && engine::network::reliable::IsReliableKind(kind))
		{
			if (ConnectedClient* client = FindClient(endpoint))
			{
				if (packet.size() > engine::network::reliable::ReliableChannel::kMaxMessageSize)
				{
					// Jamais hors canal : il doublerait les messages en attente de renvoi.
					LOG_ERROR(Net, "[ServerApp] Message fiable trop gros, abandonné (kind={}, size={}, max={}, client_id={})",
						static_cast<uint32_t>(kind), packet.size(),
						engine::network::reliable::ReliableChannel::kMaxMessageSize, client->clientId);
					return false;
				}
				std::vector<std::byte> envelope;
				if (!client->reliable.Queue(packet, NowMonotonicMs(), envelope))
				{
					LOG_WARN(Net, "[ServerApp] Canal fiable en échec, message abandonné (kind={}, client_id={})",
						static_cast<uint32_t>(kind), client->clientId);
					return false; // PumpReliableChannels coupe le client
				}
				(void)SendDatagramRaw(endpoint, envelope); // perdu : renvoyé par le canal
				return true;
			}
		}
		return SendDatagramRaw(endpoint, packet);
	}

	void ServerApp::HandleReliableEnvelope(const Datagram& datagram, engine::server::MessageKind kind)
	{
		ConnectedClient* client = FindClient(datagram.endpoint);
		if (client == nullptr)
			return; // pas de session : rien à accuser ni à livrer
		const std::span<const std::byte> bytes(datagram.bytes.data(), datagram.size);
		const uint64_t nowMs = NowMonotonicMs();
		if (kind == engine::server::MessageKind::ReliableAck)
		{
			if (!client->reliable.OnAck(bytes, nowMs))
				++m_udpRejectedDatagrams;
			return;
		}
		std::vector<std::vector<std::byte>> delivered;
		if (!client->reliable.OnReliable(bytes, nowMs, delivered))
		{
			++m_udpRejectedDatagrams;
			return;
		}
		// Livrés dans l'ordre, chacun traité comme un paquet reçu de ce point de
		// terminaison. Seuls les types fiables sont admis dans l'enveloppe (pas d'Input
		// ni de Hello qui contourneraient leurs propres règles). `client` n'est plus lu :
		// un message peut couper la session.
		const Endpoint endpoint = datagram.endpoint;
		for (const std::vector<std::byte>& inner : delivered)
		{
			engine::server::MessageKind innerKind{};
			if (inner.size() > sizeof(Datagram::bytes) || !engine::server::PeekMessageKind(inner, innerKind)
				|| !engine::network::reliable::IsReliableKind(innerKind))
			{
				++m_udpRejectedDatagrams;
				continue;
			}
			Datagram one{};
			one.endpoint = endpoint;
			std::copy(inner.begin(), inner.end(), one.bytes.begin());
			one.size = inner.size();
			DispatchGamePacket(one);
		}
	}

	void ServerApp::PumpReliableChannels()
	{
		const uint64_t nowMs = NowMonotonicMs();
		std::vector<uint32_t> failed;
		std::vector<std::vector<std::byte>> out;
		for (ConnectedClient& client : m_clients)
		{
			out.clear();
			client.reliable.Update(nowMs, out);
			for (const std::vector<std::byte>& datagram : out)
				(void)SendDatagramRaw(client.endpoint, datagram);
			if (client.reliable.Failed())
				failed.push_back(client.clientId);
		}
		for (const uint32_t clientId : failed)
		{
			LOG_WARN(Net, "[ServerApp] Canal fiable en échec (aucun accusé depuis {} ms ou file pleine) : client coupé (client_id={})",
				engine::network::reliable::ReliableChannel::kGiveUpMs, clientId);
			DisconnectConnectedClient(clientId, "reliable_channel_failed");
		}
	}

	bool ServerApp::SendDatagramRaw(const Endpoint& endpoint, std::span<const std::byte> packet)
	{
		std::vector<std::byte> sealed;
		switch (m_udpSessions.Seal(PackEndpointKey(endpoint), packet, sealed))
		{
		case UdpSessionTable::SealResult::Sealed:
			return m_transport.Send(endpoint, sealed);
		case UdpSessionTable::SealResult::Failed:
			LOG_WARN(Net, "[ServerApp] UDP: scellement impossible (endpoint={})", UdpTransport::EndpointToString(endpoint));
			return false;
		case UdpSessionTable::SealResult::NoSession:
			break;
		}
		// Pas de session chiffrée pour ce point de terminaison : rien ne part en clair en
		// mode « required ».
		if (m_udpEncryptionMode == UdpEncryptionMode::Required)
			return false;
		return m_transport.Send(endpoint, packet);
	}

	void ServerApp::ProcessSecureHandshake(const Endpoint& endpoint, std::span<const std::byte> datagram)
	{
		engine::network::UdpHandshakeHeader header{};
		if (!engine::network::PeekUdpHandshake(datagram, header))
		{
			++m_udpRejectedDatagrams;
			return;
		}
		if (m_admittedRegistry == nullptr)
		{
			// Shard sans registre d'admission (build Windows) : aucune clé connue.
			++m_udpRejectedDatagrams;
			LOG_WARN(Net, "[ServerApp] Hello chiffré ignoré : pas de registre d'admission (endpoint={})",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		if (!m_admittedRegistry->IsAdmitted(header.characterId, nowMs))
		{
			// Admission (et clé) pas encore reçues du master : même attente que le Hello
			// en clair (TA.3), datagramme scellé conservé pour être rouvert plus tard.
			// Rien n'est encore authentifié, donc la file est bornée : une entrée par
			// (personnage, sel), au plus kMaxSealedPendingPerCharacter par personnage et
			// kMaxPendingHellosPerAddress par adresse IP, kMaxPendingHellos au total. Une
			// entrée en attente n'est jamais évincée au profit d'une nouvelle. La file
			// n'est qu'un raccourci : le client répète son Hello tant qu'il n'a pas de
			// Welcome, et un Hello reçu après l'admission n'a pas besoin d'attendre.
			if (QueueSealedHello(endpoint, header, datagram, nowMs, std::nullopt) == SealedHelloQueueResult::Queued)
				LOG_INFO(Net, "[ServerApp] Hello chiffré mis en attente d'admission (character_key={}, endpoint={})",
					header.characterId, UdpTransport::EndpointToString(endpoint));
			return;
		}
		const auto key = m_admittedRegistry->AdmittedUdpSessionKey(header.characterId, nowMs);
		if (!key)
		{
			++m_udpRejectedDatagrams;
			LOG_WARN(Net, "[ServerApp] Hello chiffré refusé : aucune clé de session pour ce personnage "
				"(client ou master antérieur ?) (character_key={}, endpoint={})",
				header.characterId, UdpTransport::EndpointToString(endpoint));
			return;
		}

		// Ouverture (UdpSessionTable) + anti-rejeu du sel (AdmittedCharacterRegistry : les
		// sels vivent avec la clé, tant qu'elle peut ouvrir une poignée de main).
		std::vector<std::byte> plain;
		const uint64_t characterId = header.characterId;
		const engine::network::UdpSessionKey sessionKey = *key;
		const auto result = m_udpSessions.OpenHandshake(PackEndpointKey(endpoint), sessionKey, datagram, nowMs,
			[this, characterId, &sessionKey, nowMs](const engine::network::UdpSessionSalt& salt) {
				return m_admittedRegistry->ConsumeUdpSalt(characterId, sessionKey, salt, nowMs)
					== AdmittedCharacterRegistry::SaltUse::Fresh;
			},
			plain);
		if (result == UdpSessionTable::HandshakeResult::Duplicate)
		{
			// Même Hello renvoyé (le client le répète tant qu'il n'a pas de Welcome) : la
			// session est en place. Client déjà admis : on renvoie seulement le Welcome
			// (perdu ?). Sinon (Hello refusé plus tôt, lecture terminée…) : on rejoue le
			// Hello comme le faisait un Hello en clair répété.
			ConnectedClient* existing = FindClient(endpoint);
			if (existing != nullptr && existing->persistenceCharacterKey == characterId)
				(void)SendWelcome(*existing);
			else if (existing == nullptr)
				HandleHello(endpoint, characterId, true);
			return;
		}
		if (result != UdpSessionTable::HandshakeResult::Opened)
		{
			const char* reason = "authentification";
			if (result == UdpSessionTable::HandshakeResult::Replayed)
				reason = "rejeu ou sels épuisés";
			else if (result == UdpSessionTable::HandshakeResult::EndpointBusy)
				reason = "adresse liée à un autre personnage";
			// FIX.2 — authentification ratée alors que le personnage est admis : presque
			// toujours une reconnexion rapide (nouvel EnterWorld) dont la clé n'est pas
			// encore arrivée du master — le Hello est scellé avec la NOUVELLE clé, le shard
			// ne connaît que l'ancienne. Avant, on le jetait et le client attendait son
			// prochain renvoi (~2 s, incident du 28/10/2026). On le garde : il est rejoué
			// dès que la clé admise change (DrainPendingHellos), sinon il expire.
			if (result == UdpSessionTable::HandshakeResult::AuthFailed)
			{
				const SealedHelloQueueResult queued = QueueSealedHello(endpoint, header, datagram, nowMs, sessionKey);
				if (queued == SealedHelloQueueResult::Queued)
					LOG_INFO(Net, "[ServerApp] Hello chiffré mis en attente de la nouvelle clé (reconnexion ?) "
						"(character_key={}, endpoint={})", characterId, UdpTransport::EndpointToString(endpoint));
				if (queued != SealedHelloQueueResult::Full)
					return;
				// File pleine : QueueSealedHello a déjà compté le rejet.
			}
			else
			{
				++m_udpRejectedDatagrams;
			}
			LOG_WARN(Net, "[ServerApp] Hello chiffré refusé ({}) (character_key={}, endpoint={})",
				reason, characterId, UdpTransport::EndpointToString(endpoint));
			return;
		}
		HelloMessage hello{};
		if (!DecodeHello(plain, hello) || hello.clientNonce != header.characterId)
		{
			// Authentique mais incohérent (bug client) : on retire la session installée.
			m_udpSessions.Drop(PackEndpointKey(endpoint));
			++m_udpRejectedDatagrams;
			LOG_WARN(Net, "[ServerApp] Hello chiffré invalide (character_key={}, endpoint={})",
				header.characterId, UdpTransport::EndpointToString(endpoint));
			return;
		}
		LOG_INFO(Net, "[ServerApp] Session UDP chiffrée ouverte (character_key={}, endpoint={})",
			header.characterId, UdpTransport::EndpointToString(endpoint));
		HandleHello(endpoint, hello.clientNonce, true);
	}

	ServerApp::SealedHelloQueueResult ServerApp::QueueSealedHello(const Endpoint& endpoint, const engine::network::UdpHandshakeHeader& header,
		std::span<const std::byte> datagram, uint64_t nowMs,
		const std::optional<engine::network::UdpSessionKey>& rejectedWithKey)
	{
		// Rien n'est encore authentifié, donc la file est bornée : une entrée par
		// (personnage, sel), au plus kMaxSealedPendingPerCharacter par personnage et
		// kMaxPendingHellosPerAddress par adresse IP, kMaxPendingHellos au total. Une
		// entrée en attente n'est jamais évincée au profit d'une nouvelle. La file
		// n'est qu'un raccourci : le client répète son Hello tant qu'il n'a pas de
		// Welcome.
		size_t sameCharacter = 0;
		size_t sameAddress = 0;
		for (const PendingHello& entry : m_pendingHellos)
		{
			if (entry.endpoint.address == endpoint.address)
				++sameAddress;
			if (entry.characterKey != header.characterId || entry.sealedHello.empty())
				continue;
			engine::network::UdpHandshakeHeader pendingHeader{};
			if (engine::network::PeekUdpHandshake(entry.sealedHello, pendingHeader) && pendingHeader.salt == header.salt)
				return SealedHelloQueueResult::AlreadyQueued; // même Hello déjà en attente
			++sameCharacter;
		}
		if (sameCharacter >= kMaxSealedPendingPerCharacter || sameAddress >= kMaxPendingHellosPerAddress
			|| m_pendingHellos.size() >= kMaxPendingHellos)
		{
			++m_udpRejectedDatagrams;
			LOG_WARN(Net, "[ServerApp] Hello chiffré non mis en attente : file pleine (total={}, personnage={}, adresse={})",
				m_pendingHellos.size(), sameCharacter, sameAddress);
			return SealedHelloQueueResult::Full;
		}
		PendingHello pending{ endpoint, header.characterId, nowMs, {}, rejectedWithKey };
		pending.sealedHello.assign(datagram.begin(), datagram.end());
		m_pendingHellos.push_back(std::move(pending));
		return SealedHelloQueueResult::Queued;
	}

	void ServerApp::DropUdpSession(const Endpoint& endpoint)
	{
		m_udpSessions.Drop(PackEndpointKey(endpoint));
	}

	void ServerApp::PruneUdpSessions(uint64_t nowMs)
	{
		// En usage : un client connecté OU un Hello dont la lecture en base est en cours
		// (ETAPE2.7, peut dépasser le délai si la file de sauvegarde est chargée).
		std::unordered_set<uint64_t> loadingEndpoints;
		for (const auto& [characterKey, pending] : m_pendingCharacterLoads)
		{
			(void)characterKey;
			loadingEndpoints.insert(PackEndpointKey(pending.endpoint));
		}
		m_udpSessions.Prune(nowMs, kUdpClientIdleTimeoutMs, [this, &loadingEndpoints](uint64_t endpointKey) {
			return m_clientIndexByPackedEndpoint.find(endpointKey) != m_clientIndexByPackedEndpoint.end()
				|| loadingEndpoints.find(endpointKey) != loadingEndpoints.end();
		});
	}

	void ServerApp::EvictIdleUdpClients()
	{
		const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		if (m_clients.empty())
		{
			PruneUdpSessions(nowMs); // ETAPE3.1 — même sans client connecté
			return;
		}
		// Recolte les candidats puis appelle DisconnectConnectedClient hors-boucle (qui
		// modifie m_clients + reconstruit les index hash TG.2 ; iterer en mutation casserait).
		std::vector<uint32_t> toDisconnect;
		toDisconnect.reserve(m_clients.size());
		for (const ConnectedClient& client : m_clients)
		{
			if (client.lastUdpActivityAtMs == 0)
				continue; // jamais initialise (defensive)
			if (nowMs - client.lastUdpActivityAtMs >= kUdpClientIdleTimeoutMs)
			{
				LOG_INFO(Net,
					"[ServerApp] Evict idle UDP client (client_id={}, character_key={}, endpoint={}, idle_ms={})",
					client.clientId, client.persistenceCharacterKey,
					UdpTransport::EndpointToString(client.endpoint),
					nowMs - client.lastUdpActivityAtMs);
				toDisconnect.push_back(client.clientId);
			}
		}
		for (uint32_t clientId : toDisconnect)
		{
			DisconnectConnectedClient(clientId, "udp_idle_timeout");
		}
		// ETAPE3.1 — sessions chiffrées orphelines et sels expirés.
		PruneUdpSessions(nowMs);
	}
}
