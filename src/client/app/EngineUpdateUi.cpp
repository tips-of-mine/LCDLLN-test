// Engine — sous-étapes d'Engine::Update() liées aux interfaces (overlay ImGui,
// HUD, fenêtres de jeu).
//
// Code déplacé tel quel depuis Engine::Update() (aucun changement de
// comportement) : découpage d'une fonction de ~6 000 lignes en sous-fonctions
// nommées. La documentation de chaque fonction est dans Engine.h.

#include "src/client/app/Engine.h"
#include "src/client/world/ServiceSigns.h"
#include "src/client/app/EngineHelpers.h"
#include "src/client/render/ImGuiOverlay.h"

#include "src/shared/platform/FileSystem.h"
#include "src/shared/anniversary/CakeItemToken.h" // SP3 anniversaires (2026-07-18)
#include "src/shared/world/BoatDeck.h"
#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/DialogueImGuiRenderer.h"
#include "src/client/render/ChatImGuiRenderer.h"
#include "src/client/render/QuestImGuiRenderer.h"
#include "src/client/render/MailImGuiRenderer.h"
#include "src/client/render/GmTicketImGuiRenderer.h"
#include "src/client/render/ReputationImGuiRenderer.h"
#include "src/client/render/CharacterSheetImGuiRenderer.h"
#include "src/client/render/ArenaImGuiRenderer.h"
#include "src/client/render/BattleGroundImGuiRenderer.h"
#include "src/client/render/OutdoorPvpImGuiRenderer.h"
#include "src/client/render/WeatherImGuiRenderer.h"
#include "src/client/render/GameEventImGuiRenderer.h"
#include "src/client/render/GuildImGuiRenderer.h"
#include "src/client/render/AuctionImGuiRenderer.h"
#include "src/client/render/LootRollImGuiRenderer.h"
#include "src/client/render/LfgImGuiRenderer.h"
#include "src/client/render/CinematicImGuiRenderer.h"
#include "src/client/render/SkillBookImGuiRenderer.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/render/CharacterWindowImGuiRenderer.h"
#include "src/client/gameplay/ActionBarLayout.h"
#include "src/client/ui_common/CurrencyFormat.h"
#include "src/client/render/LnTheme.h"
#include "src/client/render/DeferredPipeline.h"

#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "imgui.h"
#include "src/client/render/CompassHud.h"
#endif

namespace engine
{
	using namespace engine::helpers; // utilitaires internes partagés (EngineHelpers.h)

#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::InitImGuiOverlayIfNeeded()
	{
		m_imguiOverlay = std::make_unique<engine::render::ImGuiOverlay>();
		if (m_imguiOverlay->Init(
			m_vkInstance.GetHandle(),
			m_vkDeviceContext,
			m_vkSwapchain.GetImageFormat(),
			m_vkSwapchain.GetImageCount(),
			VK_API_VERSION_1_1,
			m_window.GetNativeHandle(),
			&m_cfg,
			m_worldEditorExe,
			[this] { ApplyWorldEditorImGuiTheme(); }))
		{
			InitWorldEditorImGuiPanels();
			// Sous-projet C MVP (Task 12) — Initialise le viewport
			// offscreen 512x512 dedie a l'apercu race dans l'ecran
			// ImGui AuthImGuiCharacterCreate. Doit etre fait apres
			// ImGui_ImplVulkan_Init (couvert par m_imguiOverlay->Init
			// plus haut, qui appelle ImGui_ImplVulkan_Init dans tous les
			// cas — meme en mode client/jeu, le contexte ImGui partage
			// est utilise par AuthImGui). En cas d'echec, AuthImGui
			// recevra nullptr -> l'ecran de creation perso retombera
			// sur le fallback texte (cf. AuthImGuiCharacterCreate).
			if (!m_racePreviewViewport.Init(
				m_vkDeviceContext.GetDevice(),
				m_vkDeviceContext.GetPhysicalDevice(),
				m_vkDeviceContext.GetGraphicsQueue(),
				m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
				/*width*/ 512u, /*height*/ 512u))
			{
				LOG_WARN(Render, "[Engine] RacePreviewViewport init failed -- ecran creation perso restera sans apercu 3D");
			}
			else
			{
				// Sous-chantier A phase 2 — pipeline forward 3D de l'apercu +
				// cablage des materiaux avatar (set bindless + ids peau/habit).
				// loadSpirv (defini plus haut) n'est pas dans la portee ici :
				// on relit le SPIR-V directement depuis le content root.
				auto loadPreviewSpv = [&](const char* p) -> std::vector<uint32_t> {
					std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, p);
					std::vector<uint32_t> out;
					if (!bytes.empty() && bytes.size() % 4 == 0)
					{
						out.resize(bytes.size() / 4);
						std::memcpy(out.data(), bytes.data(), bytes.size());
					}
					return out;
				};
				const std::vector<uint32_t> previewVert = loadPreviewSpv("shaders/skinned_preview.vert.spv");
				const std::vector<uint32_t> previewFrag = loadPreviewSpv("shaders/skinned_preview.frag.spv");
				if (m_pipeline && !previewVert.empty() && !previewFrag.empty())
				{
					auto& matCache = m_pipeline->GetMaterialDescriptorCache();
					if (matCache.IsValid()
						&& m_racePreviewViewport.InitForwardPipeline(
							matCache.GetLayout(),
							previewVert.data(), previewVert.size(),
							previewFrag.data(), previewFrag.size()))
					{
						const uint32_t previewOutfitId = (m_avatarMaterialId != 0u)
							? m_avatarMaterialId : matCache.GetDefaultMaterialIndex();
						m_racePreviewViewport.SetAvatarMaterials(
							matCache.GetDescriptorSet(),
							previewOutfitId,
							m_avatarBodyMaterialIdMale,
							m_avatarBodyMaterialIdFemale,
							m_avatarBodyMaterialIdMaleDark,
							m_avatarBodyMaterialIdFemaleDark,
							m_avatarBodyMaterialNames,
							m_avatarSkinDepthBiasConstant,
							m_avatarSkinDepthBiasSlope);
						LOG_INFO(Render, "[Engine] RacePreviewViewport forward 3D pret (outfitId={})", previewOutfitId);
					}
					else
					{
						LOG_WARN(Render, "[Engine] RacePreviewViewport InitForwardPipeline echoue -- apercu en clear couleur");
					}
				}
				else
				{
					LOG_WARN(Render, "[Engine] shaders skinned_preview absents -- apercu 3D en clear couleur");
				}
			}
			// Branche le DayNightCycle au panneau "Atmosphere" pour que l'utilisateur
			// puisse regler time-of-day et timeScale en live depuis l'editeur monde.
			BindWorldEditorDayNight();
			m_imguiOverlay->AttachPlatformWindow(m_window.GetNativeHandle(), m_window);
			m_authImGui = std::make_unique<engine::render::AuthImGuiRenderer>();
			m_authImGui->BindAuthUiBridge(&m_authUi, &m_cfg, &m_window);
			// Sous-projet C MVP (Task 12) — passe le viewport offscreen
			// race au renderer ImGui de l'ecran de creation perso. Le
			// renderer accepte nullptr (IsValid() == false -> fallback
			// texte) ; on transmet quand meme la reference pour que
			// l'edit dynamique reste possible si on veut recreer le
			// viewport apres un resize (pas le cas en MVP).
			m_authImGui->SetRacePreview(&m_racePreviewViewport);
			// Sous-projet C MVP (Task 12) — Donne au AuthUiPresenter une
			// reference vers Engine pour la resolution race_str ->
			// SkinnedMesh* (deleguee a Engine::GetRaceMesh) consommee
			// par AuthImGuiCharacterCreate quand l'utilisateur change
			// la race selectionnee dans le combo.
			m_authUi.SetEngineForRaceMeshLookup(this);
			// Phase 3.11.1 — partage du même contexte ImGui (NewFrame/Render gérés par m_imguiOverlay).
			m_chatImGui = std::make_unique<engine::render::ChatImGuiRenderer>();
			m_chatImGui->BindChatUi(&m_chatUi, &m_cfg);
			// Cellule de dialogue PNJ : renderer ImGui dédié (fenêtre centrale).
			// Partage le contexte ImGui avec auth/chat ; rendu Windows uniquement
			// (le .cpp est gardé par #if _WIN32). Constructeur par défaut.
			m_dialogueImGui = std::make_unique<engine::render::DialogueImGuiRenderer>();
			// PR-B — Acceptation/rendu de quête DANS la conversation : le renderer
			// de dialogue injecte les boutons Accepter/Rendre depuis UIModel.giverList
			// (titres via le catalogue). Le callback d'action est câblé plus bas
			// (même bloc GameplayNet que le panneau donneur, m_gameplayUdp requis).
			m_dialogueImGui->BindQuestGiver(&m_uiModelBinding, &m_questTextCatalog);
			// SP2 Task 5 — Renderer ImGui journal/tracker/panneau donneur.
			// Partage le contexte ImGui avec auth/chat/dialogue. Le callback
			// d'action panneau donneur est cable plus bas (apres m_gameplayUdp
			// disponible), dans le meme bloc que le wiring GameplayNet.
			m_questImGui = std::make_unique<engine::render::QuestImGuiRenderer>();
			m_questImGui->BindQuestUi(&m_questUi, &m_questTextCatalog, &m_uiModelBinding, &m_cfg);
			// CMANGOS.18 (Phase 3.18 step 4) — Renderer ImGui de la boite mail.
			// Partage le contexte ImGui avec auth/chat. Visible uniquement quand
			// m_mailVisible (toggle via /mail). La taille viewport est mise a
			// jour dans le boucle Render plus bas.
			m_mailImGui = std::make_unique<engine::render::MailImGuiRenderer>();
			m_mailImGui->SetPresenter(&m_mailUi);
			// CMANGOS.32 (Phase 5.32 step 3+4) — Renderer ImGui du panneau
			// Support GM. Partage le contexte ImGui avec auth/chat/mail. Visible
			// uniquement quand m_gmTicketsVisible (toggle via /ticket). La
			// taille viewport est mise a jour dans la boucle Render plus bas.
			m_gmTicketImGui = std::make_unique<engine::render::GmTicketImGuiRenderer>();
			m_gmTicketImGui->SetPresenter(&m_gmTicketUi);
			// CMANGOS.24 (Phase 3.24 step 3+4) — Renderer ImGui du panneau
			// Reputation. Partage le contexte ImGui avec auth/chat/mail/gmtickets.
			// Visible uniquement quand m_reputationVisible (toggle via /rep).
			// La taille viewport est mise a jour dans la boucle Render plus bas.
			m_reputationImGui = std::make_unique<engine::render::ReputationImGuiRenderer>();
			m_reputationImGui->SetPresenter(&m_reputationUi);
			// R1-B (Task 4) — Renderer ImGui de la feuille de personnage. Lit
			// directement m_uiModelBinding.GetModel().playerStats (pas de
			// presenter). Visible quand m_characterSheetVisible (toggle touche C
			// hors combat). Taille viewport mise a jour dans la boucle Render.
			m_characterSheetImGui = std::make_unique<engine::render::CharacterSheetImGuiRenderer>();
			// CMANGOS.33 (Phase 5.33 step 3+4) — Renderer ImGui du panneau LFG.
			// Partage le contexte ImGui avec auth/chat/mail/gmtickets/reputation.
			// Visible uniquement quand m_lfgVisible (toggle via /lfg).
			m_lfgImGui = std::make_unique<engine::render::LfgImGuiRenderer>();
			m_lfgImGui->SetPresenter(&m_lfgUi);
			// CMANGOS.30 (Phase 5.30 step 3+4) — Renderer overlay cinematique
			// (black bars + skip hint). Visible uniquement quand une cinematique
			// est en cours (state.isPlaying == true). Pas de toggle slash command :
			// le declenchement est server-pushed.
			m_cinematicImGui = std::make_unique<engine::render::CinematicImGuiRenderer>();
			m_cinematicImGui->SetPresenter(&m_cinematicUi);
			// CMANGOS.39 (Phase 4.39 step 3+4) — Renderer ImGui du panneau
			// Skill Book. Partage le contexte ImGui avec auth/chat/mail/gmtickets/
			// reputation/lfg. Visible uniquement quand m_skillBookVisible
			// (toggle via /skills ou touche B).
			m_skillBookImGui = std::make_unique<engine::render::SkillBookImGuiRenderer>();
			m_skillBookImGui->SetPresenter(&m_skillBookUi);
			// Grimoire (Task 13) — Renderer ImGui du panneau Grimoire / Carnet de
			// techniques. Visible quand m_grimoireVisible (touche V ou /grimoire /
			// /sorts). Partage le contexte ImGui post-auth.
			m_grimoireImGui = std::make_unique<engine::render::GrimoireImGuiRenderer>();
			m_grimoireImGui->SetPresenter(&m_grimoireUi);
			// SP-D — Renderer ImGui du panneau arbre de compétences par-classe.
			// Visible quand m_classSkillTreeVisible (touche Y ou /arbre / /competences).
			m_classSkillTreeImGui = std::make_unique<engine::render::ClassSkillTreeImGuiRenderer>();
			m_classSkillTreeImGui->SetPresenter(&m_classSkillTreeUi);
			// SP-E — Cache d'icônes de compétences (PNG icons/skills/<classId>/...).
			// Init APRÈS ImGui_ImplVulkan_Init (assuré par m_imguiOverlay->Init
			// plus haut). Idempotent. Partagé avec le Grimoire / la barre d'action.
			if (!m_skillIconCache.IsInitialized())
			{
				(void)m_skillIconCache.Init(m_vkDeviceContext.GetDevice(), &m_assetRegistry);
			}
			m_classSkillTreeImGui->SetIconCache(&m_skillIconCache);
			if (m_grimoireImGui) { m_grimoireImGui->SetIconCache(&m_skillIconCache); }
			// Chantier 1 — fenêtre Personnage unifiée à onglets (F1). Regroupe les 3
			// panneaux (pilotés en mode embarqué) + inventaire + argent + perso 3D
			// (viewport branché en Task 4). Bind après création des sous-renderers et
			// init du cache d'icônes.
			m_characterWindowImGui = std::make_unique<engine::render::CharacterWindowImGuiRenderer>();
			m_characterWindowImGui->Bind(&m_cfg, &m_uiModelBinding, &m_invUi, &m_skillIconCache,
				m_skillBookImGui.get(), m_grimoireImGui.get(), m_classSkillTreeImGui.get());
			// SP2 anniversaires (2026-07-18) — onglet Exploits (renderer lié
			// au presenter m_exploitsUi ; la requête part au toggle F1).
			m_exploitsImGui = std::make_unique<engine::render::ExploitsImGuiRenderer>();
			m_exploitsImGui->Bind(&m_exploitsUi);
			// Lore v1.0.114 — compteur « Cimetières visités : N/666 » de l'onglet Exploits.
			m_exploitsImGui->BindDiscovery(&m_uiModelBinding.GetModel().discovery);
			m_characterWindowImGui->SetExploitsRenderer(m_exploitsImGui.get());
			// Chantier 2 SP-A — catalogue d'objets client (noms/slots/bonus pour le
			// panneau équipement + tooltips). Source d'affichage ; le serveur reste
			// autoritaire. Non fatal : catalogue absent -> noms bruts (#itemId).
			{
				const std::string itemsText =
					engine::platform::FileSystem::ReadAllTextContent(m_cfg, "items/items.json");
				if (itemsText.empty() || !m_itemCatalog.LoadFromJson(itemsText))
					LOG_WARN(Net, "[Engine] catalogue d'objets client indisponible (items/items.json)");
				else
					LOG_INFO(Net, "[Engine] catalogue d'objets client charge : {} objet(s)", m_itemCatalog.Count());
				m_characterWindowImGui->SetItemCatalog(&m_itemCatalog);
			}
			// Task 4 — aperçu 3D : le conteneur affiche la texture offscreen du
			// viewport perso (alimenté quand la fenêtre est ouverte, cf. Update).
			m_characterWindowImGui->SetRaceViewport(&m_racePreviewViewport);
			// CMANGOS.21 (Phase 5.21 step 3+4) — Renderer ImGui du panneau
			// Arena. Visible uniquement quand m_arenaVisible (toggle via
			// /arena ou touche A). Le popup proposal s'affiche aussi quand
			// pendingProposalId.has_value() == true (meme si le panneau
			// principal est masque), pour que le joueur ne rate pas la
			// formation de match.
			m_arenaImGui = std::make_unique<engine::render::ArenaImGuiRenderer>();
			m_arenaImGui->SetPresenter(&m_arenaUi);
			// CMANGOS.10 (Phase 5 step 3+4) — Renderer ImGui du panneau
			// BattleGround. Visible quand m_battleGroundVisible (toggle
			// /bg ou touche G), ou quand un match BG est actif (le
			// scoreboard s'auto-affiche apres le push 136 MatchStart).
			m_battleGroundImGui = std::make_unique<engine::render::BattleGroundImGuiRenderer>();
			m_battleGroundImGui->SetPresenter(&m_battleGroundUi);
			// CMANGOS.36 (Phase 5.36 step 3+4) — Renderer ImGui du panneau
			// OutdoorPvp. Visible uniquement quand m_outdoorPvpVisible
			// (toggle via /pvp ou touche P).
			m_outdoorPvpImGui = std::make_unique<engine::render::OutdoorPvpImGuiRenderer>();
			m_outdoorPvpImGui->SetPresenter(&m_outdoorPvpUi);
			// CMANGOS.42 (Phase 4.42 step 3+4) — Renderer ImGui Weather.
			// Le panel principal n'est visible que quand m_weatherVisible
			// (toggle via /weather ou touche Y). Le HUD top-right est
			// rendu independamment des que activeZoneId est set sur le
			// presenter (selectionne via le bouton "Set Active" du panel).
			m_weatherImGui = std::make_unique<engine::render::WeatherImGuiRenderer>();
			m_weatherImGui->SetPresenter(&m_weatherUi);
			// CMANGOS.31 (Phase 5.31 step 3+4) — Renderer ImGui GameEvents.
			// Le panel principal n'est visible que quand m_gameEventVisible
			// (toggle via /events ou touche E). Le toast 5s sur dernier
			// StateChange reçu est rendu independamment du flag (peut
			// arriver panneau ferme).
			m_gameEventImGui = std::make_unique<engine::render::GameEventImGuiRenderer>();
			m_gameEventImGui->SetPresenter(&m_gameEventUi);
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Renderer ImGui
			// Guildes. Le panel principal n'est visible que quand
			// m_guildVisible (toggle via /guild ou touche U). Le toast 5s
			// sur dernier MotdUpdate reçu est rendu independamment du flag.
			m_guildImGui = std::make_unique<engine::render::GuildImGuiRenderer>();
			m_guildImGui->SetPresenter(&m_guildUi);
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Renderer
			// ImGui Hotel des Ventes. Le panel principal n'est visible
			// que quand m_auctionHouseVisible (toggle via /ah ou touche
			// H). Les toasts 5s sur derniere bid + dernier
			// AuctionExpired sont rendus independamment du flag.
			m_auctionHouseImGui = std::make_unique<engine::render::AuctionImGuiRenderer>();
			m_auctionHouseImGui->SetPresenter(&m_auctionHouseUi);

			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Renderer ImGui Loot
			// Roll. Le panel principal n'est visible que quand
			// m_lootRollVisible (toggle via /loot ou touche L). Le toast
			// 5s sur dernier RollResult reçu est rendu independamment.
			m_lootRollImGui = std::make_unique<engine::render::LootRollImGuiRenderer>();
			m_lootRollImGui->SetPresenter(&m_lootRollUi);
			// M43.4 — Editor Hub overlay : créé inconditionnellement, ne s'affiche que
			// si --editor est actif (cf. condition Render branch plus bas).
			CreateEditorHubImGui();
		}
		else
		{
			m_imguiOverlay.reset();
		}
	}
#endif
#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::RenderAuthImGuiFrame()
	{
		float dw = static_cast<float>(std::max(1, m_width));
		float dh = static_cast<float>(std::max(1, m_height));
		if (m_vkSwapchain.IsValid())
		{
			const VkExtent2D extUi = m_vkSwapchain.GetExtent();
			if (extUi.width > 0 && extUi.height > 0)
			{
				dw = static_cast<float>(extUi.width);
				dh = static_cast<float>(extUi.height);
			}
		}
		// Phase 2 — aperçu 3D : avance l'anim/orbit puis rend l'avatar dans
		// l'image offscreen AVANT que la draw list ImGui n'échantillonne la
		// texture (RenderOffscreen est autonome : submit + wait idle). No-op
		// si pas de mesh/pipeline forward -> l'image garde le clear de Init.
		{
			const float previewNowSec = EngineNowSec();
			float previewDt = (m_racePreviewLastNowSec > 0.0f)
				? (previewNowSec - m_racePreviewLastNowSec) : 0.0f;
			m_racePreviewLastNowSec = previewNowSec;
			if (previewDt < 0.0f)  previewDt = 0.0f;
			if (previewDt > 0.1f)  previewDt = 0.1f; // clamp gros hitch
			// L'écran de création garde la rotation auto (la fenêtre Personnage la
			// coupe ; on la réactive ici au cas où elle a servi avant).
			m_racePreviewViewport.SetAutoOrbit(true);
			m_racePreviewViewport.Tick(previewDt);
			m_racePreviewViewport.RenderOffscreen();
		}

		const engine::client::AuthUiPresenter::VisualState authVsImgui = m_authUi.GetVisualState();
		const engine::client::AuthUiPresenter::RenderModel authRmImgui = m_authUi.BuildRenderModel();
		m_authImGui->Render(authVsImgui, authRmImgui, dw, dh);
		// Chat HUD desactive sur les ecrans pre-EnterWorld (auth/ShardPick/
		// CharacterSelect/CharacterCreate) suite a retour utilisateur "on laisse
		// tomber pour le moment, l'affichage du CHAT juste apres l'authentification".
		// Le chat reapparaitra une fois la branche !authGateActive prise (in-game).
		UpdateUiAudio(/*inGame*/ false); // SND.3 — clics des écrans de connexion
		ImGui::Render();
	}
#endif

#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::RenderInGameImGuiFrame(engine::RenderState& out)
	{
		// Phase 3.11.1 — Rendu du panneau chat. NewFrame déjà appelé plus haut via
		// chatImguiOverlayNewFrame. ImGui::Render finalise la draw list pour le RecordToBackbuffer.
		float dw = static_cast<float>(std::max(1, m_width));
		float dh = static_cast<float>(std::max(1, m_height));
		if (m_vkSwapchain.IsValid())
		{
			const VkExtent2D extUi = m_vkSwapchain.GetExtent();
			if (extUi.width > 0 && extUi.height > 0)
			{
				dw = static_cast<float>(extUi.width);
				dh = static_cast<float>(extUi.height);
			}
		}
		// `inWorldShard` = true uniquement post-EnterWorld : ajoute le canal Zone.
		m_chatImGui->Render(dw, dh, m_authUi.IsInWorldShard());
#if defined(_WIN32)
		// Boussole HUD : visible en jeu. Forward caméra = row 2 du view matrix.
		// L'aiguille rouge pointe le Nord et tourne quand le joueur pivote.
		// Masquée avec le radar/tracker par la bascule HUD (m_hudMapClusterHidden,
		// touche controls.keybind.hud_toggle).
		if (m_authUi.IsInWorldShard() && !m_hudMapClusterHidden)
		{
			const uint32_t rIdxHud = m_renderReadIndex.load(std::memory_order_acquire);
			const engine::RenderState& rsHud = m_renderStates[rIdxHud];
			const engine::render::DayNightCycle::State& dnHud = m_dayNight.GetState();
			engine::render::DrawCompassHud(
				rsHud.viewMatrix.m[2], rsHud.viewMatrix.m[10],
				dnHud.timeOfDay, dnHud.isDaytime,
				dw, dh);
		}
#endif
		// Cellule de dialogue PNJ : fenêtre centrale dédiée (no-op si inactif).
		// Partage la frame ImGui en cours (NewFrame déjà appelé plus haut).
		if (m_dialogueImGui && m_dialogue.IsActive())
			m_dialogueImGui->Render(m_dialogue, dw, dh);
		// SP2 Task 5 — Journal + tracker + panneau donneur. No-op si non bindé
		// (BindQuestUi est appelé au boot, cf. plus haut) ou si giverList/
		// journalEntries sont vides (rien à afficher).
		// Radar : alimente le presenter avec la position CLIENT-AUTORITAIRE du
		// joueur AVANT le rendu. Le serveur exclut le joueur local de son propre
		// snapshot (AoI self-skip) -> UIModel.playerStats.position reste figée au
		// spawn et le radar ne suivait jamais le joueur (retour joueur 2026-07-07).
		// In-world uniquement (contrôleur valide). UpdatePlayerWorldPosition ne
		// reconstruit le radar que si le déplacement dépasse ~5 cm.
		if (m_authUi.IsInWorldShard())
		{
			const engine::math::Vec3 radarPlayerPos = m_characterController.GetPosition();
			m_questUi.UpdatePlayerWorldPosition(radarPlayerPos.x, radarPlayerPos.z);
		}
		// PR-B — quand un dialogue PNJ est actif, on supprime le panneau donneur
		// séparé : l'acceptation/rendu se fait DANS la conversation (boutons injectés
		// par DialogueImGuiRenderer). Le panneau reste le fallback hors dialogue.
		if (m_questImGui)
		{
			m_questImGui->SetGiverPanelSuppressed(m_dialogue.IsActive());
			m_questImGui->SetMinimapZoomIndex(m_minimapZoomIndex);
			m_questImGui->Render(dw, dh, m_authUi.IsInWorldShard(), !m_hudMapClusterHidden);
		}
		// Marqueurs ImGui des interactibles : label flottant projete (visibilite v1
		// sans mesh). Surligne + " [E]" si a portee. Cf. m_interactables (#39/#40).
		{
			const int ivw = static_cast<int>(dw);
			const int ivh = static_cast<int>(dh);
			ImDrawList* fg = ImGui::GetForegroundDrawList();
			// Z-order : un menu modal (pause / options) doit recouvrir TOUT le monde.
			// Les labels world-space (interactibles, plaques de nom) sont traces sur la
			// foreground draw list ImGui, qu'ImGui composite TOUJOURS au-dessus des fenetres
			// (dont le panneau du menu pause, qui est une simple fenetre). On suspend donc
			// ces labels quand un menu modal est ouvert, sinon ils perforent le menu.
			// Les plaques/labels monde (nameplates mobs, noms joueurs, butin) passent
			// par le foreground draw list, donc AU-DESSUS des fenetres ImGui. On les
			// masque des qu'un menu OU un panneau in-game est ouvert, sinon ils
			// debordent par-dessus et rendent le panneau illisible (ex. le label
			// « Sanglier des collines » au milieu de l'arbre de competences).
			const bool worldOverlaysHidden = m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible
				|| (m_characterWindowImGui && m_characterWindowImGui->IsVisible())
				|| m_auctionHouseVisible || m_arenaVisible || m_battleGroundVisible
				|| m_outdoorPvpVisible || m_guildVisible || m_lootRollVisible
				|| m_craftingVisible || m_advancedCombatVisible
				|| m_weatherVisible;
			RenderInteractableMarkersImGui(out, ivw, ivh, fg, worldOverlaysHidden);
			RenderServiceSignTooltipImGui(out, ivw, ivh, fg, worldOverlaysHidden);
			// TD.4 — Plaques de nom au-dessus des avatars joueurs distants.
			// Source : `m_uiModelBinding.GetModel().remoteEntities` (peuple par
			// ApplySnapshot, cf. TD.1) ; le serveur expose `playerClientId` ≠ 0
			// uniquement pour les joueurs (mobs/lootbags = 0 → pas de plaque).
			// Position : meme pattern que RecordRemoteAvatars — repli sur le
			// snapshot brut si l'etat lisse n'existe pas encore (graceful) ;
			// le `+ 1.2` est calibre sur la hauteur d'avatar (`py - 0.9` = pieds,
			// donc tete ~ `py + 0.9`, plaque au-dessus = `py + 1.2`).
			// FIX.10 — désélection automatique : une cible sortie du champ de vision
			// (distance XZ > client.nameplate.vision_distance_m) est abandonnée, quel
			// que soit son type. Fait à chaque frame, même quand les plaques sont
			// masquées par un panneau.
			const engine::client::NameplateVisibilityParams plateParams = CurrentNameplateParams();
			{
				const engine::client::UIModel& selModel = m_uiModelBinding.GetModel();
				if (selModel.targetStats.hasTarget)
				{
					const engine::math::Vec3 selPlayerPos = m_characterController.GetPosition();
					for (const engine::client::UIRemoteEntity& re : selModel.remoteEntities)
					{
						if (re.entityId != selModel.targetStats.entityId)
							continue;
						const float sdx = re.positionX - selPlayerPos.x;
						const float sdz = re.positionZ - selPlayerPos.z;
						if (engine::client::ShouldAutoDeselect(std::sqrt(sdx * sdx + sdz * sdz), plateParams))
						{
							LOG_DEBUG(Core, "[Nameplate] cible {} hors du champ de vision : deselection", re.entityId);
							m_uiModelBinding.ClearLocalTarget();
						}
						break;
					}
				}
			}
			RenderNameplatesImGui(out, ivw, ivh, fg, worldOverlaysHidden, plateParams);
			// =============================================================
			// Combat SP2 — ciblage (clic / Tab), attaque (T), cadre cible,
			// log combat HUD, panneau avance (J) et ecran de mort. Touches
			// declarees dans le registre de binds combat (cf. Engine.h).
			// Libelles ASCII : la police in-game (Windlass) n'a pas tous
			// les glyphes accentues (convention du menu pause).
			// =============================================================
			if (m_gameplayNetInitialized && m_authUi.IsInWorldShard())
			{
				const engine::client::UIModel& uiModel = m_uiModelBinding.GetModel();
				const bool localDead = (uiModel.playerStats.stateFlags & 1u) != 0u;
				const bool chatFocus = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
				const bool menuOpen = m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible;
				const bool keysAllowed = !menuOpen && !chatFocus && !localDead
					&& !ImGui::GetIO().WantCaptureKeyboard;
				const bool mouseAllowed = !menuOpen && !chatFocus && !localDead
					&& !ImGui::GetIO().WantCaptureMouse;

				// --- Selection / attaque a la souris. Pick ecran-espace du mob
				// vivant le plus proche du curseur (rayon kPickRadiusPx), sans
				// raycast 3D. Factorise : utilise par le clic GAUCHE (selection)
				// et par le clic DROIT relache sans drag (selection + attaque
				// auto — le drag droit reste le mouselook camera).
				// Validation v12 — rayon élargi (40 → 70 px : le joueur clique le
				// CORPS du mob, pas le point d'ancrage projeté) et ancre abaissée
				// vers le torse (+0.2 m au lieu de +0.5).
				constexpr float kPickRadiusPx = 70.0f;
				const auto pickMobAtScreen = [&](const ImVec2& screenPos) -> engine::server::EntityId
				{
					engine::server::EntityId bestId = 0;
					float bestPickDistSq = kPickRadiusPx * kPickRadiusPx;
					// FIX.10 — un mob hors du champ de vision n'est pas sélectionnable
					// (il serait désélectionné à la frame suivante).
					const float pickVision = CurrentNameplateParams().visionDistanceM;
					const engine::math::Vec3 pickPlayerPos = m_characterController.GetPosition();
					for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
					{
						if (re.archetypeId == 0u || (re.stateFlags & 1u) != 0u
							|| re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
							continue;
						{
							const float pvx = re.positionX - pickPlayerPos.x;
							const float pvz = re.positionZ - pickPlayerPos.z;
							if (pvx * pvx + pvz * pvz > pickVision * pickVision)
								continue;
						}
						float wx = re.positionX, wy = re.positionY, wz = re.positionZ;
						const auto sit = m_remoteSmoothed.find(re.entityId);
						if (sit != m_remoteSmoothed.end() && sit->second.valid)
						{
							wx = sit->second.x; wy = sit->second.y; wz = sit->second.z;
						}
						wy = ResolveRemoteDisplayCenterY(false, wy, wx, wz);
						float sxp = 0.0f, syp = 0.0f;
						if (!WorldToScreenPx(out.viewProjMatrix.m, wx, wy + 0.2f, wz, ivw, ivh, sxp, syp))
							continue;
						const float dxPick = sxp - screenPos.x;
						const float dyPick = syp - screenPos.y;
						const float distSq = dxPick * dxPick + dyPick * dyPick;
						if (distSq < bestPickDistSq)
						{
							bestPickDistSq = distSq;
							bestId = re.entityId;
						}
					}
					// Diagnostic du pick (selection souris rapportee inoperante) :
					// une ligne par clic monde — a lire dans le log client.
					LOG_INFO(Core,
						"[CombatPick] clic=({:.0f},{:.0f}) picked_entity_id={} best_dist_px={:.1f} entites={}",
						screenPos.x, screenPos.y, bestId,
						(bestId != 0) ? std::sqrt(bestPickDistSq) : -1.0f,
						static_cast<int>(uiModel.remoteEntities.size()));
					return bestId;
				};

				if (mouseAllowed && m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const ImVec2 mousePos = ImGui::GetIO().MousePos;
					// Groupes SP1 — un clic sur un cadre de groupe est une sélection
					// d'allié (gérée plus bas), jamais un pick de mob.
					bool overPartyFrame = false;
					if (uiModel.inParty)
					{
						const engine::client::PartyHudState& partyPickState = m_partyHud.GetState();
						for (size_t pickFrame = 0; pickFrame < engine::client::kMaxPartyFrames; ++pickFrame)
						{
							const engine::client::PartyMemberFrame& pframe = partyPickState.frames[pickFrame];
							if (!pframe.visible)
							{
								continue;
							}
							if (mousePos.x >= pframe.frameBounds.x
								&& mousePos.x <= pframe.frameBounds.x + pframe.frameBounds.width
								&& mousePos.y >= pframe.frameBounds.y
								&& mousePos.y <= pframe.frameBounds.y + pframe.frameBounds.height)
							{
								overPartyFrame = true;
								break;
							}
						}
					}
					if (!overPartyFrame)
					{
						const engine::server::EntityId pickedId = pickMobAtScreen(mousePos);
						if (pickedId != 0)
						{
							// Validation v12 (décision design) — le clic GAUCHE sur un
							// mob cible ET attaque (un coup), comme le clic droit bref :
							// le combat se joue entièrement à la souris.
							(void)m_uiModelBinding.SetLocalTarget(pickedId);
							if (m_attackSendCooldownSec <= 0.0f)
							{
								const uint32_t lmbClientId = m_gameplayUdp.ServerClientId();
								if (lmbClientId != 0u)
								{
									(void)m_gameplayUdp.SendAttackRequest(lmbClientId, pickedId);
									m_attackSendCooldownSec = 0.25f;
									constexpr float kMeleeRangeHintMetersLmb = 4.0f;
									const engine::math::Vec3 playerPosLmb = m_characterController.GetPosition();
									for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
									{
										if (re.entityId != pickedId)
											continue;
										const float dxl = re.positionX - playerPosLmb.x;
										const float dzl = re.positionZ - playerPosLmb.z;
										if ((dxl * dxl + dzl * dzl)
											> kMeleeRangeHintMetersLmb * kMeleeRangeHintMetersLmb)
										{
											m_outOfRangeHintSec = 1.2f;
										}
										break;
									}
								}
							}
						}
					}
				}

				// --- Clic droit « court » = cibler + ATTAQUE AUTO (le drag droit
				// reste le mouselook caméra : on discrimine au relâchement par la
				// dérive du curseur depuis la pression).
				constexpr float kRmbClickMaxDriftPx = 6.0f;
				if (mouseAllowed && m_input.WasMousePressed(engine::platform::MouseButton::Right))
				{
					const ImVec2 rmbPos = ImGui::GetIO().MousePos;
					m_rmbPressMouseX = rmbPos.x;
					m_rmbPressMouseY = rmbPos.y;
					m_rmbClickCandidate = true;
				}
				if (m_input.WasMouseReleased(engine::platform::MouseButton::Right))
				{
					const ImVec2 rmbPos = ImGui::GetIO().MousePos;
					const float driftX = rmbPos.x - m_rmbPressMouseX;
					const float driftY = rmbPos.y - m_rmbPressMouseY;
					const bool isClick = m_rmbClickCandidate && mouseAllowed
						&& (driftX * driftX + driftY * driftY)
							<= kRmbClickMaxDriftPx * kRmbClickMaxDriftPx;
					m_rmbClickCandidate = false;
					if (isClick)
					{
						const engine::server::EntityId pickedId = pickMobAtScreen(rmbPos);
						if (pickedId != 0)
						{
							// Validation v12 (décision design) — PAS d'attaque auto :
							// chaque clic droit = cibler + UN coup (le serveur revalide
							// portée/cooldown ; indication « Hors de portee » si loin).
							(void)m_uiModelBinding.SetLocalTarget(pickedId);
							if (m_attackSendCooldownSec <= 0.0f)
							{
								const uint32_t rmbClientId = m_gameplayUdp.ServerClientId();
								if (rmbClientId != 0u)
								{
									(void)m_gameplayUdp.SendAttackRequest(rmbClientId, pickedId);
									m_attackSendCooldownSec = 0.25f;
									constexpr float kMeleeRangeHintMetersRmb = 4.0f;
									const engine::math::Vec3 playerPosRmb = m_characterController.GetPosition();
									for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
									{
										if (re.entityId != pickedId)
											continue;
										const float dxr = re.positionX - playerPosRmb.x;
										const float dzr = re.positionZ - playerPosRmb.z;
										if ((dxr * dxr + dzr * dzr)
											> kMeleeRangeHintMetersRmb * kMeleeRangeHintMetersRmb)
										{
											m_outOfRangeHintSec = 1.2f;
										}
										break;
									}
								}
							}
						}
					}
				}

				// --- Tab : cycle des mobs vivants par distance croissante au joueur.
				HandleTabTargetingImGui(out, uiModel, keysAllowed);

				// --- T : attaque de la cible courante. Throttle local anti-spam
				// uniquement : portee, cooldown reel et cible vivante sont
				// revalides par le serveur (HandleAttackRequest).
				if (keysAllowed && m_input.WasPressed(engine::platform::Key::T)
					&& uiModel.targetStats.hasTarget
					&& (uiModel.targetStats.stateFlags & 1u) == 0u
					&& m_attackSendCooldownSec <= 0.0f)
				{
					const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
					if (gameplayClientId != 0u)
					{
						(void)m_gameplayUdp.SendAttackRequest(gameplayClientId, uiModel.targetStats.entityId);
						m_attackSendCooldownSec = 0.25f;
						// Validation v12 — le rejet « hors de portée » du serveur est
						// SILENCIEUX (aucun message wire). Indication locale : si la
						// cible est au-delà de la portée de mêlée (4 m, doit suivre
						// kDefaultAttackRangeMeters de ServerApp.cpp), on affiche
						// « Hors de portee » 1,2 s sous le cadre cible. Le serveur
						// reste l'autorité : la requête est envoyée quand même.
						constexpr float kMeleeRangeHintMeters = 4.0f;
						const engine::math::Vec3 playerPosAtk = m_characterController.GetPosition();
						for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
						{
							if (re.entityId != uiModel.targetStats.entityId)
								continue;
							const float dxa = re.positionX - playerPosAtk.x;
							const float dza = re.positionZ - playerPosAtk.z;
							if ((dxa * dxa + dza * dza) > kMeleeRangeHintMeters * kMeleeRangeHintMeters)
							{
								m_outOfRangeHintSec = 1.2f;
							}
							break;
						}
					}
				}

				// --- J : panneau combat avance (DPS meter + log filtrable).
				if (keysAllowed && m_input.WasPressed(engine::platform::Key::J))
				{
					m_advancedCombatVisible = !m_advancedCombatVisible;
				}

				// --- M : carte du monde (hors hôtel des ventes, où M filtre les objets)
				// + bannière « Vous entrez à … » au changement de ville / région.
				if (keysAllowed && !uiModel.auction.isOpen && m_input.WasPressed(engine::platform::Key::M))
				{
					m_worldMapVisible = !m_worldMapVisible;
					// FIX.11 — chaque ouverture repart de la vue par défaut :
					// recentrée sur le personnage, ≈ 2 km visibles.
					if (m_worldMapVisible)
						m_worldMapView = engine::render::WorldMapViewState{};
				}
				DrawWorldPlacesHud(menuOpen ? nullptr : ImGui::GetForegroundDrawList(), dw, dh, ImGui::GetIO().DeltaTime);

				// Chantier 1 — la touche I est LIBÉRÉE : l'inventaire est désormais
				// l'onglet Personnage de la fenêtre unifiée (ouverte par F1).

				// --- Cadre cible (haut-centre) : nom + barre de PV. Les PV de la
				// cible suivent les snapshots (cf. UIModelBinding::ApplySnapshot).
				// On masque le cadre dès que la cible est morte (bit 1 de stateFlags) :
				// inutile de laisser traîner « (MORT) » tant qu'aucune autre cible n'est
				// sélectionnée — ça encombrait le HUD (retour joueur 2026-07-04).
				RenderTargetFrameImGui(dw, fg, uiModel);

				const engine::client::CombatHudState& hudState = m_combatHud.GetState();

				// NB : les jauges PV/ressource du joueur sont rendues en bas-centre
				// par le bloc « Validation v12 » plus bas (uiModel.playerStats) — ne
				// PAS les redessiner ici depuis les bounds du presenter (bas-gauche),
				// qui chevauchent le panneau de chat.

				// --- Log combat HUD (bas-droite) : dernieres lignes formatees par
				// le CombatHudPresenter (suffixes critique/rate inclus).
				if (!hudState.combatLogLines.empty())
				{
					constexpr size_t kHudLogMaxLines = 6u;
					const size_t lineCount = std::min(hudState.combatLogLines.size(), kHudLogMaxLines);
					const float lineH = ImGui::GetTextLineHeight() + 2.0f;
					float ly = dh - 140.0f - lineH * static_cast<float>(lineCount);
					for (size_t li = hudState.combatLogLines.size() - lineCount; li < hudState.combatLogLines.size(); ++li)
					{
						const engine::client::HudCombatLogLine& logLine = hudState.combatLogLines[li];
						const ImU32 col = logLine.incoming ? IM_COL32(255, 140, 120, 230) : IM_COL32(220, 220, 180, 230);
						const ImVec2 lts = ImGui::CalcTextSize(logLine.text.c_str());
						fg->AddText(ImVec2(dw - lts.x - 24.0f, ly), col, logLine.text.c_str());
						ly += lineH;
					}
				}

				// --- Panneau combat avance (J) : DPS meter + log filtrable (M39.4).
				RenderAdvancedCombatPanelImGui(dw);

				// --- Combat SP3 : barre d'action (touches 1-4, sorts du profil).
				// Cooldowns affichés localement (cosmétique) ; le serveur revalide
				// kit/cooldown/coût/cible/portée à chaque CastRequest.
				// SP-C — kit effectif : compétences de classe connues (fallback kit profil).
				// BuildEffectiveKit retourne un vecteur valide pour toute la durée du bloc.
				auto BuildEffectiveKit = [&]() -> std::vector<engine::client::SpellDisplay>
				{
					const std::string& classId    = uiModel.classId;
					const std::string& profileId  = uiModel.playerStats.profileId;
					const std::vector<std::string>& knownIds = uiModel.knownSkillIds;
					// Chemin compétences de classe : classId connu + skills connus + catalogue disponible.
					if (!classId.empty() && !knownIds.empty())
					{
						const std::vector<engine::client::ClassSkillDisplay>* classSkills =
							m_classSkillCatalog.GetClassSkills(classId);
						if (classSkills != nullptr)
						{
							std::vector<engine::client::SpellDisplay> kit;
							kit.reserve(knownIds.size());
							for (const std::string& skillId : knownIds)
							{
								for (const engine::client::ClassSkillDisplay& cs : *classSkills)
								{
									if (cs.skillId == skillId)
									{
										kit.push_back(engine::client::ToSpellDisplay(cs, classId));
										break;
									}
								}
							}
							if (!kit.empty())
							{
								return kit;
							}
						}
					}
					// Fallback : kit profil depuis le catalogue de sorts (comportement original).
					if (!profileId.empty())
					{
						const std::vector<engine::client::SpellDisplay>* profileKit =
							m_spellCatalog.FindKit(profileId);
						if (profileKit != nullptr)
						{
							return *profileKit;
						}
					}
					return {};
				};
				const std::vector<engine::client::SpellDisplay> effectiveKit = BuildEffectiveKit();
				// FIX.1 — la barre est TOUJOURS dessinée : sans kit (classe et
				// compétences pas encore reçues du shard, ex. pas de Welcome UDP),
				// ses 10 cases apparaissent vides avec leurs touches au lieu de
				// disparaître. Les envois restent conditionnés à ServerClientId() != 0.
				RenderActionBarAndBeltImGui(dw, dh, fg, uiModel, keysAllowed, effectiveKit);

				// --- Combat SP3 : BuffBar (M31.2 enfin câblée) — auras du joueur
				// (au-dessus de la barre d'action) et de la cible (sous le cadre).
				RenderBuffBarsImGui(out, dw, dh, ivw, ivh, fg, uiModel);

				// --- Groupes SP1 : cadres de groupe (PartyHudPresenter M32.2 enfin
				// câblé) + ciblage allié au clic (consommé par les sorts SingleAlly).
				RenderPartyFramesImGui(fg, uiModel, mouseAllowed);

				// --- Groupes SP1 : popup d'invitation (Accepter / Refuser).
				// Modale souris : affichée même pendant un cast ou un menu.
				if (uiModel.partyInvite.pending)
				{
					const float inviteW = 380.0f;
					const float inviteH = 140.0f;
					ImGui::SetNextWindowPos(ImVec2((dw - inviteW) * 0.5f, dh * 0.22f), ImGuiCond_Always);
					ImGui::SetNextWindowSize(ImVec2(inviteW, inviteH), ImGuiCond_Always);
					ImGui::SetNextWindowBgAlpha(0.96f);
					ImGui::Begin("##ln_party_invite", nullptr,
						ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
						| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
					const std::string inviteText = uiModel.partyInvite.inviterName + " vous invite dans un groupe";
					const float inviteTextW = ImGui::CalcTextSize(inviteText.c_str()).x;
					ImGui::SetCursorPosX((inviteW - inviteTextW) * 0.5f);
					ImGui::TextUnformatted(inviteText.c_str());
					ImGui::Separator();
					ImGui::Spacing();
					const uint32_t inviteClientId = m_gameplayUdp.ServerClientId();
					ImGui::SetCursorPosX((inviteW - 2.0f * 150.0f - 12.0f) * 0.5f);
					if (ImGui::Button("Accepter", ImVec2(150.0f, 36.0f)) && inviteClientId != 0u)
					{
						(void)m_gameplayUdp.SendPartyAccept(inviteClientId);
						m_uiModelBinding.ClearPartyInvite();
					}
					ImGui::SameLine();
					if (ImGui::Button("Refuser", ImVec2(150.0f, 36.0f)) && inviteClientId != 0u)
					{
						(void)m_gameplayUdp.SendPartyDecline(inviteClientId);
						m_uiModelBinding.ClearPartyInvite();
					}
					ImGui::End();
				}


				// (Le cercle de sélection provisoire — anneau + cône de vision en
				// ImGui foreground — a été remplacé par TargetReticleSystem :
				// decal orienté au sol, mis à jour chaque frame côté Update.)

				// Marqueurs de reapparition (cimetiere/auberge) : le rendu placeholder
				// (label "Auberge"/"Cimetiere" + anneau au sol sur la foreground draw
				// list) a ete RETIRE. Ces lieux sont desormais materialises par du DECOR
				// physique pose dans world.scenery (cf. config.json, positions issues de
				// respawn/respawn_points.txt : cimetiere ~120,120 ; auberge ~88,100).
				// m_respawnMarkers reste charge par LoadRespawnMarkers() (donnees) mais
				// n'est plus dessine cote client.

				// --- Validation v12 : ramassage du butin — touche F sur le sac
				// le plus proche (~3 m, le serveur revalide). L'InventoryDelta
				// de retour est déjà routé (UIModelBinding) : les objets
				// apparaissent dans l'inventaire, le serveur despawne le sac.
				{
					const engine::math::Vec3 playerPosLoot = m_characterController.GetPosition();
					uint64_t nearestBagId = 0;
					float nearestBagDistSq = 3.0f * 3.0f;
					for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
					{
						if (re.playerClientId != 0u || re.archetypeId != 0u)
							continue; // seuls les sacs de butin ont les deux ids à 0.
						const float dxb = re.positionX - playerPosLoot.x;
						const float dzb = re.positionZ - playerPosLoot.z;
						const float distSqB = dxb * dxb + dzb * dzb;
						if (distSqB < nearestBagDistSq)
						{
							nearestBagDistSq = distSqB;
							nearestBagId = re.entityId;
						}
					}
					if (keysAllowed && nearestBagId != 0ull
						&& m_input.WasPressed(engine::platform::Key::F))
					{
						const uint32_t lootClientId = m_gameplayUdp.ServerClientId();
						if (lootClientId != 0u)
							(void)m_gameplayUdp.SendPickupRequest(lootClientId, nearestBagId);
					}
				}

				// --- v1.0.125 : bateaux — touche E près d'un navire à quai (embarquer), ou à
				// bord quand il est à quai (débarquer) ; les interactibles locaux (capitaine
				// du port…) gardent la priorité. Le shard tranche (quai, distance, or).
				{
					m_boardableBoatEntityId = 0;
					const engine::math::Vec3 pBoat = m_characterController.GetPosition();
					float bestBoat = static_cast<float>(engine::world::boat::kBoardRadiusM - 5.0);
					bestBoat *= bestBoat;
					bool aboardDocked = false;
					for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
					{
						if (!engine::server::IsBoatArchetype(re.archetypeId)
							|| (re.stateFlags & engine::server::kEntityStateBoatDocked) == 0u)
							continue;
						if (re.entityId == m_aboardBoatEntityId) { aboardDocked = true; continue; }
						const float dxb = re.positionX - pBoat.x, dzb = re.positionZ - pBoat.z;
						if (m_aboardBoatEntityId == 0u && dxb * dxb + dzb * dzb < bestBoat)
						{
							bestBoat = dxb * dxb + dzb * dzb;
							m_boardableBoatEntityId = re.entityId;
						}
					}
					if (keysAllowed && m_interactableInRange < 0 && m_gameplayNetInitialized
						&& m_input.WasPressed(engine::platform::Key::E))
					{
						const uint32_t boatClientId = m_gameplayUdp.ServerClientId();
						if (boatClientId != 0u && m_aboardBoatEntityId != 0u && aboardDocked)
							(void)m_gameplayUdp.SendBoatBoardRequest(boatClientId, m_aboardBoatEntityId, false);
						else if (boatClientId != 0u && m_boardableBoatEntityId != 0u)
							(void)m_gameplayUdp.SendBoatBoardRequest(boatClientId, m_boardableBoatEntityId, true);
					}
				}

				// --- Métiers SP1 : récolte — touche E sur le node disponible le
				// plus proche (~5 m) ; les interactibles locaux gardent la priorité.
				{
					const engine::math::Vec3 playerPosHarvest = m_characterController.GetPosition();
					uint64_t nearestNodeId = 0;
					float nearestNodeDistSq = 25.0f; // 5 m de portée de récolte V1.
					for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
					{
						if (!engine::server::IsGatheringNodeArchetype(re.archetypeId))
							continue;
						if ((re.stateFlags & 1u) != 0u)
							continue; // épuisé
						const float dxn = re.positionX - playerPosHarvest.x;
						const float dzn = re.positionZ - playerPosHarvest.z;
						const float distSqN = dxn * dxn + dzn * dzn;
						if (distSqN < nearestNodeDistSq)
						{
							nearestNodeDistSq = distSqN;
							nearestNodeId = re.entityId;
						}
					}
					if (keysAllowed && nearestNodeId != 0ull
						&& m_interactableInRange < 0
						&& !uiModel.harvest.inProgress
						&& m_input.WasPressed(engine::platform::Key::E))
					{
						const uint32_t harvestClientId = m_gameplayUdp.ServerClientId();
						if (harvestClientId != 0u)
							(void)m_gameplayUdp.SendHarvestRequest(harvestClientId, nearestNodeId);
					}
					// Barre de progression de récolte (présentateur M36.1).
					const engine::client::HarvestCastBarState& harvestState = m_harvestBar.GetState();
					if (harvestState.visible)
					{
						fg->AddRectFilled(ImVec2(harvestState.barX, harvestState.barY),
							ImVec2(harvestState.barX + harvestState.barWidth,
								harvestState.barY + harvestState.barHeight),
							IM_COL32(20, 22, 28, 220), 4.0f);
						fg->AddRectFilled(ImVec2(harvestState.barX, harvestState.barY),
							ImVec2(harvestState.barX + harvestState.barWidth
									* std::clamp(harvestState.fillFraction, 0.0f, 1.0f),
								harvestState.barY + harvestState.barHeight),
							IM_COL32(120, 200, 120, 230), 4.0f);
						fg->AddText(ImVec2(harvestState.barX, harvestState.barY - 18.0f),
							IM_COL32(230, 230, 230, 255), harvestState.label.c_str());
					}
				}

				// --- Validation v12 : fenêtre de butin AUTOMATIQUE. S'ouvre seule
				// dès qu'un LootNotify arrive (mort d'un mob avec loot, objets déjà
				// crédités à l'inventaire par le serveur) ; les morts suivantes
				// ABONDENT la même fenêtre (cumul par objet) ; « Fermer » la vide.
				if (uiModel.lootWindow.visible)
				{
					ImGui::SetNextWindowPos(ImVec2(dw - 320.0f, dh * 0.5f - 120.0f), ImGuiCond_FirstUseEver);
					ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_Always);
					bool lootOpen = true;
					if (ImGui::Begin("Butin", &lootOpen,
						ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
					{
						for (const engine::server::ItemStack& lootEntry : uiModel.lootWindow.entries)
						{
							ImGui::TextUnformatted(
								m_invUi.ResolveItemLabel(lootEntry.itemId, lootEntry.quantity).c_str());
						}
						ImGui::Separator();
						if (ImGui::Button("Fermer", ImVec2(-1.0f, 26.0f)))
						{
							lootOpen = false;
						}
					}
					ImGui::End();
					if (!lootOpen)
					{
						m_uiModelBinding.CloseLootWindow();
					}
				}

				// --- Métiers SP1 : panneau d'artisanat (touche K, M36.2/M36.3).
				if (keysAllowed && m_input.WasPressed(engine::platform::Key::K))
				{
					m_craftingVisible = !m_craftingVisible;
					// Première ouverture : charge la liste du premier métier connu.
					if (m_craftingVisible && !uiModel.crafting.professions.empty()
						&& uiModel.crafting.recipes.empty())
					{
						const uint32_t craftClientId = m_gameplayUdp.ServerClientId();
						if (craftClientId != 0u)
							(void)m_gameplayUdp.SendCraftRecipeListRequest(craftClientId,
								uiModel.crafting.professions.front().professionKey);
					}
				}
				RenderCraftingWindowImGui(dw, uiModel);

				// --- FIX.1 : bandeau d'état de la session de jeu. Sans Welcome du
				// shard (UDP bloqué, endpoint mal annoncé, shard arrêté), le HUD
				// ne reçoit ni stats ni sorts : le joueur doit savoir pourquoi.
				// Affiché après 2 s d'attente pour ne pas clignoter à l'entrée.
				{
					const float bannerNowSec = EngineNowSec();
					if (m_gameplayUdp.ServerClientId() != 0u)
					{
						m_gameplayWelcomeWaitSinceSec = -1.0f;
					}
					else
					{
						if (m_gameplayWelcomeWaitSinceSec < 0.0f)
							m_gameplayWelcomeWaitSinceSec = bannerNowSec;
						if (bannerNowSec - m_gameplayWelcomeWaitSinceSec >= 2.0f)
						{
							const bool gaveUp = m_gameplayUdp.HelloFastAttemptsExhausted();
							const char* bannerText = gaveUp
								? "Serveur de jeu injoignable - nouvelle tentative toutes les 5 s (verifiez votre connexion)"
								: "Connexion au serveur de jeu en cours...";
							const ImVec2 bannerTs = ImGui::CalcTextSize(bannerText);
							const float bannerX = (dw - bannerTs.x) * 0.5f;
							const float bannerY = 48.0f;
							fg->AddRectFilled(ImVec2(bannerX - 14.0f, bannerY - 8.0f),
								ImVec2(bannerX + bannerTs.x + 14.0f, bannerY + bannerTs.y + 8.0f),
								gaveUp ? IM_COL32(110, 30, 26, 220) : IM_COL32(40, 46, 60, 210), 6.0f);
							fg->AddText(ImVec2(bannerX, bannerY), IM_COL32(255, 240, 220, 255), bannerText);
						}
					}
				}

				// --- Validation v12 : jauges du joueur (PV + ressource), bas-centre.
				// PV rafraîchis en temps réel par les CombatEvent (chaque coup reçu)
				// et l'événement de résurrection. Avant le premier coup reçu,
				// maxHealth vaut 0 → on affiche la feuille de stats (pleine).
				RenderPlayerGaugesImGui(dw, dh, fg, uiModel);

				// --- Bourse : pièces or / argent / bronze du joueur, coin bas-droit.
				// Alimentée par WalletUpdate (UIModel.wallet.gold), réinterprétée comme
				// un total en BRONZE (unité de base : 100 bronze = 1 argent, 100 argent
				// = 1 or ; retour joueur 2026-07-08). Chaque palier = une pastille teintée
				// + son compte. On masque or/argent tant qu'ils sont nuls (bronze toujours
				// visible). Non concernée par la bascule HUD carte (bourse toujours visible).
				RenderWalletImGui(dw, dh, fg, uiModel);


				// --- Ecran de mort : overlay sombre + bouton Reapparaitre →
				// RespawnRequest (le serveur ne l'honore que si le flag dead est pose ;
				// l'overlay disparait quand le snapshot post-respawn retire le flag).
				if (localDead)
				{
					fg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(dw, dh), IM_COL32(10, 0, 0, 140));
					const float panelW = 360.0f;
					const float panelH = 170.0f;
					ImGui::SetNextWindowPos(ImVec2((dw - panelW) * 0.5f, (dh - panelH) * 0.5f), ImGuiCond_Always);
					ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
					ImGui::SetNextWindowBgAlpha(0.95f);
					ImGui::Begin("##ln_death_overlay", nullptr,
						ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
						| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
					ImGui::SetWindowFontScale(1.3f);
					const char* deadTitle = "VOUS ETES MORT";
					const float deadTitleW = ImGui::CalcTextSize(deadTitle).x;
					ImGui::SetCursorPosX((panelW - deadTitleW) * 0.5f);
					ImGui::TextUnformatted(deadTitle);
					ImGui::SetWindowFontScale(1.0f);
					ImGui::Separator();
					ImGui::Spacing();
					ImGui::Spacing();
					// Validation v12 (wire v13) — deux destinations : cimetière ou
					// auberge la plus proche (le serveur choisit le point du type
					// demandé le plus proche du lieu de mort).
					const float deathBtnW = (panelW - 30.0f) * 0.5f;
					ImGui::SetCursorPosX(10.0f);
					if (ImGui::Button("Cimetiere le plus proche", ImVec2(deathBtnW, 40.0f)))
					{
						const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
						if (gameplayClientId != 0u)
							(void)m_gameplayUdp.SendRespawnRequest(gameplayClientId,
								engine::server::kRespawnDestinationGraveyard);
					}
					ImGui::SameLine();
					if (ImGui::Button("Auberge la plus proche", ImVec2(deathBtnW, 40.0f)))
					{
						const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
						if (gameplayClientId != 0u)
							(void)m_gameplayUdp.SendRespawnRequest(gameplayClientId,
								engine::server::kRespawnDestinationInn);
					}
					ImGui::End();
				}
			}
		}
		// Menu de panneaux : barre de menus ImGui toujours visible en jeu,
		// acces souris a tous les panneaux togglables sans raccourci clavier
		// dedie. Les panneaux gardent par ailleurs leurs touches existantes ;
		// GameEvents n'est plus accessible que par ce menu (touche E liberee).
		// Chaque MenuItem reflete l'etat *Visible et reproduit le RequestList
		// d'ouverture du toggle clavier correspondant.
		RenderMainMenuBarImGui();
		// CMANGOS.18 (Phase 3.18 step 4) — Render du panneau Mail si visible.
		// Le panneau partage la frame ImGui en cours (NewFrame deja appele
		// par chatImguiOverlayNewFrame plus haut). Visible uniquement quand
		// l'utilisateur a fait /mail (toggle dans le SendCallback du chat).
		if (m_mailVisible && m_mailImGui && m_mailUi.IsInitialized())
		{
			m_mailImGui->SetEnabled(true);
			m_mailImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_mailImGui->Render();
		}
		// CMANGOS.32 (Phase 5.32 step 3+4) — Render du panneau Support GM si visible.
		// Le panneau partage la frame ImGui en cours (cf. m_mailImGui ci-dessus).
		if (m_gmTicketsVisible && m_gmTicketImGui && m_gmTicketUi.IsInitialized())
		{
			m_gmTicketImGui->SetEnabled(true);
			m_gmTicketImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_gmTicketImGui->Render();
		}
		// CMANGOS.24 (Phase 3.24 step 3+4) — Tick le toast (expire ~3s) puis
		// render le panneau Reputation si visible. Le toast push est rendu
		// en plus de la liste si actif (overlay non bloquant).
		m_reputationUi.TickToast(static_cast<float>(m_time.DeltaSeconds()));
		if (m_reputationVisible && m_reputationImGui && m_reputationUi.IsInitialized())
		{
			m_reputationImGui->SetEnabled(true);
			m_reputationImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_reputationImGui->Render();
		}
		// Chantier 1 — les caractéristiques (ex-fiche F1) sont désormais dans
		// l'onglet Personnage de la fenêtre unifiée (rendu plus bas). Ancien
		// panneau F1 autonome retiré.
		// CMANGOS.33 (Phase 5.33 step 3+4) — Render du panneau LFG si visible.
		// Le modal proposal s'affiche aussi quand hasProposal == true (meme si
		// le panneau principal est masque), pour que le joueur ne rate pas
		// la formation de groupe.
		if (m_lfgImGui && m_lfgUi.IsInitialized()
			&& (m_lfgVisible || m_lfgUi.GetState().hasProposal))
		{
			m_lfgImGui->SetEnabled(true);
			m_lfgImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_lfgImGui->Render();
		}
		// CMANGOS.30 (Phase 5.30 step 3+4) — Render de l'overlay cinematique
		// (black bars + skip hint) pendant la lecture d'une cinematique.
		// state.isPlaying == false => pas de rendering (no-op interne).
		if (m_cinematicImGui && m_cinematicUi.IsInitialized()
			&& m_cinematicUi.GetState().isPlaying)
		{
			m_cinematicImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_cinematicImGui->Render();
		}
		// CMANGOS.39 (Phase 4.39 step 3+4) — Tick l'indicateur Use puis
		// render le panneau Skill Book si visible. L'indicateur Use est rendu
		// en plus de la liste si actif (overlay non bloquant).
		m_skillBookUi.TickIndicator(static_cast<float>(m_time.DeltaSeconds()));
		// Chantier 1 — le Skill Book est désormais l'onglet Compétences de la
		// fenêtre unifiée (rendu embarqué plus bas). Panneau autonome retiré.
		// Grimoire (Task 13) — Sync du presenter (profil + layout autoritaire)
		// puis render conditionnel si le panneau est ouvert.
		// SP-C — kit effectif : compétences de classe connues (fallback kit profil).
		const engine::client::UIModel& grimoireModel = m_uiModelBinding.GetModel();
		{
			std::vector<engine::client::SpellDisplay> grimoireEffectiveKit;
			const std::string& gClassId   = grimoireModel.classId;
			const std::string& gProfileId = grimoireModel.playerStats.profileId;
			const std::vector<std::string>& gKnownIds = grimoireModel.knownSkillIds;
			if (!gClassId.empty() && !gKnownIds.empty())
			{
				const std::vector<engine::client::ClassSkillDisplay>* gClassSkills =
					m_classSkillCatalog.GetClassSkills(gClassId);
				if (gClassSkills != nullptr)
				{
					grimoireEffectiveKit.reserve(gKnownIds.size());
					for (const std::string& skillId : gKnownIds)
					{
						for (const engine::client::ClassSkillDisplay& cs : *gClassSkills)
						{
							if (cs.skillId == skillId)
							{
								grimoireEffectiveKit.push_back(engine::client::ToSpellDisplay(cs, gClassId));
								break;
							}
						}
					}
				}
			}
			m_grimoireUi.Sync(gProfileId, grimoireModel.playerStats.actionBarLayout, grimoireEffectiveKit);
		}
		// Chantier 1 — le Grimoire est désormais l'onglet Techniques de la fenêtre
		// unifiée (rendu embarqué plus bas). Le Sync ci-dessus reste nécessaire.
		// SP-D — Sync + render conditionnel de l'arbre de compétences par-classe.
		// Niveau réel du perso (capturé à EnterWorld depuis CHARACTER_LIST) : verrouille
		// les paliers > niveau et affiche le bon « Niveau joueur ». Le serveur revalide.
		{
			const engine::client::UIModel& treeModel = m_uiModelBinding.GetModel();
			// Le level-up EN JEU doit débloquer les paliers de l'arbre. Le niveau
			// live vient de PlayerXpUpdate (barre d'XP, playerStats.level), qui suit
			// les montées de niveau ; m_activeCharacterLevel n'était capturé qu'à
			// l'EnterWorld et jamais remis à jour -> l'arbre restait bloqué au niveau
			// d'entrée (retour joueur 2026-07-07 : perso niv. 5 mais arbre « Niveau
			// joueur : 1 », compétences verrouillées). On synchronise donc le niveau
			// actif sur le niveau serveur (monotone ; fallback = EnterWorld si aucun
			// PlayerXpUpdate reçu, ex. serveur ancien).
			if (treeModel.playerStats.hasXp && treeModel.playerStats.level > m_activeCharacterLevel)
				m_activeCharacterLevel = treeModel.playerStats.level;
			m_classSkillTreeUi.Sync(treeModel.classId, treeModel.knownSkillIds, m_activeCharacterLevel);
			// Chantier 1 — l'arbre est désormais l'onglet Arbre de la fenêtre unifiée
			// (rendu embarqué juste après). Le Sync ci-dessus reste nécessaire.
		}
		// Task 4 — aperçu 3D : fait tourner + rend l'avatar du joueur dans l'image
		// offscreen tant que la fenêtre Personnage est ouverte, AVANT que la draw
		// list ImGui n'échantillonne la texture. RenderOffscreen est autonome
		// (submit + wait idle) : coûteux (stall GPU) -> uniquement fenêtre ouverte.
		// Le mesh/genre/teinte sont posés à l'ouverture (toggle F1) ; ici on ne fait
		// qu'avancer l'anim/orbit et rendre.
		if (m_characterWindowImGui && m_characterWindowImGui->IsVisible())
		{
			const float pNow = EngineNowSec();
			float pDt = (m_racePreviewLastNowSec > 0.0f) ? (pNow - m_racePreviewLastNowSec) : 0.0f;
			m_racePreviewLastNowSec = pNow;
			if (pDt < 0.0f) pDt = 0.0f;
			if (pDt > 0.1f) pDt = 0.1f; // clamp gros hitch
			m_racePreviewViewport.Tick(pDt);
			m_racePreviewViewport.RenderOffscreen();
		}
		// Chantier 1 — fenêtre Personnage unifiée à onglets (F1). Rendue APRÈS les
		// Sync du Grimoire et de l'arbre (leurs presenters embarqués lisent l'état à
		// jour). Point single-pass (même bloc que les autres panneaux) -> pas de
		// doublon d'inventaire.
		RenderCharacterWindowImGui(dw, dh);
		// CMANGOS.21 (Phase 5.21 step 3+4) — Render du panneau Arena si
		// visible. Le popup proposal s'affiche aussi quand pendingProposalId
		// est set (meme si le panneau principal est masque), pour que le
		// joueur ne rate pas la formation de match.
		if (m_arenaImGui && m_arenaUi.IsInitialized()
			&& (m_arenaVisible || m_arenaUi.GetState().pendingProposalId.has_value()))
		{
			m_arenaImGui->SetEnabled(true);
			m_arenaImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_arenaImGui->Render();
		}
		// CMANGOS.10 (Phase 5 step 3+4) — Render du panneau BattleGround si
		// visible OU si un match est actif (le scoreboard s'affiche tout
		// seul tant que activeMatchId est set, meme si le panneau principal
		// est masque, pour que le joueur ne rate pas le match push-pousse).
		if (m_battleGroundImGui && m_battleGroundUi.IsInitialized()
			&& (m_battleGroundVisible || m_battleGroundUi.GetState().activeMatchId.has_value()))
		{
			m_battleGroundImGui->SetEnabled(true);
			m_battleGroundImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_battleGroundImGui->Render();
		}
		// CMANGOS.36 (Phase 5.36 step 3+4) — Render du panneau OutdoorPvp
		// si visible. Pas de scoreboard auto-affiche : V1 le panneau est
		// strictement toggle-only via /pvp ou la touche P.
		if (m_outdoorPvpImGui && m_outdoorPvpUi.IsInitialized()
			&& m_outdoorPvpVisible)
		{
			m_outdoorPvpImGui->SetEnabled(true);
			m_outdoorPvpImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_outdoorPvpImGui->Render();
		}
		// CMANGOS.42 (Phase 4.42 step 3+4) — Render du panel Weather si
		// m_weatherVisible (toggle via /weather ou touche Y), ET du HUD
		// top-right si activeZoneId set. Render() lui-meme gere ces
		// 2 cas independamment : on lui passe juste l'enabled flag pour
		// le panel ; le HUD est conditionne par le presenter state.
		if (m_weatherImGui && m_weatherUi.IsInitialized())
		{
			m_weatherImGui->SetEnabled(m_weatherVisible);
			m_weatherImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_weatherImGui->Render();
		}
		// CMANGOS.31 (Phase 5.31 step 3+4) — Render du panel GameEvents
		// si m_gameEventVisible (toggle via /events ou touche E), ET du
		// toast 5s sur dernier StateChange reçu (rendu independamment
		// par Render() si lastChangeTimeMs est recent).
		if (m_gameEventImGui && m_gameEventUi.IsInitialized())
		{
			m_gameEventImGui->SetEnabled(m_gameEventVisible);
			m_gameEventImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_gameEventImGui->Render();
		}
		// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Render du panel Guildes
		// si m_guildVisible (toggle via /guild ou touche U), ET du toast 5s
		// sur dernier MotdUpdate reçu (rendu independamment par Render()
		// si lastMotdChangeTimeMs est recent).
		if (m_guildImGui && m_guildUi.IsInitialized())
		{
			m_guildImGui->SetEnabled(m_guildVisible);
			m_guildImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_guildImGui->Render();
		}
		// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Render du panel
		// Hotel des Ventes si m_auctionHouseVisible (toggle via /ah ou
		// touche H), ET des toasts 5s sur derniere bid + dernier
		// AuctionExpired (rendus independamment par Render() si
		// lastBidTimeMs / lastExpirationTimeMs sont recents).
		if (m_auctionHouseImGui && m_auctionHouseUi.IsInitialized())
		{
			m_auctionHouseImGui->SetEnabled(m_auctionHouseVisible);
			m_auctionHouseImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_auctionHouseImGui->Render();
		}
		// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Render du panneau Loot
		// Roll si m_lootRollVisible (toggle via /loot ou touche L), ET du
		// toast 5s sur dernier RollResult reçu (rendu independamment par
		// Render() si lastResultTimeMs est recent).
		if (m_lootRollImGui && m_lootRollUi.IsInitialized())
		{
			m_lootRollImGui->SetEnabled(m_lootRollVisible);
			m_lootRollImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_lootRollImGui->Render();
		}
		// DIAG chat-only branch (in-game).
		if ((m_currentFrame % 60u) == 0u)
		{
			LOG_INFO(Render, "[ChatDiag-InGameBranch] frame={} dw={} dh={} inWorldShard={} chatFocus={}",
				m_currentFrame, dw, dh, m_authUi.IsInWorldShard(), m_chatUi.IsChatFocusActive());
		}
		// Menu pause in-game superpose au chat HUD : meme branche de rendu pour
		// que le ImGui::Render() finalise les deux draw lists en une seule passe.
		RenderPauseMenuImGui(dw, dh);
		// Menu Options en jeu : on réutilise EXACTEMENT l'écran d'options de l'auth
		// (source unique, spec B2). Le menu Pause (ESC) reste inchangé ; seul son lien
		// « Options » mène ici. RenderOptionsOverlay renvoie false à la fermeture.
		// Garde m_authImGui : aligne sur la défensive des autres panneaux in-game
		// du même bloc (m_mailImGui &&, m_gmTicketImGui &&, …). m_authImGui null
		// (shutdown) -> on referme proprement l'overlay plutôt que de déréférencer.
		if (m_inGameOptionsPanelVisible)
		{
			const bool stillOpen = m_authImGui && m_authImGui->RenderOptionsOverlay(dw, dh);
			if (!stillOpen)
			{
				m_inGameOptionsPanelVisible = false;
				m_authUi.CloseLanguageOptionsInGame();
				m_inGamePauseMenuVisible = true; // retour au menu Pause
				ReloadKeybindsFromDisk(); // prise d'effet live des rebinds
			}
		}
		UpdateUiAudio(/*inGame*/ true); // SND.3 — clics et évènements de jeu
		ImGui::Render();
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::HandleTabTargetingImGui(engine::RenderState& out, const engine::client::UIModel& uiModel, const bool& keysAllowed)
	{
		if (keysAllowed && m_input.WasPressed(engine::platform::Key::Tab))
		{
			const engine::math::Vec3 playerPos = m_characterController.GetPosition();
			// Filtre de visibilite : seuls les ennemis a l'ecran (dans le
			// frustum camera) sont ciblables au Tab — un mob dans le dos ou
			// hors champ ne doit pas etre selectionne. On reutilise la MEME
			// matrice view-projection que le rendu monde de cette frame
			// (out.viewProjMatrix = out.projMatrix * out.viewMatrix, cf. ~9852),
			// column-major (m[col*4+row], cf. Math.h). Pas d'operateur
			// Mat4*Vec4 dans Math.h : on projette le point a la main, comme
			// WorldToScreenPx plus haut. Retourne false si l'ennemi est
			// derriere la camera (w <= ~0) ou hors du carre NDC [-1,1]^2.
			// On vise le torse (y + 1.0 m) plutot que les pieds : un mob dont
			// les pieds tombent juste sous le bord bas mais dont le corps est
			// visible reste ciblable.
			const float* tabVp = out.viewProjMatrix.m;
			auto isOnScreen = [tabVp](float wx, float wy, float wz) -> bool
			{
				const float cx = tabVp[0] * wx + tabVp[4] * wy + tabVp[8] * wz + tabVp[12];
				const float cy = tabVp[1] * wx + tabVp[5] * wy + tabVp[9] * wz + tabVp[13];
				const float cw = tabVp[3] * wx + tabVp[7] * wy + tabVp[11] * wz + tabVp[15];
				if (cw <= 1e-5f)
					return false; // derriere la camera (ou plan near degenere)
				const float invW = 1.0f / cw;
				const float ndcX = cx * invW;
				const float ndcY = cy * invW;
				return ndcX >= -1.0f && ndcX <= 1.0f
					&& ndcY >= -1.0f && ndcY <= 1.0f;
			};
			std::vector<std::pair<float, engine::server::EntityId>> candidates;
			for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
			{
				if (re.archetypeId == 0u || (re.stateFlags & 1u) != 0u
					|| re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
					continue;
				// Monde vivant : les PNJ d'ambiance (villageois, gardes…) ne sont
				// pas des ennemis — jamais ciblés au Tab (le serveur refuse l'attaque).
				// Seconde carte : sauf ceux hostiles à la race du joueur (Légion / humains).
				if (const engine::client::CreatureAppearance* tabApp = m_creatureCatalog.Find(re.archetypeId);
					tabApp != nullptr && tabApp->ambient && !tabApp->IsHostileToRace(m_localRaceId))
					continue;
				// Rejet des ennemis non visibles (hors frustum / dans le dos).
				if (!isOnScreen(re.positionX, re.positionY + 1.0f, re.positionZ))
					continue;
				const float dxc = re.positionX - playerPos.x;
				const float dzc = re.positionZ - playerPos.z;
				// FIX.10 — pas de cible hors du champ de vision (elle serait
				// désélectionnée à la frame suivante).
				const float tabVision = CurrentNameplateParams().visionDistanceM;
				if (dxc * dxc + dzc * dzc > tabVision * tabVision)
					continue;
				candidates.emplace_back(dxc * dxc + dzc * dzc, re.entityId);
			}
			// Diag (sur appui Tab uniquement, non spammy) : si l'utilisateur
			// rapporte « Tab ne selectionne rien », ce log distingue les cas
			// « aucune entite repliquee » / « aucun candidat visible » /
			// « cible deja active ». L'analyse de code n'a revele aucun bug
			// dans le filtre (archetype/mort/node) ni dans la propagation
			// SetLocalTarget -> targetStats ; la cause racine d'une selection
			// vide doit donc etre tranchee en jeu via ce compteur.
			LOG_INFO(Core,
				"[TabTarget] remoteEntities={} candidates_visibles={} hasTarget={}",
				uiModel.remoteEntities.size(), candidates.size(),
				uiModel.targetStats.hasTarget);
			if (!candidates.empty())
			{
				std::sort(candidates.begin(), candidates.end());
				// Suivant de la cible courante (wrap) ; sans cible : la plus proche.
				size_t nextIndex = 0;
				if (uiModel.targetStats.hasTarget)
				{
					for (size_t ci = 0; ci < candidates.size(); ++ci)
					{
						if (candidates[ci].second == uiModel.targetStats.entityId)
						{
							nextIndex = (ci + 1u) % candidates.size();
							break;
						}
					}
				}
				(void)m_uiModelBinding.SetLocalTarget(candidates[nextIndex].second);
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderPartyFramesImGui(ImDrawList* fg, const engine::client::UIModel& uiModel, const bool& mouseAllowed)
	{
		if (uiModel.inParty)
		{
			const engine::client::PartyHudState& partyState = m_partyHud.GetState();
			// Mapping cadre → membre : même ordre que le présentateur
			// (slot 0 = joueur local, puis l'ordre de partyMembers).
			std::vector<uint32_t> frameClientIds;
			frameClientIds.push_back(uiModel.playerStats.clientId);
			for (const engine::client::UIPartyMemberEntry& member : uiModel.partyMembers)
			{
				if (member.clientId != uiModel.playerStats.clientId)
				{
					frameClientIds.push_back(member.clientId);
				}
			}
			const bool clickThisFrame = mouseAllowed
				&& m_input.WasMousePressed(engine::platform::MouseButton::Left);
			const ImVec2 mousePosParty = ImGui::GetIO().MousePos;
			for (size_t frameIndex = 0; frameIndex < engine::client::kMaxPartyFrames; ++frameIndex)
			{
				const engine::client::PartyMemberFrame& frame = partyState.frames[frameIndex];
				if (!frame.visible)
				{
					continue;
				}
				const engine::client::HudRect& fb = frame.frameBounds;
				// entityId == clientId pour les joueurs (invariant HandleHello).
				const uint64_t memberEntityId = (frameIndex < frameClientIds.size())
					? static_cast<uint64_t>(frameClientIds[frameIndex])
					: 0ull;
				const bool isSelectedAlly = (memberEntityId != 0ull
					&& memberEntityId == m_selectedAllyEntityId);
				fg->AddRectFilled(ImVec2(fb.x, fb.y), ImVec2(fb.x + fb.width, fb.y + fb.height),
					IM_COL32(12, 14, 20, 205), 5.0f);
				fg->AddRect(ImVec2(fb.x, fb.y), ImVec2(fb.x + fb.width, fb.y + fb.height),
					isSelectedAlly ? IM_COL32(220, 190, 90, 240) : IM_COL32(80, 84, 96, 200),
					5.0f, 0, isSelectedAlly ? 2.5f : 1.5f);
				std::string memberLabel = frame.displayName;
				if (frame.isLeader)
				{
					memberLabel += "  [C]";
				}
				fg->AddText(ImVec2(fb.x + 6.0f, fb.y + 4.0f), IM_COL32(230, 230, 230, 255), memberLabel.c_str());
				// Barres PV / mana depuis les widgets du présentateur.
				auto drawMemberBar = [fg](const engine::client::HudBarWidget& bar, ImU32 fillColor)
				{
					if (!bar.visible || bar.bounds.width <= 0.0f)
					{
						return;
					}
					const float fillFraction = (bar.maxValue > 0u)
						? std::clamp(static_cast<float>(bar.currentValue) / static_cast<float>(bar.maxValue), 0.0f, 1.0f)
						: 0.0f;
					fg->AddRectFilled(ImVec2(bar.bounds.x, bar.bounds.y),
						ImVec2(bar.bounds.x + bar.bounds.width, bar.bounds.y + bar.bounds.height),
						IM_COL32(34, 36, 42, 220), 2.0f);
					fg->AddRectFilled(ImVec2(bar.bounds.x, bar.bounds.y),
						ImVec2(bar.bounds.x + bar.bounds.width * fillFraction, bar.bounds.y + bar.bounds.height),
						fillColor, 2.0f);
				};
				drawMemberBar(frame.hpBar, IM_COL32(70, 170, 80, 235));
				drawMemberBar(frame.manaBar, IM_COL32(70, 110, 200, 235));
				// Clic sur le cadre = sélection/désélection de l'allié (soins).
				if (clickThisFrame && memberEntityId != 0ull
					&& mousePosParty.x >= fb.x && mousePosParty.x <= fb.x + fb.width
					&& mousePosParty.y >= fb.y && mousePosParty.y <= fb.y + fb.height)
				{
					m_selectedAllyEntityId = isSelectedAlly ? 0ull : memberEntityId;
					LOG_INFO(Core, "[Engine] Allié sélectionné (entity_id={})", m_selectedAllyEntityId);
				}
			}
			// Label du mode de loot sous les cadres.
			if (partyState.visibleCount > 0u)
			{
				const engine::client::HudRect& lastFrame =
					partyState.frames[partyState.visibleCount - 1u].frameBounds;
				const std::string lootLabel = "Loot: " + partyState.lootModeLabel;
				fg->AddText(ImVec2(lastFrame.x, lastFrame.y + lastFrame.height + 6.0f),
					IM_COL32(190, 190, 190, 220), lootLabel.c_str());
			}
		}
		else if (m_selectedAllyEntityId != 0ull)
		{
			// Sorti du groupe : plus d'allié sélectionnable.
			m_selectedAllyEntityId = 0ull;
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderWalletImGui(float& dw, float& dh, ImDrawList* fg, const engine::client::UIModel& uiModel)
	{
		{
			const engine::client::CoinBreakdown coins =
				engine::client::SplitCoins(uiModel.wallet.gold);
			struct CoinTier { uint32_t value; ImU32 fill; ImU32 ring; };
			std::vector<CoinTier> tiers;
			if (coins.gold > 0u)
				tiers.push_back({coins.gold, IM_COL32(240, 200, 70, 255), IM_COL32(180, 140, 30, 255)});
			if (coins.gold > 0u || coins.silver > 0u)
				tiers.push_back({coins.silver, IM_COL32(214, 220, 228, 255), IM_COL32(150, 160, 172, 255)});
			tiers.push_back({coins.bronze, IM_COL32(205, 125, 60, 255), IM_COL32(150, 80, 35, 255)});

			const float padX = 11.0f;
			const float padY = 6.0f;
			const float coinR = 6.0f;
			const float coinTextGap = 5.0f;
			const float groupGap = 12.0f;
			const float textH = ImGui::GetTextLineHeight();
			std::vector<std::string> labels;
			labels.reserve(tiers.size());
			float contentW = 0.0f;
			for (size_t i = 0; i < tiers.size(); ++i)
			{
				labels.push_back(std::to_string(tiers[i].value));
				const ImVec2 ts = ImGui::CalcTextSize(labels[i].c_str());
				contentW += coinR * 2.0f + coinTextGap + ts.x;
				if (i + 1 < tiers.size())
					contentW += groupGap;
			}
			const float pillW = padX * 2.0f + contentW;
			const float pillH = padY * 2.0f + std::max(textH, coinR * 2.0f);
			const float bourseMargin = 16.0f;
			const float px1 = dw - bourseMargin;
			const float py1 = dh - bourseMargin;
			const float px0 = px1 - pillW;
			const float py0 = py1 - pillH;
			const float midY = (py0 + py1) * 0.5f;
			fg->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(18, 20, 26, 215), 7.0f);
			fg->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(120, 100, 40, 220), 7.0f, 0, 1.5f);
			float cursorX = px0 + padX;
			for (size_t i = 0; i < tiers.size(); ++i)
			{
				const ImVec2 cc(cursorX + coinR, midY);
				fg->AddCircleFilled(cc, coinR, tiers[i].fill);
				fg->AddCircle(cc, coinR, tiers[i].ring, 14, 1.2f);
				cursorX += coinR * 2.0f + coinTextGap;
				const ImVec2 ts = ImGui::CalcTextSize(labels[i].c_str());
				fg->AddText(ImVec2(cursorX, midY - ts.y * 0.5f),
					IM_COL32(245, 235, 205, 255), labels[i].c_str());
				cursorX += ts.x + groupGap;
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderMainMenuBarImGui()
	{
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Panneaux"))
			{
				if (ImGui::MenuItem("Personnage (F1)", nullptr,
					m_characterWindowImGui && m_characterWindowImGui->IsVisible()))
				{
					// Ouvre la fenêtre Personnage unifiée (onglet Compétences).
					if (m_characterWindowImGui)
						m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Competences);
					m_skillBookUi.RequestList();
				}
				if (ImGui::MenuItem("Arenes", nullptr, m_arenaVisible))
				{
					m_arenaVisible = !m_arenaVisible;
					if (m_arenaVisible) m_arenaUi.RequestTeams();
				}
				if (ImGui::MenuItem("Champs de bataille", nullptr, m_battleGroundVisible))
				{
					m_battleGroundVisible = !m_battleGroundVisible;
					if (m_battleGroundVisible) m_battleGroundUi.RequestList();
				}
				if (ImGui::MenuItem("PvP exterieur", nullptr, m_outdoorPvpVisible))
				{
					m_outdoorPvpVisible = !m_outdoorPvpVisible;
					if (m_outdoorPvpVisible) m_outdoorPvpUi.RequestList();
				}
				if (ImGui::MenuItem("Meteo", nullptr, m_weatherVisible))
				{
					m_weatherVisible = !m_weatherVisible;
					if (m_weatherVisible) m_weatherUi.RequestList();
				}
				if (ImGui::MenuItem("Evenements", nullptr, m_gameEventVisible))
				{
					m_gameEventVisible = !m_gameEventVisible;
					if (m_gameEventVisible) m_gameEventUi.RequestList();
				}
				if (ImGui::MenuItem("Guilde", nullptr, m_guildVisible))
				{
					m_guildVisible = !m_guildVisible;
					if (m_guildVisible) m_guildUi.RequestList();
				}
				if (ImGui::MenuItem("Hotel des ventes", nullptr, m_auctionHouseVisible))
				{
					m_auctionHouseVisible = !m_auctionHouseVisible;
					if (m_auctionHouseVisible) m_auctionHouseUi.RequestList(0u);
				}
				if (ImGui::MenuItem("Jets de butin", nullptr, m_lootRollVisible))
				{
					m_lootRollVisible = !m_lootRollVisible;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderCharacterWindowImGui(float& dw, float& dh)
	{
		if (m_characterWindowImGui)
		{
			m_characterWindowImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
			m_characterWindowImGui->Render(m_uiModelBinding.GetModel());
			// Chantier 2 SP-A — draine l'intention equip/unequip signalée au clic et
			// l'envoie au serveur (le renderer n'a pas accès au socket). Le serveur
			// revalide possession + slot.
			engine::render::CharacterWindowImGuiRenderer::PendingEquipAction equipAction;
			if (m_characterWindowImGui->ConsumeEquipAction(equipAction))
			{
				const uint32_t equipClientId = m_gameplayUdp.ServerClientId();
				if (equipAction.kind ==
					engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::Equip)
					(void)m_gameplayUdp.SendEquipRequest(equipClientId, equipAction.itemId);
				else if (equipAction.kind ==
					engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::Unequip)
					(void)m_gameplayUdp.SendUnequipRequest(equipClientId, equipAction.slot);
			// Roadmap-3 (2026-07-19) — consommable cliqué dans le sac :
			// placé au 1er slot LIBRE de la CEINTURE (slot 1 remplacé si
			// pleine). Envoi kind 99 ; le serveur valide la possession et
			// renvoie le layout autoritaire (kind 100).
			else if (equipAction.kind ==
				engine::render::CharacterWindowImGuiRenderer::PendingEquipAction::Kind::SlotCake)
			{
				// Ceinture v2 — layout à taille dynamique (vector).
				std::vector<std::string> belt = m_uiModelBinding.GetModel().playerStats.beltLayout;
				const std::string beltToken = engine::anniversary::MakeItemToken(equipAction.itemId);
				bool alreadySlotted = false;
				for (const std::string& s : belt)
					if (s == beltToken) { alreadySlotted = true; break; }
				if (!alreadySlotted && !belt.empty())
				{
					size_t target = 0;
					for (size_t i = 0; i < belt.size(); ++i)
						if (belt[i].empty()) { target = i; break; }
					belt[target] = beltToken;
					(void)m_gameplayUdp.SendSetBeltLayout(equipClientId, belt);
				}
			}
			}
			// Retour joueur 2026-07-21 — rangée « Ceinture » de la fenêtre
			// Personnage : nouveau layout demandé (drop depuis le sac,
			// réorganisation, retrait au clic droit), envoyé kind 99 ; le
			// serveur revalide la possession et renvoie le layout
			// autoritaire (kind 100), qui rafraîchit fenêtre ET barre HUD.
			std::vector<std::string> windowBeltLayout;
			if (m_characterWindowImGui->ConsumeBeltLayout(windowBeltLayout))
			{
				const uint32_t beltWinClientId = m_gameplayUdp.ServerClientId();
				if (beltWinClientId != 0u)
					(void)m_gameplayUdp.SendSetBeltLayout(beltWinClientId, windowBeltLayout);
			}
		}
	}


	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderServiceSignTooltipImGui(engine::RenderState& out, const int& ivw, const int& ivh, ImDrawList* fg, const bool& worldOverlaysHidden)
	{
		const double now = ImGui::GetTime();
		if (worldOverlaysHidden || m_dialogueActive)
		{
			m_pinnedServiceSign.clear();
			return;
		}
		// Panneau épinglé par un clic : son nom reste au-dessus du panneau 4 s.
		if (!m_pinnedServiceSign.empty())
		{
			float sx = 0.0f, sy = 0.0f;
			if (now > m_pinnedServiceSignUntil
				|| !WorldToScreenPx(out.viewProjMatrix.m, m_pinnedServiceSignPos.x, m_pinnedServiceSignPos.y + 0.55f,
					m_pinnedServiceSignPos.z, ivw, ivh, sx, sy))
				m_pinnedServiceSign.clear();
			else
			{
				const std::string name = m_authUi.UiTranslate(engine::client::ServiceNameKey(m_pinnedServiceSign));
				const ImVec2 ts = ImGui::CalcTextSize(name.c_str());
				const float pad = 5.0f;
				const ImVec2 a(sx - ts.x * 0.5f - pad, sy - ts.y - pad * 2.0f), b(sx + ts.x * 0.5f + pad, sy);
				fg->AddRectFilled(a, b, IM_COL32(20, 18, 16, 210), 4.0f);
				fg->AddRect(a, b, IM_COL32(196, 150, 72, 255), 4.0f);
				fg->AddText(ImVec2(a.x + pad, a.y + pad), IM_COL32(236, 220, 180, 255), name.c_str());
			}
		}
		if (m_visibleServiceSigns.empty() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
			return;
		// Survol : panneau sous le curseur (le plus proche de la caméra s'ils se recouvrent).
		static thread_local std::vector<engine::client::ServiceSignScreen> screen;
		static thread_local std::vector<size_t> source;
		screen.clear();
		source.clear();
		const float fovY = out.camera.fovYDeg * 3.14159265f / 180.0f;
		for (size_t i = 0; i < m_visibleServiceSigns.size(); ++i)
		{
			const VisibleServiceSign& sgn = m_visibleServiceSigns[i];
			float sx = 0.0f, sy = 0.0f;
			if (!WorldToScreenPx(out.viewProjMatrix.m, sgn.pos.x, sgn.pos.y, sgn.pos.z, ivw, ivh, sx, sy)) continue;
			screen.push_back({ sx, sy, engine::client::ServiceSignPickRadiusPx(sgn.distM, static_cast<float>(ivh), fovY), sgn.distM });
			source.push_back(i);
		}
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int hit = engine::client::PickServiceSign(screen, mouse.x, mouse.y);
		if (hit < 0) return;
		const VisibleServiceSign& sgn = m_visibleServiceSigns[source[static_cast<size_t>(hit)]];
		ImGui::SetTooltip("%s", m_authUi.UiTranslate(engine::client::ServiceNameKey(sgn.key)).c_str());
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			m_pinnedServiceSign = sgn.key;
			m_pinnedServiceSignPos = sgn.pos;
			m_pinnedServiceSignUntil = now + 4.0;
		}
	}

	void Engine::RenderInteractableMarkersImGui(engine::RenderState& out, const int& ivw, const int& ivh, ImDrawList* fg, const bool& worldOverlaysHidden)
	{
		if (!worldOverlaysHidden)
		for (std::size_t ii = 0; ii < m_interactables.size(); ++ii)
		{
			const InteractableEntity& e = m_interactables[ii];
			// Pendant un dialogue PNJ : on masque le label flottant + badge « E » de
				// l'interactible (le PNJ engagé est derrière la cellule, son label
				// transparaîtrait au travers).
				if (m_dialogueActive)
					continue;
				const float my = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z) + 1.9f;
			float sx = 0.0f, sy = 0.0f;
			if (!WorldToScreenPx(out.viewProjMatrix.m, e.position.x, my, e.position.z, ivw, ivh, sx, sy))
				continue;
			const bool inRange = (static_cast<int>(ii) == m_interactableInRange);
			const ImU32 col = inRange ? IM_COL32(255, 220, 80, 255) : IM_COL32(210, 210, 210, 200);
			// Label du nom seul ; le "[E]" texte est remplace par un badge touche
			// (chantier C) + la surbrillance 3D du prop signale l'interactivite.
			// FIX.10 — le nom n'apparaît qu'à portée d'interaction (retour joueur
			// 29/10/2026 : « Coffre », « Caisse en bois »… saturaient l'écran). Le
			// marqueur de quête plus bas garde sa propre portée.
			if (inRange)
			{
				const ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
				fg->AddText(ImVec2(sx - ts.x * 0.5f, sy - ts.y), col, e.label.c_str());
			}
			// Repere d'interaction : badge "touche E" sous le label quand a portee.
			if (inRange)
			{
				const ImVec2 keySz = ImGui::CalcTextSize("E");
				const float pad = 6.0f;
				const float bw = keySz.x + pad * 2.0f;
				const float bh = keySz.y + pad * 2.0f;
				const float bx = sx - bw * 0.5f;
				const float by = sy + 4.0f;
				fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(20, 20, 24, 220), 4.0f);
				fg->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 220, 80, 255), 4.0f, 0, 2.0f);
				fg->AddText(ImVec2(bx + pad, by + pad), IM_COL32(255, 220, 80, 255), "E");
			}
			// SP2 Task 6 — Marqueur world-space donneur de quête : rune procédurale
			// (losange + anneau) au-dessus des PNJ liés à une quête en cours de
			// proposition ou de rendu. Source des liens PNJ<->quête : table
			// data-driven chargée au boot (cf. Init, m_questGiverTable.Load).
			// Statuts UIModel.quests miroir de engine::client::QuestStatus (wire
			// uint8) — dupliqués en constantes locales comme QuestImGuiRenderer.cpp
			// pour ne pas tirer de dépendance supplémentaire ici.
			if (!e.npcTargetId.empty())
			{
				constexpr uint8_t kQuestStatusOffered = 1;
				constexpr uint8_t kQuestStatusReadyToTurnIn = 3;
				const std::vector<engine::client::QuestGiverLink>* links = m_questGiverTable.ForNpc(e.npcTargetId);
				if (links != nullptr && !links->empty())
				{
					// Culling distance : seulement si le PNJ est à portée « visible »
					// configurée (defaut 35 m), indépendamment du rayon d'interaction
					// (e.radius, ~2.5 m) qui régit le badge "E".
					const engine::math::Vec3 markerPlayerPos = m_characterController.GetPosition();
					// Distance XZ uniquement (comme le rayon d'interaction / badge "E",
					// cf. calcul dx²+dz² du "[E]") : la position des interactables est
					// chargée avec y=0 (jamais collée au sol) alors que le joueur est
					// ~100 m plus haut (plateau terrain). Inclure le Y ferait toujours
					// dépasser markerMaxDist → le rune ne s'afficherait jamais.
					const float mdx = e.position.x - markerPlayerPos.x;
					const float mdz = e.position.z - markerPlayerPos.z;
					const float markerDist = std::sqrt(mdx * mdx + mdz * mdz);
					const float markerMaxDist = static_cast<float>(
						m_cfg.GetDouble("client.quest.giver_marker_distance_m", 35.0));
					if (markerDist <= markerMaxDist)
					{
						// Priorité d'affichage si plusieurs liens : rendre > proposer
						// (un PNJ à la fois donneur ET receveur pour des quêtes
						// différentes doit d'abord signaler ce qu'il attend du joueur).
						bool hasOffer = false;
						bool hasTurnIn = false;
						const auto& questsModel = m_uiModelBinding.GetModel().quests;
						for (const engine::client::QuestGiverLink& link : *links)
						{
							for (const engine::client::UIQuestEntry& q : questsModel)
							{
								if (q.questId != link.questId)
									continue;
								if (link.role == 0 && q.status == kQuestStatusOffered)
									hasOffer = true;
								else if (link.role == 1 && q.status == kQuestStatusReadyToTurnIn)
									hasTurnIn = true;
							}
						}
						if (hasOffer || hasTurnIn)
						{
							// Ancrage : ~2.2 m au-dessus de la tête (my = sol + 1.9, tête
							// approx au-dessus -> on projette depuis le sol + 2.2 direct
							// pour rester simple et stable si le mesh du PNJ varie).
							const float runeWorldY = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z) + 2.2f;
							float rsx = 0.0f, rsy = 0.0f;
							if (WorldToScreenPx(out.viewProjMatrix.m, e.position.x, runeWorldY, e.position.z, ivw, ivh, rsx, rsy))
							{
								// Rendre prioritaire sur proposer si les deux sont vrais.
								const bool turnInVariant = hasTurnIn;
								const ImU32 runeFill = turnInVariant
									? IM_COL32(120, 200, 255, 235)   // rendre : teinte bleu-glacé
									: IM_COL32(255, 205, 60, 235);   // proposer : doré plein
								const ImU32 runeRing = turnInVariant
									? IM_COL32(230, 245, 255, 255)
									: IM_COL32(255, 240, 190, 255);
								const float runeR = 18.0f; // agrandie (retour joueur : trop petite a 11)
								// Losange (4 sommets) rempli.
								const ImVec2 diamond[4] = {
									ImVec2(rsx, rsy - runeR),
									ImVec2(rsx + runeR * 0.62f, rsy),
									ImVec2(rsx, rsy + runeR),
									ImVec2(rsx - runeR * 0.62f, rsy),
								};
								fg->AddConvexPolyFilled(diamond, 4, runeFill);
								// Anneau autour du losange pour la lisibilité + signature "rune".
								fg->AddCircle(ImVec2(rsx, rsy), runeR + 4.0f, runeRing, 24, 2.5f);
							}
						}
					}
				}
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderNameplatesImGui(engine::RenderState& out, const int& ivw, const int& ivh, ImDrawList* fg, const bool& worldOverlaysHidden, const engine::client::NameplateVisibilityParams& plateParams)
	{
		if (!worldOverlaysHidden)
		{
			const auto& remotes = m_uiModelBinding.GetModel().remoteEntities;
			const engine::math::Vec3 platePlayerPos = m_characterController.GetPosition();
			const engine::client::UIModel& plateSelModel = m_uiModelBinding.GetModel();
				// Anti-chevauchement des plaques : boites deja posees cette frame
				// (x,y,z,w = gauche,haut,droite,bas). Une nouvelle plaque qui en
				// recouvre une est remontee d'une hauteur de plaque (empilement).
				std::vector<ImVec4> placedLabelRects;
			// Le Journal de quêtes (coin haut-gauche, toujours visible) est une
			// fenêtre ImGui ; les plaques de mob passent par le foreground draw
			// list, donc AU-DESSUS. On récupère son rectangle pour sauter toute
			// plaque qui retomberait dessus (retour joueur 2026-07-09 : « Sanglier
			// des collines » s'affichait par-dessus le journal).
			ImVec4 journalRect(0.0f, 0.0f, 0.0f, 0.0f);
			bool hasJournalRect = false;
			{
				const engine::client::QuestUiState& qs = m_questUi.GetState();
				if (qs.layoutValid)
				{
					const engine::client::QuestUiRect& jb = qs.journalPanelBounds;
					journalRect = ImVec4(jb.x, jb.y, jb.x + jb.width, jb.y + jb.height);
					hasJournalRect = true;
				}
			}
			for (const engine::client::UIRemoteEntity& re : remotes)
			{
				// Combat SP1 : les mobs (archetypeId != 0) ont leur plaque.
				// Validation v12 — les loot bags (les deux ids à 0) aussi :
				// « Butin [F] » au sol (le serveur les répliquait depuis M28
				// mais le client ne les montrait jamais — butin imperdable
				// mais invisible). Ramassage : touche F, bloc combat plus bas.
				// Mob mort en attente de despawn : pas de plaque (kEntityStateDead).
				// Métiers SP1 — les nodes échappent à ce skip : un node épuisé
				// garde sa plaque, grisée avec le suffixe « (epuise) ».
				if (re.playerClientId == 0u && (re.stateFlags & 1u) != 0u
					&& re.archetypeId < engine::server::kGatheringNodeArchetypeBase)
					continue;
				// FIX.10 — plaques des créatures : animaux et mobs hostiles seulement
				// s'ils sont sélectionnés (ou animaux pour un chasseur), PNJ dans le
				// champ de vision ; joueurs, butin et nœuds inchangés.
				{
					const float pdx = re.positionX - platePlayerPos.x;
					const float pdz = re.positionZ - platePlayerPos.z;
					const bool plateSelected = plateSelModel.targetStats.hasTarget
						&& plateSelModel.targetStats.entityId == re.entityId;
					if (!engine::client::ShouldShowNameplate(ClassifyRemoteForNameplate(re), plateSelected,
							std::sqrt(pdx * pdx + pdz * pdz), plateParams))
						continue;
				}
				float wx = re.positionX, wy = re.positionY, wz = re.positionZ;
				const auto sit = m_remoteSmoothed.find(re.entityId);
				if (sit != m_remoteSmoothed.end() && sit->second.valid)
				{
					wx = sit->second.x; wy = sit->second.y; wz = sit->second.z;
				}
				// Combat SP1 fix — mobs/nodes : Y de spawn brut (souvent 0, jamais
				// collé au terrain par le serveur) → snap visuel au sol client,
				// sinon la plaque flotte au ras du sol (cf. ResolveRemoteDisplayCenterY).
				// v1.0.125 — bateau : plaque au-dessus du pont (Y = flottaison répliquée).
				wy = engine::server::IsBoatArchetype(re.archetypeId)
					? wy + 6.0f
					: ResolveRemoteDisplayCenterY(re.playerClientId != 0u, wy, wx, wz);
				float sxp = 0.0f, syp = 0.0f;
				if (!WorldToScreenPx(out.viewProjMatrix.m, wx, wy + 1.2f, wz, ivw, ivh, sxp, syp))
					continue;
				// TD.5 : on affiche le vrai nom du perso s'il est connu (chargé depuis
				// la DB serveur et propagé via le SnapshotEntity). Sinon, fallback sur
				// "P<clientId>" — utile quand la DB serveur ne sert pas (Windows / DB
				// indisponible) ou pour identifier l'entité dans un log.
				// Combat SP1 : plaque mob "Nom (niv. N)  PV/PVmax" résolue via le
				// CreatureCatalog ; fallback générique si l'archétype est inconnu
				// du catalogue client (catalogue absent ou désynchronisé).
				std::string label;
				// SP — suffixe « (niv. N) » des mobs, rendu en plus petit a droite
				// du nom. Vide pour joueurs/nodes/butin (pas de niveau affiche).
				std::string levelSuffix;
				// Validation v12 — armé par la branche mob ci-dessous : dessine la
				// barre de vie sous la plaque (jamais pour joueurs/nodes).
				bool drawMobHealthBar = false;
				if (re.playerClientId != 0u)
				{
					label = !re.displayName.empty()
						? re.displayName
						: ("P" + std::to_string(re.playerClientId));
				}
				else if (re.archetypeId == 0u)
				{
					// Validation v12 — sac de butin au sol.
					label = "Butin [F]";
				}
				else if (engine::server::IsBoatArchetype(re.archetypeId))
				{
					// v1.0.125 — bateau : « Navire à quai [E] Embarquer / Débarquer ».
					const bool docked = (re.stateFlags & engine::server::kEntityStateBoatDocked) != 0u;
					label = docked ? "Navire a quai" : "Navire";
					if (docked && m_aboardBoatEntityId == re.entityId)
						label += " [E] Debarquer";
					else if (docked && m_boardableBoatEntityId == re.entityId)
						label += " [E] Embarquer";
				}
				else if (re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
				{
					// Métiers SP1 — label de node de récolte : nom FR par type
					// (V1 : map locale ; les noms en data = amélioration future)
					// + « [E] » si disponible et à portée de récolte (~5 m).
					const uint32_t nodeTypeId =
						re.archetypeId - engine::server::kGatheringNodeArchetypeBase;
					const char* nodeName = "Ressource";
					switch (nodeTypeId)
					{
					case 1u: nodeName = "Filon de minerai"; break;
					case 2u: nodeName = "Herbes sauvages"; break;
					case 3u: nodeName = "Arbre a abattre"; break;
					case 4u: nodeName = "Carcasse"; break;
					default: break;
					}
					label = nodeName;
					if ((re.stateFlags & 1u) != 0u)
					{
						label += " (epuise)";
					}
					else
					{
						const engine::math::Vec3 playerPosNode = m_characterController.GetPosition();
						const float dxNode = re.positionX - playerPosNode.x;
						const float dzNode = re.positionZ - playerPosNode.z;
						if ((dxNode * dxNode + dzNode * dzNode) <= 25.0f)
						{
							label += " [E]";
						}
					}
				}
				else if (const engine::client::CreatureAppearance* amb = m_creatureCatalog.Find(re.archetypeId);
					amb != nullptr && amb->ambient)
				{
					// Monde vivant — PNJ d'ambiance : nom + rôle, sans PV ni niveau ;
					// « [E] » quand il est assez près pour lui parler.
					label = amb->name;
					if (!amb->role.empty()) levelSuffix = " - " + amb->role;
					if (!amb->dialogueId.empty() || !amb->talkTarget.empty())
					{
						const engine::math::Vec3 pp = m_characterController.GetPosition();
						const float dxa = re.positionX - pp.x, dza = re.positionZ - pp.z;
						if (dxa * dxa + dza * dza <= kAmbientTalkRadiusM * kAmbientTalkRadiusM)
							label += " [E]";
					}
				}
				else
				{
					const engine::client::CreatureAppearance* appearance =
						m_creatureCatalog.Find(re.archetypeId);
					const std::string mobName = (appearance != nullptr)
						? appearance->name
						: ("Creature " + std::to_string(re.archetypeId));
					const uint32_t mobLevel = (appearance != nullptr) ? appearance->level : 1u;
					// Validation v12 — les PV chiffrés quittent le label : ils sont
					// désormais portés par la barre de vie dessinée sous la plaque.
					label = mobName;
					// FIX.10 — niveau révélé seulement au cœur du cercle de perception.
					{
						const float ldx = re.positionX - platePlayerPos.x;
						const float ldz = re.positionZ - platePlayerPos.z;
						if (engine::client::ShouldShowLevel(std::sqrt(ldx * ldx + ldz * ldz), plateParams))
							levelSuffix = " (niv. " + std::to_string(mobLevel) + ")";
					}
					drawMobHealthBar = (re.maxHealth > 0u);
				}
				// Largeur TOTALE = nom (taille normale) + suffixe « (niv. N) » (0.78x).
				ImFont* plateFont = ImGui::GetFont();
				const float nivFontSize = ImGui::GetFontSize() * 0.78f;
				const ImVec2 nameTs = ImGui::CalcTextSize(label.c_str());
				const ImVec2 nivTs = levelSuffix.empty()
					? ImVec2(0.0f, 0.0f)
					: plateFont->CalcTextSizeA(nivFontSize, FLT_MAX, 0.0f, levelSuffix.c_str());
				const ImVec2 ts(nameTs.x + nivTs.x, nameTs.y);
				// Empilement vertical : remonte la plaque tant qu'elle recouvre une
				// plaque deja posee cette frame (mobs superposes -> textes lisibles).
				const float plateHalfW = ts.x * 0.5f + 6.0f;
				const float plateH = ts.y + 2.0f + (drawMobHealthBar ? 24.0f : 4.0f);
				for (int attempt = 0; attempt < 12; ++attempt)
				{
					const float top = syp - ts.y - 2.0f;
					const float bot = syp + (drawMobHealthBar ? 24.0f : 4.0f);
					bool overlap = false;
					for (const ImVec4& r : placedLabelRects)
					{
						if ((sxp - plateHalfW) < r.z && (sxp + plateHalfW) > r.x
							&& top < r.w && bot > r.y)
						{ overlap = true; break; }
					}
					if (!overlap) break;
					syp -= (plateH + 3.0f);
				}
				// Ne pas dessiner la plaque si elle retombe sur le Journal de quêtes
				// (elle serait masquée par la fenêtre de toute façon, mais le
				// foreground la peindrait par-dessus -> illisible).
				if (hasJournalRect
					&& (sxp - plateHalfW) < journalRect.z && (sxp + plateHalfW) > journalRect.x
					&& (syp - ts.y - 2.0f) < journalRect.w
					&& (syp + (drawMobHealthBar ? 24.0f : 4.0f)) > journalRect.y)
				{
					continue;
				}
				placedLabelRects.emplace_back(sxp - plateHalfW, syp - ts.y - 2.0f,
					sxp + plateHalfW, syp + (drawMobHealthBar ? 24.0f : 4.0f));
				// Halo noir derriere le texte pour la lisibilite sur fond clair (ciel).
				fg->AddRectFilled(
					ImVec2(sxp - ts.x * 0.5f - 4.0f, syp - ts.y - 2.0f),
					ImVec2(sxp + ts.x * 0.5f + 4.0f, syp + 2.0f),
					IM_COL32(0, 0, 0, 140), 3.0f);
				// Validation v12 — surbrillance de la CIBLE courante : avec
				// plusieurs mobs au même nom et aux mêmes PV, la bascule Tab
				// était invisible (le cadre cible affichait la même chose).
				// Bordure dorée autour de la plaque du mob ciblé.
				{
					const engine::client::UIModel& plateModel = m_uiModelBinding.GetModel();
					if (plateModel.targetStats.hasTarget
						&& plateModel.targetStats.entityId == re.entityId)
					{
						fg->AddRect(
							ImVec2(sxp - ts.x * 0.5f - 6.0f, syp - ts.y - 4.0f),
							ImVec2(sxp + ts.x * 0.5f + 6.0f, syp + 4.0f),
							IM_COL32(235, 190, 60, 255), 3.0f, 0, 2.0f);
					}
				}
				// Nom a taille normale, puis suffixe niveau plus petit, aligne en bas.
				const float labelLeft = sxp - ts.x * 0.5f;
				fg->AddText(ImVec2(labelLeft, syp - ts.y), IM_COL32(220, 230, 255, 255), label.c_str());
				if (!levelSuffix.empty())
				{
					fg->AddText(plateFont, nivFontSize,
						ImVec2(labelLeft + nameTs.x, syp - nivTs.y),
						IM_COL32(180, 195, 225, 230), levelSuffix.c_str());
				}
				// Validation v12 — barre de vie du mob sous la plaque, alimentée
				// par currentHealth/maxHealth du snapshot (10 Hz) : elle descend
				// donc en direct à chaque coup porté. Couleur par tiers de PV
				// (vert > 50 %, orange > 25 %, rouge en dessous).
				if (drawMobHealthBar)
				{
					const float hpFrac = std::clamp(
						static_cast<float>(re.currentHealth) / static_cast<float>(re.maxHealth),
						0.0f, 1.0f);
					const float barW = std::max(64.0f, ts.x);
					const float barH = 5.0f;
					const float barLeft = sxp - barW * 0.5f;
					const float barTop = syp + 4.0f;
					const ImU32 fillColor = (hpFrac > 0.5f)
						? IM_COL32(80, 200, 80, 230)
						: ((hpFrac > 0.25f)
							? IM_COL32(230, 160, 40, 230)
							: IM_COL32(210, 60, 50, 230));
					fg->AddRectFilled(ImVec2(barLeft, barTop),
						ImVec2(barLeft + barW, barTop + barH), IM_COL32(20, 22, 26, 200), 2.0f);
					fg->AddRectFilled(ImVec2(barLeft, barTop),
						ImVec2(barLeft + barW * hpFrac, barTop + barH), fillColor, 2.0f);
					fg->AddRect(ImVec2(barLeft, barTop),
						ImVec2(barLeft + barW, barTop + barH), IM_COL32(0, 0, 0, 160), 2.0f);
					// PV chiffrés compacts au-dessus de la barre, à droite.
					const std::string hpText = std::to_string(re.currentHealth) + "/"
						+ std::to_string(re.maxHealth);
					const ImVec2 hpTs = ImGui::CalcTextSize(hpText.c_str());
					fg->AddText(ImVec2(barLeft + barW - hpTs.x, barTop + barH + 1.0f),
						IM_COL32(200, 205, 215, 220), hpText.c_str());
				}
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderTargetFrameImGui(float& dw, ImDrawList* fg, const engine::client::UIModel& uiModel)
	{
		if (uiModel.targetStats.hasTarget
			&& (uiModel.targetStats.stateFlags & 1u) == 0u)
		{
			const float frameW = 260.0f;
			const float frameH = 54.0f;
			const float fx = (dw - frameW) * 0.5f;
			const float fy = 56.0f;
			const bool targetDead = (uiModel.targetStats.stateFlags & 1u) != 0u;
			// Nom : catalogue creatures (la cible V1 est toujours un mob).
			std::string targetName = "Cible";
			for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
			{
				if (re.entityId != uiModel.targetStats.entityId)
					continue;
				const engine::client::CreatureAppearance* app = m_creatureCatalog.Find(re.archetypeId);
				if (app != nullptr)
					targetName = app->name;
				break;
			}
			if (targetDead)
				targetName += "  (MORT)";
			fg->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH), IM_COL32(10, 12, 16, 200), 6.0f);
			fg->AddRect(ImVec2(fx, fy), ImVec2(fx + frameW, fy + frameH),
				targetDead ? IM_COL32(120, 120, 120, 200) : IM_COL32(200, 60, 50, 220), 6.0f, 0, 2.0f);
			fg->AddText(ImVec2(fx + 10.0f, fy + 6.0f),
				targetDead ? IM_COL32(150, 150, 150, 255) : IM_COL32(235, 235, 235, 255), targetName.c_str());
			// Distance XZ joueur→cible : affichée SOUS le cadre, centrée et en
			// plus petit (0.8x). Avant elle etait en haut a droite DANS le cadre
			// et chevauchait le nom (souvent long) -> illisible. Orange au-dela
			// de 4 m (coherent avec « Hors de portee »), vert sinon.
			{
				const engine::math::Vec3 playerPosFrame = m_characterController.GetPosition();
				for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
				{
					if (re.entityId != uiModel.targetStats.entityId)
						continue;
					const float dxf = re.positionX - playerPosFrame.x;
					const float dzf = re.positionZ - playerPosFrame.z;
					const float distM = std::sqrt(dxf * dxf + dzf * dzf);
					const std::string distText =
						std::to_string(static_cast<int>(std::lround(distM))) + " m";
					ImFont* distFont = ImGui::GetFont();
					const float distFontSize = ImGui::GetFontSize() * 0.8f;
					const ImVec2 distTs =
						distFont->CalcTextSizeA(distFontSize, FLT_MAX, 0.0f, distText.c_str());
					fg->AddText(distFont, distFontSize,
						ImVec2(fx + (frameW - distTs.x) * 0.5f, fy + frameH + 2.0f),
						(distM > 4.0f) ? IM_COL32(240, 170, 60, 255) : IM_COL32(170, 220, 170, 255),
						distText.c_str());
					break;
				}
			}
			const float barX = fx + 10.0f, barY = fy + 30.0f, barW = frameW - 20.0f, barH = 14.0f;
			const float hpFrac = (uiModel.targetStats.maxHealth > 0u)
				? std::clamp(static_cast<float>(uiModel.targetStats.currentHealth)
					/ static_cast<float>(uiModel.targetStats.maxHealth), 0.0f, 1.0f)
				: 0.0f;
			fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(40, 40, 44, 220), 3.0f);
			fg->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * hpFrac, barY + barH),
				targetDead ? IM_COL32(110, 110, 110, 230) : IM_COL32(190, 50, 40, 230), 3.0f);
			const std::string hpText = std::to_string(uiModel.targetStats.currentHealth) + "/"
				+ std::to_string(uiModel.targetStats.maxHealth);
			const ImVec2 hpTs = ImGui::CalcTextSize(hpText.c_str());
			fg->AddText(ImVec2(barX + (barW - hpTs.x) * 0.5f, barY - 1.0f), IM_COL32(255, 255, 255, 255), hpText.c_str());
			// Validation v12 — indication transitoire « Hors de portee »
			// (cf. armement dans le bloc T ci-dessus).
			if (m_outOfRangeHintSec > 0.0f)
			{
				const char* rangeHint = "Hors de portee  (approchez-vous)";
				const ImVec2 hintTs = ImGui::CalcTextSize(rangeHint);
				fg->AddText(ImVec2(fx + (frameW - hintTs.x) * 0.5f, fy + frameH + 6.0f),
					IM_COL32(240, 170, 60, 255), rangeHint);
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderAdvancedCombatPanelImGui(float& dw)
	{
		if (m_advancedCombatVisible)
		{
			const engine::client::AdvancedCombatState& acs = m_advancedCombat.GetState();
			ImGui::SetNextWindowPos(ImVec2(dw - 420.0f, 90.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Combat avance", &m_advancedCombatVisible))
			{
				ImGui::Text("Combat : %s  (%.1f s)", acs.inCombat ? "en cours" : "inactif", acs.fightElapsedSec);
				ImGui::Separator();
				ImGui::TextUnformatted("DPS");
				for (const engine::client::DpsMeterEntry& row : acs.dpsMeter)
				{
					ImGui::Text("%u. %s", row.rank, row.displayName.c_str());
					ImGui::SameLine(200.0f);
					ImGui::Text("%.1f dps", row.dps);
					ImGui::ProgressBar(row.barFraction, ImVec2(-1.0f, 6.0f), "");
				}
				// Combat SP4 — menace sur la cible courante (ThreatUpdate).
				if (acs.threatMeterVisible && !acs.threatMeter.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted("Menace");
					for (const engine::client::ThreatMeterEntry& threatRow : acs.threatMeter)
					{
						const ImVec4 rowColor = (threatRow.color == engine::client::ThreatColor::Red)
							? ImVec4(0.9f, 0.3f, 0.25f, 1.0f)
							: (threatRow.color == engine::client::ThreatColor::Yellow)
								? ImVec4(0.9f, 0.8f, 0.3f, 1.0f)
								: ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
						ImGui::TextColored(rowColor, "%s", threatRow.displayName.c_str());
						ImGui::SameLine(200.0f);
						ImGui::TextColored(rowColor, "%.0f %%", threatRow.threatPercent);
						ImGui::ProgressBar(threatRow.barFraction, ImVec2(-1.0f, 6.0f), "");
					}
				}
				ImGui::Separator();
				uint32_t filter = acs.activeFilter;
				bool filterChanged = false;
				auto toggleChip = [&filter, &filterChanged](const char* lbl, uint32_t mask)
				{
					bool on = (filter & mask) != 0u;
					if (ImGui::Checkbox(lbl, &on))
					{
						filter = on ? (filter | mask) : (filter & ~mask);
						filterChanged = true;
					}
					ImGui::SameLine();
				};
				toggleChip("Degats", engine::client::CombatLogFilterDamage);
				toggleChip("Soins", engine::client::CombatLogFilterHealing);
				toggleChip("Morts", engine::client::CombatLogFilterDeaths);
				ImGui::NewLine();
				if (filterChanged)
					m_advancedCombat.SetLogFilter(filter);
				ImGui::Separator();
				ImGui::BeginChild("##ln_combat_log", ImVec2(0.0f, 0.0f), true);
				for (const engine::client::CombatLogLine& logLine : acs.visibleLogLines)
					ImGui::TextUnformatted(logLine.text.c_str());
				ImGui::EndChild();
			}
			ImGui::End();
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderActionBarAndBeltImGui(float& dw, float& dh, ImDrawList* fg, const engine::client::UIModel& uiModel, const bool& keysAllowed, const std::vector<engine::client::SpellDisplay>& effectiveKit)
	{
		{
			const float nowSec = EngineNowSec();
			const float slotSize = 58.0f;
			const float slotGap = 8.0f;
			// Grimoire — layout effectif des 10 slots (slot i → spellId).
			const std::array<std::string, 10> resolvedLayout =
				engine::client::ResolveActionBarLayout(uiModel.playerStats.actionBarLayout, effectiveKit);
			const size_t slotCount = resolvedLayout.size(); // 10
			const float barWidth = slotSize * static_cast<float>(slotCount)
				+ slotGap * static_cast<float>(slotCount - 1);
			const float barX = (dw - barWidth) * 0.5f;
			const float barY = dh - slotSize - 16.0f;
			for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
			{
				const std::string& slotSpellId = resolvedLayout[slotIndex];
				const engine::client::SpellDisplay* spellPtr =
					slotSpellId.empty() ? nullptr : engine::client::FindSpellInKit(effectiveKit, slotSpellId);
				const float sx0 = barX + static_cast<float>(slotIndex) * (slotSize + slotGap);
				// Touche du slot (remappable) : défaut Digit1..Digit9 puis Digit0.
				const engine::platform::Key slotKey = KeyFromName(
					m_cfg.GetString("controls.keybind.action_slot_" + std::to_string(slotIndex + 1),
						(slotIndex < 9) ? std::string(1, static_cast<char>('1' + slotIndex)) : std::string("0")),
					(slotIndex < 9)
						? static_cast<engine::platform::Key>('1' + static_cast<int>(slotIndex))
						: engine::platform::Key::Digit0);
				// SP3 anniversaires (2026-07-18) — slot occupé par un
				// GÂTEAU (jeton "item:<id>") : case dédiée, la touche
				// envoie un CastRequest avec le jeton (activation
				// serveur : buff groupe/guilde tant que slotté).
				uint32_t cakeItemId = 0u;
				if (engine::anniversary::ParseCakeToken(slotSpellId, cakeItemId))
				{
					fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
						IM_COL32(58, 34, 52, 220), 6.0f);
					fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
						IM_COL32(220, 150, 190, 200), 6.0f, 0, 2.0f);
					const engine::items::ItemDefinition* cakeDef = m_itemCatalog.Find(cakeItemId);
					const char* cakeLabel = (cakeDef != nullptr && !cakeDef->name.empty())
						? cakeDef->name.c_str() : "Gateau";
					fg->AddText(ImVec2(sx0 + 4.0f, barY + slotSize * 0.5f - 8.0f),
						IM_COL32(255, 230, 245, 255), cakeLabel);
					const std::string cakeKeyLabel = KeyGlyph(slotKey);
					fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f),
						IM_COL32(255, 220, 240, 220), cakeKeyLabel.c_str());
					if (keysAllowed && m_input.WasPressed(slotKey))
					{
						const uint32_t cakeClientId = m_gameplayUdp.ServerClientId();
						if (cakeClientId != 0u)
							(void)m_gameplayUdp.SendCastRequest(cakeClientId, 0ull, slotSpellId);
					}
					continue;
				}
				if (spellPtr == nullptr)
				{
					// Slot vide : case grisée + glyphe touche, pas d'action.
					fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
						IM_COL32(14, 16, 22, 160), 6.0f);
					fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
						IM_COL32(70, 70, 76, 160), 6.0f, 0, 2.0f);
					const std::string emptyKeyLabel = KeyGlyph(slotKey);
					fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f), IM_COL32(150, 140, 110, 200), emptyKeyLabel.c_str());
					continue;
				}
				const engine::client::SpellDisplay& spell = *spellPtr;
				// Coût payable + cooldown : visuel grisé si indisponible.
				const uint32_t cost = spell.resourceCostPercent
					* uiModel.playerStats.secondaryResourceMax / 100u;
				const bool affordable = uiModel.playerStats.secondaryResourceCurrent >= cost;
				const auto cooldownIt = m_spellCooldownUiUntilSec.find(spell.spellId);
				const float cooldownRemaining = (cooldownIt != m_spellCooldownUiUntilSec.end())
					? std::max(0.0f, cooldownIt->second - nowSec)
					: 0.0f;
				const bool ready = affordable && cooldownRemaining <= 0.0f;
				fg->AddRectFilled(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
					IM_COL32(14, 16, 22, 215), 6.0f);
				fg->AddRect(ImVec2(sx0, barY), ImVec2(sx0 + slotSize, barY + slotSize),
					ready ? IM_COL32(200, 180, 90, 220) : IM_COL32(90, 90, 96, 200), 6.0f, 0, 2.0f);
				// SP-E — icône du sort en fond du slot (compétences de classe).
				// Repli sur le nom texte si pas d'icône (kit profil, fichier absent).
				bool slotHasIcon = false;
				if (!spell.iconPath.empty())
				{
					const uint64_t slotTexId = m_skillIconCache.GetOrLoad(spell.iconPath);
					if (slotTexId != 0)
					{
						const float pad = 3.0f;
						fg->AddImage(static_cast<ImTextureID>(slotTexId),
							ImVec2(sx0 + pad, barY + pad),
							ImVec2(sx0 + slotSize - pad, barY + slotSize - pad));
						slotHasIcon = true;
					}
				}
				// Numéro de touche (haut-gauche) + nom du sort (bas, tronqué) si pas d'icône.
				const std::string keyLabel = KeyGlyph(slotKey);
				fg->AddText(ImVec2(sx0 + 4.0f, barY + 2.0f), IM_COL32(255, 230, 150, 230), keyLabel.c_str());
				if (!slotHasIcon)
				{
					std::string spellLabel = spell.name.substr(0, 9);
					fg->AddText(ImVec2(sx0 + 4.0f, barY + slotSize - 18.0f),
						ready ? IM_COL32(225, 225, 225, 255) : IM_COL32(140, 140, 140, 255),
						spellLabel.c_str());
				}
				// Voile + décompte pendant le cooldown affiché.
				if (cooldownRemaining > 0.0f)
				{
					const float fillFraction = (spell.cooldownMs > 0u)
						? std::clamp(cooldownRemaining / (static_cast<float>(spell.cooldownMs) / 1000.0f), 0.0f, 1.0f)
						: 0.0f;
					fg->AddRectFilled(
						ImVec2(sx0, barY + slotSize * (1.0f - fillFraction)),
						ImVec2(sx0 + slotSize, barY + slotSize),
						IM_COL32(0, 0, 0, 150), 6.0f);
					char cooldownText[16];
					std::snprintf(cooldownText, sizeof(cooldownText), "%.0f", cooldownRemaining);
					const ImVec2 cdTs = ImGui::CalcTextSize(cooldownText);
					fg->AddText(ImVec2(sx0 + (slotSize - cdTs.x) * 0.5f, barY + (slotSize - cdTs.y) * 0.5f),
						IM_COL32(255, 255, 255, 255), cooldownText);
				}
				// Touche remappable du slot : envoi du CastRequest.
				if (keysAllowed && m_input.WasPressed(slotKey))
				{
					const bool targetOk = !spell.needsEnemyTarget
						|| (uiModel.targetStats.hasTarget
							&& (uiModel.targetStats.stateFlags & 1u) == 0u);
					const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
					if (targetOk && ready && gameplayClientId != 0u)
					{
						// Groupes SP1 — sorts d'allié : envoie l'allié sélectionné au
						// cadre de groupe (0 = soi, défaut serveur).
						uint64_t castTarget = 0ull;
						if (spell.needsEnemyTarget)
						{
							castTarget = uiModel.targetStats.entityId;
						}
						else if (spell.targetsAlly)
						{
							castTarget = m_selectedAllyEntityId;
						}
						(void)m_gameplayUdp.SendCastRequest(gameplayClientId, castTarget, spell.spellId);
						if (spell.cooldownMs > 0u)
						{
							m_spellCooldownUiUntilSec[spell.spellId] =
								nowSec + static_cast<float>(spell.cooldownMs) / 1000.0f;
						}
					}
				}
			}

			// Ceinture v2 (2026-07-20) — barre d'objets ACTIFS à taille
			// DYNAMIQUE (4 par défaut, 12 max — capacité autoritaire
			// = ceinture équipée en slot Waist, reçue via kind 100).
			// Fenêtre ImGui invisible : chaque case est un vrai
			// widget → drag & drop natif (sac → case, case → case),
			// tooltips, clic droit pour vider. Activation : Maj+1..9
			// ou clic complet (relâché sans drag) sur la case.
			{
				const std::vector<std::string>& beltLayout = uiModel.playerStats.beltLayout;
				const size_t beltCount = beltLayout.size();
				if (beltCount > 0u)
				{
					const float beltSlotSz = 48.0f;
					const float beltGap = 6.0f;
					// Retour de test 2026-07-20 — disposition : la
					// rangée BASSE porte les slots 1..6 ; les slots
					// 7..12 s'empilent sur une 2e rangée AU-DESSUS,
					// alignés colonne par colonne (7 au-dessus du 1,
					// 8 au-dessus du 2, etc.).
					const size_t perRow = std::min<size_t>(beltCount, 6u);
					const size_t rowCount = (beltCount + 5u) / 6u;
					const float beltW = static_cast<float>(perRow) * beltSlotSz
						+ static_cast<float>(perRow - 1u) * beltGap;
					const float beltH = static_cast<float>(rowCount) * beltSlotSz
						+ static_cast<float>(rowCount - 1u) * beltGap;
					// Retour de test 2026-07-20 — séparation nette
					// avec les icônes de pouvoir : écart élargi +
					// trait séparateur vertical (dessiné plus bas).
					const float beltX = barX + barWidth + 56.0f;
					const float beltY = dh - beltH - 16.0f; // bord HAUT du bloc
					const bool shiftHeld = m_input.IsDown(engine::platform::Key::Shift);
					const uint32_t beltClientId = m_gameplayUdp.ServerClientId();

					ImGui::SetNextWindowPos(ImVec2(beltX - 34.0f, beltY - 20.0f));
					ImGui::SetNextWindowSize(ImVec2(beltW + 40.0f, beltH + 26.0f));
					ImGui::Begin("##belt_bar", nullptr,
						ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground
						| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
						| ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
						| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
					ImDrawList* bdl = ImGui::GetWindowDrawList();
					// Retour de test 2026-07-20 — pas de libellé « Ceinture x/y »
					// au-dessus de la barre : les cases se suffisent.
					// Trait séparateur vertical entre la barre
					// d'action (icônes de pouvoir) et la ceinture.
					bdl->AddLine(ImVec2(beltX - 26.0f, dh - beltSlotSz - 20.0f),
						ImVec2(beltX - 26.0f, dh - 12.0f),
						IM_COL32(150, 170, 155, 120), 2.0f);

					// Copie de travail : mutée par drop/échange/vidage,
					// envoyée en une fois si changement.
					std::vector<std::string> newBelt(beltLayout.begin(), beltLayout.end());
					bool beltChanged = false;

					for (size_t bi = 0; bi < beltCount; ++bi)
					{
						// row 0 = rangée BASSE (slots 1..6) ; les rangées
						// suivantes montent AU-DESSUS, mêmes colonnes.
						const size_t row = bi / 6u;
						const size_t colIdx = bi % 6u;
						const float bx0 = beltX + static_cast<float>(colIdx) * (beltSlotSz + beltGap);
						const float by0 = beltY
							+ static_cast<float>(rowCount - 1u - row) * (beltSlotSz + beltGap);
						const std::string& beltTok = beltLayout[bi];
						uint32_t beltItemId = 0u;
						const bool occupied = engine::anniversary::ParseItemToken(beltTok, beltItemId);
						const engine::items::ItemDefinition* bdef =
							occupied ? m_itemCatalog.Find(beltItemId) : nullptr;

						ImGui::SetCursorScreenPos(ImVec2(bx0, by0));
						char beltBtnId[16];
						std::snprintf(beltBtnId, sizeof(beltBtnId), "##belt%d", static_cast<int>(bi));
						ImGui::InvisibleButton(beltBtnId, ImVec2(beltSlotSz, beltSlotSz));
						const bool hovered = ImGui::IsItemHovered();

						// Quantité restante en sac (somme des piles).
						uint32_t beltQty = 0u;
						if (occupied)
						{
							for (const engine::client::InventorySlotState& is : m_invUi.GetState().slots)
								if (is.occupied && is.itemId == beltItemId)
									beltQty += is.quantity;
						}

						// Visuel : fond, bordure (survol = doré), raccourci,
						// initiale de l'objet en grand + quantité en bas-droite.
						const ImU32 fillCol = occupied
							? IM_COL32(32, 46, 36, 235) : IM_COL32(14, 16, 22, 170);
						const ImU32 borderCol = hovered
							? IM_COL32(235, 205, 120, 255)
							: IM_COL32(120, 170, 130, occupied ? 220 : 110);
						bdl->AddRectFilled(ImVec2(bx0, by0),
							ImVec2(bx0 + beltSlotSz, by0 + beltSlotSz), fillCol, 6.0f);
						bdl->AddRect(ImVec2(bx0, by0),
							ImVec2(bx0 + beltSlotSz, by0 + beltSlotSz), borderCol, 6.0f, 0,
							hovered ? 2.5f : 2.0f);
						if (bi < 9u)
						{
							char beltKeyLabel[12];
							std::snprintf(beltKeyLabel, sizeof(beltKeyLabel), "M+%d",
								static_cast<int>(bi) + 1);
							bdl->AddText(ImVec2(bx0 + 3.0f, by0 + 2.0f),
								IM_COL32(200, 230, 205, 190), beltKeyLabel);
						}
						if (occupied)
						{
							// Initiale (2 lettres) au centre — lisible sans atlas d'icônes.
							const char* bname = (bdef != nullptr && !bdef->name.empty())
								? bdef->name.c_str() : "?";
							char initials[3] = { bname[0], bname[1] != '\0' ? bname[1] : ' ', '\0' };
							const ImVec2 initSz = ImGui::CalcTextSize(initials);
							bdl->AddText(ImVec2(bx0 + (beltSlotSz - initSz.x) * 0.5f,
								by0 + (beltSlotSz - initSz.y) * 0.5f),
								IM_COL32(240, 250, 240, 255), initials);
							char beltQtyTxt[12];
							std::snprintf(beltQtyTxt, sizeof(beltQtyTxt), "%u", beltQty);
							const ImVec2 qtySz = ImGui::CalcTextSize(beltQtyTxt);
							bdl->AddText(ImVec2(bx0 + beltSlotSz - qtySz.x - 3.0f,
								by0 + beltSlotSz - qtySz.y - 2.0f),
								beltQty > 0u ? IM_COL32(255, 240, 190, 255)
								             : IM_COL32(255, 120, 110, 255),
								beltQtyTxt);
						}

						// Source de drag (réorganisation case → case).
						if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
						{
							int fromIndex = static_cast<int>(bi);
							ImGui::SetDragDropPayload("LN_BELT_MOVE", &fromIndex, sizeof(int));
							ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
								? bdef->name.c_str() : "Objet");
							ImGui::EndDragDropSource();
						}
						// Cible de drop : échange interne OU objet du sac.
						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload* mv = ImGui::AcceptDragDropPayload("LN_BELT_MOVE"))
							{
								if (mv->DataSize == static_cast<int>(sizeof(int)))
								{
									const int from = *static_cast<const int*>(mv->Data);
									if (from >= 0 && from < static_cast<int>(newBelt.size())
										&& from != static_cast<int>(bi))
									{
										std::swap(newBelt[static_cast<size_t>(from)], newBelt[bi]);
										beltChanged = true;
									}
								}
							}
							if (const ImGuiPayload* it = ImGui::AcceptDragDropPayload("LN_EQUIP_ITEM"))
							{
								if (it->DataSize == static_cast<int>(sizeof(uint32_t)))
								{
									const uint32_t droppedId = *static_cast<const uint32_t*>(it->Data);
									const std::string droppedTok =
										engine::anniversary::MakeItemToken(droppedId);
									bool already = false;
									for (const std::string& s : newBelt)
										if (s == droppedTok) { already = true; break; }
									if (!already)
									{
										newBelt[bi] = droppedTok;
										beltChanged = true;
									}
								}
							}
							ImGui::EndDragDropTarget();
						}
						// Clic droit : vider la case.
						if (occupied && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
						{
							newBelt[bi].clear();
							beltChanged = true;
						}
						// Tooltip riche.
						if (hovered && occupied)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
								? bdef->name.c_str() : "Objet");
							if (bdef != nullptr && !bdef->description.empty())
								ImGui::TextDisabled("%s", bdef->description.c_str());
							ImGui::Separator();
							if (bi < 9u)
								ImGui::TextDisabled("Maj+%d ou clic : utiliser", static_cast<int>(bi) + 1);
							else
								ImGui::TextDisabled("Clic : utiliser");
							ImGui::TextDisabled("Clic droit : retirer  |  Glisser : deplacer");
							ImGui::TextDisabled("En sac : %u", beltQty);
							ImGui::EndTooltip();
						}

						// Activation : Maj+1..9, ou clic COMPLET (press +
						// release dans la case, sans drag en cours — permet
						// de glisser sans consommer l'objet).
						const bool beltKeyPressed = keysAllowed && shiftHeld && bi < 9u
							&& m_input.WasPressed(static_cast<engine::platform::Key>('1' + static_cast<int>(bi)));
						const bool beltClickCompleted = ImGui::IsItemDeactivated()
							&& hovered && ImGui::GetDragDropPayload() == nullptr;
						if (occupied && (beltKeyPressed || beltClickCompleted)
							&& beltClientId != 0u)
						{
							(void)m_gameplayUdp.SendCastRequest(beltClientId, 0ull, beltTok);
						}
					}
					ImGui::End();

					if (beltChanged && beltClientId != 0u)
					{
						(void)m_gameplayUdp.SendSetBeltLayout(beltClientId, newBelt);
					}
				}
			}

			// --- Barre de cast (au-dessus de la barre d'action) pilotée
			// par CastBarUpdate ; progression calculée localement.
			if (uiModel.castBar.active && uiModel.castBar.durationMs > 0u)
			{
				const uint64_t nowNs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count());
				const float elapsedSec = static_cast<float>(nowNs - uiModel.castBar.startedAtNs) / 1.0e9f;
				const float durationSec = static_cast<float>(uiModel.castBar.durationMs) / 1000.0f;
				const float progress = std::clamp(elapsedSec / durationSec, 0.0f, 1.0f);
				const float castBarWidth = 260.0f;
				const float castBarHeight = 18.0f;
				const float cbx = (dw - castBarWidth) * 0.5f;
				const float cby = barY - castBarHeight - 10.0f;
				fg->AddRectFilled(ImVec2(cbx, cby), ImVec2(cbx + castBarWidth, cby + castBarHeight),
					IM_COL32(20, 22, 28, 220), 4.0f);
				fg->AddRectFilled(ImVec2(cbx, cby), ImVec2(cbx + castBarWidth * progress, cby + castBarHeight),
					IM_COL32(120, 170, 230, 230), 4.0f);
				const std::string castLabel = m_spellCatalog.ResolveSpellName(uiModel.castBar.spellId);
				const ImVec2 castTs = ImGui::CalcTextSize(castLabel.c_str());
				fg->AddText(ImVec2(cbx + (castBarWidth - castTs.x) * 0.5f, cby + 1.0f),
					IM_COL32(255, 255, 255, 255), castLabel.c_str());
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderBuffBarsImGui(engine::RenderState& out, float& dw, float& dh, const int& ivw, const int& ivh, ImDrawList* fg, const engine::client::UIModel& uiModel)
	{
		{
			const uint64_t nowNs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			// Construit la liste StatusEffect d'une entité depuis le modèle
			// (les timers décrémentent localement entre deux AuraUpdate).
			auto buildEffects = [&uiModel, nowNs](engine::server::EntityId entityId,
				std::list<engine::gameplay::StatusEffect>& outEffects)
			{
				const auto auraIt = uiModel.entityAuras.find(entityId);
				if (auraIt == uiModel.entityAuras.end())
				{
					return;
				}
				for (const engine::client::UIAuraEntry& aura : auraIt->second)
				{
					engine::gameplay::StatusEffect effect{};
					effect.effectId = aura.spellId;
					effect.targetId = entityId;
					effect.stacks = aura.stacks;
					// Mapping wire (SpellEffectType serveur) → type client.
					switch (aura.effectType)
					{
					case 1u: effect.type = engine::gameplay::StatusEffectType::DoT; break;
					case 3u: effect.type = engine::gameplay::StatusEffectType::HoT; break;
					case 5u: effect.type = engine::gameplay::StatusEffectType::Debuff; break;
					case 7u: effect.type = engine::gameplay::StatusEffectType::Slow; break;
					default: effect.type = engine::gameplay::StatusEffectType::Buff; break;
					}
					effect.startTimeNs = aura.receivedAtNs;
					effect.durationSeconds = static_cast<float>(aura.remainingMs) / 1000.0f;
					effect.expireTimeNs = aura.receivedAtNs
						+ static_cast<uint64_t>(aura.remainingMs) * 1000000ull;
					outEffects.push_back(std::move(effect));
				}
			};
			std::list<engine::gameplay::StatusEffect> playerEffects;
			buildEffects(static_cast<engine::server::EntityId>(uiModel.playerStats.playerEntityId), playerEffects);
			m_buffBar.UpdatePlayer(playerEffects.empty() ? nullptr : &playerEffects, nowNs);
			std::list<engine::gameplay::StatusEffect> targetEffects;
			if (uiModel.targetStats.hasTarget)
			{
				buildEffects(uiModel.targetStats.entityId, targetEffects);
			}
			m_buffBar.UpdateTarget(targetEffects.empty() ? nullptr : &targetEffects, nowNs);

			// Rendu en pastilles texte (icônes graphiques = chantier atlas).
			auto drawBar = [this, fg](const engine::client::BuffBarWidget& bar, float x, float y, bool harmful)
			{
				if (!bar.visible)
				{
					return;
				}
				float cursorX = x;
				for (const engine::client::BuffIconWidget& icon : bar.icons)
				{
					if (!icon.visible)
					{
						continue;
					}
					std::string chip = m_spellCatalog.ResolveSpellName(icon.effectId);
					if (!icon.isPermanent && icon.remainingSeconds > 0.0f)
					{
						char timerText[16];
						std::snprintf(timerText, sizeof(timerText), " %.0fs", icon.remainingSeconds);
						chip += timerText;
					}
					if (icon.stacks > 1u)
					{
						chip += " x" + std::to_string(icon.stacks);
					}
					const ImVec2 chipTs = ImGui::CalcTextSize(chip.c_str());
					fg->AddRectFilled(ImVec2(cursorX, y), ImVec2(cursorX + chipTs.x + 10.0f, y + chipTs.y + 6.0f),
						harmful ? IM_COL32(70, 20, 20, 210) : IM_COL32(20, 50, 25, 210), 4.0f);
					fg->AddText(ImVec2(cursorX + 5.0f, y + 3.0f), IM_COL32(230, 230, 230, 255), chip.c_str());
					cursorX += chipTs.x + 16.0f;
				}
			};
			const engine::client::BuffBarState& playerBuffState = m_buffBar.GetPlayerState();
			drawBar(playerBuffState.buffBar, 18.0f, dh - 120.0f, false);
			drawBar(playerBuffState.debuffBar, 18.0f, dh - 92.0f, true);
			if (uiModel.targetStats.hasTarget)
			{
				const engine::client::BuffBarState& targetBuffState = m_buffBar.GetTargetState();
				// Sous le cadre cible (haut-centre, cf. bloc SP2 : fy 56 + 54).
				drawBar(targetBuffState.buffBar, (dw - 260.0f) * 0.5f, 118.0f, false);
				drawBar(targetBuffState.debuffBar, (dw - 260.0f) * 0.5f, 144.0f, true);
			}

			// --- Combat SP4 : threat meter — alimente le présentateur pour la
			// cible courante (rebuild complet, ≤ 5 entrées : coût négligeable).
			if (uiModel.targetStats.hasTarget)
			{
				m_advancedCombat.ClearThreat(uiModel.targetStats.entityId);
				const auto threatIt = uiModel.threatByMob.find(uiModel.targetStats.entityId);
				if (threatIt != uiModel.threatByMob.end())
				{
					for (const engine::client::UIThreatEntry& threatEntry : threatIt->second)
					{
						std::string threatName = "Joueur";
						if (threatEntry.playerEntityId == uiModel.playerStats.playerEntityId)
						{
							threatName = "Vous";
						}
						else
						{
							for (const engine::client::UIRemoteEntity& re : uiModel.remoteEntities)
							{
								if (re.entityId == threatEntry.playerEntityId)
								{
									threatName = !re.displayName.empty()
										? re.displayName
										: ("P" + std::to_string(re.playerClientId));
									break;
								}
							}
						}
						m_advancedCombat.UpdateThreat(uiModel.targetStats.entityId,
							threatEntry.playerEntityId, threatName, threatEntry.threatValue);
					}
				}
			}

			// --- Combat SP4 : FX d'auras — sync de la couche données
			// (AuraFXSystem, M39.4 enfin câblé) puis halo écran-espace coloré
			// aux pieds de chaque entité sous aura (couleur ResolveAuraVisuals).
			{
				const engine::math::Vec3 localFxPos = m_characterController.GetPosition();
				std::list<engine::gameplay::StatusEffect> fxEffects;
				for (const auto& auraPair : uiModel.entityAuras)
				{
					const engine::server::EntityId fxEntityId = auraPair.first;
					float fxX = localFxPos.x, fxY = localFxPos.y, fxZ = localFxPos.z;
					if (fxEntityId != uiModel.playerStats.playerEntityId)
					{
						const auto smoothedIt = m_remoteSmoothed.find(fxEntityId);
						if (smoothedIt == m_remoteSmoothed.end() || !smoothedIt->second.valid)
						{
							continue; // entité sans position connue : pas de FX ce frame.
						}
						fxX = smoothedIt->second.x;
						fxY = smoothedIt->second.y;
						fxZ = smoothedIt->second.z;
						// Combat SP1 fix — un halo de mob doit suivre le snap visuel
						// au sol (sinon l'anneau se projette sous le terrain). On
						// retrouve l'entité pour distinguer joueur (Y fiable) / mob.
						for (const engine::client::UIRemoteEntity& fxRe : uiModel.remoteEntities)
						{
							if (fxRe.entityId != fxEntityId)
								continue;
							fxY = ResolveRemoteDisplayCenterY(
								fxRe.playerClientId != 0u, fxY, fxX, fxZ);
							break;
						}
					}
					fxEffects.clear();
					buildEffects(fxEntityId, fxEffects);
					m_auraFx.Sync(fxEntityId, fxEffects.empty() ? nullptr : &fxEffects, fxX, fxY, fxZ);
				}
				// Purge des FX d'entités qui n'ont plus aucune aura répliquée.
				std::vector<uint64_t> staleFxEntities;
				for (const engine::client::ActiveAura& fxAura : m_auraFx.GetAuras())
				{
					if (uiModel.entityAuras.find(fxAura.entityId) == uiModel.entityAuras.end())
					{
						staleFxEntities.push_back(fxAura.entityId);
					}
				}
				for (const uint64_t staleEntityId : staleFxEntities)
				{
					m_auraFx.RemoveEntity(staleEntityId);
				}
				// Rendu : un anneau par aura, aux pieds de l'entité.
				for (const engine::client::ActiveAura& fxAura : m_auraFx.GetAuras())
				{
					if (!fxAura.alive)
					{
						continue;
					}
					float haloX = 0.0f, haloY = 0.0f;
					if (!WorldToScreenPx(out.viewProjMatrix.m,
						fxAura.positionX, fxAura.positionY - 0.85f, fxAura.positionZ,
						ivw, ivh, haloX, haloY))
					{
						continue;
					}
					const ImU32 haloColor = IM_COL32(
						static_cast<int>(fxAura.glowColor.r * 255.0f),
						static_cast<int>(fxAura.glowColor.g * 255.0f),
						static_cast<int>(fxAura.glowColor.b * 255.0f),
						190);
					fg->AddCircle(ImVec2(haloX, haloY), 20.0f, haloColor, 24, 2.5f);
				}
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderCraftingWindowImGui(float& dw, const engine::client::UIModel& uiModel)
	{
		if (m_craftingVisible)
		{
			ImGui::SetNextWindowPos(ImVec2(dw * 0.5f - 230.0f, 120.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(460.0f, 430.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Artisanat", &m_craftingVisible))
			{
				const uint32_t craftClientId = m_gameplayUdp.ServerClientId();
				if (uiModel.crafting.professions.empty())
				{
					ImGui::TextUnformatted("Aucun metier connu.");
				}
				// Onglets métiers → demande de liste de recettes.
				for (size_t professionIndex = 0;
					professionIndex < uiModel.crafting.professions.size(); ++professionIndex)
				{
					const engine::client::UIProfessionEntry& prof =
						uiModel.crafting.professions[professionIndex];
					if (professionIndex > 0)
						ImGui::SameLine();
					const std::string tabLabel = prof.professionKey
						+ " (" + std::to_string(prof.skillLevel) + ")";
					if (ImGui::SmallButton(tabLabel.c_str())
						&& prof.professionKey != uiModel.crafting.activeProfessionKey
						&& craftClientId != 0u)
					{
						(void)m_gameplayUdp.SendCraftRecipeListRequest(
							craftClientId, prof.professionKey);
					}
				}
				ImGui::Separator();
				// Liste des recettes (clic = sélection).
				ImGui::BeginChild("##ln_craft_recipes", ImVec2(0.0f, 220.0f), true);
				for (size_t recipeIndex = 0;
					recipeIndex < uiModel.crafting.recipes.size(); ++recipeIndex)
				{
					const engine::client::UICraftRecipeRow& recipe =
						uiModel.crafting.recipes[recipeIndex];
					const bool selected = (uiModel.crafting.selectedRecipeIndex
						== static_cast<uint32_t>(recipeIndex));
					const std::string rowLabel = recipe.recipeId
						+ "  (comp. " + std::to_string(recipe.skillRequired)
						+ ", x" + std::to_string(recipe.outputQuantity) + ")";
					if (ImGui::Selectable(rowLabel.c_str(), selected))
					{
						(void)m_uiModelBinding.SelectCraftRecipe(
							static_cast<uint32_t>(recipeIndex));
					}
				}
				ImGui::EndChild();
				// Fabrication : bouton + annulation + cast bar + qualité.
				const bool craftInProgress = uiModel.crafting.craftFillFraction > 0.0f;
				const bool canCraft = !craftInProgress
					&& uiModel.crafting.selectedRecipeIndex < uiModel.crafting.recipes.size();
				if (ImGui::Button("Fabriquer", ImVec2(150.0f, 32.0f))
					&& canCraft && craftClientId != 0u)
				{
					(void)m_gameplayUdp.SendCraftRequest(craftClientId,
						uiModel.crafting.recipes[uiModel.crafting.selectedRecipeIndex].recipeId);
				}
				if (craftInProgress)
				{
					ImGui::SameLine();
					if (ImGui::Button("Annuler", ImVec2(110.0f, 32.0f)) && craftClientId != 0u)
					{
						(void)m_gameplayUdp.SendCraftCancelRequest(craftClientId);
					}
					ImGui::ProgressBar(uiModel.crafting.craftFillFraction, ImVec2(-1.0f, 10.0f), "");
				}
				else if (!canCraft)
				{
					ImGui::TextUnformatted("Selectionnez une recette.");
				}
				// Qualité du dernier craft (M36.3) via le présentateur câblé.
				const engine::client::CraftingPanelState& craftState = m_craftingUi.GetState();
				if (!craftState.lastQualityLabel.empty())
				{
					ImGui::TextColored(
						ImVec4(craftState.lastQualityColor.r, craftState.lastQualityColor.g,
							craftState.lastQualityColor.b, 1.0f),
						"Derniere fabrication : %s", craftState.lastQualityLabel.c_str());
				}
				if (!craftState.statusText.empty())
				{
					ImGui::TextUnformatted(craftState.statusText.c_str());
				}
			}
			ImGui::End();
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderPlayerGaugesImGui(float& dw, float& dh, ImDrawList* fg, const engine::client::UIModel& uiModel)
	{
		{
			const uint32_t playerMaxHp = (uiModel.playerStats.maxHealth > 0u)
				? uiModel.playerStats.maxHealth
				: uiModel.playerStats.sheetMaxHealth;
			if (playerMaxHp > 0u)
			{
				const uint32_t playerCurHp = (uiModel.playerStats.maxHealth > 0u)
					? uiModel.playerStats.currentHealth
					: playerMaxHp;
				const float hpFracPlayer = std::clamp(
					static_cast<float>(playerCurHp) / static_cast<float>(playerMaxHp), 0.0f, 1.0f);
				const float gaugeW = 320.0f;
				const float gaugeH = 18.0f;
				const float gaugeX = (dw - gaugeW) * 0.5f;
				const float gaugeY = dh - 152.0f;
				const ImU32 hpColor = (hpFracPlayer > 0.5f)
					? IM_COL32(80, 200, 80, 235)
					: ((hpFracPlayer > 0.25f)
						? IM_COL32(230, 160, 40, 235)
						: IM_COL32(210, 60, 50, 235));
				fg->AddRectFilled(ImVec2(gaugeX - 2.0f, gaugeY - 2.0f),
					ImVec2(gaugeX + gaugeW + 2.0f, gaugeY + gaugeH + 2.0f), IM_COL32(0, 0, 0, 160), 4.0f);
				fg->AddRectFilled(ImVec2(gaugeX, gaugeY),
					ImVec2(gaugeX + gaugeW, gaugeY + gaugeH), IM_COL32(35, 38, 44, 230), 3.0f);
				fg->AddRectFilled(ImVec2(gaugeX, gaugeY),
					ImVec2(gaugeX + gaugeW * hpFracPlayer, gaugeY + gaugeH), hpColor, 3.0f);
				const std::string playerHpText =
					std::to_string(playerCurHp) + " / " + std::to_string(playerMaxHp);
				const ImVec2 playerHpTs = ImGui::CalcTextSize(playerHpText.c_str());
				fg->AddText(ImVec2(gaugeX + (gaugeW - playerHpTs.x) * 0.5f, gaugeY + 1.0f),
					IM_COL32(255, 255, 255, 255), playerHpText.c_str());
				// Ressource de classe (mana/énergie…) : barre fine dessous,
				// alimentée par les ResourceUpdate (SP3).
				if (uiModel.playerStats.secondaryResourceMax > 0u)
				{
					const float resFrac = std::clamp(
						static_cast<float>(uiModel.playerStats.secondaryResourceCurrent)
							/ static_cast<float>(uiModel.playerStats.secondaryResourceMax),
						0.0f, 1.0f);
					const float resY = gaugeY + gaugeH + 4.0f;
					fg->AddRectFilled(ImVec2(gaugeX, resY),
						ImVec2(gaugeX + gaugeW, resY + 8.0f), IM_COL32(35, 38, 44, 230), 3.0f);
					fg->AddRectFilled(ImVec2(gaugeX, resY),
						ImVec2(gaugeX + gaugeW * resFrac, resY + 8.0f), IM_COL32(70, 130, 220, 235), 3.0f);
				}
				// --- Barre d'XP (PR-C) : fine barre dorée sous la vie/ressource,
				// alimentée par PlayerXpUpdate (serveur, enter-world + chaque gain).
				// Niveau à gauche, progression xp/prochain niveau centrée dessous.
				// hasXp faux tant qu'aucun PlayerXpUpdate reçu -> pas de barre.
				if (uiModel.playerStats.hasXp)
				{
					const bool atCap = (uiModel.playerStats.xpForNextLevel == 0u);
					const float xpFrac = atCap ? 1.0f : std::clamp(
						static_cast<float>(uiModel.playerStats.xpIntoLevel)
							/ static_cast<float>(uiModel.playerStats.xpForNextLevel),
						0.0f, 1.0f);
					const float xpY = gaugeY + gaugeH + 16.0f;
					const float xpH = 6.0f;
					fg->AddRectFilled(ImVec2(gaugeX, xpY),
						ImVec2(gaugeX + gaugeW, xpY + xpH), IM_COL32(30, 26, 12, 230), 2.0f);
					fg->AddRectFilled(ImVec2(gaugeX, xpY),
						ImVec2(gaugeX + gaugeW * xpFrac, xpY + xpH), IM_COL32(220, 180, 60, 235), 2.0f);
					// Niveau, à gauche de la barre.
					char lvlBuf[32];
					std::snprintf(lvlBuf, sizeof(lvlBuf), "Nv. %u", uiModel.playerStats.level);
					const ImVec2 lvlTs = ImGui::CalcTextSize(lvlBuf);
					fg->AddText(ImVec2(gaugeX - lvlTs.x - 8.0f, xpY - 4.0f),
						IM_COL32(230, 210, 150, 255), lvlBuf);
					// Progression xp/prochain niveau (ou « Niveau max ») centrée dessous.
					char xpBuf[48];
					if (atCap)
						std::snprintf(xpBuf, sizeof(xpBuf), "Niveau max");
					else
						std::snprintf(xpBuf, sizeof(xpBuf), "%u / %u XP",
							uiModel.playerStats.xpIntoLevel, uiModel.playerStats.xpForNextLevel);
					const ImVec2 xpTs = ImGui::CalcTextSize(xpBuf);
					fg->AddText(ImVec2(gaugeX + (gaugeW - xpTs.x) * 0.5f, xpY + xpH + 1.0f),
						IM_COL32(200, 190, 160, 220), xpBuf);
				}
			}
			else
			{
				// FIX.1 — stats pas encore reçues du shard : jauge grisée au même
				// emplacement plutôt que rien (le HUD garde sa forme, le bandeau
				// ci-dessus explique l'attente).
				const float gaugeW = 320.0f;
				const float gaugeH = 18.0f;
				const float gaugeX = (dw - gaugeW) * 0.5f;
				const float gaugeY = dh - 152.0f;
				fg->AddRectFilled(ImVec2(gaugeX - 2.0f, gaugeY - 2.0f),
					ImVec2(gaugeX + gaugeW + 2.0f, gaugeY + gaugeH + 2.0f), IM_COL32(0, 0, 0, 160), 4.0f);
				fg->AddRectFilled(ImVec2(gaugeX, gaugeY),
					ImVec2(gaugeX + gaugeW, gaugeY + gaugeH), IM_COL32(55, 58, 64, 230), 3.0f);
				const char* waitText = "PV : en attente du serveur";
				const ImVec2 waitTs = ImGui::CalcTextSize(waitText);
				fg->AddText(ImVec2(gaugeX + (gaugeW - waitTs.x) * 0.5f, gaugeY + 1.0f),
					IM_COL32(200, 200, 205, 230), waitText);
			}
		}
	}

	// Sous-étape extraite de RenderInGameImGuiFrame — voir la documentation dans Engine.h.
	void Engine::RenderPauseMenuImGui(float& dw, float& dh)
	{
		if (m_inGamePauseMenuVisible)
		{
			const LnTheme::Palette& th = LnTheme::Active();
			// Conversion couleur thème -> ImVec4 ImGui, locale au menu pause.
			auto iv = [](const LnTheme::Rgba& c, float a) -> ImVec4 {
				return ImVec4(c.r, c.g, c.b, a);
			};
			const float menuW = 340.f;
			const float menuH = 250.f;
			const float btnW = menuW - 64.f;
			ImGui::SetNextWindowPos(ImVec2((dw - menuW) * 0.5f, (dh - menuH) * 0.5f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
			// Chrome variante C : fond panneau + cadre accentué (doré en Or royal,
			// vert en Sylve émeraude). On retire SetNextWindowBgAlpha : il écraserait
			// l'alpha du WindowBg ci-dessous.
			ImGui::PushStyleColor(ImGuiCol_WindowBg, iv(th.panel, 0.96f));
			ImGui::PushStyleColor(ImGuiCol_Border, iv(th.accent, 1.f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
			ImGui::Begin("##ln_pause_menu", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);

			// Titre PAUSE : couleur accent, centré sur la zone de contenu réelle
			// (GetContentRegionAvail tient compte du padding -> centrage correct).
			ImGui::SetWindowFontScale(1.3f);
			ImGui::PushStyleColor(ImGuiCol_Text, iv(th.accent, 1.f));
			const char* title = "PAUSE";
			const float titleW = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - titleW) * 0.5f);
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.f);

			// Séparateur doré.
			ImGui::PushStyleColor(ImGuiCol_Separator, iv(th.accent, 0.7f));
			ImGui::Separator();
			ImGui::PopStyleColor();
			ImGui::Spacing();
			ImGui::Spacing();

			// Bouton thémé centré, avec halo de bordure (accent, ou rouge danger)
			// dessiné par-dessus au survol.
			auto pauseButton = [&](const char* label, bool danger) -> bool {
				const LnTheme::Rgba hov = danger ? th.errorCol : th.accent;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - btnW) * 0.5f);
				ImGui::PushStyleColor(ImGuiCol_Button, iv(th.surface, 1.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iv(hov, 0.22f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, iv(hov, 0.35f));
				ImGui::PushStyleColor(ImGuiCol_Border, iv(th.border, 1.f));
				ImGui::PushStyleColor(ImGuiCol_Text, danger ? iv(th.errorCol, 1.f) : iv(th.text, 1.f));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
				const bool pressed = ImGui::Button(label, ImVec2(btnW, 34.f));
				if (ImGui::IsItemHovered())
				{
					ImGui::GetWindowDrawList()->AddRect(
						ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
						ImGui::ColorConvertFloat4ToU32(iv(hov, 1.f)), 3.f, 0, 1.5f);
				}
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(5);
				return pressed;
			};

			if (pauseButton("Reprendre", false))
			{
				m_inGamePauseMenuVisible = false;
			}
			ImGui::Spacing();
			if (pauseButton("Options", false))
			{
				m_inGamePauseMenuVisible = false;
				m_inGameOptionsPanelVisible = true;
				// B2/ST5 — pose le contexte in-game et initialise les valeurs *Pending
				// de l'écran d'options unifié (sans toucher la phase auth).
				m_authUi.OpenLanguageOptionsInGame();
			}
			ImGui::Spacing();
			if (pauseButton("Se deconnecter", false))
			{
				RequestLogoutToLoginScreen();
			}
			ImGui::Spacing();
			if (pauseButton("Quitter le jeu", true))
			{
				OnQuit();
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
		}
	}

#endif
} // namespace engine
