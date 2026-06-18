#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/core/Profiler.h"
#include "src/shared/core/Time.h"
#include "src/shared/core/memory/FrameArena.h"
#include "src/client/audio/AudioEngine.h"
#include "src/client/auth/AuthUi.h"
#include "src/client/chat/ChatUi.h"
#include "src/client/mail/MailUi.h"
#include "src/client/quest/QuestUi.h"
#include "src/client/social/IgnoreListUi.h"
#include "src/client/gmtickets/GmTicketUi.h"
#include "src/client/reputation/ReputationUi.h"
#include "src/client/arena/ArenaUi.h"
#include "src/client/battleground/BattleGroundUi.h"
#include "src/client/outdoorpvp/OutdoorPvpUi.h"
#include "src/client/weather/WeatherUi.h"
#include "src/client/events/GameEventUi.h"
#include "src/client/guild/GuildUi.h"
#include "src/client/auction/AuctionUi.h"
#include "src/client/loot/LootRollUi.h"
#include "src/client/lfg/LfgUi.h"
#include "src/client/cinematics/CinematicUi.h"
#include "src/client/skills/SkillBookUi.h"
#include "src/client/grimoire/GrimoireUi.h"
#include "src/client/trade/TradeWindowUi.h"
#include "src/client/economy/AuctionUi.h"
#include "src/client/net/GameplayUdpClient.h"
#include "src/client/inventory/InventoryUi.h"
// Combat SP2 — présentateurs combat câblés (HUD cible/log + panneau avancé).
#include "src/client/combat/CombatHud.h"
#include "src/client/combat/AdvancedCombatUi.h"
// Combat SP3 — barre d'action (kits de sorts) + BuffBar (auras répliquées).
#include "src/client/combat/BuffBarPresenter.h"
#include "src/client/gameplay/SpellKitCatalog.h"
#include "src/client/gameplay/ClassSkillCatalog.h"
#include "src/client/render/SkillIconCache.h"
#include "src/client/skills/ClassSkillTreeUi.h"
// Combat SP4 — FX visuels d'auras (halo aux pieds des entités).
#include "src/client/combat/AuraFXSystem.h"
// Réticule de ciblage au sol (cercles + cône de vision 120°, decal orienté).
#include "src/client/combat/TargetReticleSystem.h"
// Groupes SP1 — cadres de groupe (M32.2 enfin câblé).
#include "src/client/social/PartyHud.h"
// Métiers SP1 — barre de récolte (M36.1) + panneau d'artisanat (M36.2).
#include "src/client/crafting/HarvestCastBar.h"
#include "src/client/crafting/CraftingUi.h"
#include "src/client/debug/ProfilerHud.h"
#include "src/client/economy/ShopUi.h"
#include "src/client/ui_common/UIModel.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"
#include "src/client/render/AssetRegistry.h"
#include "src/client/render/ImpostorAsset.h"
#include "src/client/render/DecalSystem.h"
#include "src/client/render/FrameGraph.h"
#include "src/client/render/vk/VkDeviceContext.h"
#include "src/client/render/vk/VkFrameSync.h"
#include "src/client/render/vk/VkInstance.h"
#include "src/client/render/vk/VkSwapchain.h"
#include "src/client/render/ShaderCache.h"
#include "src/client/render/ShaderCompiler.h"
#include "src/client/render/ShaderHotReload.h"
#include "src/client/render/AuthGlyphPass.h"
#include "src/client/render/AuthLogoPass.h"
#include "src/client/render/TaaJitter.h"
#include "src/client/render/Camera.h"
#include "src/client/render/CascadedShadowMaps.h"
#include "src/client/render/UnderwaterPass.h"
#include "src/client/render/WaterMeshGpu.h"
#include "src/client/render/WaterPass.h"
#include "src/client/render/DayNightCycle.h"
#include "src/client/render/SkyPass.h"
#include "src/client/render/gi/DdgiVolume.h"
#include "src/client/render/gi/DdgiUpdatePass.h"
#include "src/client/render/gi/DdgiQuality.h"
#include "src/client/render/WeatherSystem.h"
#include "src/client/render/DynamicLightSystem.h"
#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/math/Frustum.h"
#include "src/shared/math/Math.h"
#include "src/client/world/WorldModel.h"
#include "src/client/world/CreatureCatalog.h"
#include "src/client/world/ChunkBudgetStats.h"
#include "src/client/world/LodConfig.h"
#include "src/client/world/HlodRuntime.h"
#include "src/client/world/ProbeData.h"
#include "src/client/world/StreamingScheduler.h"
#include "src/client/world/StreamCache.h"
#include "src/client/render/vk/DeferredDestroyQueue.h"
#include "src/client/render/GpuUploadQueue.h"
#include "src/client/render/vk/StagingAllocator.h"
#include "src/client/render/terrain/TerrainRenderer.h"
#include "src/client/world/terrain/HeightmapHeightField.h"
#include "src/client/world/terrain/ChunkHeightField.h"
#include "src/client/render/terrain_chunk/TerrainChunkRenderer.h"
// Sous-projet A (Task 15) : skinned humanoid avatar runtime.
#include "src/client/render/skinned/SkinnedRenderer.h"
#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/client/render/skinned/SkinnedMeshLoader.h"
#include "src/client/render/skinned/AvatarMaterialRouting.h"
#include "src/client/render/skinned/AnimationSampler.h"
// Sous-projet B.1 (Task 11) : crossfade entre clips de locomotion (7 etats).
#include "src/client/render/skinned/AnimationCrossfade.h"
// Sous-projet B.1 (Task 9) : physics + collision pour l'avatar joueur.
#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/TerrainCollider.h"
#include "src/client/gameplay/CompositeWorldCollider.h"
#if defined(_WIN32)
#include "src/client/render/terrain/TerrainEditingTools.h"
#include "src/world_editor/ui/TexturePreviewCache.h"
#endif
#include "src/world_editor/render/EditorViewportRenderTarget.h"
// Sous-projet C MVP (Task 12) — viewport offscreen pour l'apercu race
// dans l'ecran ImGui de creation de personnage.
#include "src/client/render/race/RacePreviewViewport.h"
// Cellule de dialogue PNJ (logique pure + journal).
#include "src/client/dialogue/DialogueTree.h"
#include "src/client/dialogue/DialoguePresenter.h"
#include "src/client/dialogue/QuestConversationJournal.h"

struct GLFWwindow;

namespace engine::render
{
	class AuthImGuiRenderer;
	class ChatImGuiRenderer;
	class DialogueImGuiRenderer;
	class MailImGuiRenderer;
	class GmTicketImGuiRenderer;
	class ReputationImGuiRenderer;
	class CharacterSheetImGuiRenderer;
	class LfgImGuiRenderer;
	class CinematicImGuiRenderer;
	class SkillBookImGuiRenderer;
	class GrimoireImGuiRenderer;
	class ClassSkillTreeImGuiRenderer;
	class ArenaImGuiRenderer;
	class BattleGroundImGuiRenderer;
	class OutdoorPvpImGuiRenderer;
	class WeatherImGuiRenderer;
	class GameEventImGuiRenderer;
	class GuildImGuiRenderer;
	class AuctionImGuiRenderer;
	class LootRollImGuiRenderer;
	class EditorHubImGuiRenderer;
	class DeferredPipeline;
}
namespace engine::editor
{
	class EditorMode;
	class WorldEditorImGui;
	class WorldEditorSession;
}
namespace engine::editor::world
{
	/// M100.1 — Coquille du nouvel éditeur monde, indépendante du shell M43.x.
	class WorldEditorShell;
}

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine
{
	/// Minimal render state produced by Update and consumed by Render.
	struct RenderState
	{
		engine::render::Camera camera;
		engine::math::Mat4 viewMatrix;
		engine::math::Mat4 projMatrix;
		engine::math::Mat4 viewProjMatrix;
		/// ViewProj SANS le jitter TAA (= projMatrix non-jitterée * viewMatrix).
		/// Utilisée par la passe nuages : raymarcher avec la matrice jitterée fait
		/// trembler le rayon en sous-pixel chaque frame -> scintillement des nuages
		/// (haute fréquence, non reprojetables par le TAA). Stable temporellement.
		engine::math::Mat4 viewProjMatrixUnjittered;
		/// M07.1: ViewProj from previous frame (for TAA reprojection).
		engine::math::Mat4 prevViewProjMatrix;
		/// M07.1: Current frame jitter in NDC (x, y), applied to projection.
		float jitterCurrNdc[2]{ 0.0f, 0.0f };
		engine::math::Frustum frustum;
		engine::render::CascadesUniform cascades;
		// Snapshot des point lights actives pour la passe Lighting. Rempli au
		// moment de l'assemblage du RenderState (même endroit que cascades),
		// lu dans le lambda Lighting → découple m_dynamicLights du thread de
		// rendu (anti data-race).
		std::vector<engine::render::ActivePointLight> pointLights;
		float objectModelMatrix[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		bool objectVisible = true;

		// Placeholder draw-list marker.
		uint32_t drawItemCount = 0;
		/// M09.5: Debug overlay HLOD state (e.g. "HLOD: 3 inst: 5 culled: 2").
		std::string hlodDebugText;
		/// M18.1: Debug overlay profiler state (CPU totals + GPU pass breakdown).
		std::string profilerDebugText;
		/// M29.1: Chat panel text (history + input) for debug overlay / future HUD renderer.
		std::string chatDebugText;
		/// M35.2: Vendor shop + inventory interaction debug HUD (requires `client.gameplay_udp.enabled`).
		std::string gameplayHudDebugText;
		/// STAB.13: Login/register panel text (until master/shard gate completes).
		std::string authHudText;
	};

	/// Engine loop: BeginFrame/Update/Render/EndFrame with double-buffered RenderState.
	class Engine final
	{
	public:
		Engine(int argc, char** argv);
		~Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		/// Run the main loop until quit is requested.
		int Run();

		/// Registers a shader for hot-reload (path relative to paths.content, e.g. "shaders/foo.vert").
		void WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines = {});

		/// Returns the shader cache (e.g. to get SPIR-V by key).
		engine::render::ShaderCache& GetShaderCache() { return m_shaderCache; }
		const engine::render::ShaderCache& GetShaderCache() const { return m_shaderCache; }

		/// M07.2: TAA history ping-pong. next = write target this frame, prev = read (previous frame).
		engine::render::ResourceId GetTaaHistoryPrevId() const;
		engine::render::ResourceId GetTaaHistoryNextId() const;

		/// Sous-projet C MVP — Retourne le mesh skinned pour la race `raceId`,
		/// ou le mesh "humains" si la race est inconnue / pas chargee. Retourne
		/// nullptr si meme "humains" est absent (cas pathologique : boot rate).
		/// Utilise par EnterWorld pour resoudre le mesh du perso joueur, et
		/// par RacePreviewViewport pour l'apercu race dans l'ecran de
		/// creation de personnage.
		engine::render::skinned::SkinnedMesh* GetRaceMesh(const std::string& raceId);

		/// Variante genre-explicite : resout le mesh raceId pour `gender`
		/// ("male"/"female"). Cherche "raceId|gender", retombe sur le male de la
		/// race, puis sur "humains|male", puis nullptr. Utilise par l'apercu de
		/// creation (bascule de genre live) et en interne par GetRaceMesh.
		engine::render::skinned::SkinnedMesh* GetRaceMesh(const std::string& raceId,
		                                                  const std::string& gender);

		/// Change le genre actif de l'avatar ("male"/"female") : met a jour
		/// m_avatarGender (mesh + materiau de peau du genre seront pris au prochain
		/// EnterWorld et par l'apercu) et persiste le choix (fichier dedie merge au
		/// boot). Sans effet si gender invalide. Appele par le selecteur de creation.
		void SetAvatarGender(const std::string& gender);

		/// Combat SP1 fix — Y d'affichage « équivalent centre capsule » d'une
		/// entité distante. Les JOUEURS répliquent un centre capsule fiable
		/// (positionY serveur reprise telle quelle). Les MOBS, NODES de récolte
		/// et LOOT BAGS répliquent le Y brut de leur donnée de spawn (souvent
		/// 0.0) que le serveur ne colle jamais au terrain (pas de heightfield
		/// serveur) : sans correction ils se dessinent SOUS le terrain — mesh
		/// invisible (depth-testé) et plaque flottante au ras du sol (foreground
		/// sans depth test). On échantillonne donc la heightmap CLIENT et on
		/// renvoie sol + 0.9 m, cohérent avec l'offset -0.9 (centre→pieds) du
		/// rendu skinné. Affichage uniquement : le Y serveur reste l'autorité
		/// gameplay (les tests de portée serveur sont en XZ).
		/// \param isPlayer    true si l'entité est un joueur (playerClientId != 0).
		/// \param serverY     Y répliqué par le serveur (mètres monde).
		/// \param worldX,worldZ  position horizontale pour l'échantillon terrain.
		float ResolveRemoteDisplayCenterY(bool isPlayer, float serverY,
		                                  float worldX, float worldZ) const;

		/// Persiste et applique le genre d'un personnage donne, par NOM
		/// (fix client interim #1 : le genre n'est pas encore stocke serveur).
		/// Met a jour m_avatarGender pour la session ET ecrit
		/// `characters.<nom>.gender` dans character_appearance.json (merge au boot),
		/// pour que l'EnterWorld d'un perso existant retrouve son genre au relog.
		/// Sans effet si gender invalide ; repli sur SetAvatarGender si nom vide.
		/// Appele a la creation de personnage (nom + genre finaux).
		void SetCharacterGender(const std::string& characterName, const std::string& gender);

		/// Genre actif courant ("male"/"female").
		const std::string& GetAvatarGender() const { return m_avatarGender; }

	private:
		/// Clef de stockage d'un mesh de race dans m_raceMeshes : "raceId|gender".
		static std::string RaceMeshKey(const std::string& raceId, const std::string& gender)
		{
			return raceId + "|" + gender;
		}

		void BeginFrame();
		void Update();
		void Render();
		void EndFrame();

		void SwapRenderState();

		void OnResize(int w, int h);
		void OnQuit();
		/// Menu pause in-game (touche Echap post-auth) : ouvre/ferme le panneau avec
		/// les actions Quitter / Options / Deconnecter. La 1ere version (M44.x) est
		/// minimale ; pas de gating vers UI options ni reset complet du world state.
		void ToggleInGamePauseMenu();
		/// Termine la session in-game et ramene le client a l'ecran de connexion :
		/// ferme la connexion UDP gameplay, marque l'auth UI comme non complete,
		/// repositionne sur Phase::Login. Le master/shard handshake recommencera
		/// au prochain clic Se connecter.
		void RequestLogoutToLoginScreen();
		/// Optional UDP gameplay + shop/inventory HUD (M35.2); controlled by `client.gameplay_udp.enabled`.
		void InitGameplayNet();
		/// Tears down presenters, UI binding, and UDP socket created by \ref InitGameplayNet.
		void ShutdownGameplayNet();
		/// Handles shop open (V), buy clicks/digits, right-drag sell + Y/N confirm when enabled.
		void UpdateGameplayNet(float deltaSeconds);
		/// Drains non-blocking UDP packets into \ref m_uiModelBinding (Welcome excluded; client id from handshake).
		void PumpGameplayPackets();
		/// Consomme et applique les commandes de réglages (vidéo / audio / contrôles / jeu)
		/// stagées par l'écran d'options (\ref AuthUiPresenter). Idempotent : chaque
		/// `ConsumePending*Settings()` renvoie une commande vide tant qu'aucun apply n'est
		/// demandé, donc l'appel est sûr CHAQUE FRAME (auth comme in-game). C'est ce qui
		/// permet de réutiliser l'écran d'options en jeu (refactor B2).
		///
		/// \param authGateActive true tant que le flux d'auth n'est pas terminé. Garde-fou
		///        réseau : la (ré)initialisation / l'arrêt du réseau gameplay
		///        (\ref InitGameplayNet / \ref ShutdownGameplayNet) déclenchés par un
		///        changement de `client.gameplay_udp.enabled` ne sont effectués QUE pendant
		///        l'auth, pour ne pas couper intempestivement une session gameplay active
		///        si l'utilisateur applique des réglages en jeu. La persistance config et
		///        les catégories vidéo/audio/contrôles sont appliquées inconditionnellement.
		void ApplyConsumedSettingsCommands(bool authGateActive);
		/// Load optional zone probe and atmosphere assets from content-relative paths.
		void LoadZoneProbeAssets();
		/// Charge un .spv terrain (même chargeur que l’éditeur monde).
		std::vector<uint32_t> LoadTerrainSpirvWords(const char* relativeSpvPath);

		/// M100 — Task 12 : crée le descriptor set layout / pool / UBO host-visible /
		/// descriptor set utilisés comme set 0 (caméra) du `TerrainChunkPipeline`.
		/// Rempli par \ref UpdateTerrainChunkCameraUbo chaque frame avec viewProj.
		///
		/// \param outError Renseigné en cas d'échec.
		/// \return true si toutes les ressources sont créées.
		bool CreateTerrainChunkCameraResources(std::string& outError);

		/// M100 — Task 12 : libère les ressources caméra créées par
		/// \ref CreateTerrainChunkCameraResources. Idempotent.
		void DestroyTerrainChunkCameraResources();

		/// M100 — Task 12 : copie `viewProjMat4` (16 floats) dans le mapped pointer
		/// du UBO caméra. Doit être appelé avant `RenderVisibleChunks` chaque frame.
		void UpdateTerrainChunkCameraUbo(const float* viewProjMat4);
#if defined(_WIN32)
		/// World editor: (re)charge heightmap + outils sculpt depuis le document.
		void RebuildWorldEditorTerrainGpu();

		/// M100.46+ — Pont CPU TerrainDocument → GPU HeightmapData.
		/// Itère les chunks chargés du TerrainDocument du Shell, copie leurs
		/// heights float (mètres) vers la HeightmapData CPU uint16 du
		/// TerrainRenderer (conversion par `terrain.height_scale`), puis
		/// pousse au GPU via `m_worldEditorTerrainTools.FlushHeightmap`.
		///
		/// Appelée chaque frame uniquement si `m_worldEditorTerrainNeedsSync`
		/// est vrai (flag set par le callback `OnChunkChanged` du document).
		/// No-op si l'éditeur monde n'est pas actif ou si les structures
		/// ne sont pas valides.
		void SyncWorldEditorHeightmapFromDocument();

		/// Si WorldEditorSession::ConsumeSplatRefsDirty() == true, repack les
		/// 4 layers (procedural fallback + textures importees via le cache)
		/// dans m_terrain.GetSplatting() et reuploade le GPU array.
		/// A appeler chaque frame en world-editor mode, apres les autres ticks.
		/// No-op si --world-editor non actif ou cache non pret.
		void ProcessSplatRefsDirty();
#endif

		engine::core::Config m_cfg;

		engine::platform::Window m_window;
		engine::platform::Input m_input;

		engine::render::VkInstance m_vkInstance;
		engine::render::VkDeviceContext m_vkDeviceContext;
		engine::render::VkSwapchain m_vkSwapchain;
		std::array<engine::render::FrameResources, 2> m_frameResources{};
		engine::render::ShaderCache m_shaderCache;
		engine::render::ShaderHotReload m_shaderHotReload;
		uint32_t m_currentFrame = 0;
		GLFWwindow* m_glfwWindowForVk = nullptr;
		bool m_swapchainResizeRequested = false;

		engine::render::FrameGraph m_frameGraph;
		engine::render::Registry m_fgRegistry;
		engine::render::ResourceId m_fgSceneColorId      = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgBackbufferId      = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferAId        = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgDecalOverlayId    = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferBId        = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgGBufferCId        = engine::render::kInvalidResourceId;
		/// M07.3: velocity buffer (currNDC - prevNDC), R16G16F.
		engine::render::ResourceId m_fgGBufferVelocityId = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgDepthId           = engine::render::kInvalidResourceId;
		/// SceneColor_HDR: output of the deferred lighting pass (R16G16B16A16_SFLOAT). Added in M03.2.
		engine::render::ResourceId m_fgSceneColorHDRId   = engine::render::kInvalidResourceId;
		/// M100.14 — SceneColor_HDR_PostWater: SceneColor_HDR ping-pong target after the
		/// Water render pass writes back into the HDR scene. Bloom_Prefilter / Bloom_Combine
		/// (et toute autre passe HDR aval) lisent désormais ce resource id au lieu de
		/// m_fgSceneColorHDRId. Si WaterPass::Init échoue, un fallback Water_Passthrough
		/// (vkCmdCopyImage) garantit que cette ressource est bien renseignée chaque frame.
		engine::render::ResourceId m_fgSceneColorHDRPostWaterId = engine::render::kInvalidResourceId;
		/// M45.2 — SceneColor_HDR_Fogged: SceneColor_HDR_PostWater après application du
		/// brouillard volumique + god rays (VolumetricFog), ou copie passthrough si la
		/// passe fog est invalide. Lu en SampledRead par Bloom_Prefilter / Bloom_Combine.
		engine::render::ResourceId m_fgSceneColorFoggedId = engine::render::kInvalidResourceId;
		/// Nuages — SceneColor_HDR_Clouds : SceneColor_HDR_Fogged après composition
		/// des nuages volumétriques (CloudPass). Lu en SampledRead par Bloom
		/// (Prefilter + Combine) lorsque la passe nuages est active. Même desc que
		/// Fogged (R16G16B16A16_SFLOAT, extent swapchain).
		engine::render::ResourceId m_fgCloudsId = engine::render::kInvalidResourceId;

		/// Temps réel cumulé (secondes) pour l'advection des nuages par le vent
		/// (push-constant CloudPass). Avancé chaque frame par le dt d'Update.
		float m_cloudTimeSeconds = 0.0f;
		/// M45.2 — true si VolumetricFogPass::IsValid() au boot (passe fog active) ;
		/// sinon Engine enregistre un passthrough (copie PostWater -> Fogged).
		bool m_volumetricFogReady = false;
		/// M45.3 — SceneColor_HDR_Dof : SceneColor_HDR_WithBloom après profondeur de
		/// champ / bokeh (DepthOfField), ou copie passthrough si la passe DoF est
		/// invalide. Lu en SampledRead par Tonemap (à la place de WithBloom).
		engine::render::ResourceId m_fgSceneColorDofId = engine::render::kInvalidResourceId;
		/// M45.3 — true si DepthOfFieldPass::IsValid() au boot (passe DoF active) ;
		/// sinon Engine enregistre un passthrough (copie WithBloom -> Dof).
		bool m_dofReady = false;
		/// M45.7 — GI dynamique DDGI. DÉSACTIVÉ par défaut (gi.ddgi.enabled=false) :
		/// le volume n'est PAS alloué, la passe DDGI_Update n'est PAS enregistrée et
		/// le LightingPass garde useDdgi=0 => rendu STRICTEMENT identique au chemin
		/// probes statiques. Activé seulement si la config + l'allocation réussissent.
		engine::render::gi::DdgiVolume m_ddgiVolume;
		/// M45.7 — passe compute qui met à jour l'atlas d'irradiance du volume DDGI.
		engine::render::gi::DdgiUpdatePass m_ddgiUpdatePass;
		/// M45.7 — true uniquement si la qualité DDGI est dynamique ET allocation/init réussis.
		bool m_ddgiEnabled = false;
		/// M45.8 — amortissement : 1 sonde sur N mise à jour par frame (>= 1).
		/// Dérivé du niveau de qualité (DynamicLow=8, DynamicHigh=2). Poussé au
		/// shader ddgi_update via DdgiUpdateParams::gridSpacing.w.
		uint32_t m_ddgiUpdateDivisor = 4;
		/// M45.8 — intensité du terme indirect DDGI dans le lighting. Dérivée du
		/// niveau de qualité (DynamicLow=0.5, DynamicHigh=1.0 ; 0 si statique).
		/// Poussée au LightingPass via LightParams::ddgiAtlas[3].
		float m_ddgiIntensity = 1.0f;
		/// SceneColor_LDR: output of the tonemap pass (R8G8B8A8_UNORM). Added in M03.4.
		engine::render::ResourceId m_fgSceneColorLDRId   = engine::render::kInvalidResourceId;
		/// M08.2: SceneColor_HDR + bloom (combine pass output); tonemap reads this.
		engine::render::ResourceId m_fgSceneColorHDRWithBloomId = engine::render::kInvalidResourceId;
		/// M37.3: UnderwaterHDR — output of the underwater post-effect pass (R16G16B16A16_SFLOAT).
		engine::render::ResourceId m_fgUnderwaterHDRId = engine::render::kInvalidResourceId;
		/// SSAO_Raw: output of SSAO generate pass (R16F). M06.2.
		engine::render::ResourceId m_fgSsaoRawId        = engine::render::kInvalidResourceId;
		/// SSAO_Blur_Temp: intermediate for bilateral blur H pass. M06.3.
		engine::render::ResourceId m_fgSsaoBlurTempId   = engine::render::kInvalidResourceId;
		/// SSAO_Blur: output of bilateral blur (2 passes). M06.3.
		engine::render::ResourceId m_fgSsaoBlurId       = engine::render::kInvalidResourceId;
		/// M07.2: TAA history ping-pong (format LDR = input TAA format).
		engine::render::ResourceId m_fgHistoryAId       = engine::render::kInvalidResourceId;
		engine::render::ResourceId m_fgHistoryBId        = engine::render::kInvalidResourceId;
		/// Shadow maps per cascade (depth + sampled). Added in M04.2.
		std::array<engine::render::ResourceId, engine::render::kCascadeCount> m_fgShadowMapIds{};
		/// M08.1: Bloom mip pyramid (BloomMip0 = full res, .. BloomMip5 = 1/32).
		std::array<engine::render::ResourceId, engine::render::kBloomMipCount> m_fgBloomDownMipIds{};
		std::array<engine::render::ResourceId, engine::render::kBloomMipCount> m_fgBloomUpMipIds{};

		/// All deferred passes (geometry, shadow, SSAO, lighting, bloom, tonemap, TAA). Init/Destroy in Engine.cpp.
		std::unique_ptr<engine::render::DeferredPipeline> m_pipeline;
		engine::render::AuthGlyphPass m_authGlyphPass;
		engine::render::AuthLogoPass m_authLogoPass;
		engine::render::MeshHandle m_geometryMeshHandle;
		/// Texture peau de l'avatar (1x1 sRGB violet clair, visible sur sol blanc et ciel sombre).
		engine::render::TextureHandle m_avatarSkinTextureHandle;
		/// Index materiel HABIT de l'avatar dans la MaterialDescriptorCache
		/// (0 = default fallback, non-zero = materiel dedie). Materiau par defaut
		/// applique a tous les sous-maillages sauf ceux listes comme "peau".
		uint32_t m_avatarMaterialId = 0u;
		/// Index materiel PEAU de l'avatar, un par genre (corps/mains). 0 si la
		/// texture de peau correspondante est absente -> les sous-maillages peau
		/// retombent sur l'habit. Le draw choisit l'id selon m_avatarGender.
		uint32_t m_avatarBodyMaterialIdMale = 0u;
		uint32_t m_avatarBodyMaterialIdFemale = 0u;
		/// Index materiel PEAU teinte FONCEE (skinColorIdx=1), un par genre. 0 si la
		/// texture _Dark est absente -> le draw retombe sur la teinte claire.
		uint32_t m_avatarBodyMaterialIdMaleDark = 0u;
		uint32_t m_avatarBodyMaterialIdFemaleDark = 0u;
		/// Genre actif de l'avatar ("male" / "female"). Pilote le mesh in-world
		/// (GetRaceMesh) ET le materiau de peau au draw. Modifie en live par le
		/// selecteur de creation (SetAvatarGender) ; defaut depuis config au boot.
		std::string m_avatarGender = "male";
		/// Teinte de peau active in-world : 0 = claire (défaut), 1 = foncée.
		/// Appliquée à EnterWorld depuis enterCmd.skinColorIdx (DB serveur,
		/// migration 0068) ; pilote le choix du matériau de peau au draw.
		int m_avatarSkinTone = 0;
		/// Genre pour lequel le diagnostic peau a deja ete logge (evite le spam
		/// par frame ; on relogue uniquement au changement de genre). Cf. le bloc
		/// [AvatarSkinDiag] dans Engine.cpp (rendu de l'avatar skinne).
		std::string m_avatarSkinDiagLoggedGender;
		/// Noms de materiaux glTF (ex. "MI_Regular_Male") dont les sous-maillages
		/// recoivent le materiau de peau du genre actif. Tout autre nom -> habit.
		/// Renseigne depuis client.character_creation.body_material_names.
		std::vector<std::string> m_avatarBodyMaterialNames;
		/// Depth bias applique aux sous-maillages PEAU (corps) au draw pour les
		/// pousser DERRIERE l'habit coplanaire et eviter le z-fighting/flicker
		/// (« parait double ») sur l'avatar modulaire (peau sous l'habit, bras).
		/// Reglable a chaud via client.character_creation.skin_depth_bias_* (pas
		/// de rebuild necessaire pour ajuster). Defauts sensibles si absent.
		float m_avatarSkinDepthBiasConstant = 4.0f;
		float m_avatarSkinDepthBiasSlope    = 4.0f;
		engine::render::DecalSystem m_decalSystem;
		std::vector<engine::render::VisibleDecal> m_visibleDecals;

		/// M37.3: underwater post-effect pass (blue tint, depth fog, blur vignette).
		engine::render::UnderwaterPass m_underwaterPass;
		/// M37.3: true when camera.y < waterLevel (underwater detection result, updated each frame).
		bool m_isUnderwater = false;

		/// M100.14 — Water rendering FG-intégré (lit la WaterScene M100.13).
		/// \note WaterPass::Init nécessite VMA + skybox cube + normal map ; sur les
		///       builds STAB.7 (VMA disabled) la passe reste invalide et le fallback
		///       Water_Passthrough (vkCmdCopyImage SceneColor_HDR → PostWater) prend
		///       le relais — le rendu reste fonctionnel, juste sans réflexion.
		engine::render::WaterPass    m_waterPass;
		/// Buffer GPU des meshes d'eau (lakes + rivers). Reconstruit à la demande
		/// lorsque la WaterScene devient dirty (mode éditeur ou client).
		engine::render::WaterMeshGpu m_waterMeshGpu;
		/// Long-lived command pool dédié aux uploads water (RESET between Rebuilds).
		/// Évite le coût vkCreateCommandPool/vkDestroyCommandPool par frame d'édition.
		VkCommandPool m_waterTransferPool = VK_NULL_HANDLE;
		/// Textures muettes 1×1 pour WaterPass (créées au boot si VMA dispo).
		/// normalMap = (128,128,255) = normale plate vers le haut.
		/// skybox = cube 6 faces = bleu ciel (fallback réflexion).
		VkImage        m_waterNormalMapImg     = VK_NULL_HANDLE;
		VkDeviceMemory m_waterNormalMapMem     = VK_NULL_HANDLE;
		VkImageView    m_waterNormalMapView    = VK_NULL_HANDLE;
		VkSampler      m_waterNormalMapSampler = VK_NULL_HANDLE;
		VkImage        m_waterSkyboxImg        = VK_NULL_HANDLE;
		VkDeviceMemory m_waterSkyboxMem        = VK_NULL_HANDLE;
		VkImageView    m_waterSkyboxView       = VK_NULL_HANDLE;
		VkSampler      m_waterSkyboxSampler    = VK_NULL_HANDLE;
		/// Scene d'eau côté client (post-EnterWorld). Nullptr en mode --world-editor
		/// (ce mode utilise WaterDocument du WorldEditorShell). Non encore renseignée
		/// par la chaîne de chargement client M100.14 → la passe restera inactive
		/// tant que m_clientWaterScene reste vide ou non-dirty.
		std::shared_ptr<engine::world::water::WaterScene> m_clientWaterScene;
		/// Drapeau dirty côté client : mis à true à chaque réception/rechargement
		/// d'une WaterScene. Reset à false par le rebuild GPU (cf. Engine::Render).
		bool m_waterClientSceneDirty = false;

		/// M38.1: day/night cycle (time-of-day, sun direction, sky gradient colours).
		engine::render::DayNightCycle m_dayNight;

		/// WorldClock sync (Task 6.2) — accumulateur (s) du controle de derive.
		/// Incremente chaque frame dans Update() ; quand il depasse
		/// m_worldClockDriftCheckSec on renvoie un WorldClockStateRequest (203)
		/// au master pour rafraichir l'offset d'horloge (correction sub-seconde).
		float m_worldClockResyncTimer = 0.0f;
		/// Intervalle de re-synchro horloge (s), lu depuis
		/// game.worldclock.drift_check_sec (defaut 300). <=0 desactive la re-sync.
		float m_worldClockDriftCheckSec = 300.0f;

		std::array<float, 3> m_iblLastSunDir { 0.0f, 1.0f, 0.0f }; ///< dernière dir soleil capturée pour l'IBL (suivi jour/nuit).
		float m_iblRegenTimer = 0.0f;                              ///< throttle de re-capture IBL (s).

		/// Sky pass V1 (M38.1 + Phase 5 Lunar) : pipeline ciel + disque lunaire
		/// procedural via push-constants. Consomme sky.frag et sky.vert. Le
		/// pipeline est cree au boot dans InitVulkan ; m_skyPassReady passe a
		/// true uniquement si la creation reussit (sinon fallback : le ciel
		/// reste un clearColor simple, sans rendering du disque lunaire).
		engine::render::SkyPass m_skyPass;
		bool                    m_skyPassReady = false;

		/// M38.2: weather system (state machine, rain/snow particles, fog density, audio volume).
		engine::render::WeatherSystem m_weatherSystem;

		/// M38.3: dynamic point-light system (streetlamps, torches, window emissive).
		engine::render::DynamicLightSystem m_dynamicLights;

		engine::render::AssetRegistry m_assetRegistry;
		std::unique_ptr<engine::editor::EditorMode> m_editorMode;
		std::unique_ptr<engine::editor::WorldEditorImGui> m_worldEditorImGui;
		/// Overlay auth Dear ImGui (cycle de vie ImGui partagé avec \ref m_worldEditorImGui sur Windows).
		std::unique_ptr<engine::render::AuthImGuiRenderer> m_authImGui;
		/// Phase 3.11.1 — Panneau chat Dear ImGui (post-auth, partage le même contexte ImGui que m_authImGui).
		std::unique_ptr<engine::render::ChatImGuiRenderer> m_chatImGui;
		/// CMANGOS.18 (Phase 3.18 step 4) — Panneau boite mail (post-auth, ImGui).
		/// Partage le contexte ImGui avec m_authImGui / m_chatImGui. Visibilite
		/// pilotee par \c m_mailVisible (toggle via slash command /mail).
		std::unique_ptr<engine::render::MailImGuiRenderer> m_mailImGui;
		/// CMANGOS.32 (Phase 5.32 step 3+4) — Panneau "Support GM" (post-auth, ImGui).
		/// Partage le contexte ImGui avec m_authImGui / m_chatImGui / m_mailImGui.
		/// Visibilite pilotee par \c m_gmTicketsVisible (toggle via slash command /ticket).
		std::unique_ptr<engine::render::GmTicketImGuiRenderer> m_gmTicketImGui;
		/// CMANGOS.24 (Phase 3.24 step 3+4) — Panneau "Reputation" (post-auth, ImGui).
		/// Partage le contexte ImGui avec auth/chat/mail/gmtickets. Visible uniquement
		/// quand m_reputationVisible (toggle via slash command /rep ou /reputation).
		std::unique_ptr<engine::render::ReputationImGuiRenderer> m_reputationImGui;
		/// CMANGOS.33 (Phase 5.33 step 3+4) — Panneau "LFG" (post-auth, ImGui).
		/// Partage le contexte ImGui avec auth/chat/mail/gmtickets/reputation.
		/// Visible uniquement quand m_lfgVisible (toggle via slash command /lfg).
		std::unique_ptr<engine::render::LfgImGuiRenderer> m_lfgImGui;
		/// CMANGOS.30 (Phase 5.30 step 3+4) — Overlay cinematique (black bars
		/// + skip hint). Visible uniquement quand une cinematique est en cours
		/// de lecture (m_cinematicUi.GetState().isPlaying). Toggle pilote par
		/// les push 108 du master.
		std::unique_ptr<engine::render::CinematicImGuiRenderer> m_cinematicImGui;
		/// CMANGOS.39 (Phase 4.39 step 3+4) — Panneau "Skill Book" (post-auth, ImGui).
		/// Partage le contexte ImGui avec auth/chat/mail/gmtickets/reputation/lfg.
		/// Visible uniquement quand m_skillBookVisible (toggle via slash command
		/// /skills ou touche B).
		std::unique_ptr<engine::render::SkillBookImGuiRenderer> m_skillBookImGui;
		/// Grimoire (Task 13) — Renderer ImGui du panneau Grimoire / Carnet de
		/// techniques. Partage le contexte ImGui post-auth. Visible quand
		/// m_grimoireVisible (toggle via touche V remappable ou /grimoire / /sorts).
		std::unique_ptr<engine::render::GrimoireImGuiRenderer> m_grimoireImGui;
		/// CMANGOS.21 (Phase 5.21 step 3+4) — Panneau "Arena" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Visible uniquement quand m_arenaVisible (toggle via slash command
		/// /arena ou touche A).
		std::unique_ptr<engine::render::ArenaImGuiRenderer> m_arenaImGui;
		/// CMANGOS.10 (Phase 5 step 3+4) — Panneau "BattleGround" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Visible uniquement quand m_battleGroundVisible (toggle via slash
		/// command /bg ou touche G) ou quand un match BG est actif (le
		/// scoreboard s'affiche tout seul, push 136).
		std::unique_ptr<engine::render::BattleGroundImGuiRenderer> m_battleGroundImGui;
		/// CMANGOS.36 (Phase 5.36 step 3+4) — Panneau "Outdoor PvP" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Visible uniquement quand m_outdoorPvpVisible (toggle via slash
		/// command /pvp ou touche P).
		std::unique_ptr<engine::render::OutdoorPvpImGuiRenderer> m_outdoorPvpImGui;
		/// CMANGOS.42 (Phase 4.42 step 3+4) — Panneau "Weather" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Le panel principal est visible uniquement quand m_weatherVisible
		/// (toggle via slash command /weather ou touche Y). Le HUD top-right
		/// est rendu independamment du flag des que activeZoneId est set.
		std::unique_ptr<engine::render::WeatherImGuiRenderer> m_weatherImGui;
		/// CMANGOS.31 (Phase 5.31 step 3+4) — Panneau "Game Events" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Le panel principal est visible uniquement quand m_gameEventVisible
		/// (toggle via slash command /events ou touche E). Le toast 5s sur
		/// dernier StateChange reçu est rendu independamment du flag.
		std::unique_ptr<engine::render::GameEventImGuiRenderer> m_gameEventImGui;
		/// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Panneau "Guilds" (post-auth, ImGui).
		/// Partage le contexte ImGui avec les autres panneaux post-auth.
		/// Le panel principal est visible uniquement quand m_guildVisible
		/// (toggle via slash command /guild ou touche U). Le toast 5s sur
		/// dernier MotdUpdate reçu est rendu independamment du flag.
		std::unique_ptr<engine::render::GuildImGuiRenderer> m_guildImGui;
		/// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Panneau "Auction
		/// House" (post-auth, ImGui). Partage le contexte ImGui avec les
		/// autres panneaux post-auth. Le panel principal est visible
		/// uniquement quand m_auctionHouseVisible (toggle via slash command
		/// /ah ou touche H). Les toasts 5s sur derniere bid + dernier
		/// AuctionExpired sont rendus independamment du flag.
		std::unique_ptr<engine::render::AuctionImGuiRenderer> m_auctionHouseImGui;
		/// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Panneau "Loot Roll" (post-auth, ImGui).
		/// Le panel principal est visible uniquement quand m_lootRollVisible
		/// (toggle via slash command \c /loot ou touche L). Le toast 5s sur
		/// dernier RollResult reçu est rendu independamment du flag.
		std::unique_ptr<engine::render::LootRollImGuiRenderer> m_lootRollImGui;
		/// M43.4 — Panneau "Editor Hub" overlay quand `--editor` actif.
		std::unique_ptr<engine::render::EditorHubImGuiRenderer> m_editorHubImGui;

		// --- Dialogue PNJ (cellule dédiée) ---
		engine::client::DialoguePresenter m_dialogue;                                        ///< Logique runtime du dialogue.
		std::unique_ptr<engine::render::DialogueImGuiRenderer> m_dialogueImGui;              ///< Rendu (Windows).
		std::unique_ptr<engine::client::QuestConversationJournal> m_dialogueJournal;         ///< Journal local (créé au login).
		bool m_dialogueActive = false;                                                       ///< Vrai pendant un dialogue (verrouille le déplacement).

		/// Données carte / import (uniquement si \c m_worldEditorExe).
		std::unique_ptr<engine::editor::WorldEditorSession> m_worldEditorSession;
		/// M100.1 — Coquille du nouvel éditeur monde "couche au-dessus".
		/// Instancié si `--editor-world` ou `editor.world.enabled = true`.
		/// Cohabite avec WorldEditorImGui (les deux peuvent être actifs).
		/// Le shell appelle ImGui (Windows-only) — toujours nul sur Linux.
		std::unique_ptr<engine::editor::world::WorldEditorShell> m_worldEditorShell;

		/// M100.34 incrément 1 — Cible offscreen R8G8B8A8 dédiée au viewport
		/// éditeur. Possédée par Engine, init après VkDeviceContext valide
		/// + ImGui_ImplVulkan_Init OK, détruite avant le shutdown Vulkan.
		/// Le `ScenePanel` lit `GetImguiTextureId()` chaque frame pour
		/// l'afficher via `ImGui::Image`. L'image est créée mais reste
		/// noire en PR 1 (rendu réel branché en PR 2 via passe FrameGraph
		/// qui copie `SceneColor_LDR`).
		engine::editor::world::EditorViewportRenderTarget m_editorViewportTarget;

		/// Sous-projet C MVP (Task 12) — Viewport offscreen Vulkan dedie
		/// a l'apercu 3D du mesh skinned d'une race dans l'ecran ImGui
		/// AuthImGuiCharacterCreate. Init au boot apres ImGui_ImplVulkan_Init
		/// (meme bloc que m_editorViewportTarget) puis passe au
		/// AuthImGuiRenderer via SetRacePreview(). Shutdown avant le
		/// EditorViewportRenderTarget pour respecter l'ordre LIFO de
		/// liberation des descriptors ImGui (idem que m_editorViewportTarget).
		engine::render::race::RacePreviewViewport m_racePreviewViewport;

		/// Phase 2 — horodatage (EngineNowSec) du dernier Tick de l'aperçu race,
		/// pour calculer un delta-time robuste alimentant la rotation orbit +
		/// l'échantillonnage d'animation. 0 = pas encore tické.
		float m_racePreviewLastNowSec = 0.0f;

#if defined(_WIN32)
		std::unique_ptr<engine::editor::TexturePreviewCache> m_texturePreviewCache;

		/// Nombre de frames en vol pour le purge differé du TexturePreviewCache.
		/// Aligne sur HiZ/GpuDrivenCulling kDefaultFramesInFlight = 2.
		static constexpr uint32_t kEditorTexCacheFramesInFlight = 2u;
#endif
		/// Sous-projet A (Task 15) — Runtime skinned avatar humanoide (Y Bot Mixamo).
		/// Remplace le cube `avatar_placeholder.mesh` post-EnterWorld lorsque le glb
		/// est chargeable et que le pipeline Vulkan s'initialise correctement.
		/// Init lazy : tente une seule fois au boot apres la creation du materiel
		/// avatar (cf. Engine.cpp ~ligne 3736). Si Init du renderer ou du mesh
		/// echoue, m_skinnedAvatarReady reste a false et le draw retombe sur le
		/// cube placeholder (preservation du chemin de fallback existant).
		///
		/// Cycle de vie :
		///   - Init   : une fois au boot, dans le bloc de cration du DeferredPipeline.
		///   - Record : chaque frame in-game, dans la lambda FrameGraph "Geometry"
		///              via `GeometryPass::RecordTerrainChunkBatch` (passe LOAD
		///              compatible — meme formats GBuffer + depth).
		///   - Destroy: au Shutdown, juste avant `m_pipeline->Destroy` (les
		///              ressources Vulkan dependent du device, pas du pipeline).
		engine::render::skinned::SkinnedRenderer                  m_skinnedRenderer;
		/// Sous-projet C MVP — Pointeur vers le mesh skinned de la race du
		/// perso courant. Pointe par defaut vers m_raceMeshes["humains"] au
		/// boot (si le mesh humain a ete charge avec succes ; null sinon, et
		/// l'avatar fallback sur le cube placeholder via m_skinnedAvatarReady
		/// = false). Sera reassigne par EnterWorld (Task 8) depuis le race_str
		/// du payload — cf. Engine::GetRaceMesh (Task 7) qui resout race_str
		/// -> SkinnedMesh* via m_raceMeshes avec fallback humains.
		engine::render::skinned::SkinnedMesh*                     m_currentSkinnedMesh = nullptr;
		/// Sous-projet C MVP — Map race_str -> mesh skinned + clips charges
		/// au boot. Remplit a partir d'un CharacterCreationPresenter local
		/// (instancie dans le bloc skinned-pipeline d'Engine::Init, Init()
		/// parse races.json synchronement, lifetime limitee au bloc). Engine
		/// n'a pas (encore) de membre presenter permanent ; le parsing est
		/// donc fait 2x au worst-case (ici au boot + une fois plus tard via
		/// l'AuthUI quand l'utilisateur ouvre l'ecran de creation perso).
		/// Seules les races avec meshPath non vide sont chargees ; les
		/// autres sont silencieusement skip (l'utilisateur ne pourra pas
		/// creer un perso avec cette race tant que le mesh n'est pas defini).
		///
		/// Cycle de vie : peuple au boot dans le bloc skinned-pipeline, libere
		/// au Shutdown (boucle sur la map -> SkinnedMesh::Destroy avant
		/// m_skinnedRenderer.Destroy). Les SkinnedMesh sont stockes par valeur :
		/// les pointeurs `m_currentSkinnedMesh` restent stables tant que la map
		/// n'est pas modifiee apres le boot (pas de rehash apres le remplissage).
		std::unordered_map<std::string, engine::render::skinned::SkinnedMesh> m_raceMeshes;
		/// Combat SP1 — apparences d'archétypes de créatures (nom/niveau/mesh/
		/// échelle), chargées au boot depuis creatures/archetypes.json (tolérant :
		/// catalogue vide = rendu mob en fallback). Consommé par la plaque de nom
		/// et RecordRemoteAvatars pour les entités archetypeId != 0.
		engine::client::CreatureCatalog                           m_creatureCatalog;
		std::chrono::steady_clock::time_point                     m_playerAnimStartTime;
		/// True une fois SkinnedRenderer::Init OK + SkinnedMeshLoader::Load OK.
		/// Sert de gate per-frame : si false, on dessine le cube placeholder.
		bool                                                      m_skinnedAvatarReady = false;

		/// Sous-projet B.1 (Task 11) — State machine de locomotion de l'avatar.
		/// 7 etats :
		///   - Idle           : clip "Idle" looped, perso immobile au sol.
		///   - StartWalking   : clip "StartWalking" one-shot (lift-off de Idle vers
		///                      la marche pleine vitesse). Transite vers Walk ou Run
		///                      a la fin du clip.
		///   - Walk           : clip "Walk" looped, marche normale (input.run = false).
		///                      Renomme de "Walking" (A polish) -> "Walk" pour la
		///                      coherence Mixamo + brievete.
		///   - Run            : clip "Run" looped, course (input.run = true / Shift).
		///   - Jump           : clip "Jump" one-shot, phase takeoff (les premiers 40%
		///                      du clip), declenche par input.jumpPressed depuis Idle/
		///                      Walk/Run/StartWalking.
		///   - Fall           : clip "Fall" looped, en l'air apres la fin du takeoff
		///                      ou si le CC perd le contact sol sans avoir saute.
		///   - Land           : clip "Land" one-shot au touch ground depuis Fall.
		///                      Transite vers Idle/Walk/Run selon input a la fin du clip.
		///
		/// Transitions driven par `CharacterController::IsGrounded()`, `input.jumpPressed`,
		/// `input.run` et `moveDirXZ` (cf. `Engine::Update`). Crossfade entre clips
		/// par `m_avatarCrossfade.Play(...)` (kCrossfadeDuration = 0.15 s).
		///
		/// **Visibilite public** : l'enum est expose en public pour que les helpers
		/// free-function `StateToClipName` / `ClipLoops` (anonymous namespace de
		/// `Engine.cpp`) puissent y acceder depuis l'exterieur de la classe (C++
		/// access control s'applique meme depuis le meme TU). Aucun client externe
		/// d'Engine ne devrait s'en servir — c'est de l'interface interne render/anim.
	public:
		enum class AvatarLocomotionState
		{
			Idle,
			StartWalking,
			Walk,
			WalkBack,
			Run,
			Sprint,
			CrouchIdle,
			CrouchWalk,
			Roll,
			Emote,
			Attack,
			Cast,
			Interact,
			Punch,
			Jump,
			Fall,
			Land,
			SwimIdle,
			SwimForward
		};
	private:
		AvatarLocomotionState                                     m_avatarLocoState = AvatarLocomotionState::Idle;
		/// Instant (s, EngineNowSec) du dernier appui sur Ctrl — détection du
		/// double-tap pour déclencher la roulade/esquive (Roll). -10 = jamais.
		float                                                    m_lastCtrlTapSec = -10.0f;
		/// Emote en cours de demande (slash command), consommée par la state machine
		/// de locomotion : passe l'avatar en état Emote (anim en boucle, annulée au
		/// déplacement). m_currentEmoteRole = rôle d'anim actif (clip joué en Emote).
		std::string                                              m_pendingEmoteRole;
		std::string                                              m_currentEmoteRole;
		/// Combo coups de poing : role d'anim du poing en cours (Punch=Jab / PunchCross)
		/// et bascule Jab<->Cross a chaque coup. Clip dynamique (cf. point de lecture).
		std::string                                              m_currentPunchRole = "Punch";
		bool                                                     m_punchAlt = false;
		/// Interaction : rôle d'anim joué par l'état Interact (clip dynamique, même
		/// patron que m_currentPunchRole). "Interact" par défaut (geste générique sur E) ;
		/// passe à "PickUp_Table" près d'un coffre (se pencher → saisir → se redresser).
		std::string                                              m_currentInteractRole = "Interact";
		/// Verrou de déplacement : instant (s, EngineNowSec) jusqu'auquel les entrées
		/// de déplacement de l'avatar sont neutralisées (MoveInput vide) pendant un
		/// geste verrouillant (ouverture de coffre). Toujours tester via
		/// `EngineNowSec() < m_avatarMoveLockUntilSec` (jamais `!= 0`). 0 = aucun verrou.
		float                                                    m_avatarMoveLockUntilSec = 0.0f;
		/// Sequence de sort : phase (0=Enter,1=Shoot,2=Exit) + clip a rejouer en
		/// cours d'etat (vide=aucun ; consomme 1 fois par la SM, rejoue un one-shot
		/// sans changer d'etat). Garde-fou 3s dans le case Cast => jamais bloque.
		int                                                      m_castPhase = 0;
		std::string                                              m_avatarPendingClipRole;
		/// Interaction (touche E) v1 : entites interactibles (objets / PNJ). Cibles
		/// de TEST invisibles (pas de rendu/dialogue avance) -> a remplacer par de
		/// vraies entites. m_interactableInRange = index a portee (-1 = aucun).
		struct InteractableEntity
		{
			engine::math::Vec3 position{};
			float radius = 2.5f;
			bool isNpc = false;
			std::string label;
			std::string role; ///< Sous-titre affiché dans la cellule de dialogue (ex. "Garde du pont").
			std::string message;
			/// Dialogue PNJ multi-lignes (format legacy, optionnel). Conservé comme
			/// source de repli : converti en \ref dialogueTree au chargement
			/// (\see DialogueConfigLoader). Pour les objets non-PNJ, on affiche `message`.
			std::vector<std::string> dialogue;
			/// Arbre de dialogue (format moderne). Si vide, le client le construit à partir
			/// de \ref dialogue (legacy) au chargement. \see DialogueConfigLoader.
			engine::client::DialogueTree dialogueTree;
			/// Mesh statique optionnel du prop (chantier B). Chemin relatif à
			/// paths.content (ex. "meshes/props/Chest_Wood.gltf"). Vide = pas de mesh
			/// rendu (marqueur ImGui seul, comportement historique).
			std::string meshPath;
			float meshScale = 1.0f;    ///< Échelle uniforme appliquée au mesh.
			float meshYawDeg = 0.0f;   ///< Rotation Y (degrés) du mesh.
			float meshRotXDeg = 0.0f;  ///< Pré-rotation X (degrés) — conversion GLTF Y-up vers monde Z-up (ex. -90 pour les races). Appliquée avant yaw.
		};
		std::vector<InteractableEntity> m_interactables;
		int m_interactableInRange = -1;

		/// Une partie de prop = un sous-ensemble de matériau (un MeshAsset GPU + son
		/// index matériau bindless). Un prop multi-matériaux a plusieurs parties.
		struct PropPart
		{
			engine::render::MeshHandle mesh;
			uint32_t materialIndex = 0;
			/// Variante du matériau avec MaterialFlags::Highlight (teinte de surbrillance).
			/// Utilisée au draw quand le prop est l'interactible ciblé (chantier C).
			uint32_t highlightMaterialIndex = 0;
		};
		/// Prop rendu in-world : matrice modèle monde + parties (une par matériau).
		struct PropRenderable
		{
			engine::math::Mat4 modelMatrix = engine::math::Mat4::Identity();
			std::vector<PropPart> parts;
			/// Index dans m_interactables (pour comparer à m_interactableInRange et
			/// décider la surbrillance). -1 si non lié à un interactible.
			int interactableIndex = -1;
			/// Position monde (base) du prop, pour le culling de distance dans
			/// RecordPropsGeometry (sommets cuits en monde, modelMatrix=identité).
			engine::math::Vec3 worldPos{ 0.0f, 0.0f, 0.0f };
			/// M45.5 — chemin du mesh source (relatif à paths.content), clé du cache
			/// d'atlas d'impostors `m_impostorAtlases`. Vide pour les interactibles.
			std::string meshPath;
			/// M45.5 — centre monde de la sphère englobante du prop (= centre du
			/// billboard impostor). Calculé au build depuis les sommets cuits.
			engine::math::Vec3 impostorCenter{ 0.0f, 0.0f, 0.0f };
			/// M45.5 — rayon (m) de la sphère englobante (demi-taille du billboard).
			float impostorRadius = 0.0f;
		};
		/// Props statiques rendus (chantier B), construits au boot depuis les
		/// interactibles ayant un meshPath. Cf. LoadInteractableProps / RecordPropsGeometry.
		std::vector<PropRenderable> m_props;

		/// Aperçu éditeur — nombre de props « monde » (scenery + bâtiments posés)
		/// présents dans m_props après le boot. SyncEditorBuildingPreview ne touche
		/// QU'aux props de brouillon ajoutés au-delà de ce seuil, pour ne pas
		/// effacer le décor/les bâtiments chargés au boot.
		size_t m_editorBaselinePropCount = 0;

		/// Gizmo éditeur — position monde de la pièce de bâtiment active (cible du
		/// gizmo), calculée dans SyncEditorBuildingPreview. m_editorGizmoValid =
		/// false si aucune pièce active.
		engine::math::Vec3 m_editorGizmoPos{};
		bool m_editorGizmoValid = false;

		/// Aperçu éditeur — origine MONDE (XZ), yaw et échelle du groupe avec
		/// lesquels le brouillon de bâtiment a été rendu au dernier rebuild de
		/// SyncEditorBuildingPreview. Mémorisé pour que le picking viewport
		/// (clic = sélection de la pièce la plus proche) calcule la position
		/// monde de chaque pièce avec la MÊME transform que celle affichée.
		float m_editorPreviewOriginX = 0.0f;
		float m_editorPreviewOriginZ = 0.0f;
		float m_editorPreviewYaw     = 0.0f;
		float m_editorPreviewScale   = 1.0f;
		bool  m_editorPreviewValid   = false;

		/// M45.5 — atlas d'impostors par chemin de mesh (clé = PropRenderable::meshPath).
		/// Peuplé à la demande dans BuildPropFromMesh quand world.impostor.enabled :
		/// pour chaque mesh de DÉCOR, tente de charger `<même nom>.mipo` à côté du
		/// .gltf. Absent => le prop n'aura pas d'impostor (fallback mesh). Les atlas
		/// (textures GPU) appartiennent à m_assetRegistry.
		std::unordered_map<std::string, engine::render::ImpostorAsset> m_impostorAtlases;
		/// M45.5 — flag global impostors (lu depuis world.impostor.enabled au boot).
		/// false par défaut => AUCUN code impostor ne s'exécute (rendu inchangé).
		bool m_impostorEnabled = false;

		/// fix/hiz-gate-off — flag global Hi-Z occlusion culling (lu depuis
		/// render.hiz.enabled au boot). false par défaut : la passe HiZ_Build n'est
		/// PAS enregistrée dans le frame graph (sinon HiZPyramidPass réécrit son
		/// descriptor set unique en vol => violation de validation chaque frame), et
		/// le GpuDrivenCullingPass reçoit un view Hi-Z nul => il retombe sur son
		/// fallback Hi-Z conservateur (occlusionEnabled=false). Rendu inchangé car le
		/// cull ne culait que le cube placeholder inerte. Réactivable (=true) si le
		/// GPU-cull est un jour réellement branché sur plusieurs draw-items.
		bool m_hiZEnabled = false;

		/// M45.5 — tente de charger l'atlas `.mipo` associé à `meshPath` (à côté du
		/// .gltf, même nom + extension `.mipo`) dans `m_impostorAtlases` s'il n'y est
		/// pas déjà. No-op si !m_impostorEnabled. \return true si un atlas valide est
		/// disponible pour ce mesh après l'appel.
		bool EnsureImpostorAtlas(const std::string& meshPath);

		/// Élément de décor solide non interactif (arbres, props nature) chargé depuis
		/// world.scenery. Rendu comme un prop ; ne participe pas à l'interaction (touche E).
		struct SceneryInstance
		{
			std::string meshPath;
			float x = 0.0f, z = 0.0f;
			float yawDeg = 0.0f;
			float scale = 1.0f;
			float collisionRadius = 0.0f; ///< 0 = empreinte XZ auto du mesh.
			bool  solid = true;            ///< false = pas de collision (sous-bois traversable).
		};
		std::vector<SceneryInstance> m_scenery;

		/// Cache matériau par nom (Furniture/Metal/Cloth/Bark...) -> {index, index highlight},
		/// partagé entre props interactifs et décor pour dédupliquer les matériaux. Vidé au
		/// début de LoadInteractableProps.
		std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> m_trimMatCache;

		/// Cache de textures 1×1 de couleur plate (clé = RGB packé 0xRRGGBB), pour
		/// colorer les pièces de bâtiment dont le glTF ne fournit aucune texture
		/// (murs/sols : matériaux MI_Plaster/MI_WoodTrim sans image → blanc sinon).
		std::unordered_map<uint32_t, engine::render::TextureHandle> m_solidColorTextures;
		/// Retourne (en la créant/mettant en cache) une texture 1×1 sRGB de la
		/// couleur donnée, à utiliser comme baseColor d'un matériau plat.
		engine::render::TextureHandle SolidColorTexture(uint8_t r, uint8_t g, uint8_t b);

		/// Collisionneur composite (terrain + cylindres des props/décor/PNJ). Construit au
		/// boot par LoadInteractableProps + LoadScenery, consommé par CharacterController.
		engine::gameplay::CompositeWorldCollider m_worldCollider;

		/// Charge les meshes glTF statiques des interactibles (groupés par matériau,
		/// matériaux trim chargés dans le cache bindless) et peuple m_props. (Re)construit
		/// aussi m_worldCollider (terrain + cylindres). À appeler au boot, après l'init du
		/// pipeline (cache matériaux valide) et le parse des interactibles. Main thread.
		void LoadInteractableProps();

		/// Charge le décor solide depuis world.scenery (arbres, props nature) : mesh baké
		/// comme un prop + cylindre de collision enregistré dans m_worldCollider. À appeler
		/// au boot juste après LoadInteractableProps (qui réinitialise le collisionneur).
		void LoadScenery();

		/// Auberge éditable — Charge les bâtiments de la zone active. La carte ne
		/// stocke que des RÉFÉRENCES (`instances/zone_<id>/buildings.bin`,
		/// BuildingPlacement : type + variante + transform) ; on résout chaque
		/// référence contre la bibliothèque `buildings/templates/<type>.json`
		/// (BuildingTemplateLibrary) pour obtenir les pièces. Pour chaque pièce,
		/// compose la matrice monde `T(origine) · Ry(yaw) · S(scale) · T(local)
		/// · R(euler local) · S(scale local)` et la rend via
		/// `BuildPropFromMeshMatrix` (bake SANS ground-snap par pièce : l'origine
		/// du bâtiment est snappée une seule fois, les pièces gardent leur Y
		/// local pour empiler toit / étage). À appeler après LoadScenery.
		void LoadBuildings();

		/// Aperçu 3D live des bâtiments dans la vue de l'ÉDITEUR. Reconstruit
		/// `m_props` (que l'éditeur n'utilise que pour cet aperçu) à partir des
		/// placements du `BuildingDocument` (résolus via la bibliothèque) + du
		/// brouillon en cours du `BuildingEditorPanel` (rendu à la position de
		/// pose courante), via `BuildPropFromMeshMatrix`. No-op hors mode éditeur
		/// ou si l'aperçu n'est pas marqué « sale » (edge-triggered : on ne
		/// reconstruit qu'après un changement, pour éviter la création de
		/// ressources GPU à chaque frame). À appeler dans la boucle éditeur,
		/// avant le rendu des props.
		void SyncEditorBuildingPreview();

		/// Dessine le gizmo de la pièce de bâtiment active en overlay ImGui :
		/// axes de translation + anneaux de rotation, X=rouge / Y=vert / Z=bleu,
		/// projetés monde→écran via la viewProj courante. No-op hors éditeur ou si
		/// aucune pièce active. À appeler pendant la frame ImGui de l'éditeur.
		void DrawEditorBuildingGizmo();

		/// Gère le cliquer-glisser sur le gizmo (étape 3.2) : saisit un axe de
		/// translation (ligne) ou un anneau de rotation au clic, puis applique le
		/// déplacement/rotation à la pièce SÉLECTIONNÉE du brouillon pendant le
		/// drag. Conversion écran→monde→local cohérente avec la transform du
		/// groupe mémorisée (m_editorPreview*). \param mouseX,mouseY position
		/// souris (pixels fenêtre). \return true si un handle du gizmo capture la
		/// souris cette frame (le clic ne doit alors PAS sélectionner une pièce).
		/// No-op (false) hors mode édition bâtiment, sans pièce sélectionnée, ou
		/// si le gizmo n'est pas valide. À appeler dans l'input viewport éditeur.
		bool UpdateEditorBuildingGizmoDrag(int mouseX, int mouseY);

		/// Taille MONDE des handles du gizmo (longueur d'axe + rayon d'anneau),
		/// proportionnelle à la distance caméra→gizmo pour une taille ~CONSTANTE à
		/// l'écran (sinon minuscule de loin / énorme de près). Doit être identique
		/// dans le dessin et le picking, d'où ce helper partagé. \param axisLen,ringR out.
		void GizmoHandleSizes(float& axisLen, float& ringR) const;

		// Gizmo drag — état du glissement en cours (axe saisi, mode, suivi souris).
		int   m_gizmoDragAxis = -1;       ///< axe saisi 0=X/1=Y/2=Z ; -1 = aucun
		int   m_gizmoDragMode = 0;        ///< 0 = translation, 1 = rotation
		float m_gizmoDragLastX = 0.0f;    ///< dernière position souris X (pixels)
		float m_gizmoDragLastY = 0.0f;    ///< dernière position souris Y (pixels)
		float m_gizmoDragLastAngle = 0.0f;///< dernier angle souris/centre (rad, mode rotation)
		float m_gizmoDragAxisSx = 0.0f;   ///< direction écran de l'axe (unité) X
		float m_gizmoDragAxisSy = 0.0f;   ///< direction écran de l'axe (unité) Y
		float m_gizmoDragWorldPerPix = 0.0f; ///< mètres monde par pixel le long de l'axe (translation)
		int   m_gizmoDragRefreshTick = 0;    ///< compteur de frames pour rafraîchir le mesh pendant le drag (throttle)

		/// Charge le coffre (Chest_Wood) en mesh SKINNE (squelette + clips Open/Close)
		/// pour l'animer a l'interaction. A appeler au boot apres LoadInteractableProps.
		void LoadAnimatedChest();
		/// Dessine le coffre anime (pose courante du crossfade) dans la passe Geometry.
		void RecordAnimatedChest(VkCommandBuffer cmd, engine::render::Registry& reg,
		                         const engine::RenderState& rs);

		/// Helper partagé : charge un mesh statique, cuit sa transformation monde dans les
		/// sommets, l'ancre au sol, crée ses matériaux (texture trim OU couleur de sommet si
		/// pas de texture), l'ajoute à m_props et, si \p solid, enregistre un cylindre de
		/// collision. Voir l'implémentation pour le détail des paramètres.
		void BuildPropFromMesh(const std::string& meshPath, float wx, float wz,
			float yawDeg, float rotXDeg, float scale, int interactableIndex,
			bool solid, float collisionRadius);

		/// Variante de BuildPropFromMesh qui CUIT une matrice monde explicite
		/// \p worldM dans les sommets, SANS ré-ancrage au sol (pas de lift
		/// `groundY - minY`). Utilisée pour les pièces d'un Building, dont le Y
		/// local est autoritaire (toit/étage empilés). Le cylindre de collision
		/// (si \p solid) est posé sous l'empreinte XZ réelle des sommets bakés,
		/// de leur minimum à leur maximum Y monde. Partage `m_trimMatCache`.
		/// \param worldM matrice modèle monde complète (déjà composée).
		void BuildPropFromMeshMatrix(const std::string& meshPath,
			const engine::math::Mat4& worldM, int interactableIndex,
			bool solid, float collisionRadius);

		/// Dessine les props (m_props) dans la passe Geometry, après l'avatar : un
		/// GeometryPass.Record par partie (matériau) avec loadOp=LOAD (superposition au
		/// GBuffer terrain/avatar). No-op si pas de props ou pas de load pass.
		void RecordPropsGeometry(VkCommandBuffer cmd, engine::render::Registry& reg,
		                         const engine::RenderState& rs);
		/// TD.2 — dessine les avatars des joueurs distants (m_uiModelBinding remoteEntities)
		/// dans la passe Geometry, un GeometryPass.Record par entité avec le mesh placeholder
		/// (m_geometryMeshHandle), à leur position/orientation réseau. Path instancié éprouvé
		/// (identique aux props) — évite le SkinnedRenderer (anneau 3 slots). No-op si aucune
		/// entité distante / pas de mesh / pas de load pass.
		void RecordRemoteAvatars(VkCommandBuffer cmd, engine::render::Registry& reg,
		                         const engine::RenderState& rs);
		/// B2/ST5 — Recharge les binds clavier persistés (keybinds.json) dans la
		/// config runtime (m_cfg) pour une prise d'effet LIVE des remaps faits via
		/// l'écran d'options unifié, sans redémarrage du client.
		///
		/// L'écran d'options écrit keybinds.json (controls.keybind.*) mais ne touche
		/// pas m_cfg. Comme BuildMoveInput / la résolution des touches d'action lisent
		/// m_cfg.GetString("controls.keybind.*") à chaque frame, re-merger le fichier
		/// dans m_cfg suffit à appliquer les nouveaux binds immédiatement.
		///
		/// Réutilise le mécanisme de boot : Config::LoadFromFile merge (override) les
		/// clés du fichier par-dessus les valeurs existantes — pas de duplication de
		/// parseur. Fichier absent/malformé -> no-op (les binds courants sont conservés).
		/// Effet de bord : modifie m_cfg ; à appeler en main thread (fermeture overlay).
		void ReloadKeybindsFromDisk();
		/// Instant d'entrée dans l'état courant. Utilisé pour :
		///   - détecter la fin de StartWalking / Jump / Land (durée écoulée >= clip.duration).
		///   - tracer la transition Jump -> Fall après 40% du clip Jump (takeoff).
		std::chrono::steady_clock::time_point                     m_avatarLocoStateEnterTime;
		/// Sous-projet B.1 (Task 11) — Crossfade entre clips de locomotion.
		///
		/// `Play(clip, loops, nowSec)` est appele dans `Engine::Update` a chaque
		/// transition d'etat (la state machine remplit `m_avatarLocoState` puis
		/// declenche le crossfade vers le clip correspondant). `Sample(skel, nowSec)`
		/// est appele dans le lambda Geometry pour produire la pose locale qui
		/// alimente `ComputeGlobalMatrices` + `ComputeFinalMatrices` + `Record`.
		///
		/// La meme valeur de `now` (secondes depuis steady_clock::time_since_epoch())
		/// doit etre utilisee dans Play et Sample : sinon le t reel applique au clip
		/// est decale et la pose initiale "snap" au lieu de demarrer a 0.
		engine::render::skinned::AnimationCrossfade               m_avatarCrossfade;
		// Coffre anime (Chest_Wood) : rendu via le pipeline skinne pour jouer
		// Chest_Open/Chest_Close a l'interaction (remplace le rendu statique du coffre).
		engine::render::skinned::SkinnedMesh                      m_chestMesh;
		engine::render::skinned::AnimationCrossfade               m_chestCrossfade;
		bool                                                     m_chestLoaded = false;
		bool                                                     m_chestOpen = false;
		/// Instant (EngineNowSec) auquel le coffre se referme automatiquement après
		/// ouverture. Armé/réarmé à chaque interaction E ; comparé chaque frame.
		float                                                    m_chestAutoCloseAtSec = 0.0f;
		engine::math::Vec3                                       m_chestPos{ 0.0f, 0.0f, 0.0f };
		float                                                    m_chestYawDeg = 0.0f;
		float                                                    m_chestScale = 1.0f;
		float                                                    m_chestRotXDeg = 0.0f;
		int                                                      m_chestInteractableIndex = -1;
		uint32_t                                                 m_chestMatFurniture = 0u;
		uint32_t                                                 m_chestMatMetal = 0u;
		std::vector<uint32_t>                                    m_chestSubmeshMat;

		/// Sous-projet B.1 (Task 9) — Physics et collision pour le joueur.
		///
		/// `m_characterController` integre la cinematique du joueur (deplacement
		/// + gravite + saut + steps) ; sa position est lue chaque frame et
		/// poussee vers `m_orbitalCameraController.SetTargetPosition` (la camera
		/// suit le perso). `m_terrainCollider` est bind sur `m_terrain` au boot
		/// (apres `m_terrain.Init`) ; il implemente l'interface IWorldCollider
		/// consommee par `CharacterController::Update` pour les sweep capsule.
		///
		/// `m_avatarYaw` suit la direction de mouvement (snap immediat sur
		/// `atan2(moveDirXZ.x, moveDirXZ.z)`) ; la model matrix de l'avatar
		/// applique directement `R_y(yaw)`. La direction de "face" intrinseque
		/// du mesh Mixamo Y Bot est +Z dans son repere local. La valeur initiale
		/// pi correspond a "dos a la camera" pour une camera a yaw=0 (forward
		/// camera = -Z), de sorte que le perso au spawn (avant tout input) est
		/// vu de dos.
		///
		/// `m_lastMoveInput` memorise la derniere intention d'input clavier
		/// pour que la state machine de locomotion (Task 11 etendra a 7 etats)
		/// puisse decider Idle/Walk/Run/Jump sans avoir a reconstruire l'input.
		engine::gameplay::CharacterController                     m_characterController;
		engine::gameplay::TerrainCollider                         m_terrainCollider;
		/// Phase 2 (chantier C) — sources de hauteur injectées dans
		/// `m_terrainCollider` au boot (BindHeightFields). `m_chunkField`
		/// (terrain.bin résidents) prioritaire si résident ; `m_heightmapField`
		/// (wrappe `m_terrain`) en repli. Non-owning vis-à-vis de leurs sources.
		std::unique_ptr<engine::world::terrain::HeightmapHeightField> m_heightmapField;
		std::unique_ptr<engine::world::terrain::ChunkHeightField>     m_chunkField;
		float                                                     m_avatarYaw = 3.14159265f;
		uint32_t m_gameplayInputSeq = 0;         ///< TC.2 : séquence monotone des Input UDP.
		float    m_gameplayInputAccumSec = 0.0f; ///< TC.2 : accumulateur de cadence d'envoi.
		/// TD.3 : position lissée d'un avatar distant (interpolation exponentielle vers la
		/// cible snapshot, mise à jour par frame dans UpdateGameplayNet).
		struct RemoteAvatarSmoothed { float x = 0.0f; float y = 0.0f; float z = 0.0f; float yaw = 0.0f; bool valid = false; };
		/// TD.3 : positions lissées par EntityId. Si une entité n'y figure pas, le rendu
		/// (RecordRemoteAvatars) retombe sur sa position snapshot brute (graceful).
		std::unordered_map<engine::server::EntityId, RemoteAvatarSmoothed> m_remoteSmoothed;

		/// TD.7 : état d'animation d'un avatar distant. Une instance d'AnimationCrossfade
		/// par avatar distant (état stateful : clip courante, blend en cours). Le clip
		/// est dérivé de la vélocité serveur (Idle si |v| < seuil, Walk sinon). lastClipName
		/// évite de relancer le même clip chaque frame (qui réinitialiserait le timer et
		/// causerait des glitches). Hashmap purgée en même temps que m_remoteSmoothed dans
		/// UpdateGameplayNet (entités sorties d'AoI / déconnexion).
		struct RemoteAvatarAnim
		{
			engine::render::skinned::AnimationCrossfade crossfade;
			std::string lastClipName;
		};
		std::unordered_map<engine::server::EntityId, RemoteAvatarAnim> m_remoteAnims;
		engine::gameplay::MoveInput                               m_lastMoveInput{};

		/// Terrain décalé (jeu + world editor exclusif : un seul actif selon le binaire / reload).
		engine::render::terrain::TerrainRenderer m_terrain;
		/// M100 — Task 12 : runtime mesh-terrain par chunk avec splat 8-layer.
		/// Cohabite avec `m_terrain` legacy : skippe les chunks sans terrain.bin/splat.bin.
		std::unique_ptr<engine::render::terrain_chunk::TerrainChunkRenderer> m_terrainChunkRenderer;
		// Phase 0 (chantier C) : nombre de chunks dessinés à la frame précédente.
		// Lu par le gating terrain legacy (rendu exclusif anti z-fighting).
		// Remis à jour chaque frame à la fin du bloc de dessin des chunks.
		std::uint32_t m_lastFrameChunkDrawCount = 0u;
		/// M100 — Task 12 : descriptor set layout pour le set 0 caméra
		/// (UBO `CameraUBO { mat4 viewProj; }`) du `TerrainChunkPipeline`.
		VkDescriptorSetLayout m_terrainChunkCameraSetLayout = VK_NULL_HANDLE;
		/// M100 — Task 12 : pool descriptor set pour allouer le set caméra.
		VkDescriptorPool m_terrainChunkCameraPool = VK_NULL_HANDLE;
		/// M100 — Task 12 : descriptor set caméra (set 0) écrit chaque frame.
		VkDescriptorSet m_terrainChunkCameraSet = VK_NULL_HANDLE;
		/// M100 — Task 12 : UBO host-visible (64 octets, mat4 viewProj).
		VkBuffer m_terrainChunkCameraUbo = VK_NULL_HANDLE;
		VkDeviceMemory m_terrainChunkCameraUboMem = VK_NULL_HANDLE;
		void* m_terrainChunkCameraUboMapped = nullptr;
#if defined(_WIN32)
		engine::render::terrain::TerrainEditingTools m_worldEditorTerrainTools;
#endif
		/// M100.46+ — Pont TerrainDocument → HeightmapData GPU. Mis à true
		/// quand `TerrainDocument::OnCommit` est appelé (callback enregistré
		/// au boot éditeur monde) ; consommé au tick suivant par
		/// `SyncWorldEditorHeightmapFromDocument` qui copie les chunks du
		/// document dans la heightmap CPU (uint16) puis appelle
		/// `FlushHeightmap` pour pousser au GPU.
		std::atomic<bool> m_worldEditorTerrainNeedsSync{ false };
		/// M08.4: Optional color grading LUT (strip 256x16 .texr). Loaded from config color_grading.lut_path.
		engine::render::TextureHandle m_colorGradingLutHandle;
		/// Fond plein écran pour l’écran auth (PNG sous paths.content, ex. ui/login/background.png).
		engine::render::TextureHandle m_authUiBackgroundTexture;
		bool m_authUiBackgroundLayoutReady = false;
		engine::render::TextureHandle m_authLogoTexture;
		engine::render::TextureHandle m_authLogoSuccessTexture;
		engine::render::TextureHandle m_authLogoErrorTexture;
		engine::render::TextureHandle m_authUiInfoLoginTexture;
		engine::render::TextureHandle m_authUiInfoRegisterTexture;
		engine::render::TextureHandle m_authFlagFrTexture;
		engine::render::TextureHandle m_authFlagEnTexture;
		bool m_authLogoImageLayoutReady = false;
		bool m_authUiInfoLoginLayoutReady = false;
		bool m_authUiInfoRegisterLayoutReady = false;
		bool m_authFlagFrLayoutReady = false;
		bool m_authFlagEnLayoutReady = false;
		/// Centralised GPU allocator (VMA). Opaque pointer; cast to VmaAllocator in Engine.cpp.
		void* m_vmaAllocator = nullptr;
		engine::audio::AudioEngine m_audioEngine;
		engine::core::Profiler m_profiler;
		engine::client::ProfilerHudPresenter m_profilerHud;
		engine::client::AuthUiPresenter m_authUi;
		engine::client::ChatUiPresenter m_chatUi;
		/// CMANGOS.18 (Phase 3.18 step 4) — Presenter boite mail. Recoit les
		/// reponses opcodes 50/52/54/56/58 via le push handler du master ;
		/// fire-and-forget des requetes 49/51/53/55/57 via \c m_authUi.
		engine::client::MailUiPresenter m_mailUi;
		/// CMANGOS.18 (Phase 3.18 step 4) — Visibilite du panneau mail
		/// (toggle via slash command \c /mail). Faux par defaut.
		bool                            m_mailVisible = false;
		/// CMANGOS.23 (Phase 5.23 step 3+4) — Presenter quete cote client.
		/// Recoit les reponses opcodes 60/62/64/66/67 via le push handler du
		/// master ; fire-and-forget des requetes 59/61/63/65 via
		/// \c m_authUi.SendGenericRequestAsync.
		engine::client::QuestUiPresenter m_questUi;
		/// CMANGOS.23 (Phase 5.23 step 3+4) — Visibilite du panneau quete
		/// (toggle via slash command \c /quest ou \c /quests). Faux par defaut.
		bool                             m_questVisible = false;
		/// CMANGOS.25 (Phase 3.25 step 3+4) — Presenter liste d'ignore. Recoit
		/// les reponses opcodes 69/71/73 via le push handler du master ;
		/// fire-and-forget des requetes 68/70/72 via
		/// \c m_authUi.SendGenericRequestAsync.
		engine::client::IgnoreListUiPresenter m_ignoreListUi;
		/// CMANGOS.32 (Phase 5.32 step 3+4) — Presenter boite a tickets support GM.
		/// Recoit les reponses opcodes 77/79/81/82 via le push handler du master ;
		/// fire-and-forget des requetes 76/78/80 via
		/// \c m_authUi.SendGenericRequestAsync.
		engine::client::GmTicketUiPresenter m_gmTicketUi;
		/// CMANGOS.32 (Phase 5.32 step 3+4) — Visibilite du panneau Support GM
		/// (toggle via slash command \c /ticket ou \c /gmticket). Faux par defaut.
		bool                                m_gmTicketsVisible = false;
		/// CMANGOS.24 (Phase 3.24 step 3+4) — Presenter de la liste de reputations.
		/// Recoit la reponse opcode 96 et le push 97 via le push handler du master ;
		/// fire-and-forget de la requete 95 via \c m_authUi.SendGenericRequestAsync.
		engine::client::ReputationUiPresenter m_reputationUi;
		/// CMANGOS.24 (Phase 3.24 step 3+4) — Visibilite du panneau Reputation
		/// (toggle via slash command \c /rep ou \c /reputation). Faux par defaut.
		bool                                  m_reputationVisible = false;
		/// R1-B (Task 4) — Renderer ImGui de la feuille de personnage (stats
		/// derivees). Lit directement \c m_uiModelBinding.GetModel().playerStats
		/// (pas de presenter dedie). Instancie avec les autres renderers ImGui.
		std::unique_ptr<engine::render::CharacterSheetImGuiRenderer> m_characterSheetImGui;
		/// R1-B (Task 4) — Visibilite de la feuille de personnage (toggle touche X
		/// hors combat). Faux par defaut.
		bool                                  m_characterSheetVisible = false;
		/// CMANGOS.33 (Phase 5.33 step 3+4) — Presenter de la fenetre LFG
		/// (LookForGroup). Recoit les responses opcodes 101/103/105 et la push
		/// notification 106 via le push handler du master ; fire-and-forget des
		/// requetes 100/102/104/107 via \c m_authUi.SendGenericRequestAsync.
		engine::client::LfgUiPresenter        m_lfgUi;
		/// CMANGOS.33 (Phase 5.33 step 3+4) — Visibilite du panneau LFG (toggle
		/// via slash command \c /lfg). Faux par defaut.
		bool                                  m_lfgVisible = false;
		/// CMANGOS.30 (Phase 5.30 step 3+4) — Presenter de lecture cinematique.
		/// Recoit le push opcode 108 (PlayNotification) + responses 110/112 via
		/// le push handler du master ; envoie 109 (Ack) et 111 (SkipRequest)
		/// via \c m_authUi.SendGenericRequestAsync. Tick chaque frame quand
		/// une cinematique est active (interpolation camera + sound cues).
		engine::client::CinematicUiPresenter  m_cinematicUi;
		/// CMANGOS.27 (Phase 4.27 step 3+4) — Presenter de la fenetre d'echange
		/// direct entre 2 joueurs. Recoit les responses opcodes 84/87/89/92 et
		/// les push notifications 85/90/94 via le push handler du master ;
		/// fire-and-forget des requetes 83/86/88/91/93 via
		/// \c m_authUi.SendGenericRequestAsync.
		engine::client::TradeWindowUiPresenter m_tradeWindowUi;
		/// CMANGOS.39 (Phase 4.39 step 3+4) — Presenter de la skill book cote
		/// client. Recoit les responses opcodes 114/116/118 et la push 119
		/// via le push handler du master ; fire-and-forget des requetes
		/// 113/115/117 via \c m_authUi.SendGenericRequestAsync.
		engine::client::SkillBookUiPresenter m_skillBookUi;
		/// CMANGOS.39 (Phase 4.39 step 3+4) — Visibilite du panneau Skill Book
		/// (toggle via slash command \c /skills ou touche B). Faux par defaut.
		bool                                  m_skillBookVisible = false;
		/// Grimoire (Task 13) — Presenter du panneau Grimoire / Carnet de techniques.
		/// Reçoit le layout autoritaire via UIModel.actionBarLayout (kind 89 ACK).
		/// Émet SetActionBarLayout via m_gameplayUdp.SendSetActionBarLayout (kind 88).
		engine::client::GrimoireUiPresenter   m_grimoireUi;
		/// Grimoire (Task 13) — Visibilite du panneau Grimoire (toggle touche V
		/// remappable via controls.keybind.grimoire, ou /grimoire / /sorts). Faux par defaut.
		bool                                  m_grimoireVisible = false;
		/// SP-D — Presenter de l'arbre de compétences par-classe.
		/// Lit ClassSkillCatalog + UIModel.classId/knownSkillIds ; émet SendChooseClassSkill.
		engine::client::ClassSkillTreeUiPresenter m_classSkillTreeUi;
		/// SP-D — Visibilité du panneau arbre de compétences (toggle touche Y remappable
		/// via controls.keybind.skilltree, ou /arbre / /competences). Faux par défaut.
		bool                                  m_classSkillTreeVisible = false;
		/// SP-D — Renderer ImGui du panneau arbre de compétences.
		/// Forward-déclaré dans Engine.h (ClassSkillTreeImGuiRenderer).
		std::unique_ptr<engine::render::ClassSkillTreeImGuiRenderer> m_classSkillTreeImGui;
		/// SP-E - Cache d'icones de competences (PNG -> texture ImGui), partage par
		/// l'arbre, le Grimoire et la barre d'action. Init apres ImGui, Shutdown avant.
		engine::client::SkillIconCache m_skillIconCache;
		/// CMANGOS.21 (Phase 5.21 step 3+4) — Presenter de la fenetre Arena.
		/// Recoit les responses opcodes 121/123/125/128 et les push notifications
		/// 126/129 via le push handler du master ; fire-and-forget des requetes
		/// 120/122/124/127 via \c m_authUi.SendGenericRequestAsync.
		engine::client::ArenaUiPresenter      m_arenaUi;
		/// CMANGOS.21 (Phase 5.21 step 3+4) — Visibilite du panneau Arena
		/// (toggle via slash command \c /arena ou touche A). Faux par defaut.
		bool                                  m_arenaVisible = false;
		/// CMANGOS.10 (Phase 5 step 3+4) — Presenter de la fenetre BattleGround.
		/// Recoit les responses opcodes 131/133/135 et les push notifications
		/// 136/137/138 via le push handler du master ; fire-and-forget des
		/// requetes 130/132/134/139 via \c m_authUi.SendGenericRequestAsync.
		engine::client::BattleGroundUiPresenter m_battleGroundUi;
		/// CMANGOS.10 (Phase 5 step 3+4) — Visibilite du panneau BattleGround
		/// (toggle via slash command \c /bg ou touche G). Faux par defaut.
		bool                                  m_battleGroundVisible = false;
		/// CMANGOS.36 (Phase 5.36 step 3+4) — Presenter de la fenetre OutdoorPvp.
		/// Recoit les responses opcodes 141/143/145/147 et les push notifications
		/// 148/149 via le push handler du master ; fire-and-forget des requetes
		/// 140/142/144/146 via \c m_authUi.SendGenericRequestAsync.
		engine::client::OutdoorPvpUiPresenter m_outdoorPvpUi;
		/// CMANGOS.36 (Phase 5.36 step 3+4) — Visibilite du panneau OutdoorPvp
		/// (toggle via slash command \c /pvp ou touche P). Faux par defaut.
		bool                                  m_outdoorPvpVisible = false;
		/// CMANGOS.42 (Phase 4.42 step 3+4) — Presenter de la fenetre Weather.
		/// Recoit les responses opcodes 151/153/155 et la push notification
		/// 156 via le push handler du master ; fire-and-forget des requetes
		/// 150/152/154 via \c m_authUi.SendGenericRequestAsync.
		engine::client::WeatherUiPresenter   m_weatherUi;
		/// CMANGOS.42 (Phase 4.42 step 3+4) — Visibilite du panneau Weather
		/// (toggle via slash command \c /weather ou touche Y). Faux par defaut.
		/// Le HUD top-right est rendu independamment quand activeZoneId set.
		bool                                  m_weatherVisible = false;
		/// CMANGOS.31 (Phase 5.31 step 3+4) — Presenter de la fenetre GameEvents.
		/// Recoit les responses opcodes 158/160/162 et la push notification
		/// 163 (StateChange) via le push handler du master ; fire-and-forget
		/// des requetes 157/159/161 via \c m_authUi.SendGenericRequestAsync.
		engine::client::GameEventUiPresenter  m_gameEventUi;
		/// CMANGOS.31 (Phase 5.31 step 3+4) — Visibilite du panneau GameEvents
		/// (toggle via slash command \c /events ou touche E). Faux par defaut.
		/// Le toast 5s sur dernier StateChange reçu est rendu independamment.
		bool                                  m_gameEventVisible = false;
		/// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Presenter de la fenetre Guildes.
		/// Recoit les responses opcodes 165/167/169/171 et la push notification
		/// 172 (MotdUpdate) via le push handler du master ; fire-and-forget des
		/// requetes 164/166/168/170 via \c m_authUi.SendGenericRequestAsync.
		engine::client::GuildUiPresenter      m_guildUi;
		/// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Visibilite du panneau Guildes
		/// (toggle via slash command \c /guild ou touche U). Faux par defaut.
		/// Le toast 5s sur dernier MotdUpdate reçu est rendu independamment.
		bool                                  m_guildVisible = false;
		/// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Presenter de la
		/// fenetre Hotel des Ventes. Recoit les responses opcodes
		/// 174/176/178/180 et la push notification 181 (AuctionExpired) via
		/// le push handler du master ; fire-and-forget des requetes
		/// 173/175/177/179 via \c m_authUi.SendGenericRequestAsync.
		engine::client::AuctionHousePresenter m_auctionHouseUi;
		/// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Visibilite du
		/// panneau Hotel des Ventes (toggle via slash command \c /ah ou
		/// touche H). Faux par defaut. Le toast 5s sur derniere bid + le
		/// toast 5s sur dernier AuctionExpired reçu sont rendus
		/// independamment.
		bool                                  m_auctionHouseVisible = false;
		/// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Presenter de la fenetre Loot
		/// Roll. Recoit les push opcodes 182/185 et la response 184/187 via le
		/// push handler du master ; fire-and-forget des requetes 183/186 via
		/// \c m_authUi.SendGenericRequestAsync.
		engine::client::LootRollUiPresenter   m_lootRollUi;
		/// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Visibilite du panneau Loot Roll
		/// (toggle via slash command \c /loot ou touche L). Faux par defaut.
		/// Le toast 5s sur dernier RollResult reçu est rendu independamment.
		bool                                  m_lootRollVisible = false;
		/// Phase 3.5 — Bannière "Bienvenue, X" affichée transitoirement après EnterWorld.
		/// Vide quand inactive. Comparée à \c steady_clock::now() chaque frame.
		std::string                                  m_enterWorldBannerText;
		std::chrono::steady_clock::time_point        m_enterWorldBannerExpiry{};

		/// Phase 3.6.6 — Identité du personnage actif (renseignée à la consommation de
		/// EnterWorldCommand). 0 = pas de perso actif (pré-EnterWorld ou post-Shutdown).
		uint64_t                                     m_currentCharacterId = 0;
		/// Niveau du personnage actif (renseigné à la consommation de EnterWorldCommand
		/// depuis CHARACTER_LIST). Sert à l'arbre de compétences (paliers verrouillés +
		/// affichage « Niveau joueur »). 1 par défaut (pré-EnterWorld / perso pré-migration).
		uint32_t                                     m_activeCharacterLevel = 1;
		/// Phase 3.6.6 — Prochain instant où la sauvegarde périodique de position sera envoyée.
		/// Initialisé à now + intervalle au moment de la consommation EnterWorldCommand.
		std::chrono::steady_clock::time_point        m_nextSavePositionTime{};
		/// Etape 6 : derniere position synchronisee. Permet de detecter le mouvement
		/// (delta > seuil) et de raccourcir l'intervalle de save tant que le perso
		/// bouge. Reinitialisee au EnterWorld depuis le spawn.
		engine::math::Vec3                            m_lastSyncedPosition{ 0.f, 0.f, 0.f };
		/// Phase 3.6.6 — Intervalle entre deux sauvegardes périodiques (configurable via
		/// `client.save_position.interval_sec`, défaut 30s).
		std::chrono::seconds                         m_savePositionIntervalSec{ 30 };
		/// Phase 3.6.6 — Vrai si la sauvegarde finale au Shutdown a déjà été envoyée
		/// (évite les doublons si Shutdown est appelé deux fois).
		bool                                         m_shutdownPositionSaved = false;
		/// M35.2 — optional UDP gameplay + vendor shop / inventory presenters.
		bool m_gameplayNetInitialized = false;
		engine::client::UIModelBinding m_uiModelBinding{};
		engine::client::ShopUiPresenter m_shopUi{};
		engine::client::AuctionUiPresenter m_auctionUi{};
		engine::client::InventoryUiPresenter m_invUi{};
		// ---------------------------------------------------------------------
		// Combat SP2 — binds combat (embryon du registre central, Lot E) :
		//   clic gauche = sélection de cible (pick écran-espace sur les mobs)
		//   Tab         = cycle de cible (mobs vivants par distance croissante)
		//   T           = attaque de la cible courante (AttackRequest)
		//   J           = panneau combat avancé (DPS meter + log filtrable)
		// Présentateurs : écrits en M16.2/M39.4, câblés ici pour la première fois.
		// ---------------------------------------------------------------------
		engine::client::CombatHudPresenter m_combatHud{};
		engine::client::AdvancedCombatPresenter m_advancedCombat{};
		/// Combat SP3 — binds : touches 1-4 = sorts du kit du profil (slots 1-4).
		/// Catalogue d'affichage des sorts (même JSON que le serveur, tolérant).
		engine::client::SpellKitCatalog m_spellCatalog{};
		/// SP-D — Catalogue client des compétences par-classe (gameplay/class_skills/*.json).
		/// Politique tolérante : absent/invalide = catalogue vide + LOG_WARN (non bloquant).
		engine::client::ClassSkillCatalog m_classSkillCatalog{};
		/// Combat SP3 — BuffBar (M31.2) : auras du joueur et de la cible.
		engine::client::BuffBarPresenter m_buffBar{};
		/// Combat SP4 — FX d'auras (couche données ; rendu = halo écran-espace
		/// coloré aux pieds, couleur résolue par ResolveAuraVisuals).
		engine::client::AuraFXSystem m_auraFx{};
		/// Réticule de ciblage au sol sous l'ennemi sélectionné : deux cercles
		/// clairs + secteur de vision 120° foncé tournant avec le yaw répliqué.
		/// Decal différé persistant (remplace le rendu provisoire ImGui v12).
		engine::client::TargetReticleSystem m_targetReticle{};
		/// Groupes SP1 — cadres de groupe (PV/mana/nom/chef) + ciblage allié.
		engine::client::PartyHudPresenter m_partyHud{};
		/// Groupes SP1 — allié sélectionné au clic sur un cadre (entityId ==
		/// clientId pour les joueurs, invariant ServerApp::HandleHello) ; 0 = soi.
		/// Consommé par les sorts SingleAlly de la barre d'action (SP3).
		uint64_t m_selectedAllyEntityId = 0;
		/// Métiers SP1 — binds : E = récolter le node à portée (priorité aux
		/// interactibles locaux), K = panneau d'artisanat.
		engine::client::HarvestCastBarPresenter m_harvestBar{};
		engine::client::CraftingUiPresenter m_craftingUi{};
		bool m_craftingVisible = false;
		/// Validation v12 — compte à rebours (s) de l'indication « Hors de
		/// portee » sous le cadre cible. Armé quand T part alors que la cible
		/// est au-delà de la portée de mêlée : le serveur rejette ces attaques
		/// EN SILENCE (aucun message wire de rejet), sans cette indication le
		/// joueur ne comprend pas pourquoi « rien ne se passe ».
		float m_outOfRangeHintSec = 0.0f;
		/// Validation v12 — combat à la souris. Le clic droit MAINTENU pilote
		/// la caméra (mouselook) : un « clic » droit = pression relâchée sans
		/// mouvement (≤ kRmbClickMaxDriftPx). On mémorise la position de la
		/// pression pour discriminer clic et drag au relâchement.
		float m_rmbPressMouseX = 0.0f;
		float m_rmbPressMouseY = 0.0f;
		bool m_rmbClickCandidate = false;
		/// Validation v12 — true tant que le clip de mort de l'avatar local est
		/// posé (évite de le rejouer chaque frame ; la SM locomotion est gelée
		/// pendant la mort et relancée sur Idle au respawn).
		bool m_avatarDeathClipPlaying = false;

		/// Validation v12 (wire v13) — marqueur monde d'un point de réapparition
		/// (lecture CLIENT du même `respawn/respawn_points.txt` que le serveur :
		/// label flottant + anneau au sol, pour rendre cimetières et auberges
		/// visibles sur la carte de démo). Y résolu par la heightmap au rendu.
		struct RespawnMarker
		{
			uint8_t destinationType = 0; ///< kRespawnDestination* (0 cimetière, 1 auberge).
			uint32_t zoneId = 0;
			float x = 0.0f;
			float z = 0.0f;
		};
		std::vector<RespawnMarker> m_respawnMarkers;

		/// Charge les marqueurs de réapparition depuis le fichier data partagé.
		/// Tolérant : fichier absent/ligne invalide = marqueurs vides + WARN
		/// (l'affichage est purement informatif, le serveur reste l'autorité).
		void LoadRespawnMarkers();
		/// Combat SP3 — cooldowns AFFICHÉS de la barre d'action (spellId → fin en
		/// secondes EngineNowSec) ; purement cosmétique, le serveur fait foi.
		std::unordered_map<std::string, float> m_spellCooldownUiUntilSec;
		/// Combat SP2 — visibilité du panneau combat avancé (touche J).
		bool m_advancedCombatVisible = false;
		/// Combat SP2 — throttle local d'envoi d'AttackRequest (le serveur reste
		/// l'autorité du cooldown réel ; ceci évite juste le spam réseau).
		float m_attackSendCooldownSec = 0.0f;
		engine::client::GameplayUdpClient m_gameplayUdp{};
		size_t m_uiObserverHandle = 0;
		bool m_pendingSellActive = false;
		uint32_t m_pendingSellVendorId = 0;
		uint32_t m_pendingSellItemId = 0;
		uint32_t m_pendingSellQty = 0;
		uint32_t m_pendingSellUnitGold = 0;
		std::string m_gameplayVendorTalkTarget;
		std::string m_gameplayAuctionTalkTarget;

		engine::core::Time m_time;
		engine::core::memory::FrameArena m_frameArena;
		engine::render::FpsCameraController m_fpsCameraController;
		/// Controleur camera 3eme personne post-EnterWorld (vue orbitale arriere).
		/// Utilise UNIQUEMENT in-game (post-auth, pas en mode --editor / --world-editor).
		engine::render::OrbitalCameraController m_orbitalCameraController;
		engine::world::World m_world;
		engine::world::StreamingScheduler m_streamingScheduler;
		engine::world::StreamCache m_streamCache;
		engine::render::DeferredDestroyQueue m_deferredDestroyQueue;
		engine::render::GpuUploadQueue m_gpuUploadQueue;
		engine::render::StagingAllocator m_stagingAllocator;
		engine::world::ChunkBudgetStats m_chunkStats;
		engine::world::LodConfig m_lodConfig;
		engine::world::HlodRuntime m_hlodRuntime;
		engine::world::ProbeSet m_zoneProbes;
		engine::world::AtmosphereSettings m_zoneAtmosphere;
		std::vector<engine::world::ChunkDrawDecision> m_chunkDrawDecisions;

		std::array<RenderState, 2> m_renderStates{};
		std::atomic<uint32_t> m_renderReadIndex{ 0 };

		bool m_quitRequested = false;
		/// \c true si la ligne de commande contient \c --world-editor (injecté par lcdlln_world_editor.exe uniquement).
		bool m_worldEditorExe = false;
		bool m_editorEnabled = false;
		/// Menu pause in-game ouvert : la touche Echap post-auth bascule cet etat
		/// (au lieu de quitter le client). Tant qu'il est actif, le menu ImGui est
		/// dessine au-dessus du monde et propose Quitter / Options / Deconnecter.
		bool m_inGamePauseMenuVisible = false;
		/// Mini-panel options in-game ouvert via le bouton Options du menu pause.
		/// Contient les controles essentiels (volume master, plein ecran, vsync,
		/// sensibilite souris) ; pas le full panel auth Options qui necessite un
		/// flux specifique (AuthScreenOptions). Releve des paliers d'un PR ulterieur.
		bool m_inGameOptionsPanelVisible = false;
		bool m_vsync = true;
		double m_fixedDt = 0.0;
		int m_width = 0;
		int m_height = 0;
		/// Count repeated VK_SUBOPTIMAL_KHR returns for the same client size.
		uint32_t m_suboptimalStreak = 0;
		int m_suboptimalWidth = 0;
		int m_suboptimalHeight = 0;
		/// M07.1: When true, TAA prev history is invalid (resize/FOV/teleport); next frame prev = curr.
		bool m_taaHistoryInvalid = true;
		/// M07.2: True after first TAA history init (both buffers filled); on reset we copy only to next.
		bool m_taaHistoryEverFilled = false;
	};
}
