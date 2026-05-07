#pragma once

#include "engine/core/Config.h"
#include "engine/core/Profiler.h"
#include "engine/core/Time.h"
#include "engine/core/memory/FrameArena.h"
#include "engine/audio/AudioEngine.h"
#include "engine/client/AuthUi.h"
#include "engine/client/ChatUi.h"
#include "engine/client/AuctionUi.h"
#include "engine/client/GameplayUdpClient.h"
#include "engine/client/InventoryUi.h"
#include "engine/client/ProfilerHud.h"
#include "engine/client/ShopUi.h"
#include "engine/client/UIModel.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/AssetRegistry.h"
#include "engine/render/DecalSystem.h"
#include "engine/render/FrameGraph.h"
#include "engine/render/vk/VkDeviceContext.h"
#include "engine/render/vk/VkFrameSync.h"
#include "engine/render/vk/VkInstance.h"
#include "engine/render/vk/VkSwapchain.h"
#include "engine/render/ShaderCache.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/render/ShaderHotReload.h"
#include "engine/render/AuthGlyphPass.h"
#include "engine/render/AuthLogoPass.h"
#include "engine/render/TaaJitter.h"
#include "engine/render/Camera.h"
#include "engine/render/CascadedShadowMaps.h"
#include "engine/render/WaterRenderer.h"
#include "engine/render/UnderwaterPass.h"
#include "engine/render/DayNightCycle.h"
#include "engine/render/WeatherSystem.h"
#include "engine/render/DynamicLightSystem.h"
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"
#include "engine/world/WorldModel.h"
#include "engine/world/ChunkBudgetStats.h"
#include "engine/world/LodConfig.h"
#include "engine/world/HlodRuntime.h"
#include "engine/world/ProbeData.h"
#include "engine/world/StreamingScheduler.h"
#include "engine/world/StreamCache.h"
#include "engine/render/vk/DeferredDestroyQueue.h"
#include "engine/render/GpuUploadQueue.h"
#include "engine/render/vk/StagingAllocator.h"
#include "engine/render/terrain/TerrainRenderer.h"
#if defined(_WIN32)
#include "engine/render/terrain/TerrainEditingTools.h"
#include "engine/editor/TexturePreviewCache.h"
#endif

struct GLFWwindow;

namespace engine::render
{
	class AuthImGuiRenderer;
	class ChatImGuiRenderer;
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
#include <string>
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
		/// M07.1: ViewProj from previous frame (for TAA reprojection).
		engine::math::Mat4 prevViewProjMatrix;
		/// M07.1: Current frame jitter in NDC (x, y), applied to projection.
		float jitterCurrNdc[2]{ 0.0f, 0.0f };
		engine::math::Frustum frustum;
		engine::render::CascadesUniform cascades;
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

	private:
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
		/// Load optional zone probe and atmosphere assets from content-relative paths.
		void LoadZoneProbeAssets();
		/// Charge un .spv terrain (même chargeur que l’éditeur monde).
		std::vector<uint32_t> LoadTerrainSpirvWords(const char* relativeSpvPath);
#if defined(_WIN32)
		/// World editor: (re)charge heightmap + outils sculpt depuis le document.
		void RebuildWorldEditorTerrainGpu();

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
		/// Index materiel de l'avatar dans la MaterialDescriptorCache (0 = default fallback,
		/// non-zero = materiel dedie violet clair). Renseigne au boot apres registration.
		uint32_t m_avatarMaterialId = 0u;
		engine::render::DecalSystem m_decalSystem;
		std::vector<engine::render::VisibleDecal> m_visibleDecals;

		/// M37.1: water plane renderer (mesh + reflection/refraction RTs + forward pass).
		engine::render::WaterRenderer m_waterRenderer;

		/// M37.3: underwater post-effect pass (blue tint, depth fog, blur vignette).
		engine::render::UnderwaterPass m_underwaterPass;
		/// M37.3: true when camera.y < waterLevel (underwater detection result, updated each frame).
		bool m_isUnderwater = false;

		/// M38.1: day/night cycle (time-of-day, sun direction, sky gradient colours).
		engine::render::DayNightCycle m_dayNight;

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
		/// M43.4 — Panneau "Editor Hub" overlay quand `--editor` actif.
		std::unique_ptr<engine::render::EditorHubImGuiRenderer> m_editorHubImGui;
		/// Données carte / import (uniquement si \c m_worldEditorExe).
		std::unique_ptr<engine::editor::WorldEditorSession> m_worldEditorSession;
		/// M100.1 — Coquille du nouvel éditeur monde "couche au-dessus".
		/// Instancié si `--editor-world` ou `editor.world.enabled = true`.
		/// Cohabite avec WorldEditorImGui (les deux peuvent être actifs).
		/// Le shell appelle ImGui (Windows-only) — toujours nul sur Linux.
		std::unique_ptr<engine::editor::world::WorldEditorShell> m_worldEditorShell;
#if defined(_WIN32)
		std::unique_ptr<engine::editor::TexturePreviewCache> m_texturePreviewCache;

		/// Nombre de frames en vol pour le purge differé du TexturePreviewCache.
		/// Aligne sur HiZ/GpuDrivenCulling kDefaultFramesInFlight = 2.
		static constexpr uint32_t kEditorTexCacheFramesInFlight = 2u;
#endif
		/// Terrain décalé (jeu + world editor exclusif : un seul actif selon le binaire / reload).
		engine::render::terrain::TerrainRenderer m_terrain;
#if defined(_WIN32)
		engine::render::terrain::TerrainEditingTools m_worldEditorTerrainTools;
#endif
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
		/// Phase 3.5 — Bannière "Bienvenue, X" affichée transitoirement après EnterWorld.
		/// Vide quand inactive. Comparée à \c steady_clock::now() chaque frame.
		std::string                                  m_enterWorldBannerText;
		std::chrono::steady_clock::time_point        m_enterWorldBannerExpiry{};

		/// Phase 3.6.6 — Identité du personnage actif (renseignée à la consommation de
		/// EnterWorldCommand). 0 = pas de perso actif (pré-EnterWorld ou post-Shutdown).
		uint64_t                                     m_currentCharacterId = 0;
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
