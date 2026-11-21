// Engine — sous-étapes d'Engine::Update() liées au gameplay (session en jeu,
// entrées joueur, déplacement, caméra, locomotion).
//
// Code déplacé tel quel depuis Engine::Update() (aucun changement de
// comportement) : découpage d'une fonction de ~6 000 lignes en sous-fonctions
// nommées. La documentation de chaque fonction est dans Engine.h.

#include "src/client/app/Engine.h"
#include "src/client/world/cells/CellSeaFallback.h"
#include "src/client/app/EngineHelpers.h"

#include "src/shared/network/LunarPayloads.h"
#include "src/shared/world/BoatDeck.h"
#include "src/client/dialogue/DialogueConfigLoader.h"
#include "src/shared/movement/MovementRules.h" // ETAPE3.3 : recharge d'esquive commune avec le shard

#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "imgui.h"
#endif

namespace engine
{
	using namespace engine::helpers; // utilitaires internes partagés (EngineHelpers.h)

	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::UpdateInWorldSession(engine::RenderState& out)
	{
		if (m_audioEngine.GetCurrentZoneId() == 9999)
		{
			m_audioEngine.SetZone(0);
		}

		// Phase 3 — Première frame post-auth : consommer la EnterWorldCommand émise par
		// AuthScreenCharacterSelect ("Jouer") pour câbler la connexion gameplay UDP au shard
		// choisi par l'utilisateur. La commande est one-shot : ConsumePendingEnterWorldCommand
		// la remet à zéro après lecture, donc cette branche n'agit qu'une seule fois par session.
		const engine::client::AuthUiPresenter::EnterWorldCommand enterCmd
			= m_authUi.ConsumePendingEnterWorldCommand();
		if (enterCmd.applyRequested)
		{
			LOG_INFO(Core, "[EnterWorld] character_id={}, name='{}', shard_id={}, endpoint='{}'",
				enterCmd.characterId, enterCmd.characterName, enterCmd.shardId, enterCmd.shardEndpoint);

			// Niveau du perso (depuis CHARACTER_LIST) : sert a l'arbre de
			// competences (verrouillage paliers + affichage « Niveau joueur »).
			m_activeCharacterLevel = (enterCmd.level > 0u) ? enterCmd.level : 1u;

			// Coupe la musique des ecrans d'auth/menu (audio/music/menu_lune_noire.ogg, SND.1)
			// au moment d'entrer dans le monde : le joueur entend desormais l'ambiance
			// du shard (zone audio) et eventuellement de futures musiques in-game.
			m_audioEngine.StopMenuMusic();

			// Seconde carte (Les Enfers) : toute entrée en monde repart du continent ; le
			// shard annoncera (ZoneChange) la carte d'un personnage resté aux Enfers.
			m_uiModelBinding.ClearZone();
			if (m_cellWorld && m_activeCellMap != engine::world::cells::CellMap::Royaumes)
				(void)SwitchCellMap(engine::world::cells::CellMap::Royaumes);

			// Phase 3.5/3.6 — Téléportation de la caméra à la position de spawn.
			// Priorité 1 (Phase 3.6) : spawn par-personnage, lu depuis characters.spawn_*
			// via la payload CHARACTER_LIST puis posé dans EnterWorldCommand par
			// AuthScreenCharacterSelect. enterCmd.hasSpawn vaut false si tous les champs
			// spawn de la DB étaient à zéro (pré-migration, ou défaut DB tel quel).
			// Priorité 2 (Phase 3.5, fallback) : défaut config `client.world.default_spawn.*`.
			{
				float spawnX, spawnY, spawnZ, yawDeg, pitchDeg;
				if (enterCmd.hasSpawn)
				{
					spawnX   = enterCmd.spawnX;
					spawnY   = enterCmd.spawnY;
					spawnZ   = enterCmd.spawnZ;
					yawDeg   = enterCmd.spawnYawDeg;
					pitchDeg = enterCmd.spawnPitchDeg;
					LOG_INFO(Core, "[EnterWorld] using per-character spawn from DB");
				}
				else
				{
					spawnX   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.x", 0.0));
					spawnY   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.y", 100.0));
					spawnZ   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.z", 0.0));
					yawDeg   = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.yaw_deg", 0.0));
					pitchDeg = static_cast<float>(m_cfg.GetDouble("client.world.default_spawn.pitch_deg", -10.0));
					LOG_INFO(Core, "[EnterWorld] using config default spawn (no per-character data)");
					// Monde « Royaumes » : (0, 0) n'est pas une cellule générée — repli sur
					// le point d'arrivée du boot (le serveur corrige ensuite par ForcePosition).
					if (m_cellWorld && !m_cfg.Has("client.world.default_spawn.x"))
					{
						spawnX = static_cast<float>(m_cellBootGlobalX);
						spawnZ = static_cast<float>(m_cellBootGlobalZ);
					}
				}
				// Monde « Royaumes » : spawn GLOBAL (DB) → cellule chargée + repère de rendu.
				if (m_cellWorld)
				{
					EnterCellWorldAt(spawnX, spawnZ);
					spawnX = m_renderFrame.ToRenderX(spawnX);
					spawnZ = m_renderFrame.ToRenderZ(spawnZ);
				}
				constexpr float kDeg2Rad = 3.14159265f / 180.f;
				out.camera.position.x = spawnX;
				out.camera.position.y = spawnY;
				out.camera.position.z = spawnZ;
				out.camera.yaw = yawDeg * kDeg2Rad;
				out.camera.pitch = pitchDeg * kDeg2Rad;
				// Aligne la cible orbitale sur le spawn DB. Le controleur
				// repositionnera ensuite la camera derriere la cible (par defaut
				// 6 m d'orbite arriere) au prochain Update.
				m_orbitalCameraController.SetTargetPosition(out.camera.position);
				// Etape 6 : initialise la derniere position synchronisee au spawn
				// pour que la 1ere detection de mouvement soit correcte (sans cela
				// le perso etait "deplace" depuis (0,0,0) au tout 1er tick).
				m_lastSyncedPosition = out.camera.position;

				// Sous-projet B.1 (post-review fix) : repositionne le CharacterController
				// au spawn DB. Sans ca, le boot Init laisse le CC a (0, ground+0.9, 0)
				// et a la 1ere frame post-EnterWorld, cc.Update tourne AVANT le code de
				// teleport, donc cc.GetPosition() = (0,0,0) overwrite la position spawn
				// via SetTargetPosition(cc.GetPosition()). SavePositionAsync persiste
				// ensuite (0,0,0) en DB -> perte de la position spawn de l'utilisateur.
				// Anti-embedded : max(spawnY, GroundHeightAt+0.9) au cas ou la DB ait
				// une altitude trop basse pour la capsule (height/2 = 0.9 m).
				{
					const float groundY = m_terrainCollider.GroundHeightAt(spawnX, spawnZ);
					const float ccY = std::max(spawnY, groundY + 0.9f);
					m_characterController.Init(engine::math::Vec3{ spawnX, ccY, spawnZ });
					LOG_INFO(Core, "[EnterWorld] CharacterController repositioned to ({:.2f}, {:.2f}, {:.2f}) (groundY={:.2f}, halfHeight=0.9)",
						spawnX, ccY, spawnZ, groundY);
				}

				// #1 — genre du perso applique AVANT la selection du mesh/peau.
				// Source autoritative : enterCmd.gender (DB serveur via CHARACTER_LIST,
				// migration 0067). Repli : characters.<nom>.gender (fix client interim)
				// si le serveur ne fournit pas encore le genre (master pas redeploye).
				{
					std::string gender = enterCmd.gender;
					const char* src = "serveur";
					if (gender != "male" && gender != "female")
					{
						gender = m_cfg.GetString("characters." + enterCmd.characterName + ".gender", "");
						src = "client (repli)";
					}
					if (gender == "male" || gender == "female")
					{
						if (gender != m_avatarGender)
						{
							m_avatarGender = gender;
							m_avatarSkinDiagLoggedGender.clear();
						}
						LOG_INFO(Core, "[EnterWorld] genre perso '{}' = {} ({})",
							enterCmd.characterName, gender, src);
					}
					else
					{
						LOG_INFO(Core, "[EnterWorld] pas de genre (serveur/client) pour '{}' -> genre courant '{}'",
							enterCmd.characterName, m_avatarGender);
					}
				}
				// Teinte de peau du perso (DB serveur via CHARACTER_LIST, migration 0068).
				// 0 = claire (defaut), 1 = foncee. Applique le materiau de peau au draw.
				m_avatarSkinTone = (enterCmd.skinColorIdx == 1u) ? 1 : 0;
				LOG_INFO(Core, "[EnterWorld] teinte peau '{}' = {}",
					enterCmd.characterName, m_avatarSkinTone == 1 ? "foncee" : "claire");
				// Sous-projet C MVP — Resout le mesh de la race du perso depuis le
				// payload EnterWorld (race_str persistee en DB depuis migration 0033).
				// Fallback humains si la race n'est pas chargee dans m_raceMeshes.
				{
					const std::string& raceId = enterCmd.raceId;
					// Monde jouable : race mémorisée pour la carte (ville de départ en
					// évidence) ; la bannière se réarme (accueil dans la ville d'arrivée).
					m_localRaceId = raceId;
					m_localFactionId = enterCmd.factionId;
					m_placeBanner = engine::client::PlaceBannerTracker{};
					// SND.2/SND.3 — nouvelle entrée en jeu : pas et sons d'interface repartent de zéro.
					m_localCharacterNameForAudio = enterCmd.characterName;
					m_footstepCadence.Reset();
					m_uiAudioPrev = engine::audio::UiAudioSnapshot{};
					m_uiAudioWarmupFrames = 240;
					m_combatAudioPrev = engine::audio::CombatAudioSnapshot{}; // SND.5
					m_interactionAudioPrev = engine::audio::InteractionAudioSnapshot{}; // SND.6
					engine::render::skinned::SkinnedMesh* mesh = GetRaceMesh(raceId);
					if (mesh) {
						m_currentSkinnedMesh = mesh;
						LOG_INFO(Core, "[EnterWorld] Avatar mesh selected for race '{}' ({} bones, {} clips)",
							raceId, mesh->skeleton.bones.size(), mesh->clips.size());
					} else {
						m_skinnedAvatarReady = false;
						LOG_WARN(Core, "[EnterWorld] No mesh available for race '{}' (humains also absent) -- cube fallback",
							raceId);
					}
				}

				LOG_INFO(Core, "[EnterWorld] camera teleport ({}, {}, {}) yaw={}deg pitch={}deg",
					spawnX, spawnY, spawnZ, yawDeg, pitchDeg);
			}

			// Phase 3.5 — Bannière "Bienvenue, <perso>" affichée 5 s.
			{
				std::string personName = enterCmd.characterName.empty()
					? std::string("aventurier") : enterCmd.characterName;
				const std::string tpl = m_authUi.UiTranslate("auth.enter_world.welcome");
				if (!tpl.empty())
				{
					const std::string token{"{name}"};
					const size_t pos = tpl.find(token);
					m_enterWorldBannerText = (pos == std::string::npos)
						? tpl
						: tpl.substr(0, pos) + personName + tpl.substr(pos + token.size());
				}
				else
				{
					m_enterWorldBannerText = "Bienvenue, " + personName + " !";
				}
				m_enterWorldBannerExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			}

			// Phase 3.6.6 — Memorise l'identité du perso et arme la sauvegarde périodique.
			// La connexion master (m_authUi.m_masterClient) reste vivante grâce au fix
			// Phase 2/3 ; SavePositionAsync l'utilise en fire-and-forget.
			m_currentCharacterId = enterCmd.characterId;
			// Cellule de dialogue PNJ : journal local de conversation de quête pour ce
			// personnage + branchement du presenter (sink de journalisation + callback
			// d'action quête). Accept/turn-in partent au shard (SP2 Task 7, voir
			// callback ci-dessous).
			m_dialogueJournal = std::make_unique<engine::client::QuestConversationJournal>(m_cfg, m_currentCharacterId);
			m_dialogue.SetJournalSink(m_dialogueJournal.get());
			m_dialogue.SetQuestActionCallback(
				[this](engine::client::DialogueAction action, int questId, const std::string& questKey)
				{
					// SP2 Task 7 — accept/turn-in passent EXCLUSIVEMENT par le shard
					// (opcodes 93/94 ci-dessous, Tasks 4/5). Le système A maître (retiré,
					// Cleanup 2026-07-02) n'existe plus.
					(void)questId;
					// SP2 — wire quêtes UDP (shard) : le choix de dialogue porte la clé
					// texte de quête ; on envoie QuestAccept/TurnInRequest au shard avec
					// le PNJ dont le dialogue est actuellement ouvert (Task 4b —
					// m_currentDialogueNpcTargetId, mémorisé par OpenDialogue depuis
					// InteractableEntity::npcTargetId ; PAS UIModel.giverList.npcTargetId,
					// qui n'est mis à jour que par une réponse QuestGiverList et resterait
					// vide/périmé car OpenDialogue est purement local hors Talk explicite).
					// Ignoré si questKey vide (dialogue non relié au catalogue quêtes SP1/SP2).
					if (!questKey.empty() && m_gameplayNetInitialized)
					{
						const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
						const std::string& npcTargetId = m_currentDialogueNpcTargetId;
						if (action == engine::client::DialogueAction::AcceptQuest)
							(void)m_gameplayUdp.SendQuestAcceptRequest(gameplayClientId, questKey, npcTargetId);
						else if (action == engine::client::DialogueAction::CompleteQuest)
							(void)m_gameplayUdp.SendQuestTurnInRequest(gameplayClientId, questKey, npcTargetId);
					}
				});
			const int64_t intervalCfg = m_cfg.GetInt("client.save_position.interval_sec", 30);
			m_savePositionIntervalSec = std::chrono::seconds(std::max<int64_t>(5, intervalCfg));
			m_nextSavePositionTime = std::chrono::steady_clock::now() + m_savePositionIntervalSec;
			m_shutdownPositionSaved = false;
			// Reset defensif du chat focus : si l'utilisateur a tape Enter pendant
			// la saisie du login, ChatUiPresenter::Update aurait toggle m_chatFocus=true.
			// In-game ce flag bloque l'orbital camera Update (cf. line 3275).
			if (m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive())
			{
				m_chatUi.SetChatFocus(false);
				LOG_INFO(Core, "[EnterWorld] chat focus reset OFF (etait active depuis l'auth)");
			}
			LOG_INFO(Core, "[EnterWorld] periodic position save armed (character_id={}, interval={}s)",
				m_currentCharacterId, m_savePositionIntervalSec.count());

			// Phase 4 chat — Annonce le perso actif au master pour le sender display +
			// la résolution de cible /whisper. Fire-and-forget : la réponse arrive via
			// PumpPostAuthEvents et est juste loggée en debug (pas de blocage UI).
			// ETAPE3.1 — clé de session UDP neuve à chaque EnterWorld : elle part au master
			// (TLS) dans la requête, le master la relaie au shard (TLS) ; la session UDP de
			// jeu est ensuite chiffrée avec (cf. UdpSecureChannel.h). Pas d'EnterWorld
			// envoyé (pas de master) ou tirage aléatoire impossible : session en clair.
			m_gameplayUdp.ClearSessionKey();
			if (m_currentCharacterId != 0u && !enterCmd.characterName.empty())
			{
				engine::network::UdpSessionKey udpKey{};
				const bool keyOk = engine::network::GenerateUdpSessionKey(udpKey);
				if (!keyOk)
					LOG_ERROR(Core, "[EnterWorld] tirage de la clé de session UDP impossible : session de jeu en clair");
				const bool sent = m_authUi.SendEnterWorldAsync(m_currentCharacterId, enterCmd.characterName,
					keyOk ? &udpKey : nullptr);
				if (sent && keyOk)
					m_gameplayUdp.SetSessionKey(m_currentCharacterId, udpKey);
				// Phase 5 reconnect — mémorise l'identité pour pouvoir ré-envoyer
				// CHARACTER_ENTER_WORLD à la reconnexion sans repasser par tout le flow.
				m_authUi.RememberPostEnterWorldCharacter(m_currentCharacterId, enterCmd.characterName,
					(sent && keyOk) ? &udpKey : nullptr);
				// Copie locale effacée (la clé vit dans GameplayUdpClient et AuthUi).
				volatile uint8_t* wipe = udpKey.data();
				for (size_t i = 0; i < udpKey.size(); ++i)
					wipe[i] = 0u;
			}

			// Carte à découvrir : demandée APRÈS l'annonce du personnage (même
			// connexion TCP, traitée dans l'ordre par le master).
			StartMapDiscovery();

			// Phase 5 Lunar — fetch initial lunar state (master-authoritative).
			// Le master repondra via opcode 193 (LunarStateResponse) qui est
			// dispatche dans le push handler ci-dessus pour appeler
			// m_dayNight.OnLunarPhaseChange. Le push 194 arrive ensuite a
			// chaque changement de phase (~21h).
			{
				std::vector<uint8_t> lunarPayload;
				engine::network::lunar::BuildLunarStateRequestPayload(lunarPayload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeLunarStateRequest, lunarPayload);
			}

			// WorldClock sync — l'horloge est désormais reçue en piggyback de
			// la réponse liste des personnages (opcode 40) et stockée dans
			// l'AuthUi. On l'applique ici (main thread) au lieu d'envoyer une
			// requête 203 séparée. Repli : si aucune horloge piggyback (vieux
			// master / requête liste échouée), on garde l'ancien comportement
			// (requête 203) pour ne pas régresser le solo/compat.
			if (m_authUi.HasWorldClock())
			{
				const auto& wc = m_authUi.WorldClock();
				engine::world::WorldClockParams p;
				p.epochRefUnixMs         = wc.epochRefUnixMs;
				p.timeScaleRealMinPerDay = wc.timeScaleRealMinPerDay;
				p.offsetGameSec          = wc.offsetGameSec;
				p.paused                 = (wc.paused != 0u);
				p.pausedAtGameSec        = wc.pausedAtGameSec;
				p.lunarPeriodGameSec     = wc.lunarPeriodGameSec;
				m_dayNight.SetServerClock(p, wc.serverTimeUnixMs, m_authUi.WorldClockClientRecvMs());
				m_authUi.ClearWorldClock();
				LOG_INFO(Render, "[Engine] WorldClock applique depuis le piggyback liste perso");
			}
			else
			{
				std::vector<uint8_t> wcReq;
				engine::network::worldclock::BuildWorldClockStateRequestPayload(wcReq);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeWorldClockStateRequest, wcReq);
			}

			// Override runtime du host:port gameplay UDP par l'endpoint du shard accepté.
			// InitGameplayNet relit ces clés à l'appel (cf. ligne ~3552) ; les écraser avant
			// l'init est suffisant pour cibler le bon shard.
			if (!enterCmd.shardEndpoint.empty())
			{
				const size_t colon = enterCmd.shardEndpoint.rfind(':');
				if (colon != std::string::npos)
				{
					const std::string host = enterCmd.shardEndpoint.substr(0, colon);
					const int64_t port = std::strtoll(enterCmd.shardEndpoint.substr(colon + 1).c_str(), nullptr, 10);
					if (!host.empty() && port > 0 && port < 65536)
					{
						m_cfg.SetValue("client.gameplay_udp.host", host);
						m_cfg.SetValue("client.gameplay_udp.port", port);
						m_cfg.SetValue("client.gameplay_udp.enabled", true);
						// Phase 3.7 — Le shard utilise déjà clientNonce comme tentativeCharacterKey
						// (cf. ServerApp::HandleHello). On override la clé config pour propager le
						// character_id réel sélectionné par l'utilisateur.
						// Phase 3.7.5 — clientNonce est désormais uint64, plus de troncation des bits hauts.
						// La config porte un int64_t (signé) ; on reinterpret-cast bit-à-bit pour préserver
						// la valeur uint64 (negative-looking si bit 63 set, mais réinterprété en uint64
						// côté lecture).
						if (enterCmd.characterId != 0u)
						{
							m_cfg.SetValue("client.gameplay_udp.character_key",
								static_cast<int64_t>(enterCmd.characterId));
							LOG_INFO(Core, "[EnterWorld] propagating character_id={} as gameplay UDP character_key (uint64)",
								enterCmd.characterId);
						}
						else
						{
							// Correctif visibilité 1ère connexion — diagnostic : un characterId==0
							// ici signifie que le Hello UDP partirait avec la clé de config
							// résiduelle (souvent 1) → le shard associerait ce client au mauvais
							// personnage → invisibilité mutuelle (corrigé en amont côté
							// AuthUiPresenter, cf. CODEBASE_MAP §59). Ce log doit rester muet en
							// fonctionnement nominal ; s'il apparaît, le fallback d'id amont a
							// échoué et il faut investiguer le flux post-création.
							LOG_ERROR(Core, "[EnterWorld] character_id == 0 a l'entree en jeu : le character_key "
								"gameplay UDP n'est PAS surcharge (risque d'invisibilite mutuelle). "
								"Verifier la CHARACTER_LIST / le fallback de creation.");
						}
						// Si la session UDP a été ouverte au boot avec un host différent
						// (config par défaut), on la coupe avant de la rouvrir sur le bon shard.
						if (m_gameplayNetInitialized)
						{
							ShutdownGameplayNet();
						}
						InitGameplayNet();
					}
					else
					{
						LOG_WARN(Core, "[EnterWorld] endpoint invalide host='{}' port={} : connexion gameplay non démarrée",
							host, port);
					}
				}
				else
				{
					LOG_WARN(Core, "[EnterWorld] endpoint sans ':' ('{}') : connexion gameplay non démarrée",
						enterCmd.shardEndpoint);
				}
			}
			else
			{
				LOG_WARN(Core, "[EnterWorld] endpoint vide : connexion gameplay non démarrée (la scène 3D s'affichera quand même mais sans réseau)");
			}
		}

		// Phase 3.6.6 — Drain des événements de la connexion master encore vivante post-auth.
		// AuthUiPresenter conserve m_masterClient + m_masterSessionId après EnterWorld grâce
		// au fix Phase 2/3 (suppression des ResetMasterSession() avant MasterFlow). Ici on
		// pompe pour récupérer les réponses CHARACTER_SAVE_POSITION_RESPONSE (loggées en debug)
		// et détecter une déconnexion master inattendue (auquel cas SavePositionAsync échouera
		// proprement aux prochains ticks).
		m_authUi.PumpPostAuthEvents();

		// Phase 5 reconnect — Si une déconnexion master a été détectée par PumpPostAuthEvents,
		// AuthUi est passé en mode reconnect. TickReconnect lance la tentative auto au bon moment.
		m_authUi.TickReconnect(m_cfg);
		// ETAPE3.1 — la reconnexion a envoyé une clé de session UDP neuve au master : elle
		// servira aux prochaines sockets UDP (la session en cours garde sa clé de travail).
		{
			std::array<uint8_t, 32> refreshedKey{};
			if (m_authUi.ConsumeRefreshedUdpKey(refreshedKey))
			{
				m_gameplayUdp.SetSessionKey(m_currentCharacterId, refreshedKey);
				volatile uint8_t* wipe = refreshedKey.data();
				for (size_t i = 0; i < refreshedKey.size(); ++i)
					wipe[i] = 0u;
			}
		}

		// Phase 3.6.6 — Tick périodique de sauvegarde de position. Démarré à la consommation
		// de EnterWorldCommand (m_currentCharacterId != 0). Intervalle borné à >= 5 s côté
		// AuthUiPresenter::SavePositionAsync via la config `client.save_position.interval_sec`.
		//
		// Etape 6 vue 3eme personne : ajout d'une heuristique mouvement -> save plus
		// frequente. Quand le perso a bouge de plus de 0.5 m depuis la derniere
		// synchro, on declenche immediatement (rate-limite a 1.0 s entre 2 saves).
		// Si statique, on revient a l'intervalle long (m_savePositionIntervalSec).
		// v1.0.125 — à bord, pas de sauvegarde de position (le shard sauvegarde le
		// passager au ponton de l'escale ; jamais au milieu de la mer).
		if (m_currentCharacterId != 0u && m_aboardBoatEntityId == 0u)
		{
			const engine::math::Vec3& playerPos = m_orbitalCameraController.GetTargetPosition();
			const float dx = playerPos.x - m_lastSyncedPosition.x;
			const float dy = playerPos.y - m_lastSyncedPosition.y;
			const float dz = playerPos.z - m_lastSyncedPosition.z;
			const float dist2 = dx * dx + dy * dy + dz * dz;
			constexpr float kMoveThresholdM   = 0.5f;
			constexpr float kMoveThresholdSqr = kMoveThresholdM * kMoveThresholdM;
			const auto now = std::chrono::steady_clock::now();
			const bool intervalElapsed = now >= m_nextSavePositionTime;
			const bool movedSignificantly = dist2 >= kMoveThresholdSqr;
			if (intervalElapsed || movedSignificantly)
			{
				constexpr float kRad2Deg = 180.f / 3.14159265f;
				const float yawDeg   = out.camera.yaw   * kRad2Deg;
				const float pitchDeg = out.camera.pitch * kRad2Deg;
				// Monde « Royaumes » : le master stocke des positions GLOBALES.
				if (m_authUi.SavePositionAsync(m_currentCharacterId,
					m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalX(playerPos.x)) : playerPos.x, playerPos.y,
					m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalZ(playerPos.z)) : playerPos.z,
					yawDeg, pitchDeg))
				{
					LOG_DEBUG(Core, "[SavePosition] sync sent (character_id={}, pos=({:.1f},{:.1f},{:.1f}), reason={})",
						m_currentCharacterId, playerPos.x, playerPos.y, playerPos.z,
						movedSignificantly ? "moved" : "tick");
				}
				m_lastSyncedPosition = playerPos;
				// Si on est ici parce qu'on a bouge (et pas a cause de l'intervalle
				// long), on rate-limite la prochaine sync a 1.0 s minimum pour ne
				// pas spammer le serveur tant que le joueur enchaine les pas.
				// Sinon on retombe sur l'intervalle long (config-driven).
				const auto nextDelay = movedSignificantly ? std::chrono::milliseconds(1000)
				                                          : std::chrono::duration_cast<std::chrono::milliseconds>(m_savePositionIntervalSec);
				m_nextSavePositionTime = now + nextDelay;
			}
		}

		// Phase 5 reconnect — Si une tentative de reconnexion master est en cours,
		// la bannière de statut prend la priorité (elle remplace même la bannière welcome).
		// Le texte est court et localisé côté AuthUi (Tr("auth.info.reconnect_in_progress")).
		// Phase 3.5 — Affichage de la bannière "Bienvenue" tant qu'elle n'a pas expiré.
		// Phase 3.11 — Quand la bannière a expiré, on affiche le panneau chat à la place
		// (c'est la première surface visuelle pour le système de chat post-auth).
		// Priorité : reconnect > banner > chat > vide.
		if (m_authUi.IsReconnecting() && !m_authUi.ReconnectStatusText().empty())
		{
			m_window.SetOverlayText(m_authUi.ReconnectStatusText());
		}
		else if (!m_enterWorldBannerText.empty()
			&& std::chrono::steady_clock::now() < m_enterWorldBannerExpiry)
		{
			m_window.SetOverlayText(m_enterWorldBannerText);
		}
		else
		{
			if (!m_enterWorldBannerText.empty())
			{
				// Premier frame d'expiration : on libère explicitement le texte de la bannière.
				m_enterWorldBannerText.clear();
			}
			// Phase 3.11.1 — Si le panneau ImGui chat est actif (Windows + render.chat_imgui.enabled),
			// on n'écrit PAS le texte overlay Win32 pour éviter le double affichage.
			// Sinon (Linux build, ou flag désactivé), on retombe sur l'overlay texte legacy.
			bool chatImguiOwnsDisplay = false;
#if defined(_WIN32)
			chatImguiOwnsDisplay = m_chatImGui && m_chatUi.IsInitialized()
				&& m_cfg.GetBool("render.chat_imgui.enabled", true);
#endif
			if (chatImguiOwnsDisplay)
			{
				m_window.SetOverlayText({});
			}
			else if (m_chatUi.IsInitialized())
			{
				std::string chatHud = m_chatUi.BuildHudPanelText();
				if (!chatHud.empty())
				{
					m_window.SetOverlayText(chatHud);
				}
				else
				{
					m_window.SetOverlayText({});
				}
			}
			else
			{
				m_window.SetOverlayText({});
			}
		}
	}

	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::UpdateGameplay(engine::RenderState& out, double dt, float mouseSensitivity, bool invertY, engine::render::MovementLayout movementLayout, bool authGateActive)
	{
		if (!authGateActive && !m_chatUi.IsChatFocusActive() && !m_inGameOptionsPanelVisible
				// Menu Pause ouvert => gèle tout l'input gameplay (déplacement,
				// caméra, attaque, sorts, interaction), comme le panneau Options :
				// le joueur ne doit ni bouger ni attaquer tant que la pause est active.
				&& !m_inGamePauseMenuVisible)
		{
			// Touches d'action remappables (controls.keybind.*), resolues chaque
			// frame depuis la config pour refleter immediatement un rebind fait
			// dans le panneau Options. Defauts : sprint=Alt, crouch=Ctrl, sort=R.
			const engine::platform::Key sprintKey =
				KeyFromName(m_cfg.GetString("controls.keybind.sprint", "Alt"), engine::platform::Key::Alt);
			const engine::platform::Key crouchKey =
				KeyFromName(m_cfg.GetString("controls.keybind.crouch", "Ctrl"), engine::platform::Key::Control);
			const engine::platform::Key castKey =
				KeyFromName(m_cfg.GetString("controls.keybind.cast", "R"), engine::platform::Key::R);
			const engine::platform::Key interactKey =
				KeyFromName(m_cfg.GetString("controls.keybind.interact", "E"), engine::platform::Key::E);
			const engine::platform::Key punchKey =
				KeyFromName(m_cfg.GetString("controls.keybind.punch", "X"), engine::platform::Key::X);
			// Vue 3eme personne : controleur orbital pur (camera derriere la
			// cible). Souris libre par defaut ; clic droit maintenu = rotate
			// camera autour de la cible (yaw/pitch) ; molette = zoom.
			//
			// B.1 / Task 8 : OrbitalCameraController est devenu camera pure.
			// B.1 / Task 9 : on tick d'abord le CharacterController (physics +
			// collision sol) PUIS la camera suit la position resultante via
			// `SetTargetPosition`. Ordre important : si la camera tournait
			// avant le CC, son repere (Forward/Right XZ) servirait a projeter
			// l'input du frame courant mais sa position cible utiliserait la
			// position de la frame precedente -> 1-frame lag visible.
			auto moveInput = BuildMoveInput(m_input, m_orbitalCameraController, movementLayout, sprintKey, crouchKey);
			// Verrou de déplacement : on neutralise toutes les entrées de
			// déplacement (direction + saut) pour garder l'avatar immobile, dans
			// deux cas — (1) dialogue PNJ actif (cellule dédiée) ; (2) geste
			// verrouillant en cours (ex. ouverture de coffre via
			// m_avatarMoveLockUntilSec). La caméra reste libre dans les deux cas
			// (m_orbitalCameraController non touché). nowSec est recalculé ici (le
			// nowSec de la state machine n'est pas encore en portée à ce point) ;
			// l'écart sub-ms avec la garde roulade plus bas est sans effet visible.
			const bool moveLocked = EngineNowSec() < m_avatarMoveLockUntilSec;
			// Combat SP2 — un joueur mort ne bouge plus (overlay « Vous êtes
			// mort » affiché ; le respawn rend la main). kEntityStateDead = bit 0.
			const bool localPlayerDead = m_gameplayNetInitialized
				&& (m_uiModelBinding.GetModel().playerStats.stateFlags & 1u) != 0u;
			// ADM.1 — pouvoirs de maître de jeu (GmStateUpdate) : gel (aucun
			// déplacement), vol libre, multiplicateur de vitesse (/speed).
			const engine::client::UIGmState& gmState = m_uiModelBinding.GetModel().gmState;
			if (m_dialogueActive || moveLocked || localPlayerDead || gmState.Frozen())
				moveInput = engine::gameplay::MoveInput{};
			moveInput.flyPressed = gmState.Flying() && !localPlayerDead;
			m_characterController.SetGmFlight(gmState.Flying());
			m_characterController.SetSpeedMultiplier(gmState.Frozen() ? 0.0f : gmState.speedMultiplier);
			// Roadmap-2 (2026-07-19) — vitesses AUTORITAIRES du serveur
			// (PlayerStats kind 79 : classe + équipement + buffs d'auras).
			// 0 = pas encore reçu → la config locale reste en vigueur.
			{
				const auto& ps79 = m_uiModelBinding.GetModel().playerStats;
				if (ps79.speedRun > 0.1f)
					m_characterController.SetMoveSpeeds(ps79.speedWalk, ps79.speedRun, ps79.speedSprint);
			}
#if !defined(_WIN32)
			// Build Linux (fumée, sans clavier) : LCDLLN_DEBUG_AUTOWALK="dx,dz" fait
			// marcher le personnage dans cette direction (tests de rendu / streaming
			// sous Xvfb). Sans effet si la variable est absente.
			if (const char* autowalk = std::getenv("LCDLLN_DEBUG_AUTOWALK"))
			{
				float awx = 0.0f, awz = 0.0f;
				if (std::sscanf(autowalk, "%f,%f", &awx, &awz) == 2)
				{
					const float len = std::sqrt(awx * awx + awz * awz);
					if (len > 1e-3f)
					{
						moveInput.moveDirXZ = engine::math::Vec3{ awx / len, 0.0f, awz / len };
						moveInput.sprint = true;
					}
				}
			}
#endif
			// Collisionneur composite : terrain (sol + eau) + cylindres des props/décor.
			// Monde « Royaumes » : + collisionneurs des cellules résidentes (décalés).
			const engine::math::Vec3 ccPrevPos = m_characterController.GetPosition();
			// v1.0.125 — à bord d'un bateau : marche sur le pont dans le repère du bateau
			// (direction de la caméra ramenée dans ce repère, vitesse de course ou de
			// sprint, pas de saut), puis le personnage suit la pose lissée du bateau.
			const bool aboard = m_aboardBoatEntityId != 0u;
			if (aboard)
			{
				float bx = m_aboardLastX, by = m_aboardLastY, bz = m_aboardLastZ, byaw = m_aboardLastYaw;
				bool bdocked = m_aboardLastDocked;
				if (BoatRenderPose(m_aboardBoatEntityId, bx, by, bz, byaw, bdocked))
				{
					m_aboardLastX = bx; m_aboardLastY = by; m_aboardLastZ = bz; m_aboardLastYaw = byaw;
					m_aboardLastDocked = bdocked;
				}
				const auto& ps = m_uiModelBinding.GetModel().playerStats;
				const float runSpeed = ps.speedRun > 0.1f ? ps.speedRun : 6.0f;
				const float sprintSpeed = ps.speedSprint > 0.1f ? ps.speedSprint : 9.0f;
				const float walkSpeed = ps.speedWalk > 0.1f ? ps.speedWalk : 3.0f;
				const float speed = (moveInput.crouch ? walkSpeed * 0.5f : (moveInput.sprint ? sprintSpeed : (moveInput.run ? runSpeed : walkSpeed)))
					* (gmState.Frozen() ? 0.0f : gmState.speedMultiplier);
				double ldx = 0.0, ldz = 0.0;
				engine::world::boat::WorldToLocal(0.0, 0.0, byaw, moveInput.moveDirXZ.x, moveInput.moveDirXZ.z, ldx, ldz);
				float lx = m_aboardLocal.x + static_cast<float>(ldx) * speed * static_cast<float>(dt);
				float lz = m_aboardLocal.z + static_cast<float>(ldz) * speed * static_cast<float>(dt);
				(void)engine::world::boat::ClampToDeck(lx, lz);
				m_aboardLocal = engine::math::Vec3{ lx, engine::world::boat::kDeckY + engine::world::boat::kStandHeight, lz };
				double wx = 0.0, wz = 0.0;
				engine::world::boat::LocalToWorld(bx, bz, byaw, lx, lz, wx, wz);
				m_characterController.PlaceOnPlatform(engine::math::Vec3{ static_cast<float>(wx), by + m_aboardLocal.y, static_cast<float>(wz) });
				moveInput.jumpPressed = false;
			}
			else
			{
				PROFILE_SCOPE("CharacterController");
				if (m_cellWorld)
					m_characterController.Update(static_cast<float>(dt), moveInput, m_cellCollider);
				else
					m_characterController.Update(static_cast<float>(dt), moveInput, m_worldCollider);
			}
			UpdateFootstepAudio(static_cast<float>(dt)); // SND.2 — pas du personnage
			// ETAPE3.3 — horloge de simulation du mouvement (annoncée au shard avec
			// chaque commande, comparée à son horloge : budget de temps).
			{
				m_moveClientTimeMs += dt * 1000.0;
				if (moveInput.jumpPressed)
					m_moveLastJumpMs = (std::max)(1u, static_cast<uint32_t>(m_moveClientTimeMs));
			}
			if (m_cellWorld)
			{
				// Mur invisible : on ne sort pas des cellules générées (le shard
				// refuse aussi la position et recale le client). Lot D (bug 3) : ni
				// vers une cellule générée dont le chargement a échoué (sans
				// heightmap, le sol y serait un plan à Y = 0 sous un vide).
				const engine::math::Vec3 moved = m_characterController.GetPosition();
				const double fromGx = m_renderFrame.ToGlobalX(ccPrevPos.x), fromGz = m_renderFrame.ToGlobalZ(ccPrevPos.z);
				const double toGx = m_renderFrame.ToGlobalX(moved.x), toGz = m_renderFrame.ToGlobalZ(moved.z);
				if (!aboard && (!engine::world::cells::IsMoveAllowed(m_generatedCells, fromGx, fromGz, toGx, toGz)
						|| !engine::world::cells::IsMoveAllowedAvoiding(
							[this](engine::world::cells::CellCoord c) {
								return m_generatedCells.Contains(c)
									&& m_failedCells.count(engine::world::cells::CellZoneId(c, m_activeCellMap)) != 0;
							},
							fromGx, fromGz, toGx, toGz)))
					m_characterController.BlockHorizontalAt(ccPrevPos.x, ccPrevPos.z);
				// Streaming des cellules autour du joueur (rebase éventuel inclus).
				{
					PROFILE_SCOPE("CellStreaming");
					UpdateCellStreaming(/*synchronous*/ false);
				}
				// Carte à découvrir : cases révélées autour du joueur.
				UpdateMapDiscovery();
			}
			const engine::math::Vec3 ccPos = m_characterController.GetPosition();
			m_orbitalCameraController.SetTargetPosition(ccPos);

			// Yaw avatar : convention "back-step" simple. On declenche le
			// mode WalkBack (mesh inchange + anim Walking Backwards)
			// UNIQUEMENT si la touche back (S) est enfoncee seule, sans
			// aucune autre direction. Tout autre input (forward, strafe,
			// diagonale, S+autre direction) repasse en free-mover : le
			// mesh pivote pour aligner sa face sur la direction de
			// mouvement, et la state machine joue Walk/Run.
			//
			// L'ancienne heuristique basee sur dot(moveDir, mesh_forward)
			// donnait Q/D = strafe pur (no pivot) quand le perso etait
			// deja aligne (apres avoir avance avec W). Resultat : le
			// mesh ne pivotait plus en Q/D et l'utilisateur ne voyait
			// aucun mouvement visible (la camera suit, mesh reste centre).
			// Le simple "pure S" est plus previsible et conforme a la
			// convention MMO classique (Q/D = strafe avec pivot, S = back).
			const bool movingBack = IsPureBackInput(m_input);
			const bool hasMove = (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f);
			if (hasMove && !movingBack)
			{
				// Free-mover : aligne le mesh sur la direction de marche.
				m_avatarYaw = std::atan2(moveInput.moveDirXZ.x, moveInput.moveDirXZ.z);
			}
			// Si movingBack : on garde m_avatarYaw, le mesh reste dos cam
			// et la state machine bascule en WalkBack ci-dessous.

			// Memorise l'input pour la state machine de locomotion (Task 11
			// l'etendra a 7 etats — Idle / Walk / Run / Jump / Fall / Land /
			// Crouch — et lira `m_lastMoveInput` pour decider).
			m_lastMoveInput = moveInput;

			// --- B.1 / Task 11 : state machine 7 etats + crossfade ---
			//
			// Driven par CharacterController.IsGrounded + moveInput. Remplace la
			// state machine A (qui vivait dans le lambda Geometry, basee sur le
			// delta de la model matrix entre frames). On dispose ici de signaux
			// gameplay propres : `grounded` (sol vs air), `moving` (intention
			// d'input non-nulle), `jumpPressed` (edge sur Space), `run` (Shift).
			//
			// Transitions a portee de main :
			//   - Idle <-> Walk/Run via StartWalking (transition lift-off one-shot).
			//   - Jump -> Fall apres 40% du clip Jump (= takeoff termine).
			//   - Fall -> Land au moment ou le CC retouche le sol.
			//   - Land -> Idle/Walk/Run en fonction de l'input apres fin de Land.
			//
			// Trigger crossfade : a chaque transition, on appelle
			// `m_avatarCrossfade.Play(clip, loops, nowSec)`. Le clip vit dans
			// `m_currentSkinnedMesh->clips` (storage stable -> pointeur safe
			// jusqu'au shutdown). Le sampling pleine pose se fait dans le
			// lambda Geometry via `m_avatarCrossfade.Sample(skel, nowSec)`.
			if (m_skinnedAvatarReady && m_currentSkinnedMesh)
			{
				const bool grounded = m_characterController.IsGrounded();
				const bool moving = (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f);
				// Anti-flottement (retour de test 2026-07-20) : au ras d'un
				// bord de collision, IsGrounded oscille frame à frame et la
				// SM battait Land->Fall->Land en boucle (spam de log + pop
				// d'animation). On mémorise DEPUIS QUAND le sol est perdu ;
				// la transition vers Fall attend une tolérance « coyote ».
				if (grounded)
					m_avatarUngroundedSinceSec = -1.0f;

				const auto now = std::chrono::steady_clock::now();
				// nowSec via EngineNowSec : meme reference de temps que les sites de
				// Sample/Play, evite la perte de precision float 32 quand on utilise
				// time_since_epoch (~10^9 secondes depuis boot machine).
				const float nowSec = EngineNowSec();
				const float stateElapsed = std::chrono::duration<float>(now - m_avatarLocoStateEnterTime).count();

				const engine::render::skinned::AnimationClip* startWalkClip =
					m_currentSkinnedMesh->FindClip("StartWalking");
				const engine::render::skinned::AnimationClip* jumpClip =
					m_currentSkinnedMesh->FindClip("Jump");
				const engine::render::skinned::AnimationClip* landClip =
					m_currentSkinnedMesh->FindClip("Land");
				const engine::render::skinned::AnimationClip* rollClip =
					m_currentSkinnedMesh->FindClip("Roll");
				const engine::render::skinned::AnimationClip* attackClip =
					m_currentSkinnedMesh->FindClip("Attack");
				const engine::render::skinned::AnimationClip* castClip =
					m_currentSkinnedMesh->FindClip("Cast");
				const engine::render::skinned::AnimationClip* castShootClip =
					m_currentSkinnedMesh->FindClip("CastShoot");
				const engine::render::skinned::AnimationClip* castExitClip =
					m_currentSkinnedMesh->FindClip("CastExit");
				// Clip dynamique : "Interact" (geste générique) ou "PickUp_Table"
				// (près d'un coffre). m_currentInteractRole est fixé au déclenchement.
				const engine::render::skinned::AnimationClip* interactClip =
					m_currentSkinnedMesh->FindClip(m_currentInteractRole.c_str());
				const engine::render::skinned::AnimationClip* punchClip =
					m_currentSkinnedMesh->FindClip(m_currentPunchRole.c_str());

				// Attaque melee : clic gauche (edge). Le bloc gameplay est deja garde
				// contre le focus chat / l'auth (cf. ligne ~6969) ; on exclut en plus
				// le drag inventaire pour ne pas frapper en relachant un objet.
				// Touche d'attaque ALTERNATIVE (controls.keybind.attack, vide = clic gauche
				// seul). Permet de remapper l'attaque sur une touche, en plus du clic gauche.
				// Round-trip KeyName(KeyFromName(...)) pour ignorer une valeur invalide.
				const std::string attackKeyName = m_cfg.GetString("controls.keybind.attack", "");
				const engine::platform::Key attackKey = KeyFromName(attackKeyName, engine::platform::Key::Escape);
				const bool attackKeyBound = !attackKeyName.empty() && std::string(KeyName(attackKey)) == attackKeyName;
				// Pendant un dialogue PNJ : aucune attaque. Le clic gauche sert à
				// sélectionner une réponse (sinon le perso frappe en même temps).
				const bool attackPressed = !m_dialogueActive &&
					((m_input.WasMousePressed(engine::platform::MouseButton::Left) && !m_invUi.IsDragging())
					|| (attackKeyBound && m_input.WasPressed(attackKey)));

				// Sort : touche R (edge). Meme bloc gameplay garde contre le focus
				// chat / l'auth (cf. ligne ~6961). Geste cosmetique one-shot (pas de
				// cible ni d'aller-retour serveur), pendant clavier de l'attaque souris.
				const bool castPressed =
					m_input.WasPressed(castKey) && !m_dialogueActive;

				// Interagir : touche remappable (controls.keybind.interact, def. E),
				// edge. Action non-combat (la touche E reservee au §32 trouve ici son
				// usage). Geste cosmetique one-shot ; cible/objet a brancher plus tard.
				const bool interactPressed =
					m_input.WasPressed(interactKey);

				// Coup de poing : 2e attaque melee, touche remappable (controls.keybind.punch, def. X).
				const bool punchPressed =
					m_input.WasPressed(punchKey) && !m_dialogueActive;

				// Esquive/roulade : double-appui (fenetre 0.30s) sur la touche Crouch
				// (remappable). Touche maintenue = crouch ; deux appuis = Roll (one-shot).
				bool dodgePressed = false;
				if (m_input.WasPressed(crouchKey))
				{
					if (nowSec - m_lastCtrlTapSec <= 0.30f)
						dodgePressed = true;
					m_lastCtrlTapSec = nowSec;
				}
				// Emote demandee par slash command, consommee ici (vide = aucune).
				const std::string emoteRole = m_pendingEmoteRole;
				m_pendingEmoteRole.clear();

				AvatarLocomotionState newState = m_avatarLocoState;
				if (grounded)
				{
					switch (m_avatarLocoState)
					{
						case AvatarLocomotionState::Idle:
							if (moveInput.jumpPressed)        newState = AvatarLocomotionState::Jump;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (moving)                  newState = AvatarLocomotionState::StartWalking;
							break;
						case AvatarLocomotionState::StartWalking:
							if (!moving)                      newState = AvatarLocomotionState::Idle;
							else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (startWalkClip && stateElapsed >= startWalkClip->duration)
								newState = (moveInput.sprint ? AvatarLocomotionState::Sprint
								            : moveInput.run ? AvatarLocomotionState::Run
								            : AvatarLocomotionState::Walk);
							break;
						case AvatarLocomotionState::Walk:
							if (!moving)                      newState = AvatarLocomotionState::Idle;
							else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
							else if (moveInput.run)           newState = AvatarLocomotionState::Run;
							break;
						case AvatarLocomotionState::WalkBack:
							if (!moving)                      newState = AvatarLocomotionState::Idle;
							else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
							else if (!movingBack)             newState = AvatarLocomotionState::Walk;
							break;
						case AvatarLocomotionState::Run:
							if (!moving)                      newState = AvatarLocomotionState::Idle;
							else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
							else if (!moveInput.run)          newState = AvatarLocomotionState::Walk;
							break;
						case AvatarLocomotionState::Sprint:
							if (!moving)                      newState = AvatarLocomotionState::Idle;
							else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (!moveInput.sprint)
								newState = (moveInput.run ? AvatarLocomotionState::Run : AvatarLocomotionState::Walk);
							break;
						case AvatarLocomotionState::Jump:
							// Takeoff = first 40% of Jump clip ; then Fall (until grounded).
							if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)
								newState = AvatarLocomotionState::Fall;
							break;
						case AvatarLocomotionState::Fall:
							// Touched ground -> Land.
							newState = AvatarLocomotionState::Land;
							break;
						case AvatarLocomotionState::Land:
							if (landClip && stateElapsed >= landClip->duration)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						case AvatarLocomotionState::CrouchIdle:
						case AvatarLocomotionState::CrouchWalk:
							// Base de sortie vers la station debout ; l'override crouch ci-dessous
							// re-force le crouch tant que Ctrl est tenu.
							if (moveInput.jumpPressed)        newState = AvatarLocomotionState::Jump;
							else if (!moving)                 newState = AvatarLocomotionState::Idle;
							else if (movingBack)              newState = AvatarLocomotionState::WalkBack;
							else if (moveInput.sprint)        newState = AvatarLocomotionState::Sprint;
							else if (moveInput.run)           newState = AvatarLocomotionState::Run;
							else                              newState = AvatarLocomotionState::Walk;
							break;
						case AvatarLocomotionState::Roll:
							// One-shot : retour locomotion debout quand le clip Roll est fini.
							if (!rollClip || stateElapsed >= rollClip->duration)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						case AvatarLocomotionState::Emote:
							// Emote en boucle : interrompue par tout mouvement / saut.
							if (moving || movingBack || moveInput.jumpPressed)
							{
								if (moveInput.jumpPressed)    newState = AvatarLocomotionState::Jump;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						case AvatarLocomotionState::Attack:
							// One-shot : retour locomotion quand le clip d'attaque est fini.
							// Le deplacement reste pilote par le CharacterController pendant
							// l'attaque (geste plein corps, pas de root motion).
							if (!attackClip || stateElapsed >= attackClip->duration)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						case AvatarLocomotionState::Cast:
						{
							// Sequence : Enter (clip "Cast") -> Shoot -> Exit, les 2 derniers rejoues
							// via m_avatarPendingClipRole (sans changer d'etat). Garde-fou 3s.
							const float enterDur = castClip ? castClip->duration : 0.0f;
							const float shootDur = castShootClip ? castShootClip->duration : 0.0f;
							const float exitDur  = castExitClip ? castExitClip->duration : 0.0f;
							if (m_castPhase == 0 && stateElapsed >= enterDur)
							{
								m_avatarPendingClipRole = "CastShoot";
								m_castPhase = 1;
							}
							else if (m_castPhase == 1 && stateElapsed >= enterDur + shootDur)
							{
								m_avatarPendingClipRole = "CastExit";
								m_castPhase = 2;
							}
							if (stateElapsed >= enterDur + shootDur + exitDur || stateElapsed >= 3.0f)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						}
						case AvatarLocomotionState::Interact:
							// One-shot : retour locomotion quand le geste d'interaction est fini.
							// Action non-combat (la touche E reservee au menu Options trouve ici son usage).
							if (!interactClip || stateElapsed >= interactClip->duration)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
						case AvatarLocomotionState::Punch:
							// One-shot : retour locomotion quand le coup de poing est fini.
							if (!punchClip || stateElapsed >= punchClip->duration)
							{
								if (!moving)                  newState = AvatarLocomotionState::Idle;
								else if (movingBack)          newState = AvatarLocomotionState::WalkBack;
								else if (moveInput.sprint)    newState = AvatarLocomotionState::Sprint;
								else if (moveInput.run)       newState = AvatarLocomotionState::Run;
								else                          newState = AvatarLocomotionState::Walk;
							}
							break;
					}

					// Une action "one-shot" en cours (roulade ou geste combat/interaction)
					// bloque le declenchement d'une autre. Centralise ici pour eviter une
					// longue liste d'exclusions par override et faciliter l'ajout d'actions.
					auto busyOneShot = [&]()
					{
						return m_avatarLocoState == AvatarLocomotionState::Roll
							|| m_avatarLocoState == AvatarLocomotionState::Attack
							|| m_avatarLocoState == AvatarLocomotionState::Cast
							|| m_avatarLocoState == AvatarLocomotionState::Interact
							|| m_avatarLocoState == AvatarLocomotionState::Punch;
					};

					// Crouch (Ctrl) : etat accroupi prioritaire tant que la touche est tenue
					// (sauf amorce de saut, emote, ou action one-shot en cours). CrouchWalk si deplacement.
					if (moveInput.crouch && newState != AvatarLocomotionState::Jump
						&& newState != AvatarLocomotionState::Roll
						&& newState != AvatarLocomotionState::Emote
						&& newState != AvatarLocomotionState::Attack
						&& newState != AvatarLocomotionState::Cast
						&& newState != AvatarLocomotionState::Interact
						&& newState != AvatarLocomotionState::Punch)
						newState = moving ? AvatarLocomotionState::CrouchWalk : AvatarLocomotionState::CrouchIdle;

					// Esquive/roulade (double-appui Crouch) : Roll one-shot, prioritaire sur crouch.
					// Bloquée pendant le verrou de geste (ouverture de coffre).
					// ETAPE3.3 — même recharge que le shard (MovementLimits::dodgeCooldownMs) :
					// une esquive refusée par le shard serait recalée.
					const bool dodgeReady = m_moveLastDodgeMs == 0u
						|| m_moveClientTimeMs - static_cast<double>(m_moveLastDodgeMs)
							>= static_cast<double>(engine::movement::MovementLimits{}.dodgeCooldownMs);
					if (dodgePressed && dodgeReady && m_avatarLocoState != AvatarLocomotionState::Roll
						&& nowSec >= m_avatarMoveLockUntilSec)
					{
						newState = AvatarLocomotionState::Roll;
						// Impulsion d'esquive : direction = mouvement si actif, sinon l'avant camera.
						// Le CharacterController force la vitesse horizontale pendant dodgeDurationSec.
						engine::math::Vec3 dodgeDir = moveInput.moveDirXZ;
						if (dodgeDir.x * dodgeDir.x + dodgeDir.z * dodgeDir.z < 1e-6f)
							dodgeDir = m_orbitalCameraController.GetForwardXZ();
						m_characterController.ApplyDodgeImpulse(dodgeDir);
						// ETAPE3.3 — esquive annoncée au shard (instant dans la commande suivante).
						m_moveLastDodgeMs = (std::max)(1u, static_cast<uint32_t>(m_moveClientTimeMs));
					}

					// Emote (slash) : uniquement a l'arret (immobile, sans saut ni roll en cours).
					if (!emoteRole.empty() && !moving && !movingBack && !moveInput.jumpPressed
						&& m_avatarLocoState != AvatarLocomotionState::Roll)
					{
						m_currentEmoteRole = emoteRole;
						newState = AvatarLocomotionState::Emote;
					}

					// Attaque melee (clic gauche) : Sword_Attack one-shot. Pas pendant un saut
					// ni une autre action one-shot ; prioritaire sur le crouch.
					if (attackPressed && !moveInput.jumpPressed && !busyOneShot())
						newState = AvatarLocomotionState::Attack;

					// Coup de poing (touche C par defaut) : Punch_Jab one-shot, 2e attaque melee.
					if (punchPressed && !moveInput.jumpPressed && !busyOneShot())
					{
						// Alterne Jab <-> Cross a chaque coup (variete ; clip dynamique).
						m_currentPunchRole = m_punchAlt ? "PunchCross" : "Punch";
						m_punchAlt = !m_punchAlt;
						newState = AvatarLocomotionState::Punch;
					}

					// Sort (touche R par defaut) : Spell_Simple_Shoot one-shot.
					if (castPressed && !moveInput.jumpPressed && !busyOneShot())
						newState = AvatarLocomotionState::Cast;

					// Interagir (touche E par defaut) : geste Interact one-shot, action non-combat.
					// Près d'un coffre : joue "PickUp_Table" (se pencher → saisir → se redresser)
					// et verrouille le déplacement le temps du clip. Ailleurs : geste "Interact"
					// générique, sans verrou (comportement historique inchangé).
					if (interactPressed && !moveInput.jumpPressed && !busyOneShot())
					{
						const bool nearChest =
							m_chestLoaded && m_interactableInRange == m_chestInteractableIndex;
						if (nearChest)
						{
							m_currentInteractRole = "PickUp_Table";
							const engine::render::skinned::AnimationClip* puClip =
								m_currentSkinnedMesh->FindClip("PickUp_Table");
							// Clip absent (race non-UE5 / fallback) -> durée 0 -> verrou expire
							// immédiatement, pas de gel permanent ; le repli sur "Interact"
							// est géré juste en dessous.
							m_avatarMoveLockUntilSec = nowSec + (puClip ? puClip->duration : 0.0f);
							if (!puClip)
								m_currentInteractRole = "Interact";
						}
						else
						{
							m_currentInteractRole = "Interact";
						}
						newState = AvatarLocomotionState::Interact;
					}
				}
				else
				{
					// Not grounded : Jump (takeoff phase) -> Fall after takeoff duration ;
					// sinon (Walk/Run/Land/Idle qui perdent le sol) -> Fall APRÈS
					// une tolérance « coyote » de 150 ms (anti-flottement : les
					// micro-pertes de sol d'une frame au bord d'un obstacle ne
					// déclenchent plus la boucle Land->Fall->Land).
					if (m_avatarUngroundedSinceSec < 0.0f)
						m_avatarUngroundedSinceSec = nowSec;
					if (m_avatarLocoState == AvatarLocomotionState::Jump)
					{
						if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)
							newState = AvatarLocomotionState::Fall;
					}
					else if (m_avatarLocoState != AvatarLocomotionState::Fall
						&& nowSec - m_avatarUngroundedSinceSec >= 0.15f)
					{
						newState = AvatarLocomotionState::Fall;
					}
				}

				// Nage : si le controller est en mode eau (immersion > bassin), l'avatar
				// nage -> surclasse locomotion / saut / actions one-shot ci-dessus. A la
				// sortie d'eau, on quitte explicitement Swim* (le switch n'a pas de case
				// Swim* -> sinon l'avatar resterait fige en nage au sol).
				if (m_characterController.IsInWater())
				{
					newState = (moving || movingBack) ? AvatarLocomotionState::SwimForward : AvatarLocomotionState::SwimIdle;
				}
				else if (m_avatarLocoState == AvatarLocomotionState::SwimIdle
					|| m_avatarLocoState == AvatarLocomotionState::SwimForward)
				{
					if (!grounded)        newState = AvatarLocomotionState::Fall;
					else if (movingBack)  newState = AvatarLocomotionState::WalkBack;
					else if (moving)      newState = moveInput.sprint ? AvatarLocomotionState::Sprint : (moveInput.run ? AvatarLocomotionState::Run : AvatarLocomotionState::Walk);
					else                  newState = AvatarLocomotionState::Idle;
				}

				// Validation v12 — avatar MORT : il restait DEBOUT (la SM n'a pas
				// d'état Dead). On joue le clip « Death » du mesh s'il existe
				// (une fois, clampé sur la dernière frame), et on GÈLE la machine
				// d'états tant que le joueur est mort (sinon une transition vers
				// Idle écraserait la pose). Au respawn : retour Idle immédiat.
				const bool avatarDead =
					(m_uiModelBinding.GetModel().playerStats.stateFlags & 1u) != 0u;
				if (avatarDead && !m_avatarDeathClipPlaying)
				{
					// Validation v12 — recherche TOLÉRANTE du clip de mort : le mesh
					// humain a 63 clips mais aucun nommé exactement Death/Die/Dead
					// (constat log client). On cherche par sous-chaîne insensible à
					// la casse ; sans candidat, on liste les clips disponibles UNE
					// fois pour identifier l'asset à compléter.
					const engine::render::skinned::AnimationClip* deathClip = nullptr;
					for (const engine::render::skinned::AnimationClip& candidateClip : m_currentSkinnedMesh->clips)
					{
						std::string lowered = candidateClip.name;
						std::transform(lowered.begin(), lowered.end(), lowered.begin(),
							[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
						if (lowered.find("death") != std::string::npos
							|| lowered.find("die") != std::string::npos
							|| lowered.find("dead") != std::string::npos
							|| lowered.find("knock") != std::string::npos)
						{
							deathClip = &candidateClip;
							break;
						}
					}
					if (deathClip != nullptr)
					{
						m_avatarCrossfade.Play(*deathClip, /*loops=*/false, nowSec);
						LOG_INFO(Render, "[Avatar SM] Mort : clip '{}' joue (clamp)", deathClip->name);
					}
					else
					{
						std::string clipList;
						for (const engine::render::skinned::AnimationClip& availableClip : m_currentSkinnedMesh->clips)
						{
							if (!clipList.empty()) clipList += ", ";
							clipList += availableClip.name;
						}
						LOG_WARN(Render,
							"[Avatar SM] Mort : aucun clip de mort dans le mesh — pose Idle conservee. Clips disponibles : {}",
							clipList);
					}
					m_avatarDeathClipPlaying = true;
				}
				else if (!avatarDead && m_avatarDeathClipPlaying)
				{
					m_avatarDeathClipPlaying = false;
					if (const engine::render::skinned::AnimationClip* idleClip = m_currentSkinnedMesh->FindClip("Idle"))
					{
						m_avatarCrossfade.Play(*idleClip, /*loops=*/true, nowSec);
					}
					m_avatarLocoState = AvatarLocomotionState::Idle;
				}

				if (!avatarDead && newState != m_avatarLocoState)
				{
					// DEBUG B.1 : log chaque transition d'etat pour diagnostiquer
					// "modèle qui saute toujours". Une fois le bug compris, retirer.
					LOG_INFO(Render, "[Avatar SM] {} -> {} (grounded={} moving={} jumpPressed={} run={} stateElapsed={:.3f}s)",
						StateToClipName(m_avatarLocoState),
						StateToClipName(newState),
						grounded ? 1 : 0,
						moving ? 1 : 0,
						moveInput.jumpPressed ? 1 : 0,
						moveInput.run ? 1 : 0,
						stateElapsed);

					m_avatarLocoState = newState;
					m_avatarLocoStateEnterTime = now;
					if (newState == AvatarLocomotionState::Cast) m_castPhase = 0;

					// Trigger crossfade vers le clip du nouvel etat. Si le clip est
					// introuvable (asset manquant -> FindClip == nullptr), on log un
					// warn une seule fois et on laisse l'animation precedente continuer
					// (Sample retombera dessus jusqu'a la prochaine transition reussie).
					const char* clipName =
						(newState == AvatarLocomotionState::Emote && !m_currentEmoteRole.empty()) ? m_currentEmoteRole.c_str()
						: (newState == AvatarLocomotionState::Punch) ? m_currentPunchRole.c_str()
						: (newState == AvatarLocomotionState::Interact) ? m_currentInteractRole.c_str()
						: StateToClipName(newState);
					const engine::render::skinned::AnimationClip* newClip = m_currentSkinnedMesh->FindClip(clipName);
					if (newClip)
					{
						LOG_INFO(Render, "[Avatar SM] Play('{}') duration={:.3f}s loops={}",
							clipName, newClip->duration, ClipLoops(newState) ? 1 : 0);
						m_avatarCrossfade.Play(*newClip, ClipLoops(newState), nowSec);
					}
					else
					{
						LOG_WARN(Render, "[Avatar SM] FindClip('{}') returned nullptr — animation precedente continue", clipName);
					}
				}
				// Replay d'un clip en cours d'etat (sequence de sort) sans transition d'etat.
				if (!m_avatarPendingClipRole.empty())
				{
					const engine::render::skinned::AnimationClip* rc = m_currentSkinnedMesh->FindClip(m_avatarPendingClipRole.c_str());
					if (rc)
						m_avatarCrossfade.Play(*rc, /*loops=*/false, nowSec);
					m_avatarPendingClipRole.clear();
				}

				// Interaction (touche E) : interactible le plus proche (distance XZ) a portee.
				// Hint chat a l'entree de portee ; sur E -> effet (objet) ou dialogue (PNJ).
				// Le geste Interact joue par ailleurs (SM). v1 sans rendu des cibles.
				{
					const engine::math::Vec3 ppos = m_characterController.GetPosition();
					int nearestI = -1;
					float bestD2 = 0.0f;
					for (std::size_t i = 0; i < m_interactables.size(); ++i)
					{
						const InteractableEntity& e = m_interactables[i];
						const float dx = e.position.x - ppos.x;
						const float dz = e.position.z - ppos.z;
						const float d2 = dx * dx + dz * dz;
						if (d2 <= e.radius * e.radius && (nearestI < 0 || d2 < bestD2))
						{
							nearestI = static_cast<int>(i);
							bestD2 = d2;
						}
					}
					auto pushInteractChat = [&](const std::string& sender, const std::string& text)
					{
						engine::net::ChatMessage cm;
						cm.timestampUnixMs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch()).count());
						cm.channel = engine::net::ChatChannel::Server;
						cm.sender = sender;
						cm.text = text;
						m_chatUi.PushNetworkLine(cm);
					};
					if (nearestI != m_interactableInRange)
					{
						m_interactableInRange = nearestI;
						if (nearestI >= 0)
							pushInteractChat("[Interaction]", "Pres de " + m_interactables[nearestI].label + " - appuyez sur E.");
					}
					// Refermeture automatique du coffre 2 s apres la derniere ouverture
					// (verifie chaque frame, independamment de l'appui sur E).
					if (m_chestLoaded && m_chestOpen && EngineNowSec() >= m_chestAutoCloseAtSec)
					{
						m_chestOpen = false;
						if (const auto* clip = m_chestMesh.FindClip("Chest_Close"))
							m_chestCrossfade.Play(*clip, /*loops*/ false, EngineNowSec());
						pushInteractChat("[Objet]", "Le coffre se referme.");
					}
					// Monde vivant — PNJ d'ambiance parlants (villageois, gardes, marchands) :
					// à défaut d'interactible statique à portée, le plus proche dans
					// kAmbientTalkRadiusM répond à E (dialogue et/ou Talk serveur).
					const engine::client::UIRemoteEntity* ambientNpc = nullptr;
					const engine::client::CreatureAppearance* ambientApp = nullptr;
					if (nearestI < 0)
					{
						float bestAmb = kAmbientTalkRadiusM * kAmbientTalkRadiusM;
						for (const engine::client::UIRemoteEntity& re : m_uiModelBinding.GetModel().remoteEntities)
						{
							if (re.playerClientId != 0u || re.archetypeId == 0u) continue;
							const engine::client::CreatureAppearance* app = m_creatureCatalog.Find(re.archetypeId);
							if (app == nullptr || !app->ambient || (app->dialogueId.empty() && app->talkTarget.empty())) continue;
							const float dxa = re.positionX - ppos.x, dza = re.positionZ - ppos.z;
							const float d2a = dxa * dxa + dza * dza;
							if (d2a <= bestAmb) { bestAmb = d2a; ambientNpc = &re; ambientApp = app; }
						}
					}
					const uint32_t ambientId = ambientNpc ? static_cast<uint32_t>(ambientNpc->entityId) : 0u;
					if (ambientId != m_ambientNpcInRange)
					{
						m_ambientNpcInRange = ambientId;
						if (ambientNpc && !m_dialogueActive)
							pushInteractChat("[Interaction]", "Pres de " + ambientApp->name + " - appuyez sur E.");
					}
					if (interactPressed && nearestI < 0 && ambientNpc && !m_dialogueActive)
					{
						if (!ambientApp->dialogueId.empty())
						{
							auto cached = m_ambientDialogueCache.find(ambientApp->dialogueId);
							if (cached == m_ambientDialogueCache.end())
							{
								// LoadDialogueTree lit `<base>dialogue_id` : config éphémère.
								engine::core::Config dcfg;
								dcfg.SetValue("paths.content", engine::core::Config::Value{ m_cfg.GetString("paths.content", "game/data") });
								dcfg.SetValue("npc.dialogue_id", engine::core::Config::Value{ ambientApp->dialogueId });
								cached = m_ambientDialogueCache.emplace(ambientApp->dialogueId,
									engine::client::LoadDialogueTree(dcfg, "npc.", { ambientApp->name + " : Bien le bonjour." })).first;
							}
							engine::client::DialogueNpcRef npc;
							npc.label = ambientApp->name;
							npc.role = ambientApp->role;
							npc.entityIndex = -1; // PNJ distant : distance suivie via m_dialogueRemoteEntityId
							m_dialogue.OpenDialogue(cached->second, npc);
							m_dialogueActive = true;
							m_dialogueRemoteEntityId = ambientId;
							m_currentDialogueNpcTargetId = ambientApp->talkTarget;
						}
						else
						{
							pushInteractChat("[PNJ]", ambientApp->name + " : Approchez, voyez ce que j'ai !");
						}
						if (!ambientApp->talkTarget.empty() && m_gameplayNetInitialized)
							(void)m_gameplayUdp.SendTalkRequest(m_gameplayUdp.ServerClientId(), ambientApp->talkTarget);
					}
					if (interactPressed && nearestI >= 0)
					{
						InteractableEntity& e = m_interactables[nearestI];
						if (m_chestLoaded && e.meshPath.find("Chest_Wood") != std::string::npos)
						{
							// E ouvre le coffre (s'il ne l'est pas deja) puis (re)arme une
							// refermeture automatique 2 s plus tard (cf. check par frame).
							if (!m_chestOpen)
							{
								m_chestOpen = true;
								if (const auto* clip = m_chestMesh.FindClip("Chest_Open"))
									m_chestCrossfade.Play(*clip, /*loops*/ false, EngineNowSec());
								pushInteractChat("[Objet]", "Vous ouvrez le coffre.");
							}
							m_chestAutoCloseAtSec = EngineNowSec() + 2.0f;
						}
						else if (e.isNpc && !e.dialogueTree.nodes.empty())
						{
							// Cellule de dialogue PNJ : ouvre la fenêtre centrale dédiée
							// (plus de poussée dans le chat). Ignoré si un dialogue est
							// déjà actif (E ne ré-ouvre pas par-dessus).
							if (!m_dialogueActive)
							{
								engine::client::DialogueNpcRef npc;
								npc.label       = e.label;
								npc.role        = e.role;
								npc.entityIndex = nearestI; // index de l'interactable courant
								m_dialogue.OpenDialogue(e.dialogueTree, npc);
								m_dialogueActive = true;
								// SP2 (Task 4b) — mémorise le PNJ courant pour l'accept/turn-in
								// (Task 4) ET envoie un Talk au shard : OpenDialogue est purement
								// local (pas de round-trip serveur), donc sans ce Talk,
								// UIModel.giverList.npcTargetId reste vide/périmé (dernier PNJ
								// parlé, pas forcément celui-ci) et le shard rejetterait l'accept/
								// turn-in. Ignoré si le PNJ n'a pas d'id réseau ou si le réseau
								// gameplay n'est pas encore prêt (dialogue reste utilisable en
								// mode solo/hors-ligne, juste sans wire quête).
								m_currentDialogueNpcTargetId = e.npcTargetId;
								if (!e.npcTargetId.empty() && m_gameplayNetInitialized)
								{
									(void)m_gameplayUdp.SendTalkRequest(m_gameplayUdp.ServerClientId(), e.npcTargetId);
								}
							}
						}
						else
						{
							pushInteractChat(e.isNpc ? "[PNJ]" : "[Objet]", e.message);
						}
					}

					// Cellule de dialogue PNJ : tick par frame (auto-scroll + rupture de
					// distance à 1,50 m) et fermeture sur Échap. La distance est recalculée
					// vers le PNJ courant (entityIndex mémorisé à l'ouverture).
					if (m_dialogueActive)
					{
						float dist = 999.0f;
						const int idx = m_dialogue.Npc().entityIndex;
						if (idx >= 0 && idx < static_cast<int>(m_interactables.size()))
						{
							const engine::math::Vec3 pp = m_characterController.GetPosition();
							const engine::math::Vec3 np = m_interactables[idx].position;
							const float dx = np.x - pp.x;
							const float dz = np.z - pp.z;
							dist = std::sqrt(dx * dx + dz * dz);
						}
						else if (m_dialogueRemoteEntityId != 0u)
						{
							// PNJ d'ambiance : distance à son entité répliquée (il s'est arrêté
							// pour parler ; s'il sort d'AoI, la conversation se rompt).
							const engine::math::Vec3 pp = m_characterController.GetPosition();
							for (const engine::client::UIRemoteEntity& re : m_uiModelBinding.GetModel().remoteEntities)
							{
								if (static_cast<uint32_t>(re.entityId) != m_dialogueRemoteEntityId) continue;
								const float dx = re.positionX - pp.x, dz = re.positionZ - pp.z;
								// Rupture plus tolérante qu'un PNJ statique (1,5 m) : le PNJ bouge.
								dist = std::max(0.0f, std::sqrt(dx * dx + dz * dz) - (kAmbientTalkRadiusM - 1.5f));
								break;
							}
						}
						m_dialogue.Tick(static_cast<float>(dt), dist);

						// Échap ferme le dialogue (edge-triggered).
						if (m_input.WasPressed(engine::platform::Key::Escape))
							m_dialogue.Close(engine::client::DialogueCloseReason::UserClose);

						// Synchronise le flag : toute fermeture (distance, Échap, choix End)
						// libère le déplacement.
						if (!m_dialogue.IsActive())
						{
							m_dialogueActive = false;
							// SP2 (Task 4b) — le PNJ mémorisé n'est plus valide une fois le
							// dialogue fermé (évite qu'un futur accept/turn-in hors dialogue
							// réutilise par erreur la dernière cible).
							m_currentDialogueNpcTargetId.clear();
							m_dialogueRemoteEntityId = 0u;
							// Le panneau « Quêtes du PNJ » (giver-list, rempli au Talk) ne doit
							// vivre que le temps de la conversation : on le vide à la fermeture,
							// sinon il resterait affiché en permanence après avoir parlé une fois.
							m_uiModelBinding.ClearGiverList();
						}
					}
				}
			}

			// --- Zoom radar minimap (molette + clic sur l'arc). Si le curseur
			// survole le radar (disque + anneau des repères), la molette pilote le
			// zoom du radar (5 crans 200..1000 m) au lieu du zoom caméra, et le clic
			// sur un repère saute à ce cran. Géométrie via ComputeRadarScreenRect
			// (m_width/m_height = taille framebuffer = io.DisplaySize du radar, donc
			// même géométrie qu'au rendu). Cran persisté dans minimap_settings.json.
			bool mouseOverRadar = false;
			{
				const engine::client::RadarScreenRect radarRect =
					engine::client::ComputeRadarScreenRect(m_cfg,
						static_cast<float>(m_width), static_cast<float>(m_height));
				if (radarRect.enabled)
				{
					const float rcx = radarRect.x0 + radarRect.size * 0.5f;
					const float rcy = radarRect.y0 + radarRect.size * 0.5f;
					// Zone d'interaction = disque + marge couvrant l'anneau des repères
					// (tracés à R+6). Sans cette marge, un clic sur un repère (hors du
					// disque) ne serait jamais détecté.
					const float interactR = radarRect.size * 0.5f + 16.0f;
					const float mdx = static_cast<float>(m_input.MouseX()) - rcx;
					const float mdy = static_cast<float>(m_input.MouseY()) - rcy;
					mouseOverRadar = (mdx * mdx + mdy * mdy) <= (interactR * interactR);
					if (mouseOverRadar)
					{
						int newZoom = m_minimapZoomIndex;
						const int wheel = m_input.MouseScrollDelta();
						if (wheel != 0)
							newZoom = engine::client::StepZoomIndex(newZoom, wheel);
						if (m_input.WasMousePressed(engine::platform::MouseButton::Left))
						{
							constexpr float kTickHitRadiusPx = 12.0f;
							for (int t = 0; t < engine::client::kMinimapZoomLevelCount; ++t)
							{
								const engine::client::ScreenPoint tp =
									engine::client::RadarZoomTickPos(radarRect, t);
								const float tdx = static_cast<float>(m_input.MouseX()) - tp.x;
								const float tdy = static_cast<float>(m_input.MouseY()) - tp.y;
								if ((tdx * tdx + tdy * tdy) <= (kTickHitRadiusPx * kTickHitRadiusPx))
								{
									newZoom = t;
									break;
								}
							}
						}
						if (newZoom != m_minimapZoomIndex)
						{
							m_minimapZoomIndex = newZoom;
							m_questUi.SetMinimapRadius(engine::client::RadiusForZoomIndex(newZoom));
							engine::core::Config zoomCfg;
							zoomCfg.SetValue("client.quest.minimap.zoom_level", static_cast<int64_t>(newZoom));
							if (!zoomCfg.SaveToFile("minimap_settings.json"))
								LOG_WARN(Core, "[Minimap] echec persistance zoom (minimap_settings.json)");
							else
								LOG_INFO(Core, "[Minimap] zoom radar -> cran {} ({} m)",
									newZoom, engine::client::RadiusForZoomIndex(newZoom));
						}
					}
				}
			}

			const bool rmbLook = m_input.IsMouseDown(engine::platform::MouseButton::Right);
			// applyZoom coupé aussi quand ImGui capte la souris : sinon la molette
			// utilisée pour défiler dans une fenêtre (ex. onglet Arbre de la fenêtre
			// Personnage) zoomait AUSSI la caméra du jeu (retour joueur 2026-07-09).
			// Cette section n'est PAS sous #if defined(_WIN32) et ImGui n'est inclus
			// que sur Windows -> on garde la lecture WantCaptureMouse.
			bool imguiWantsMouse = false;
#if defined(_WIN32)
			imguiWantsMouse = ImGui::GetIO().WantCaptureMouse;
#endif
			m_orbitalCameraController.Update(m_input, dt, mouseSensitivity, invertY, rmbLook,
				!mouseOverRadar && !imguiWantsMouse, out.camera);

			// --- Clamp anti-sous-sol : la caméra ne descend pas sous le terrain. ---
			const bool occEnabled = m_cfg.GetBool("client.camera.occlusion_fade.enabled", true);
			const float clampMargin = static_cast<float>(
				m_cfg.GetDouble("client.camera.terrain_clamp_margin", 0.5));
			const float groundY = SampleGroundHeight(
				out.camera.position.x, out.camera.position.z);
			const float minCamY = groundY + clampMargin;
			if (out.camera.position.y < minCamY)
				out.camera.position.y = minCamY;

			// --- Nage en surface : la caméra reste AU-DESSUS de l'eau. ---
			// Sans cela, en nageant, la caméra orbitale (cible = centre du nageur,
			// sous la ligne d'eau) passait de quelques cm sous la surface : la nappe
			// était vue par la tranche (une ligne) et la mer / l'étang « disparaissaient ».
			// En plongée (cible à plus de `client.camera.water_dive_depth_m` sous la
			// surface), la caméra peut suivre sous l'eau.
			{
				engine::gameplay::IWorldCollider::WaterQuery camWater;
				if (m_terrainCollider.QueryWater(out.camera.position, camWater))
				{
					const engine::math::Vec3 target = m_orbitalCameraController.GetTargetPosition();
					const float diveDepth = static_cast<float>(
						m_cfg.GetDouble("client.camera.water_dive_depth_m", 1.5));
					const float waterMargin = static_cast<float>(
						m_cfg.GetDouble("client.camera.water_clamp_margin", 0.35));
					if (target.y > camWater.surfaceY - diveDepth)
						out.camera.position.y = camWater.surfaceY + waterMargin;
				}
			}

			// --- Fondu d'occlusion : collecte des occulteurs (props + bâtiments). ---
			// L'index `i` ci-dessous est l'identifiant d'occulteur ; il DOIT
			// correspondre à l'index utilisé pour FadeFor dans RecordPropsGeometry
			// (même conteneur m_props, même ordre dans la même frame).
			if (occEnabled)
			{
				std::vector<engine::render::OccluderSphere> occluders;
				occluders.reserve(m_props.size());
				for (std::size_t i = 0; i < m_props.size(); ++i)
				{
					const auto& prop = m_props[i];
					if (prop.occluderRadius <= 0.0f) continue;
					occluders.push_back({ static_cast<std::uint32_t>(i),
						prop.occluderCenter, prop.occluderRadius });
				}
				const engine::math::Vec3 focus = m_orbitalCameraController.GetTargetPosition();
				// Monde ouvert : arbres et pans de murs instanciés entre caméra et joueur.
				CollectScatterOccluders(out.camera.position, focus, occluders);
				m_cameraOcclusionFade.Update(out.camera.position, focus,
					occluders, static_cast<float>(dt));
			}
		}

		// Chat update : uniquement post-EnterWorld. Le rendu pre-game est desactive
		// (cf. chatImguiOverlayNewFrame=false plus haut), donc pas besoin d'Update
		// le presenter avant. Si on reactive le chat pre-game un jour, etendre cette
		// gate avec un OR sur IsMasterAuthenticated().
		if (!authGateActive && m_chatUi.IsInitialized())
		{
			// Phase 3.11.3 — Indique au presenter si un ImGui::InputText pilote la saisie
			// (panneau chat ImGui actif). Coupe la branche keyboard-typing/Enter dans Update
			// pour éviter une double insertion ; Escape et scroll restent actifs.
#if defined(_WIN32)
			const bool chatImguiInputActive = m_chatImGui && !m_worldEditorExe
				&& m_cfg.GetBool("render.chat_imgui.enabled", true);
			m_chatUi.SetImGuiInputActive(chatImguiInputActive);
#else
			m_chatUi.SetImGuiInputActive(false);
#endif
			m_chatUi.Update(m_input, static_cast<float>(dt));
		}
	}
} // namespace engine
