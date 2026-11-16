#include "src/client/app/Engine.h"
#include "src/client/app/EngineHelpers.h"

#include "src/client/render/static_mesh/StaticMeshLoader.h"
#include "src/client/world/scatter/PropMaterialRules.h"
#include "src/world_editor/panels/BuildingEditorPanel.h"
#include "src/world_editor/ui/EditorMode.h"
#include "src/world_editor/ui/WorldEditorImGui.h"
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/shared/platform/FileSystem.h"
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
#include "src/client/render/EditorHubImGuiRenderer.h"
#include "src/client/render/LnTheme.h"
#include "src/client/render/DeferredPipeline.h"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "imgui.h"
#endif

namespace engine
{
	using namespace engine::helpers; // utilitaires internes partagés (EngineHelpers.h)

	namespace
	{
		/// TD.8 — conversions entre l'état de locomotion local (Engine) et l'enum wire
		/// partagé (engine::server::AvatarAnimState). Les deux enums listent les mêmes états
		/// dans le même ordre ; les static_assert garantissent l'alignement à la compilation
		/// (un état inséré d'un seul côté casse la build au lieu d'un désync silencieux sur
		/// le wire). On caste sur l'uint8_t sous-jacent — pas de switch à maintenir en double.
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Idle)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Idle), "AvatarAnimState desync (Idle)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Emote)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Emote), "AvatarAnimState desync (Emote)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::Roll)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::Roll), "AvatarAnimState desync (Roll)");
		static_assert(static_cast<uint8_t>(engine::Engine::AvatarLocomotionState::SwimForward)
			== static_cast<uint8_t>(engine::server::AvatarAnimState::SwimForward), "AvatarAnimState desync (dernier état)");

		uint8_t ToWireAnimState(engine::Engine::AvatarLocomotionState s)
		{
			return static_cast<uint8_t>(s);
		}

		engine::Engine::AvatarLocomotionState FromWireAnimState(uint8_t v)
		{
			// Borne défensive : une valeur hors enum (paquet corrompu) retombe sur Idle.
			if (v > static_cast<uint8_t>(engine::server::AvatarAnimState::SwimForward))
				return engine::Engine::AvatarLocomotionState::Idle;
			return static_cast<engine::Engine::AvatarLocomotionState>(v);
		}

		engine::core::LogLevel ParseLogLevelConfig(std::string_view text)
		{
			if (text == "Trace" || text == "trace") return engine::core::LogLevel::Trace;
			if (text == "Debug" || text == "debug") return engine::core::LogLevel::Debug;
			if (text == "Info" || text == "info") return engine::core::LogLevel::Info;
			if (text == "Warn" || text == "warn") return engine::core::LogLevel::Warn;
			if (text == "Error" || text == "error") return engine::core::LogLevel::Error;
			if (text == "Fatal" || text == "fatal") return engine::core::LogLevel::Fatal;
			if (text == "Off" || text == "off") return engine::core::LogLevel::Off;
			return engine::core::LogLevel::Info;
		}

		// Détecte la disposition clavier par défaut au 1er lancement selon l'OS :
		// clavier français (AZERTY) -> "zqsd", sinon "wasd". N'est appliquée que si
		// ni config.json ni user_settings.json ne fixent controls.movement_layout ;
		// le choix du joueur (persisté dans user_settings.json) prime ensuite.
		std::string DetectDefaultMovementLayout()
		{
#if defined(_WIN32)
			const HKL  hkl    = ::GetKeyboardLayout(0);
			const WORD langId = LOWORD(reinterpret_cast<UINT_PTR>(hkl));
			if (PRIMARYLANGID(langId) == LANG_FRENCH)
				return "zqsd";
#endif
			return "wasd";
		}

		void ApplyUserSettingsOverrides(engine::core::Config& cfg)
		{
			// Binds clavier persistants (controls.keybind.*) : fichier dedie
			// keybinds.json, ecrit par le panneau Options au rebind. Merge par-dessus
			// les defauts de config.json. Absent/malforme -> ignore (on garde les defauts) ;
			// fichier dedie => un echec ne corrompt jamais user_settings.json.
			if (cfg.LoadFromFile("keybinds.json"))
				LOG_INFO(Core, "[Boot] keybinds.json applique (binds clavier persistants)");

			// ui_theme.json : préférence de thème UI (fichier dédié écrit par le
			// panneau Options, comme keybinds.json). Merge dans cfg puis applique.
			if (cfg.LoadFromFile("ui_theme.json"))
				LOG_INFO(Core, "[Boot] ui_theme.json applique (theme UI persistant)");
			// Applique le thème lu ; défaut or_royal si absent ou nom invalide
			// (SetActive renvoie false et conserve or_royal dans ce cas).
			const std::string uiTheme = cfg.GetString("ui.theme", "or_royal");
			if (!LnTheme::SetActive(uiTheme))
				LOG_WARN(Core, "[Boot] theme '{}' inconnu -> or_royal", uiTheme);

			// Apparence persistante (client.character_creation.gender) : fichier
			// dedie character_appearance.json, ecrit par le selecteur de genre de
			// l'ecran de creation. Merge par-dessus config.json. Meme logique que
			// keybinds.json (un echec ne corrompt jamais user_settings.json).
			if (cfg.LoadFromFile("character_appearance.json"))
				LOG_INFO(Core, "[Boot] character_appearance.json applique (genre avatar persistant)");

			// Zoom radar minimap (client.quest.minimap.zoom_level) : fichier dedie
			// minimap_settings.json, ecrit au changement de zoom (molette/clic sur le
			// radar). Meme logique que keybinds.json (un echec ne corrompt jamais
			// user_settings.json).
			if (cfg.LoadFromFile("minimap_settings.json"))
				LOG_INFO(Core, "[Boot] minimap_settings.json applique (zoom radar persistant)");

			engine::core::Config persisted;
			if (!persisted.LoadFromFile("user_settings.json"))
			{
				LOG_INFO(Core, "[Boot] user_settings.json not found — using config defaults");
				return;
			}

			if (persisted.Has("render.vsync"))
				cfg.SetValue("render.vsync", persisted.GetBool("render.vsync", cfg.GetBool("render.vsync", true)));
			if (persisted.Has("render.fullscreen"))
				cfg.SetValue("render.fullscreen", persisted.GetBool("render.fullscreen", cfg.GetBool("render.fullscreen", true)));
			if (persisted.Has("render.resolution_width"))
				cfg.SetValue("render.resolution_width", persisted.GetInt("render.resolution_width", cfg.GetInt("render.resolution_width", 1920)));
			if (persisted.Has("render.resolution_height"))
				cfg.SetValue("render.resolution_height", persisted.GetInt("render.resolution_height", cfg.GetInt("render.resolution_height", 1080)));
			if (persisted.Has("render.quality_preset"))
				cfg.SetValue("render.quality_preset", persisted.GetInt("render.quality_preset",
					cfg.GetInt("render.quality_preset", engine::client::kDefaultQualityPreset)));
			if (persisted.Has("render.fov"))
				cfg.SetValue("render.fov", persisted.GetDouble("render.fov", cfg.GetDouble("render.fov", 70.0)));
			if (persisted.Has("client.locale"))
				cfg.SetValue("client.locale", persisted.GetString("client.locale", cfg.GetString("client.locale", "")));
			if (persisted.Has("audio.master_volume"))
				cfg.SetValue("audio.master_volume", persisted.GetDouble("audio.master_volume", cfg.GetDouble("audio.master_volume", 1.0)));
			if (persisted.Has("audio.music_volume"))
				cfg.SetValue("audio.music_volume", persisted.GetDouble("audio.music_volume", cfg.GetDouble("audio.music_volume", 1.0)));
			if (persisted.Has("audio.sfx_volume"))
				cfg.SetValue("audio.sfx_volume", persisted.GetDouble("audio.sfx_volume", cfg.GetDouble("audio.sfx_volume", 1.0)));
			if (persisted.Has("audio.ui_volume"))
				cfg.SetValue("audio.ui_volume", persisted.GetDouble("audio.ui_volume", cfg.GetDouble("audio.ui_volume", 1.0)));
			if (persisted.Has("camera.mouse_sensitivity"))
				cfg.SetValue("camera.mouse_sensitivity", persisted.GetDouble("camera.mouse_sensitivity", cfg.GetDouble("camera.mouse_sensitivity", 0.002)));
			if (persisted.Has("controls.invert_y"))
				cfg.SetValue("controls.invert_y", persisted.GetBool("controls.invert_y", cfg.GetBool("controls.invert_y", false)));
			if (persisted.Has("controls.movement_layout"))
				cfg.SetValue("controls.movement_layout", persisted.GetString("controls.movement_layout", cfg.GetString("controls.movement_layout", "wasd")));
			if (persisted.Has("client.gameplay_udp.enabled"))
				cfg.SetValue("client.gameplay_udp.enabled", persisted.GetBool("client.gameplay_udp.enabled", cfg.GetBool("client.gameplay_udp.enabled", false)));
			if (persisted.Has("client.allow_insecure_dev"))
				cfg.SetValue("client.allow_insecure_dev", persisted.GetBool("client.allow_insecure_dev", cfg.GetBool("client.allow_insecure_dev", true)));
			if (persisted.Has("client.auth_ui.timeout_ms"))
				cfg.SetValue("client.auth_ui.timeout_ms", persisted.GetInt("client.auth_ui.timeout_ms", cfg.GetInt("client.auth_ui.timeout_ms", 5000)));
			if (persisted.Has("render.auth_ui.background_blit.enabled"))
				cfg.SetValue("render.auth_ui.background_blit.enabled",
					persisted.GetBool("render.auth_ui.background_blit.enabled", cfg.GetBool("render.auth_ui.background_blit.enabled", true)));
			if (persisted.Has("render.auth_ui.background_path"))
				cfg.SetValue("render.auth_ui.background_path",
					persisted.GetString("render.auth_ui.background_path", cfg.GetString("render.auth_ui.background_path", "ui/login/background.png")));
			if (persisted.Has("render.auth_ui.background_blit.fit"))
				cfg.SetValue("render.auth_ui.background_blit.fit",
					persisted.GetString("render.auth_ui.background_blit.fit", cfg.GetString("render.auth_ui.background_blit.fit", "cover_height")));

			LOG_INFO(Core, "[Boot] user_settings.json overrides applied (fullscreen={}, vsync={}, locale={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={}, auth_bg_blit={}, auth_bg_fit={})",
				cfg.GetBool("render.fullscreen", true),
				cfg.GetBool("render.vsync", true),
				cfg.GetString("client.locale", ""),
				cfg.GetDouble("audio.master_volume", 1.0),
				cfg.GetDouble("audio.music_volume", 1.0),
				cfg.GetDouble("audio.sfx_volume", 1.0),
				cfg.GetDouble("audio.ui_volume", 1.0),
				cfg.GetDouble("camera.mouse_sensitivity", 0.002),
				cfg.GetBool("controls.invert_y", false),
				cfg.GetString("controls.movement_layout", "wasd"),
				cfg.GetBool("client.gameplay_udp.enabled", false),
				cfg.GetBool("client.allow_insecure_dev", true),
				cfg.GetInt("client.auth_ui.timeout_ms", 5000),
				cfg.GetBool("render.auth_ui.background_blit.enabled", true),
				cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
		}

		bool HasCliFlag(int argc, char** argv, std::string_view flag)
		{
			for (int i = 1; i < argc; ++i)
			{
				if (argv[i] && std::string_view(argv[i]) == flag)
				{
					return true;
				}
			}
			return false;
		}

		/// Match \ref engine::server::VendorCatalog::ComputeSellPrice (client-side preview only).
		uint32_t ClientVendorSellUnitGold(uint32_t buyPrice)
		{
			if (buyPrice == 0u)
			{
				return 0u;
			}
			const uint64_t sp = (static_cast<uint64_t>(buyPrice) * 25ull) / 100ull;
			const uint32_t out = static_cast<uint32_t>(std::min<uint64_t>(sp, static_cast<uint64_t>(0xFFFFFFFFu)));
			return out > 0u ? out : 1u;
		}

	}

	engine::client::NameplateVisibilityParams Engine::CurrentNameplateParams() const
	{
		engine::client::NameplateVisibilityParams params;
		params.visionDistanceM = static_cast<float>(
			std::clamp(m_cfg.GetDouble("client.nameplate.vision_distance_m", 40.0), 5.0, 500.0));
		float halfRatio = 0.5f;
		if (m_perceptionModel.IsLoaded())
		{
			// Perception du classeur : niveau courant (XP > stats), classe, race, sexe.
			const engine::client::UIModel& model = m_uiModelBinding.GetModel();
			const uint32_t level = model.playerStats.hasXp ? model.playerStats.level : 1u;
			params.visionDistanceM = m_perceptionModel.RadiusM(level, model.classId, m_localRaceId, m_avatarGender);
			halfRatio = m_perceptionModel.HalfCircleRatio();
		}
		params.levelRevealDistanceM = params.visionDistanceM * halfRatio;
		params.animalSense = engine::client::ClassHasAnimalSense(m_uiModelBinding.GetModel().classId,
			m_cfg.GetString("client.nameplate.animal_sense_classes", "hunter"));
		return params;
	}

	engine::client::NameplateCategory Engine::ClassifyRemoteForNameplate(
		const engine::client::UIRemoteEntity& remote) const
	{
		if (remote.playerClientId != 0u)
			return engine::client::NameplateCategory::Player;
		// Butin (archetype 0) et nœuds de récolte : hors du périmètre FIX.10.
		if (remote.archetypeId == 0u || remote.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
			return engine::client::NameplateCategory::Other;
		const engine::client::CreatureAppearance* appearance = m_creatureCatalog.Find(remote.archetypeId);
		// Seconde carte (Les Enfers) — PNJ d'ambiance hostile à la race du joueur : ennemi.
		if (appearance != nullptr && appearance->ambient)
			return appearance->IsHostileToRace(m_localRaceId)
				? engine::client::NameplateCategory::HostileMob
				: engine::client::NameplateCategory::AmbientNpc;
		if (appearance != nullptr && appearance->animal)
			return engine::client::NameplateCategory::Animal;
		return engine::client::NameplateCategory::HostileMob;
	}

	void Engine::LoadZoneProbeAssets()
	{
		const std::string zoneMetaPath = m_cfg.GetString("world.zone_meta_path", "zone.meta");
		const std::string probesPath = m_cfg.GetString("world.probes_path", "probes.bin");
		const std::string atmospherePath = m_cfg.GetString("world.atmosphere_path", "atmosphere.json");
		std::string error;
		engine::world::OutputVersionHeader zoneHeader;

		if (!engine::world::LoadVersionedFileHeader(m_cfg, zoneMetaPath,
			engine::world::kZoneMetaMagic,
			engine::world::kZoneMetaVersion,
			zoneHeader,
			error))
		{
			LOG_WARN(Core, "[ZoneProbes] Runtime manifest fallback sky (path={}, reason={})", zoneMetaPath, error);
		}
		else if (engine::world::LoadProbeSet(m_cfg, probesPath, zoneHeader.contentHash, true, m_zoneProbes, error))
		{
			LOG_INFO(Core, "[ZoneProbes] Runtime probes ready (path={}, count={})", probesPath, m_zoneProbes.probes.size());
		}
		else
		{
			LOG_WARN(Core, "[ZoneProbes] Runtime probes fallback sky (path={}, reason={})", probesPath, error);
		}

	error.clear();
	if (engine::world::LoadAtmosphereSettings(m_cfg, atmospherePath, m_zoneAtmosphere, error))
	{
		LOG_INFO(Core, "[ZoneProbes] Runtime atmosphere ready (path={})", atmospherePath);
	}
	else
	{
		LOG_WARN(Core, "[ZoneProbes] Runtime atmosphere defaults active (path={}, reason={})", atmospherePath, error);
	}

	// M38.1 — Initialise day/night cycle with parameters from config.json.
	{
		engine::render::DayNightCycle::Params dnParams{};
		dnParams.initialTimeOfDay = static_cast<float>(
			m_cfg.GetDouble("world.day_night.initial_time",   8.0));
		dnParams.timeScale        = static_cast<float>(
			m_cfg.GetDouble("world.day_night.time_scale",    60.0));
		m_dayNight.Init(dnParams);

		// WorldClock sync (Task 6.2) — intervalle de re-synchro de l'horloge
		// serveur (controle de derive). Defaut 300 s (5 min). La valeur init
		// reste le fallback local jusqu'a la 1ere reception WorldClock (204).
		m_worldClockDriftCheckSec = static_cast<float>(
			m_cfg.GetInt("game.worldclock.drift_check_sec", 300));
	}

	// M38.2 — Initialise weather system with parameters from config.json.
	{
		engine::render::WeatherConfig wCfg{};
		wCfg.transitionDuration = static_cast<float>(m_cfg.GetDouble("world.weather.transition_duration", 30.0));
		wCfg.rainSpawnRate      = static_cast<float>(m_cfg.GetDouble("world.weather.rain_spawn_rate",    1000.0));
		wCfg.snowSpawnRate      = static_cast<float>(m_cfg.GetDouble("world.weather.snow_spawn_rate",     500.0));
		wCfg.fogDensityMax      = static_cast<float>(m_cfg.GetDouble("world.weather.fog_density_max",      0.05));
		m_weatherSystem.Init(wCfg);
	}

	// M38.3 — Initialise dynamic point-light system (streetlamps, torches, windows).
	// The definitions JSON path defaults to "lights/dynamic_lights.json" relative to
	// paths.content, overridable via "world.dynamic_lights_path" in config.json.
	m_dynamicLights.Init(m_cfg);

	// Anti-occlusion caméra — fondu tramé des props occultant la vue caméra→joueur.
	// Réglable sans recompiler via les clés client.camera.occlusion_fade.* du config.
	{
		engine::render::CameraOcclusionFade::Config occCfg{};
		occCfg.fadeMin             = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_min", 0.15));
		occCfg.radiusMargin        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.radius_margin", 0.5));
		occCfg.fadeInPerSec        = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_in_per_sec", 6.0));
		occCfg.fadeOutPerSec       = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.fade_out_per_sec", 8.0));
		occCfg.playerProtectRadius = static_cast<float>(m_cfg.GetDouble("client.camera.occlusion_fade.player_protect_radius", 0.6));
		m_cameraOcclusionFade.Init(occCfg);
	}
}

	Engine::Engine(int argc, char** argv)
		: m_cfg(engine::core::Config::Load("config.json", argc, argv))
		, m_time(120)
		, m_frameArena(/*framesInFlight*/ 2, /*perFrameCapacityBytes*/ 1024 * 1024)
	{

		// ------------------------------------------------------------------
		// Logging
		// ------------------------------------------------------------------
		bool logToFile    = false;
		bool logToConsole = false;
		const bool worldEditorExeEarly = HasCliFlag(argc, argv, "--world-editor");
		for (int i = 1; i < argc; ++i)
		{
			if (!argv[i]) continue;
			const std::string_view arg(argv[i]);
			if (arg == "-log")     logToFile    = true;
			if (arg == "-console") logToConsole = true;
		}

		engine::core::LogSettings logSettings;
		if (logToFile)
		{
			const std::string relPath = engine::core::Log::MakeTimestampedFilename(
				worldEditorExeEarly ? "lcdlln_world_editor.exe" : "lcdlln.exe");
			std::error_code ec;
			const auto absPath = std::filesystem::absolute(relPath, ec);
			logSettings.filePath = ec ? relPath : absPath.string();
			std::fprintf(stderr, "[Log] Log file: %s\n", logSettings.filePath.c_str());
			std::fflush(stderr);
		}
		logSettings.console     = logToConsole;
		logSettings.flushAlways = true;
		logSettings.level       = ParseLogLevelConfig(m_cfg.GetString("log.level", "Info"));
		logSettings.rotation_size_mb = static_cast<size_t>(std::max(static_cast<int64_t>(0), m_cfg.GetInt("log.rotation_size_mb", 10)));
		logSettings.retention_days   = static_cast<int>(m_cfg.GetInt("log.retention_days", 7));
		// Les fichiers de log par sous-systeme (Smtp, gm_log, char_log...) sont une affaire
		// SERVEUR (master/shard). Le client n'ecrit jamais dans ces sous-systemes ; on ne les
		// route donc PAS cote client. Sinon Log::Init materialise des fichiers vides (ex.
		// "lcdlln-SMTP.log") des le boot du jeu, uniquement parce que la cle existe dans le
		// config.json partage entre client et serveur. subsystemFiles reste vide cote client
		// (intentionnel) ; le master continue de lire log.subsystem_files via sa propre config.
		// M44.4 — Format JSONL pour ingestion Loki/ELK (cf. tickets/issues/M44.4_*_Issue.md).
		logSettings.jsonOutput       = m_cfg.GetBool("log.json", false);

		engine::core::Log::Init(logSettings);

		if (!logSettings.filePath.empty() || logToConsole)
		{
			LOG_INFO(Core, "[Boot] Log initialized (console={}, file={}, level={} — use log.level=Debug for verbose render/auth traces)",
				logToConsole ? "on" : "off",
				logSettings.filePath.empty() ? "<none>" : logSettings.filePath,
				m_cfg.GetString("log.level", "Info"));
		}

		// Contenu de la zone active (scenery, interactables, réglages de zone) : fusionné
		// par-dessus config.json. Les clés gardent leur préfixe world.* → call-sites inchangés.
		// Couvre aussi l'éditeur (même constructeur Engine via --world-editor).
		// Monde « Royaumes » en cellules : pas de zone.json unique — liste des cellules
		// générées + réglages client d'une cellule (cf. LoadCellWorldSettings).
		// Les éditeurs (--world-editor, --editor) éditent UN terrain (m_terrain) :
		// jamais en mode cellules.
		const bool anyEditor = HasCliFlag(argc, argv, "--world-editor") || HasCliFlag(argc, argv, "--editor")
			|| m_cfg.GetBool("editor.enabled", false);
		// Monde actif en cellules : l'éditeur ouvre la zone historique `editor.zone`
		// (défaut feyhin) — les cellules se régénèrent avec tools/world_gen.
		if (anyEditor && engine::world::cells::IsCellWorld(m_cfg.GetString("world.active_zone", "")))
		{
			const std::string editorZone = m_cfg.GetString("editor.zone", "feyhin");
			LOG_WARN(Core, "[Cells] Editeur : monde en cellules non editable ici, zone '{}' ouverte (editor.zone)", editorZone);
			m_cfg.SetValue("world.active_zone", editorZone);
		}
		if (!(!anyEditor && LoadCellWorldSettings())
			&& !engine::core::Config::LoadActiveZone(m_cfg, m_cfg.GetString("paths.content", "game/data")))
		{
			LOG_WARN(Core, "[Engine] Zone active introuvable (world.active_zone='{}') : monde par defaut",
				m_cfg.GetString("world.active_zone", ""));
		}

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		ApplyUserSettingsOverrides(m_cfg);
		// 1er lancement : si aucune source (config.json / user_settings.json) ne
		// fixe la disposition clavier, défaut basé sur le clavier OS (AZERTY -> zqsd).
		if (!m_cfg.Has("controls.movement_layout"))
		{
			const std::string osLayout = DetectDefaultMovementLayout();
			m_cfg.SetValue("controls.movement_layout", osLayout);
			LOG_INFO(Core, "[Boot] controls.movement_layout défaut OS (clavier) = {}", osLayout);
		}
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_worldEditorExe = HasCliFlag(argc, argv, "--world-editor");
		m_editorEnabled = m_worldEditorExe || HasCliFlag(argc, argv, "--editor")
			|| m_cfg.GetBool("editor.enabled", false);

		// M100.1 — Branche éditeur monde "couche au-dessus" (distincte de
		// --world-editor qui active le shell M43.x). Activée par le flag CLI
		// --editor-world ou par editor.world.enabled = true dans config.json.
		// Les deux shells peuvent cohabiter pendant la transition.
		//
		// **Auto-activation pour `lcdlln_world_editor.exe`** : depuis M100.46
		// incrément 3 (dialog Zone Presets), l'éditeur monde a besoin du
		// `WorldEditorShell` pour résoudre les 4 documents + 4 catalogs. Sans
		// le Shell, l'entrée menu « Appliquer un preset de zone… » est grisée
		// (m_shell nul). Le binaire `--world-editor` implique donc
		// `--editor-world` par défaut. La warning « deux shells coexistent »
		// reste loguée plus bas en info — c'est attendu.
		const bool worldEditorWorldFlag =
			HasCliFlag(argc, argv, "--editor-world")
			|| m_cfg.GetBool("editor.world.enabled", false)
			|| m_worldEditorExe;

		if (m_worldEditorExe)
		{
			m_worldEditorSession = std::make_unique<engine::editor::WorldEditorSession>();
#if defined(_WIN32)
			m_worldEditorSession->SetTerrainSaveHook(
				[this](const engine::core::Config& cfg, const engine::editor::WorldMapEditDocument& doc) -> bool {
					if (!m_worldEditorTerrainTools.IsValid())
					{
						return true;
					}
					if (!doc.heightmapContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveHeightmap(cfg, doc.heightmapContentRelativePath))
						{
							return false;
						}
					}
					if (!doc.splatmapContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveSplatMap(cfg, doc.splatmapContentRelativePath))
						{
							return false;
						}
					}
					if (!doc.grassMaskContentRelativePath.empty())
					{
						if (!m_worldEditorTerrainTools.SaveGrassMask(cfg, doc.grassMaskContentRelativePath))
						{
							return false;
						}
					}
					return true;
				});
#else
			// `TerrainEditingTools` / flush disque ne sont branchés que sous Windows pour le WE actuel.
			m_worldEditorSession->SetTerrainSaveHook(
				[](const engine::core::Config&, const engine::editor::WorldMapEditDocument&) -> bool { return true; });
#endif
		}

		if (!logSettings.filePath.empty() || logSettings.console)
		{
			LOG_INFO(Core, "[Boot] Config loaded (vsync={}, fixed_dt={})", m_vsync ? "on" : "off", m_fixedDt);
		}

		if (m_editorEnabled)
		{
			m_editorMode = std::make_unique<engine::editor::EditorMode>();
			if (!m_editorMode->Init(m_cfg, m_worldEditorExe))
			{
				LOG_WARN(Core, "[Boot] EditorMode init failed; editor disabled");
				m_editorMode.reset();
				m_editorEnabled = false;
			}
			else
			{
				const engine::render::Camera editorCamera = m_editorMode->BuildInitialCamera();
				m_renderStates[0].camera = editorCamera;
				m_renderStates[1].camera = editorCamera;
				if (m_worldEditorExe)
				{
					LOG_INFO(Core,
						"[Boot] World Editor exe — mode éditeur 3D (Vulkan), pas d’auth client ; flag interne --world-editor");
				}
				else
				{
					LOG_INFO(Core, "[Boot] Editor mode enabled (--editor ou editor.enabled=true)");
				}
			}
		}

		// M100.1 — Coquille du nouvel éditeur monde "couche au-dessus".
		// Indépendante de m_editorMode (qui sert le shell M43.x). Si
		// `--editor-world` ou `editor.world.enabled = true`, on instancie le
		// shell ; il vit en parallèle de WorldEditorImGui. Si les deux flags
		// sont actifs ensemble, c'est le cas non-supporté (logué en warning
		// ci-dessous). Le SetWorldEditorWorld sur m_editorMode sert d'accès
		// public si un sous-système a besoin de le savoir.
		if (worldEditorWorldFlag)
		{
			// `--world-editor` implique désormais `--editor-world` (M100.46
			// incrément 3 — la dialog Zone Presets exige le Shell). On
			// log seulement si l'utilisateur a explicitement combiné les
			// deux flags ou réglé `editor.world.enabled = true` ET lancé
			// l'exe éditeur — pour signaler la coexistence des deux shells
			// (M43.x toolbar + M100.1 WorldEditorShell).
			const bool worldEditorWorldExplicit =
				HasCliFlag(argc, argv, "--editor-world")
				|| m_cfg.GetBool("editor.world.enabled", false);
			if (m_worldEditorExe && worldEditorWorldExplicit)
			{
				LOG_INFO(Core,
					"[Boot] --world-editor + --editor-world (ou editor.world.enabled) — les deux shells coexistent");
			}
			if (m_editorMode)
			{
				m_editorMode->SetWorldEditorWorld(true);
			}
			m_worldEditorShell = std::make_unique<engine::editor::world::WorldEditorShell>();
			if (!m_worldEditorShell->Init(m_cfg))
			{
				LOG_ERROR(EditorWorld, "[Boot] WorldEditorShell::Init a échoué — shell désactivé");
				m_worldEditorShell.reset();
			}
			else
			{
				LOG_INFO(EditorWorld, "[Boot] WorldEditorShell M100.1 instancié (--editor-world)");
				// M100.46/47 — Quand le binaire éditeur monde gère lui-même la
				// barre de menu (français, plus complète), on supprime celle
				// de M100.1 pour éviter la duplication File/Fichier visible.
				if (m_worldEditorExe)
				{
					m_worldEditorShell->SetMenuBarSuppressed(true);
				}
			}
		}

		m_chunkStats.Init(m_cfg);
		m_lodConfig.Init(m_cfg);
		m_hlodRuntime.Init(m_cfg);
		LoadZoneProbeAssets();
		m_streamCache.Init(m_cfg);
		m_streamingScheduler.SetStreamCache(&m_streamCache);
		m_gpuUploadQueue.Init(m_cfg);
		LOG_INFO(Core, "[Boot] Streaming subsystems ready (ChunkStats, LOD, HLOD, StreamCache, GpuUploadQueue)");

		// ------------------------------------------------------------------
		// Window
		// ------------------------------------------------------------------
		engine::platform::Window::CreateDesc desc{};
		desc.title  = m_worldEditorExe ? "LCDLLN World Editor" : "LCDLLN Engine";
		// Editeur monde : 1920x1080 par defaut (panneaux dockes a droite + viewport
		// central genereux). Client de jeu : 1280x720 (pris en charge par fullscreen
		// reglable dans les Options).
		desc.width  = m_worldEditorExe ? 1920 : 1280;
		desc.height = m_worldEditorExe ? 1080 : 720;

		if (!m_window.Create(desc))
		{
			LOG_FATAL(Platform, "[Boot] Window::Create failed");
		}
		LOG_INFO(Core, "[Boot] Window::Create OK");
		if (!m_worldEditorExe && m_cfg.GetBool("render.fullscreen", true))
		{
			m_window.ToggleFullscreen();
		}

		m_window.SetOnResize([this](int w, int h) { OnResize(w, h); });
		m_window.SetOnClose([this]() { OnQuit(); });
		m_window.SetMessageHook([this](uint32_t msg, uint64_t wp, int64_t lp)
		{
			m_input.HandleMessage(msg, wp, lp);
		});
		m_window.GetClientSize(m_width, m_height);

		if (!m_chatUi.Init())
		{
			LOG_WARN(Core, "[Boot] ChatUiPresenter init FAILED — M29.1 chat panel disabled");
		}
		else if (!m_chatUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] ChatUiPresenter viewport FAILED — using fallback layout");
		}

		if (!m_authUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] AuthUiPresenter init FAILED — STAB.13 gate disabled");
		}
		else if (!m_authUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] AuthUiPresenter viewport FAILED — using fallback layout");
		}

		// SP2 Task 5 — Init du presenter Quete (systeme B, shard). Charge aussi
		// les textes de quete (titre/description/etapes, resolus par locale) et
		// la table PNJ->quetes, tous deux consommes par le journal/panneau
		// donneur (QuestImGuiRenderer).
		if (!m_questUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] QuestUiPresenter init FAILED — journal/tracker de quete desactives");
		}
		else if (!m_questUi.SetViewportSize(static_cast<uint32_t>(std::max(1, m_width)), static_cast<uint32_t>(std::max(1, m_height))))
		{
			LOG_WARN(Core, "[Boot] QuestUiPresenter viewport FAILED — using fallback layout");
		}
		{
			const std::string questLocale = m_cfg.GetString("client.locale", "fr");
			if (!m_questTextCatalog.Load(m_cfg, questLocale))
			{
				LOG_WARN(Core, "[Boot] QuestTextCatalog load FAILED (locale={}) — titres/descriptions retombent sur les ids", questLocale);
			}
			if (!m_questGiverTable.Load(m_cfg))
			{
				LOG_WARN(Core, "[Boot] QuestGiverTable load FAILED — table PNJ->quetes vide");
			}
			// SP3 Task 3 — Table des positions minimap (targetId -> [x,z]),
			// injectee dans le presenter pour que RebuildMinimap puisse resoudre
			// les marqueurs de POI des etapes de quete actives.
			// Monde « Royaumes » : POI GLOBAUX générés (world/<monde>/quest_poi.json),
			// ramenés dans le repère de rendu par RebaseRenderOrigin (SetOrigin).
			const bool poiLoaded = m_cellWorld
				? m_questPoiTable.LoadFrom(m_cfg, "world/" + m_cfg.GetString("world.active_zone", "") + "/quest_poi.json")
				: m_questPoiTable.Load(m_cfg);
			if (!poiLoaded)
			{
				LOG_WARN(Core, "[Boot] QuestPoiTable load FAILED — pas de marqueurs de POI sur la minimap");
			}
			m_questUi.SetPoiTable(&m_questPoiTable);
			// Zoom radar : cran persistant (0..4 -> 200..1000 m) ; défaut 600 m.
			// Remplace l'ancien rayon fixe client.quest.minimap.radius_m.
			m_minimapZoomIndex = engine::client::ClampZoomIndex(
				static_cast<int>(m_cfg.GetInt("client.quest.minimap.zoom_level",
					engine::client::kMinimapZoomDefaultIndex)));
			m_questUi.SetMinimapRadius(engine::client::RadiusForZoomIndex(m_minimapZoomIndex));
		}

		// CMANGOS.18 (Phase 3.18 step 4) — Init du presenter Mail. Doit etre
		// fait avant l'installation du push handler ci-dessous (qui dispatche
		// les opcodes Mail vers ce presenter).
		if (!m_mailUi.Init())
		{
			LOG_WARN(Core, "[Boot] MailUiPresenter init FAILED — boite mail desactivee");
		}
		else
		{
			m_mailUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.25 (Phase 3.25 step 3+4) — Init du presenter IgnoreList +
		// cable du send callback. La reception est dispatchee dans le
		// SetMasterPushHandler ci-dessous (opcodes 69/71/73). Le presenter
		// maintient une cache locale m_ignoredAccountIds.
		if (!m_ignoreListUi.Init())
		{
			LOG_WARN(Core, "[Boot] IgnoreListUiPresenter init FAILED — feature ignore desactivee");
		}
		else
		{
			m_ignoreListUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.32 (Phase 5.32 step 3+4) — Init du presenter GmTickets +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 77/79/81/82). Fire-and-forget des requetes
		// 76/78/80 via SendGenericRequestAsync.
		if (!m_gmTicketUi.Init())
		{
			LOG_WARN(Core, "[Boot] GmTicketUiPresenter init FAILED — support GM desactive");
		}
		else
		{
			m_gmTicketUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.24 (Phase 3.24 step 3+4) — Init du presenter Reputation +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 96/97). Fire-and-forget de la requete 95 via
		// SendGenericRequestAsync.
		if (!m_reputationUi.Init())
		{
			LOG_WARN(Core, "[Boot] ReputationUiPresenter init FAILED — panneau reputation desactive");
		}
		else
		{
			m_reputationUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// SP2 anniversaires (2026-07-18) — send callback du presenter Exploits
		// (requête 206 fire-and-forget ; réponse 207 dispatchée plus bas).
		m_exploitsUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
			return m_authUi.SendGenericRequestAsync(opcode, payload);
		});

		// ETAPE1.1 — masterd est un relais pur : il ne simule plus l'échange, la
		// recherche de groupe, les compétences, l'arène, les champs de bataille, le
		// JcJ extérieur, la météo, les événements, les guildes V1, l'hôtel des ventes
		// V1 ni les jets de loot (ces handlers V1 renvoyaient des données factices).
		// Les presenters correspondants restent initialisés, mais leurs requêtes ne
		// partent plus vers le master : l'envoi échoue proprement (message d'erreur
		// du presenter) en attendant leur branchement sur le shard (UDP).
		const auto retiredMasterFeature = [](const char* feature) {
			return [feature](uint16_t opcode, const std::vector<uint8_t>& /*payload*/) -> bool {
				LOG_INFO(Net, "[ETAPE1.1] '{}' : fonction retiree du master (opcode {} non envoye)", feature, opcode);
				return false;
			};
		};

		// CMANGOS.33 (Phase 5.33 step 3+4) — Init du presenter LFG + cable du
		// send callback pour les requetes 100/102/104/107. Reception dispatchee
		// dans le push handler ci-dessous (responses 101/103/105 + push 106).
		if (!m_lfgUi.Init())
		{
			LOG_WARN(Core, "[Boot] LfgUiPresenter init FAILED — panneau LFG desactive");
		}
		else
		{
			m_lfgUi.SetSendCallback(retiredMasterFeature("lfg"));
		}

		// CMANGOS.30 (Phase 5.30 step 3+4) — Init du presenter cinematique.
		// Le presenter envoie 109 (Ack) et 111 (SkipRequest) ; il recoit le
		// push 108 + responses 110/112 dans le push handler ci-dessous. Un
		// Tick(nowMs) est appele chaque frame depuis BeginFrame quand une
		// cinematique est en cours.
		if (!m_cinematicUi.Init())
		{
			LOG_WARN(Core, "[Boot] CinematicUiPresenter init FAILED — cinematiques desactivees");
		}
		else
		{
			m_cinematicUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// CMANGOS.39 (Phase 4.39 step 3+4) — Init du presenter Skill Book +
		// cable du send callback. Reception dispatchee dans le push handler
		// ci-dessous (opcodes 114/116/118 responses + 119 push). Fire-and-forget
		// des requetes 113/115/117 via SendGenericRequestAsync.
		if (!m_skillBookUi.Init())
		{
			LOG_WARN(Core, "[Boot] SkillBookUiPresenter init FAILED — panneau skill book desactive");
		}
		else
		{
			m_skillBookUi.SetSendCallback(retiredMasterFeature("skills"));
		}

		// Grimoire (Task 13) — Init du presenter Grimoire + câblage du send callback
		// vers GameplayUdpClient::SendSetActionBarLayout (kind 88). Le serveur renvoie
		// un ActionBarLayoutUpdate (kind 89) autoritaire capturé par UIModel.
		if (!m_grimoireUi.Init(&m_spellCatalog))
		{
			LOG_WARN(Core, "[Boot] GrimoireUiPresenter init FAILED — panneau Grimoire desactive");
		}
		else
		{
			m_grimoireUi.SetSendCallback([this](const std::array<std::string, 10>& slots) -> bool {
				const uint32_t clientId = m_gameplayUdp.ServerClientId();
				if (clientId == 0u)
				{
					return false;
				}
				return m_gameplayUdp.SendSetActionBarLayout(clientId, slots);
			});
		}

		// SP-D — Init du presenter arbre de compétences par-classe + câblage
		// du callback SendChooseClassSkill (GameplayUdpClient). Non bloquant.
		if (!m_classSkillTreeUi.Init(&m_classSkillCatalog))
		{
			LOG_WARN(Core, "[Boot] ClassSkillTreeUiPresenter init FAILED — arbre de compétences désactivé");
		}
		else
		{
			m_classSkillTreeUi.SetChooseCallback([this](uint32_t level, const std::string& skillId) -> bool {
				const uint32_t cid = m_gameplayUdp.ServerClientId();
				if (cid == 0u)
				{
					return false;
				}
				return m_gameplayUdp.SendChooseClassSkill(cid, level, skillId);
			});
		}

		// CMANGOS.21 (Phase 5.21 step 3+4) — Init du presenter Arena + cable
		// du send callback pour les requetes 120/122/124/127. Reception
		// dispatchee dans le push handler ci-dessous (responses 121/123/125/128
		// + push 126/129).
		if (!m_arenaUi.Init())
		{
			LOG_WARN(Core, "[Boot] ArenaUiPresenter init FAILED — panneau arena desactive");
		}
		else
		{
			m_arenaUi.SetSendCallback(retiredMasterFeature("arena"));
		}

		// CMANGOS.10 (Phase 5 step 3+4) — Init du presenter BattleGround + cable
		// du send callback pour les requetes 130/132/134/139. Reception
		// dispatchee dans le push handler ci-dessous (responses 131/133/135
		// + push 136/137/138).
		if (!m_battleGroundUi.Init())
		{
			LOG_WARN(Core, "[Boot] BattleGroundUiPresenter init FAILED — panneau BG desactive");
		}
		else
		{
			m_battleGroundUi.SetSendCallback(retiredMasterFeature("battleground"));
		}

		// CMANGOS.36 (Phase 5.36 step 3+4) — Init du presenter OutdoorPvp + cable
		// du send callback pour les requetes 140/142/144/146. Reception
		// dispatchee dans le push handler ci-dessous (responses 141/143/145/147
		// + push 148/149).
		if (!m_outdoorPvpUi.Init())
		{
			LOG_WARN(Core, "[Boot] OutdoorPvpUiPresenter init FAILED — panneau OutdoorPvp desactive");
		}
		else
		{
			m_outdoorPvpUi.SetSendCallback(retiredMasterFeature("outdoor_pvp"));
		}

		// CMANGOS.42 (Phase 4.42 step 3+4) — Init du presenter Weather + cable
		// du send callback pour les requetes 150/152/154. Reception
		// dispatchee dans le push handler ci-dessous (responses 151/153/155
		// + push 156).
		if (!m_weatherUi.Init())
		{
			LOG_WARN(Core, "[Boot] WeatherUiPresenter init FAILED — panneau Weather desactive");
		}
		else
		{
			m_weatherUi.SetSendCallback(retiredMasterFeature("weather"));
		}

		// CMANGOS.31 (Phase 5.31 step 3+4) — Init du presenter GameEvents +
		// cable du send callback pour les requetes 157/159/161. Reception
		// dispatchee dans le push handler ci-dessous (responses 158/160/162
		// + push 163 StateChange).
		if (!m_gameEventUi.Init())
		{
			LOG_WARN(Core, "[Boot] GameEventUiPresenter init FAILED — panneau GameEvents desactive");
		}
		else
		{
			m_gameEventUi.SetSendCallback(retiredMasterFeature("game_events"));
		}

		// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Init du presenter Guildes +
		// cable du send callback pour les requetes 164/166/168/170. Reception
		// dispatchee dans le push handler ci-dessous (responses 165/167/169/171
		// + push 172 MotdUpdate).
		if (!m_guildUi.Init())
		{
			LOG_WARN(Core, "[Boot] GuildUiPresenter init FAILED — panneau Guildes desactive");
		}
		else
		{
			m_guildUi.SetSendCallback(retiredMasterFeature("guild"));
		}

		// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Init du presenter
		// Hotel des Ventes + cable du send callback pour les requetes
		// 173/175/177/179. Reception dispatchee dans le push handler ci-dessous
		// (responses 174/176/178/180 + push 181 AuctionExpired).
		if (!m_auctionHouseUi.Init())
		{
			LOG_WARN(Core, "[Boot] AuctionHousePresenter init FAILED — panneau Hotel des Ventes desactive");
		}
		else
		{
			m_auctionHouseUi.SetSendCallback(retiredMasterFeature("auction"));
		}

		// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Init du presenter Loot Roll
		// + cable du send callback pour les requetes 183/186. Reception
		// dispatchee dans le push handler ci-dessous (responses 184/187 + push
		// 182 RollNotification + push 185 RollResultNotification).
		if (!m_lootRollUi.Init())
		{
			LOG_WARN(Core, "[Boot] LootRollUiPresenter init FAILED — fenetre Loot Roll desactivee");
		}
		else
		{
			m_lootRollUi.SetSendCallback(retiredMasterFeature("loot"));
		}

		// CMANGOS.27 (Phase 4.27 step 3+4) — Init du presenter TradeWindow + cable
		// du send callback pour les requetes 83/86/88/91/93. Reception dispatchee
		// dans le push handler ci-dessous (responses 84/87/89/92 + push 85/90/94).
		// Le presenter Init() existant a la signature (config) ; on lui passe m_cfg.
		if (!m_tradeWindowUi.Init(m_cfg))
		{
			LOG_WARN(Core, "[Boot] TradeWindowUiPresenter init FAILED — trade desactive");
		}
		else
		{
			m_tradeWindowUi.SetSendCallback(retiredMasterFeature("trade"));
		}

		// Chat MVP — câblage bidirectionnel ChatUi <-> AuthUi (master TCP).
		// Send : ChatUi::SubmitInputLine appelle AuthUi::SendChatAsync sur la connexion master vivante.
		// Receive : AuthUi::PumpPostAuthEvents dispatche les paquets push (CHAT_RELAY notamment)
		// vers un handler qui parse et appelle ChatUi::PushNetworkLine.
		m_chatUi.SetSendCallback([this](uint8_t channel, std::string_view targetToken, std::string_view text) -> bool {
			return HandleChatSend(channel, targetToken, text);
		});
		m_authUi.SetMasterPushHandler([this](uint16_t opcode, const uint8_t* payload, size_t payloadSize) {
			HandleMasterPush(opcode, payload, payloadSize);
		});

		if (m_worldEditorExe && m_authUi.IsInitialized())
		{
			m_authUi.BypassAuthGateForWorldEditor();
			LOG_INFO(Core, "[Boot] World Editor : saut de l’écran d’authentification");
		}

		InitGameplayNet();

		// -----------------------------------------------------------------
		// Vulkan init
		// -----------------------------------------------------------------
		if (glfwInit() != GLFW_TRUE)
		{
			LOG_WARN(Platform, "[Boot] glfwInit failed");
		}
		else
		{
			InitGraphicsAndScene();
		}

		// FS smoke test
		{
			const auto cfgText = engine::platform::FileSystem::ReadAllText("config.json");
			LOG_INFO(Platform, "FS ReadAllText('config.json'): {} bytes", cfgText.size());
			const auto contentCfgText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, "config.json");
			LOG_INFO(Platform, "FS ReadAllTextContent(paths.content/'config.json'): {} bytes", contentCfgText.size());
		}

		LOG_INFO(Core, "Engine init: vsync={} (present mode from swapchain)", m_vsync ? "on" : "off");
		LOG_INFO(Core, "[Boot] Engine boot COMPLETE");
	}

	Engine::~Engine() = default;

	int Engine::Run()
	{
		LOG_DEBUG(Core, "[Engine] Entering render loop");

		auto lastFpsLog  = std::chrono::steady_clock::now();
		auto lastPresent = lastFpsLog;
		auto lastPerfLog = lastFpsLog;

		while (!m_quitRequested && !m_window.ShouldClose())
		{
			BeginFrame();
			{
				PROFILE_SCOPE("Update");
				Update();
			}
			SwapRenderState();
			{
				PROFILE_SCOPE("Render");
				Render();
			}

			EndFrame();

			const auto now = std::chrono::steady_clock::now();
			if (now - lastFpsLog >= std::chrono::seconds(1))
			{
				LOG_INFO(Core, "fps={:.1f} dt_ms={:.3f} frame={}", m_time.FPS(), m_time.DeltaSeconds() * 1000.0, m_time.FrameIndex());
				lastFpsLog = now;
			}
			// FIX.13 — diagnostic de performance : où part le temps d'une frame
			// (CPU par phase, GPU par passe) + volume de la scène, toutes les
			// debug.perf_log_interval_s secondes (0 = désactivé).
			{
				const double perfInterval = m_cfg.GetDouble("debug.perf_log_interval_s", 5.0);
				if (perfInterval > 0.0 && now - lastPerfLog >= std::chrono::duration<double>(perfInterval))
				{
					LogPerfSummary();
					lastPerfLog = now;
				}
			}

			if (!m_vsync)
			{
				constexpr auto safetyTarget = std::chrono::microseconds(5000);
				const auto elapsed = now - lastPresent;
				if (elapsed < safetyTarget)
					std::this_thread::sleep_for(safetyTarget - elapsed);
			}
			lastPresent = std::chrono::steady_clock::now();
		}

		LOG_INFO(Core, "[Engine] Render loop exited cleanly");

		if (m_vkDeviceContext.IsValid())
		{
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
			// Sous-projet C MVP (Task 12) — Detruit le viewport offscreen
			// race AVANT m_editorViewportTarget (idem motif que celui-ci :
			// ordre LIFO de liberation des descriptors ImGui).
			m_racePreviewViewport.Shutdown(m_vkDeviceContext.GetDevice());
			// M100.34 incrément 1 — détruit l'image offscreen viewport
			// AVANT TexturePreviewCache (qui possède aussi des descriptors
			// ImGui), pour respecter l'ordre LIFO de désallocation.
			m_editorViewportTarget.Shutdown(m_vkDeviceContext.GetDevice());
#if defined(_WIN32)
			if (m_texturePreviewCache) m_texturePreviewCache->Shutdown();
			m_texturePreviewCache.reset();
			// M100.1 — Persiste le layout du shell (ImGui::SaveIniSettingsToDisk)
			// AVANT WorldEditorImGui::Shutdown, qui detruit le contexte ImGui.
			// Sinon le Shell::Shutdown appele plus bas (apres destruction du
			// contexte + du device Vulkan) ecrirait via un contexte ImGui nul
			// -> access violation a la fermeture (SEH 0xC0000005). Idempotent :
			// le 2e Shutdown plus bas est neutralise par la garde m_initialized.
			if (m_worldEditorShell) m_worldEditorShell->Shutdown();
			// SP-E — Libère les descripteurs d'icônes (ImGui_ImplVulkan_RemoveTexture)
			// AVANT la destruction du backend ImGui Vulkan ci-dessous.
			m_skillIconCache.Shutdown();
			if (m_worldEditorImGui)
			{
				m_worldEditorImGui->DetachPlatformWindow(m_window);
				m_worldEditorImGui->Shutdown(m_vkDeviceContext.GetDevice());
				m_worldEditorImGui.reset();
			}
			m_authImGui.reset();
			m_worldEditorTerrainTools.Shutdown();
#endif
		if (m_terrain.IsValid())
			m_terrain.Destroy(m_vkDeviceContext.GetDevice());
		// Monde « Royaumes » : terrains des cellules (device encore valide ici).
		ShutdownCellWorld();
		// M100 — Task 12 : shutdown terrain chunk runtime AVANT le DeferredPipeline
		// (le renderer dépend du renderPass `m_pipeline->GetGeometryPass`).
		if (m_terrainChunkRenderer)
		{
			m_terrainChunkRenderer->Shutdown(m_vkDeviceContext.GetDevice());
			m_terrainChunkRenderer.reset();
		}
		DestroyTerrainChunkCameraResources();
		m_authGlyphPass.Destroy(m_vkDeviceContext.GetDevice());
			m_authLogoPass.Destroy(m_vkDeviceContext.GetDevice());
			// M100.14 — Détruit la passe water + ses buffers GPU avant le DeferredPipeline
			// (les ressources sont indépendantes mais l'ordre symétrique d'Init est plus
			// lisible côté boot ↔ shutdown).
			m_waterPass.Destroy(m_vkDeviceContext.GetDevice());
			// Textures muettes WaterPass (normalMap + skybox cube).
			{
				VkDevice dev = m_vkDeviceContext.GetDevice();
				if (m_waterNormalMapSampler != VK_NULL_HANDLE)
				{ vkDestroySampler(dev, m_waterNormalMapSampler, nullptr); m_waterNormalMapSampler = VK_NULL_HANDLE; }
				if (m_waterNormalMapView != VK_NULL_HANDLE)
				{ vkDestroyImageView(dev, m_waterNormalMapView, nullptr);  m_waterNormalMapView = VK_NULL_HANDLE; }
				if (m_waterNormalMapImg != VK_NULL_HANDLE)
				{ vkDestroyImage(dev, m_waterNormalMapImg, nullptr);       m_waterNormalMapImg = VK_NULL_HANDLE; }
				if (m_waterNormalMapMem != VK_NULL_HANDLE)
				{ vkFreeMemory(dev, m_waterNormalMapMem, nullptr);         m_waterNormalMapMem = VK_NULL_HANDLE; }
				if (m_waterSkyboxSampler != VK_NULL_HANDLE)
				{ vkDestroySampler(dev, m_waterSkyboxSampler, nullptr);    m_waterSkyboxSampler = VK_NULL_HANDLE; }
				if (m_waterSkyboxView != VK_NULL_HANDLE)
				{ vkDestroyImageView(dev, m_waterSkyboxView, nullptr);     m_waterSkyboxView = VK_NULL_HANDLE; }
				if (m_waterSkyboxImg != VK_NULL_HANDLE)
				{ vkDestroyImage(dev, m_waterSkyboxImg, nullptr);          m_waterSkyboxImg = VK_NULL_HANDLE; }
				if (m_waterSkyboxMem != VK_NULL_HANDLE)
				{ vkFreeMemory(dev, m_waterSkyboxMem, nullptr);            m_waterSkyboxMem = VK_NULL_HANDLE; }
			}
			// Phase 5 Lunar + M38.1 Sky : detruit le SkyPass (pipeline + layout)
			// avant le DeferredPipeline (m_skyPass depend du renderPass de
			// GeometryPass, donc on libere le pipeline d'abord).
			if (m_skyPassReady)
			{
				m_skyPass.Shutdown(m_vkDeviceContext.GetDevice());
				m_skyPassReady = false;
			}
			m_waterMeshGpu.Destroy();
			// Monde ouvert : anneau de buffers d'instances du décor (meshes = AssetRegistry).
			DestroyScatterResources();
			if (m_waterTransferPool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, nullptr);
				m_waterTransferPool = VK_NULL_HANDLE;
			}
			// Sous-projet A (Task 15) + Sous-projet C MVP : libere les meshes
			// skinnes (ownership = m_raceMeshes depuis C MVP, plus
			// m_currentSkinnedMesh qui n'est plus qu'un pointeur dans la map),
			// puis le renderer. Doit etre fait apres vkDeviceWaitIdle (line ~5060)
			// et AVANT m_pipeline->Destroy (le SkinnedRenderer reutilise le
			// materialLayout du MaterialDescriptorCache mais possede son propre
			// pipeline/render pass -- l'ordre n'est pas critique entre eux, mais
			// on respecte l'ordre LIFO).
			// Idempotent : safe si Init a echoue (map vide -> boucle skippee,
			// handles VK_NULL_HANDLE skippes par SkinnedMesh::Destroy).
			for (auto& kv : m_raceMeshes)
			{
				kv.second.Destroy(m_vkDeviceContext.GetDevice());
			}
			m_raceMeshes.clear();
			m_chestMesh.Destroy(m_vkDeviceContext.GetDevice());
			m_boatMesh.Destroy(m_vkDeviceContext.GetDevice()); // v1.0.125
			m_currentSkinnedMesh = nullptr;
			m_skinnedRenderer.Destroy(m_vkDeviceContext.GetDevice());
			m_skinnedAvatarReady = false;
			// M45.7 — libère le volume DDGI + sa passe de mise à jour (idempotent :
			// sûr même si DDGI n'a jamais été activé/alloué).
			m_ddgiUpdatePass.Destroy(m_vkDeviceContext.GetDevice());
			m_ddgiVolume.Destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			m_ddgiEnabled = false;
			if (m_pipeline)
			{
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				m_pipeline.reset();
			}
			m_profilerHud.Shutdown();
			m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
			m_audioEngine.Shutdown();
			// Le réticule détient un handle de decal persistant : à retirer AVANT
			// le Shutdown du DecalSystem (sa texture appartient à l'AssetRegistry).
			m_targetReticle.Shutdown();
			m_decalSystem.Shutdown();
			m_assetRegistry.Destroy();
			m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
			m_stagingAllocator.Destroy(m_vkDeviceContext.GetDevice());
			engine::render::DestroyFrameResources(m_vkDeviceContext.GetDevice(), m_frameResources);
			// VMA is disabled (STAB.7); m_vmaAllocator is always nullptr.
		}
		m_vkSwapchain.Destroy();
		m_vkDeviceContext.Destroy();
		m_vkInstance.Destroy();
		if (m_glfwWindowForVk)
		{
			glfwDestroyWindow(m_glfwWindowForVk);
			m_glfwWindowForVk = nullptr;
		}
		glfwTerminate();

		if (m_editorMode)
		{
			m_editorMode->Shutdown(m_window);
			m_editorMode.reset();
		}
		// M100.1 — Persiste le layout ImGui du nouvel éditeur monde puis
		// libère ses panneaux. Idempotent si Init() n'a pas réussi.
		if (m_worldEditorShell)
		{
			m_worldEditorShell->Shutdown();
			m_worldEditorShell.reset();
		}
		// Phase 3.6.6 — Sauvegarde finale de la position avant de fermer la connexion master.
		// Fire-and-forget : on n'attend pas l'ack (ce serait risqué dans un chemin de shutdown
		// avec timers et ordre de destruction) — le serveur logge en cas d'échec d'UPDATE.
		// La connexion m_authUi.m_masterClient est encore vivante ici (Shutdown des UI vient juste après).
		if (!m_shutdownPositionSaved && m_currentCharacterId != 0u)
		{
			const uint32_t shutdownReadIdx = m_renderReadIndex.load(std::memory_order_acquire) & 1u;
			const engine::render::Camera& shutdownCam = m_renderStates[shutdownReadIdx].camera;
			constexpr float kRad2Deg = 180.f / 3.14159265f;
			const float yawDeg   = shutdownCam.yaw   * kRad2Deg;
			const float pitchDeg = shutdownCam.pitch * kRad2Deg;
			// Vue 3eme personne : on persiste la position cible (joueur), pas la camera.
			const engine::math::Vec3& playerPos = m_orbitalCameraController.GetTargetPosition();
			// Monde « Royaumes » : le master stocke des positions GLOBALES.
			const bool sent = m_authUi.SavePositionAsync(m_currentCharacterId,
				m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalX(playerPos.x)) : playerPos.x, playerPos.y,
				m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalZ(playerPos.z)) : playerPos.z,
				yawDeg, pitchDeg);
			LOG_INFO(Core, "[SavePosition] shutdown save (character_id={}, pos=({:.1f},{:.1f},{:.1f}), sent={})",
				m_currentCharacterId, playerPos.x, playerPos.y, playerPos.z, sent);
			m_shutdownPositionSaved = true;
		}

		// Carte à découvrir : dernières cases révélées (fire-and-forget).
		FlushMapDiscovery();
		ShutdownGameplayNet();
		m_authUi.Shutdown();
		m_chatUi.Shutdown();
		m_mailUi.Shutdown();
		m_gmTicketUi.Shutdown();
		m_reputationUi.Shutdown();
		m_lfgUi.Shutdown();
		m_cinematicUi.Shutdown();
		m_skillBookUi.Shutdown();
		m_grimoireUi.Shutdown(); // Grimoire (Task 13)
		m_classSkillTreeUi.Shutdown(); // SP-D
		m_arenaUi.Shutdown();
		m_battleGroundUi.Shutdown();
		m_outdoorPvpUi.Shutdown();
		m_weatherUi.Shutdown();
		m_gameEventUi.Shutdown();
		m_guildUi.Shutdown();
		m_auctionHouseUi.Shutdown();
		m_lootRollUi.Shutdown();
		m_window.Destroy();
		LOG_INFO(Core, "[Engine] Shutdown complete");
		return 0;
	}

	void Engine::BeginFrame()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] BeginFrame enter frame={}", m_currentFrame);
		m_input.BeginFrame();
		m_window.PollEvents();
		// FIX.14 — touches dont le WM_KEYUP s'est perdu (personnage qui avançait seul).
		m_input.SyncWithOs();

		if (m_authUi.IsInitialized() && !m_authUi.IsFlowComplete())
		{
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				if (!m_authUi.OnEscape())
					OnQuit();
			}
		}
		else if (m_cinematicUi.IsInitialized() && m_cinematicUi.GetState().isPlaying)
		{
			// CMANGOS.30 (Phase 5.30 step 3+4) — Pendant une cinematique, Esc
			// envoie une demande de skip au master. La reponse asynchrone via
			// OnSkipResponse termine effectivement la lecture si allowed=true.
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				m_cinematicUi.RequestSkip();
			}
		}
		else if (m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive())
		{
			if (m_input.WasPressed(engine::platform::Key::Escape))
			{
				m_chatUi.SetChatFocus(false);
			}
		}
		else if (m_gameplayNetInitialized && m_uiModelBinding.GetModel().auction.isOpen
			&& m_input.WasPressed(engine::platform::Key::Escape))
		{
			(void)m_uiModelBinding.CloseAuction();
			m_invUi.CancelDrag();
			m_pendingSellActive = false;
			LOG_INFO(Core, "[GameplayNet] Auction closed (Escape)");
		}
		else if (m_gameplayNetInitialized && m_uiModelBinding.GetModel().shop.isOpen
			&& m_input.WasPressed(engine::platform::Key::Escape))
		{
			(void)m_uiModelBinding.CloseShop();
			m_invUi.CancelDrag();
			m_pendingSellActive = false;
			LOG_INFO(Core, "[GameplayNet] Shop closed (Escape)");
		}
		else if (!m_editorEnabled && m_input.WasPressed(engine::platform::Key::Escape))
		{
			// Echap in-game (post-auth, pas dans un menu chat/auction/shop) :
			// toggle le menu pause au lieu de quitter directement le client.
			// Demande utilisateur explicite : 'on quitte automatiquement le jeu,
			// il ne faut surtout pas. Nous devons toujours passer par un menu
			// intermediaire, qui propose de Quitter / Options / Se deconnecter'.
			ToggleInGamePauseMenu();
		}

		if (m_input.WasPressed(engine::platform::Key::F_11))
    		m_window.ToggleFullscreen();

		// Chantier 1 — touche F2 (Skill Book) LIBÉRÉE : le livre de compétences est
		// désormais l'onglet Compétences de la fenêtre Personnage unifiée (F1). Le
		// RequestList() est déclenché à l'ouverture de la fenêtre côté conteneur au
		// besoin ; l'ancien toggle F2 autonome est retiré.

		// Chantier 1 — touches F3 (Grimoire) et F4 (arbre) LIBÉRÉES : ces panneaux
		// sont désormais des onglets de la fenêtre Personnage unifiée ouverte par F1
		// (cf. bloc F1 plus bas). Leurs anciens toggles autonomes sont retirés.

		// Bascule HUD carte — Touche U remappable post-auth masque/affiche le
		// « cluster carte » : boussole + radar minimap + tracker de quêtes (retour
		// joueur : « pouvoir boucher la boussole, le radar et le suivi de quête »).
		// Le journal et le panneau donneur ne sont PAS concernés (interactifs).
		// Mêmes guards que les autres toggles (chat focus + pause + editor + auth).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			const engine::platform::Key hudKey =
				KeyFromName(m_cfg.GetString("controls.keybind.hud_toggle", "U"), engine::platform::Key::U);
			if (inGameNoMenu && !chatBlocks && m_input.WasPressed(hudKey))
			{
				m_hudMapClusterHidden = !m_hudMapClusterHidden;
				LOG_INFO(Core, "[Engine] HUD map cluster toggle (hidden={})", m_hudMapClusterHidden);
			}
		}

		// CMANGOS.21 (Phase 5.21 step 3+4) — Touche A post-auth toggle le
		// panneau Arena (equivalent a la slash command /arena). Memes guards
		// que la touche B (chat focus + pause + editor + auth flow).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.arena", "F8"), engine::platform::Key::F_8)))
			{
				m_arenaVisible = !m_arenaVisible;
				if (m_arenaVisible)
				{
					m_arenaUi.RequestTeams();
				}
				LOG_INFO(Core, "[Engine] F8 toggle arena (visible={})", m_arenaVisible);
			}
		}

		// CMANGOS.10 (Phase 5 step 3+4) — Touche G post-auth toggle le panneau
		// BattleGround (equivalent a la slash command /bg). Memes guards que
		// la touche A (chat focus + pause + editor + auth flow).
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.battleground", "F9"), engine::platform::Key::F_9)))
			{
				m_battleGroundVisible = !m_battleGroundVisible;
				if (m_battleGroundVisible)
				{
					m_battleGroundUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] G toggle battleground (visible={})", m_battleGroundVisible);
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Touche P : toggle panneau
			// OutdoorPvp + RequestList si on l'ouvre. Memes guards que A/G.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.outdoorpvp", "F10"), engine::platform::Key::F_10)))
			{
				m_outdoorPvpVisible = !m_outdoorPvpVisible;
				if (m_outdoorPvpVisible)
				{
					m_outdoorPvpUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F10 toggle outdoorpvp (visible={})", m_outdoorPvpVisible);
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Touche Y : toggle panneau
			// Weather + RequestList si on l'ouvre. Memes guards que A/G/P.
			// Note : la touche Y est aussi utilisee comme Ctrl+Y pour le
			// redo dans WorldEditorShell, mais le guard inGameNoMenu inclut
			// !m_editorEnabled donc pas de conflit. WorldEditorShell traite
			// Ctrl+Y / Ctrl+Shift+Y dans son propre bloc en aval.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.weather", "F12"), engine::platform::Key::F_12)))
			{
				m_weatherVisible = !m_weatherVisible;
				if (m_weatherVisible)
				{
					m_weatherUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F12 toggle weather (visible={})", m_weatherVisible);
			}
			// Touche E desormais LIBRE (reservee a une future action "interagir"
			// hors combat). Le panneau GameEvents s'ouvre via la barre de menus
			// "Panneaux" (rendu ImGui in-game), plus par une touche dediee.
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Touche U : toggle
			// panneau Guildes + RequestList si on l'ouvre. Memes guards que
			// E (chat focus, pause, editor). Pas de conflit Ctrl+U.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.guild", "F5"), engine::platform::Key::F_5)))
			{
				m_guildVisible = !m_guildVisible;
				if (m_guildVisible)
				{
					m_guildUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] F5 toggle guilds (visible={})", m_guildVisible);
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Touche H : toggle
			// panneau Hotel des Ventes + RequestList si on l'ouvre. Memes
			// guards que U. Pas de conflit Ctrl+H.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.auctionhouse", "F6"), engine::platform::Key::F_6)))
			{
				m_auctionHouseVisible = !m_auctionHouseVisible;
				if (m_auctionHouseVisible)
				{
					m_auctionHouseUi.RequestList(0u);
				}
				LOG_INFO(Core, "[Engine] F6 toggle auction house (visible={})", m_auctionHouseVisible);
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Touche L : toggle
			// fenetre Loot Roll. Memes guards que U. Pas de conflit Ctrl+L.
			// Pas de fetch a l'ouverture : les pending rolls arrivent via
			// push, le bouton Simulate sert pour la demo V1.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.lootroll", "F7"), engine::platform::Key::F_7)))
			{
				m_lootRollVisible = !m_lootRollVisible;
				LOG_INFO(Core, "[Engine] F7 toggle loot roll (visible={})", m_lootRollVisible);
			}
			// Chantier 1 — Touche F1 : ouvre/ferme la fenêtre Personnage unifiée à
			// onglets (perso 3D + inventaire + caractéristiques + argent, plus les
			// onglets Compétences/Techniques/Arbre). Remplace l'ancien toggle de la
			// fiche autonome ; F2/F3/F4/I sont désormais libres. Clé de config
			// conservée (controls.keybind.charactersheet, défaut F1). Mêmes guards.
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(KeyFromName(m_cfg.GetString("controls.keybind.charactersheet", "F1"), engine::platform::Key::F_1)))
			{
				if (m_characterWindowImGui)
				{
					m_characterWindowImGui->ToggleVisible();
					if (m_characterWindowImGui->IsVisible())
					{
						// Peupler l'onglet Compétences (l'ancien toggle F2 le faisait ;
						// le Skill Book se remplit via RequestList côté serveur).
						m_skillBookUi.RequestList();
						// SP2 anniversaires (2026-07-18) — peupler l'onglet Exploits
						// (opcode 206 fire-and-forget, réponse 207 au push handler).
						m_exploitsUi.RequestExploitList();
						// Task 4 — pose l'avatar du joueur dans le viewport 3D À
						// L'OUVERTURE seulement : SetMesh réinitialise l'animation, il ne
						// doit PAS être rappelé chaque frame (sinon l'anim reste figée à
						// t=0). Le Tick/RenderOffscreen se font ensuite tant que ouvert.
						m_racePreviewViewport.SetGender(m_avatarGender);
						m_racePreviewViewport.SetSkinTone(m_avatarSkinTone);
						m_racePreviewViewport.SetMesh(m_currentSkinnedMesh);
						// Vue de face + rotation manuelle (pas d'auto-orbit) : retour
						// joueur 2026-07-09. Le drag dans la fenêtre pilote ensuite l'angle.
						m_racePreviewViewport.SetAutoOrbit(false);
						m_racePreviewViewport.SetOrbitYaw(0.0f);
						m_characterWindowImGui->ResetPreviewOrientation();
					}
				}
				LOG_INFO(Core, "[Engine] F1 toggle fenetre Personnage");
			}
		}

		// M100.2 — Dispatch des raccourcis éditeur monde vers le shell. Ctrl+Z
		// / Ctrl+Shift+Z / Ctrl+Y branchent la pile undo/redo ; F1..F12
		// (déjà gérés en M100.1) restent supportés. On ne dispatche que si
		// le shell est initialisé (CLI --editor-world ou editor.world.enabled).
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			const bool ctrl  = m_input.IsDown(engine::platform::Key::Control);
			const bool shift = m_input.IsDown(engine::platform::Key::Shift);
			// En « Mode édition bâtiment », Ctrl+Z/Ctrl+Y (et Ctrl+Shift+Z)
			// pilotent l'undo/redo du BROUILLON de bâtiment plutôt que la pile
			// undo globale (terrain/scène) — évite tout conflit : l'utilisateur
			// active cette case quand il travaille la composition.
			engine::editor::world::panels::BuildingEditorPanel* bp =
				m_worldEditorShell->GetBuildingEditorPanel();
			const bool buildMode = bp && bp->EditModeActive();
			if (m_input.WasPressed(engine::platform::Key::Z))
			{
				if (ctrl && buildMode) { if (shift) bp->Redo(); else bp->Undo(); }
				else m_worldEditorShell->HandleShortcut('Z', ctrl, shift);
			}
			if (m_input.WasPressed(engine::platform::Key::Y))
			{
				if (ctrl && buildMode) bp->Redo();
				else m_worldEditorShell->HandleShortcut('Y', ctrl, shift);
			}
			// Lot 5 (2026-07-18) — Ctrl+D (dupliquer la sélection) et Suppr
			// (supprimer la sélection). Ignorés quand ImGui édite du texte
			// (champ de saisie actif) : Suppr/Ctrl+D y ont leur sens d'édition
			// de texte et ne doivent pas toucher la scène. ImGui n'existe que
			// côté Windows (comme partout dans Engine.cpp) — sur les autres
			// plateformes l'éditeur n'a pas d'UI, pas de saisie à protéger.
#if defined(_WIN32)
			const bool imguiTextInput =
				(ImGui::GetCurrentContext() != nullptr) && ImGui::GetIO().WantTextInput;
#else
			const bool imguiTextInput = false;
#endif
			if (!imguiTextInput)
			{
				if (ctrl && m_input.WasPressed(engine::platform::Key::D))
				{
					// shift transmis tel quel : Ctrl+Shift+D reste l'outil
					// Dungeon Portal (géré par le shell).
					m_worldEditorShell->HandleShortcut('D', true, shift);
				}
				if (!ctrl && !shift && m_input.WasPressed(engine::platform::Key::Delete))
				{
					m_worldEditorShell->HandleShortcut(0x2E, false, false);
				}
				// Roadmap-6 (2026-07-19) — E / T / C : mode du gizmo de scène
				// (dÉplacer / Tourner / éChelle). Sans modifiers, hors saisie
				// de texte (garde imguiTextInput ci-dessus).
				if (!ctrl && !shift)
				{
					if (m_input.WasPressed(engine::platform::Key::E))
						m_worldEditorShell->HandleShortcut('E', false, false);
					if (m_input.WasPressed(engine::platform::Key::T))
						m_worldEditorShell->HandleShortcut('T', false, false);
					if (m_input.WasPressed(engine::platform::Key::C))
						m_worldEditorShell->HandleShortcut('C', false, false);
				}
			}
		}

		m_shaderHotReload.Poll(m_cfg);
		m_shaderHotReload.ApplyPending(m_shaderCache);

		if (m_swapchainResizeRequested)
		{
    		LOG_INFO(Platform, "[Resize] Swapchain recreate requested");

			if (m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid() && m_width > 0 && m_height > 0)
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (m_pipeline)
					m_pipeline->InvalidateFramebufferCaches(m_vkDeviceContext.GetDevice());
				if (m_terrain.IsValid())
					m_terrain.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
				InvalidateCellTerrainFramebuffers();
				// M100.14 — Le cache framebuffer de la passe water est indexé par
				// VkImageView source : un resize détruit toutes les vues FG → cache
				// stale. Invalidation au même endroit que terrain/pipeline.
				m_waterPass.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());
				// Audit 2026-06-10 (Lot B2) — UnderwaterPass cache désormais son
				// framebuffer (pattern WaterPass) ; pas membre de DeferredPipeline,
				// donc invalidation ici comme m_waterPass.
				m_underwaterPass.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());

				m_frameGraph.destroy(m_vkDeviceContext.GetDevice(), m_vmaAllocator);
				// All frame-graph images are recreated after a resize/out-of-date event, so the
				// TAA history must be rebuilt from scratch on the next frame.
				m_taaHistoryInvalid = true;
				m_taaHistoryEverFilled = false;

				bool ok = m_vkSwapchain.Recreate(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height));
				if (ok)
				{
					// Hot-fix : on ne clear le flag qu'apres succes complet. Si Recreate echoue
					// (resize trop rapide, surface lost momentanee, etc.) le flag reste true et
					// la frame suivante retente -> evite l'ecran noir permanent en cas d'echec
					// transitoire.
					m_swapchainResizeRequested = false;
					m_suboptimalStreak = 0;
					m_suboptimalWidth = m_width;
					m_suboptimalHeight = m_height;
					LOG_INFO(Platform, "[Resize] Swapchain recreated OK ({}x{})", m_width, m_height);
					if (m_worldEditorImGui)
					{
						m_worldEditorImGui->OnSwapchainRecreate(m_vkSwapchain.GetImageCount());
					}
				}
				else
				{
					LOG_WARN(Platform, "[Resize] Swapchain recreate FAILED ({}x{}) - retry next frame", m_width, m_height);
					// flag deliberement laisse a true : retry au prochain BeginFrame.
				}
			}
			else
			{
				// Cas typique : fenetre minimisee (m_width/m_height a 0). On clear le flag, il
				// sera reposte automatiquement quand WM_SIZE refire au restore (cf. Window.cpp).
				m_swapchainResizeRequested = false;
				LOG_WARN(Platform, "[Resize] Swapchain recreate skipped - device/swapchain not ready or invalid size ({}x{})", m_width, m_height);
			}
		}

		m_time.BeginFrame();
		if (m_profiler.IsInitialized())
		{
			m_profiler.BeginFrame(m_currentFrame);
		}
		m_frameArena.BeginFrame(m_time.FrameIndex());
		m_chunkStats.ResetPerFrame();
		// M100 — Task 12 : maintenance entre frames du runtime terrain chunk
		// (eviction LRU des chunks Far + reset descriptor pool). Doit être
		// appelée AVANT l'enregistrement de la frame courante.
		if (m_terrainChunkRenderer)
			m_terrainChunkRenderer->Tick(m_vkDeviceContext.GetDevice());
		PumpGameplayPackets();

		// CMANGOS.30 (Phase 5.30 step 3+4) — Tick le presenter cinematique pour
		// faire avancer l'interpolation camera + la detection de fin de
		// sequence. No-op si aucune cinematique active. Le timestamp est en
		// ms epoch (meme reference utilisee par le presenter pour startTimeMs).
		if (m_cinematicUi.IsInitialized() && m_cinematicUi.GetState().isPlaying)
		{
			using namespace std::chrono;
			const uint64_t nowMs = static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
			m_cinematicUi.Tick(nowMs);
		}
	}

	void Engine::Update()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] Update enter frame={}", m_currentFrame);
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		const auto& readState   = m_renderStates[readIdx];
		auto& out               = m_renderStates[writeIdx];

#if defined(_WIN32)
		if (!m_worldEditorImGui && m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid()
			&& m_window.GetNativeHandle() != nullptr && m_vkDeviceContext.SupportsDynamicRendering())
		{
			InitImGuiOverlayIfNeeded();
		}
		if (m_worldEditorExe && m_worldEditorSession)
		{
			UpdateWorldEditorSession();
		}
		// M100.46+ — Pont TerrainDocument → HeightmapData GPU. Consomme le
		// flag set par le callback OnChunkChanged et déclenche la copie
		// chunks → heightmap CPU + FlushHeightmap GPU. Coût ~10-30 ms par
		// tick concerné (acceptable pour des ops one-shot ; pas atteint par
		// les brushstrokes interactifs qui ont leur propre path direct).
		if (m_worldEditorTerrainNeedsSync.exchange(false, std::memory_order_acq_rel))
		{
			SyncWorldEditorHeightmapFromDocument();
		}
		if (m_worldEditorExe && m_worldEditorSession && m_texturePreviewCache)
		{
			for (const std::string& rel : m_worldEditorSession->RecentlyImportedTextures())
			{
				m_texturePreviewCache->Invalidate(rel);
			}
			m_worldEditorSession->ClearRecentlyImportedTextures();
		}
		ProcessSplatRefsDirty();
		if (m_texturePreviewCache)
		{
			m_texturePreviewCache->Tick(static_cast<uint64_t>(m_currentFrame),
			                             kEditorTexCacheFramesInFlight);
		}
#endif

	const double dt               = (m_fixedDt > 0.0) ? m_fixedDt : m_time.DeltaSeconds();

	// Temps cumulé pour l'advection des nuages (vent). Continu, non cyclique.
	m_cloudTimeSeconds += static_cast<float>(dt);

	// M38.1 — Advance day/night cycle and propagate results into m_zoneAtmosphere
	// so that the existing lighting path picks them up without further changes.
	{
		m_dayNight.Advance(static_cast<float>(dt));
		const engine::render::DayNightCycle::State& dnState = m_dayNight.GetState();
		m_zoneAtmosphere.sunDirection[0] = dnState.lightDir[0];
		m_zoneAtmosphere.sunDirection[1] = dnState.lightDir[1];
		m_zoneAtmosphere.sunDirection[2] = dnState.lightDir[2];
		m_zoneAtmosphere.sunColor[0]     = dnState.lightColor[0];
		m_zoneAtmosphere.sunColor[1]     = dnState.lightColor[1];
		m_zoneAtmosphere.sunColor[2]     = dnState.lightColor[2];
		m_zoneAtmosphere.ambientColor[0] = dnState.ambientColor[0];
		m_zoneAtmosphere.ambientColor[1] = dnState.ambientColor[1];
		m_zoneAtmosphere.ambientColor[2] = dnState.ambientColor[2];
	}

	// WorldClock sync (Task 6.2) — controle de derive. Une fois l'horloge
	// serveur branchee (mode driven), on renvoie periodiquement un
	// WorldClockStateRequest (203) au master pour rafraichir m_clockOffsetMs
	// (la reponse 204 rappelle SetServerClock). La correction est sub-seconde
	// donc invisible — pas besoin de lerp. Fire-and-forget : no-op si la
	// connexion master n'est pas vivante.
	if (m_dayNight.IsServerDriven() && m_worldClockDriftCheckSec > 0.0f)
	{
		m_worldClockResyncTimer += static_cast<float>(dt);
		if (m_worldClockResyncTimer >= m_worldClockDriftCheckSec)
		{
			m_worldClockResyncTimer = 0.0f;
			std::vector<uint8_t> wcReq;
			engine::network::worldclock::BuildWorldClockStateRequestPayload(wcReq);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeWorldClockStateRequest, wcReq);
			LOG_DEBUG(Render, "[Engine] WorldClock re-sync request envoye (drift check)");
		}
	}

	// --- Suivi jour/nuit de l'IBL : re-capture du ciel quand le soleil a
	// assez bouge, throttle pour eviter un stall trop frequent (vkQueueWaitIdle).
	// Garde sur IsValid() (IBL active). Point sur : fin du bloc day/night,
	// hors enregistrement de command buffer de frame.
	if (m_pipeline && m_pipeline->GetIrradiancePass().IsValid())
	{
		constexpr float kIblMinRegenInterval = 2.5f;   // s
		constexpr float kIblSunCosThreshold  = 0.9962f; // cos(~5deg)
		m_iblRegenTimer += static_cast<float>(dt);
		const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
		const float dotSun = dn.lightDir[0]*m_iblLastSunDir[0]
		                   + dn.lightDir[1]*m_iblLastSunDir[1]
		                   + dn.lightDir[2]*m_iblLastSunDir[2];
		if (m_iblRegenTimer >= kIblMinRegenInterval && dotSun < kIblSunCosThreshold)
		{
			engine::render::SkyCaptureParams iblSky{};
			for (int i = 0; i < 3; ++i)
			{
				iblSky.lightDir[i]     =  dn.sunDir[i];
				iblSky.zenithColor[i]  =  dn.skyZenith[i];
				iblSky.horizonColor[i] =  dn.skyHorizon[i];
				iblSky.moonDir[i]      =  dn.moonDir[i];
			}
			iblSky.moonIntensity    = dn.isDaytime ? 0.0f : 1.0f;
			iblSky.moonPhase        = static_cast<float>(dn.moonPhase);
			iblSky.moonIllumination = dn.moonIllumination;
			// Lot 2 (2026-07-18) — même modèle de ciel que sky.frag.
			iblSky.skyModel = m_cfg.GetBool("client.sky.analytic", true) ? 1.0f : 0.0f;
			m_pipeline->RegenerateIbl(m_vkDeviceContext.GetDevice(),
			                          m_vkDeviceContext.GetGraphicsQueue(), iblSky);
			m_iblRegenTimer = 0.0f;
			m_iblLastSunDir = { dn.lightDir[0], dn.lightDir[1], dn.lightDir[2] };
		}
	}

	// M38.2 — Advance weather system and propagate audio volume.
	if (m_weatherSystem.IsInitialized())
	{
		const engine::render::Camera& cam = readState.camera;
		m_weatherSystem.Tick(static_cast<float>(dt),
		                     cam.position.x, cam.position.y, cam.position.z);

		// Drive the "Weather" audio bus volume from rain intensity (spec step 6).
		// Graceful no-op if the bus is not defined in the zone audio JSON.
		m_audioEngine.SetBusVolume("Weather", m_weatherSystem.GetAudioVolume());
	}

	// M38.3 — Advance dynamic point-light system (streetlamps, torches, windows).
	// Reads the current time-of-day from the day/night cycle to auto-trigger lights.
	if (m_dynamicLights.IsInitialized())
	{
		const float timeOfDay = m_dayNight.GetState().timeOfDay;
		m_dynamicLights.Tick(timeOfDay, static_cast<float>(dt));
	}

	// v1.0.118 — zones d'ambiance locales (fond du gouffre) : assombrissement au point du
	// joueur, lissé (≈ 1,5 s) pour qu'une descente ou une remontée ne « claque » pas.
	{
		float target = 0.0f;
		if (!m_ambianceZones.empty())
		{
			const engine::math::Vec3 p = m_characterController.GetPosition();
			target = AmbianceDarknessAt(p.x, p.y, p.z);
		}
		const float k = 1.0f - std::exp(-static_cast<float>(dt) / 1.5f);
		m_ambianceDarkness += (target - m_ambianceDarkness) * k;
		if (std::fabs(m_ambianceDarkness - target) < 1e-3f)
			m_ambianceDarkness = target;
	}

	const float  mouseSensitivity = static_cast<float>(m_cfg.GetDouble("camera.mouse_sensitivity", 0.002));
		const bool invertY = m_cfg.GetBool("controls.invert_y", false);
		const std::string moveLayoutStr = m_cfg.GetString("controls.movement_layout", "wasd");
		const engine::render::MovementLayout movementLayout =
			(moveLayoutStr == "zqsd") ? engine::render::MovementLayout::ZQSD : engine::render::MovementLayout::WASD;

		out.camera = readState.camera;
		out.profilerDebugText = m_profilerHud.IsInitialized() ? m_profilerHud.GetState().debugText : std::string{};
		out.chatDebugText = m_chatUi.IsInitialized() ? m_chatUi.BuildPanelText() : std::string{};
		out.authHudText.clear();
		out.gameplayHudDebugText.clear();
		if (m_gameplayNetInitialized)
		{
			out.gameplayHudDebugText += m_shopUi.GetState().debugText;
			out.gameplayHudDebugText += '\n';
			out.gameplayHudDebugText += m_auctionUi.GetState().debugText;
			out.gameplayHudDebugText += '\n';
			out.gameplayHudDebugText += m_invUi.GetState().debugText;
			if (m_pendingSellActive)
			{
				out.gameplayHudDebugText += "\n[PENDING SELL] vendor=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellVendorId);
				out.gameplayHudDebugText += " item=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellItemId);
				out.gameplayHudDebugText += " qty=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellQty);
				out.gameplayHudDebugText += " unit_gold=";
				out.gameplayHudDebugText += std::to_string(m_pendingSellUnitGold);
				out.gameplayHudDebugText += "  -> Y confirm / N cancel\n";
			}
		}

		// ETAPE5.1 — perte du shard (kOpcodeShardLostNotification) : retour à l'écran de
		// connexion avec un message, avant le calcul de la porte d'authentification.
		if (m_shardLostPending)
		{
			m_shardLostPending = false;
			if (m_gameplayNetInitialized || m_currentCharacterId != 0)
			{
				RequestLogoutToLoginScreen();
				m_authUi.SetInfoBanner(m_authUi.UiTranslate(
					m_shardLostByShutdown ? "auth.info.server_shutdown" : "auth.info.shard_lost"));
			}
		}

#if !defined(_WIN32)
		// Build Linux (fumée) : LCDLLN_DEBUG_AUTOWALK lève l'écran d'authentification
		// (absent sous Linux) pour que le déplacement de test s'exécute.
		const bool authGateActive = m_authUi.IsInitialized() && !m_authUi.IsFlowComplete()
			&& std::getenv("LCDLLN_DEBUG_AUTOWALK") == nullptr;
#else
		const bool authGateActive = m_authUi.IsInitialized() && !m_authUi.IsFlowComplete();
#endif
		if (authGateActive)
		{
			LOG_DEBUG(Render, "[DIAG] authUi.Update begin frame={}", m_currentFrame);
			m_authUi.Update(m_input, static_cast<float>(dt), m_window, m_cfg);
			LOG_DEBUG(Render, "[DIAG] authUi.Update done frame={}", m_currentFrame);
		}

		// Refactor B2 (ST1) — Le consume/application des réglages d'options doit tourner
		// CHAQUE FRAME (auth comme in-game) pour pouvoir réutiliser l'écran d'options en
		// jeu. Les ConsumePending*Settings() sont idempotents (commande vide tant qu'aucun
		// apply n'est demandé), donc l'appel inconditionnel est sûr et ne ré-applique rien
		// hors frame d'apply. Placé APRÈS m_authUi.Update pour consommer les commandes
		// stagées par l'UI au cours de la frame courante (pas un cran de retard). La garde
		// réseau (Init/ShutdownGameplayNet) reste conditionnée à authGateActive
		// (cf. ApplyConsumedSettingsCommands).
		ApplyConsumedSettingsCommands(authGateActive);

		if (authGateActive)
		{
			if (m_chatUi.IsInitialized())
			{
				// PAS d'update du chat pendant les ecrans d'auth : sinon le Enter
				// que l'utilisateur tape pour valider le login active aussi le chat
				// focus (cf. ChatUiPresenter::Update qui toggle sur Slash/Enter).
				// Resultat : in-game, m_chatFocus reste true -> orbital camera Update
				// est skip -> camera fige a la position spawn = position avatar
				// (utilisateur voyait l'interieur du mesh humanoide). On garde
				// uniquement la mise a jour viewport pour que la geometrie chat
				// soit prete au moment d'EnterWorld.
				(void)m_chatUi;
			}
			out.authHudText = m_authUi.BuildPanelText();
			out.chatDebugText.clear();
			const bool authUiDynamicRenderingEnabled = m_cfg.GetBool("render.auth_ui_dynamic_rendering.enabled", true);
			const bool authImguiClearsHud = m_authImGui && m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
			if (m_authUi.GetVisualState().active
				&& m_vkDeviceContext.SupportsDynamicRendering()
				&& (authUiDynamicRenderingEnabled || authImguiClearsHud))
				m_window.SetOverlayText({});
			else
				m_window.SetOverlayText(out.authHudText);
		}
		else
		{
			UpdateInWorldSession(out);
		}

#if defined(_WIN32)
		// Dear ImGui : lire io.WantCapture* après NewFrame() pour la frame courante (sinon la caméra reste figée).
		const bool authImguiOverlayNewFrame = m_authImGui && m_authUi.GetVisualState().active
			&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
		// Phase 3.11.1 — NewFrame également quand le panneau chat doit s'afficher (post-auth, pas en éditeur).
		// Responsabilite : chat HUD VISIBLE des que le master a accepte l'AUTH
		// (Global + Friends) ; la liste s'enrichit du canal Zone une fois le shard
		// rejoint. Cf. retour utilisateur : 'une fois authentifie, il doit y avoir
		// le chat global + amis ; une fois le royaume choisi, + zone'.
		const bool postAuthMaster = m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
			&& !m_worldEditorExe;
		// Chat HUD : on garde l'appel a ImGui::NewFrame en post-master-auth (meme
		// pre-EnterWorld) car la branche de rendu in-game ligne 3739+ fait
		// m_chatImGui->Render + ImGui::Render() en supposant qu'un NewFrame a deja ete
		// appele plus haut. Sans cet appel, ImGui::Render() utilise des draw data stale
		// -> swapchain presente le meme framebuffer en boucle, ecran fige.
		// Le RENDU du panneau chat pre-EnterWorld reste desactive (cf. branche
		// auth-rendering qui n'appelle plus m_chatImGui->Render).
		const bool chatImguiOverlayNewFrame = m_chatImGui && m_chatUi.IsInitialized()
			&& postAuthMaster
			&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible);
		// M43.4 — NewFrame également quand --editor (sans world-editor exe) actif.
		const bool editorHubOverlayNewFrame = m_editorHubImGui && m_editorEnabled && !m_worldEditorExe;
		if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
			&& (m_worldEditorExe || authImguiOverlayNewFrame || chatImguiOverlayNewFrame || editorHubOverlayNewFrame))
		{
			float imguiDw = static_cast<float>(std::max(1, m_width));
			float imguiDh = static_cast<float>(std::max(1, m_height));
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extImg = m_vkSwapchain.GetExtent();
				if (extImg.width > 0 && extImg.height > 0)
				{
					imguiDw = static_cast<float>(extImg.width);
					imguiDh = static_cast<float>(extImg.height);
				}
			}
			m_worldEditorImGui->NewFrame(static_cast<float>(dt), imguiDw, imguiDh);
		}

		// M100.34 incrément 3 — Resize de la cible offscreen viewport sur la
		// taille réelle du ScenePanel. Sans ça, la target reste fixée à la
		// taille de la swapchain au boot et l'image est étirée par
		// `ImGui::Image` dans le panneau (aspect ratio cassé). La mesure
		// `ScenePanel::GetViewportWidth/Height` vient du frame précédent
		// (set dans `ScenePanel::Render` post-`ImGui::GetContentRegionAvail`).
		// Si elle diffère de la target actuelle, on attend que le device
		// soit idle (sinon destroy d'une image en cours d'utilisation) puis
		// on recrée à la nouvelle taille. Pattern rare (resize panel = action
		// utilisateur sporadique), donc waitIdle acceptable côté perf.
		if (m_worldEditorExe && m_worldEditorShell
			&& m_worldEditorShell->IsInitialized()
			&& m_editorViewportTarget.IsValid()
			&& !m_worldEditorShell->Panels().empty()
			&& m_worldEditorShell->Panels()[0])
		{
			ResizeWorldEditorViewportTarget();
		}

		// M100.1 — Rendu de la coquille du nouvel éditeur monde. Doit être
		// appelée après ImGui::NewFrame (fait par WorldEditorImGui::NewFrame
		// ci-dessus, qui partage le même contexte ImGui) et avant
		// ImGui::Render. RenderFrame est no-op si Init() n'a pas été appelé
		// avec succès. Pas thread : main thread uniquement.
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized()
			&& m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			RenderWorldEditorShell(out);
		}
#endif

		if (!m_editorEnabled)
		{
			UpdateGameplay(out, dt, mouseSensitivity, invertY, movementLayout, authGateActive);
		}
		else if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			UpdateWorldEditorCamera(out, dt, mouseSensitivity, invertY, movementLayout);
		}

		if (m_gameplayNetInitialized)
		{
			UpdateGameplayNet(static_cast<float>(dt));
		}
		m_world.Update(out.camera.position);

		// On aligne l'aspect sur la taille réelle de la swapchain, pas sur le size "client" du window.
		// Sinon on obtient des barres noires / RT non alignés après resize/DPI.
		if (m_vkSwapchain.IsValid())
		{
			const VkExtent2D ext = m_vkSwapchain.GetExtent();
			if (ext.width > 0 && ext.height > 0)
				out.camera.aspect = static_cast<float>(ext.width) / static_cast<float>(ext.height);
		}
		else if (m_width > 0 && m_height > 0)
		{
			out.camera.aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
		}

		engine::math::Vec3 listenerVelocity{};
		if (dt > 0.0)
		{
			const float invDt = static_cast<float>(1.0 / dt);
			listenerVelocity = engine::math::Vec3(
				(out.camera.position.x - readState.camera.position.x) * invDt,
				(out.camera.position.y - readState.camera.position.y) * invDt,
				(out.camera.position.z - readState.camera.position.z) * invDt);
		}
		m_audioEngine.SetListener(out.camera.position, listenerVelocity);
		UpdateAudioListener(out.camera); // SND.2 — orientation de l'oreille
		UpdateAmbienceAudio(static_cast<float>(dt), m_authUi.IsInWorldShard() && !m_editorEnabled); // SND.4
		UpdateCombatAudio(m_authUi.IsInWorldShard() && !m_editorEnabled); // SND.5
		UpdateInteractionAudio(m_authUi.IsInWorldShard() && !m_editorEnabled); // SND.6
		m_audioEngine.Tick(static_cast<float>(dt));
		m_decalSystem.Tick(static_cast<float>(dt));
		// Réticule de ciblage : suit la cible (position/yaw lissés) et pilote le
		// decal persistant (fade in/out). APRÈS UpdateGameplayNet (m_remoteSmoothed
		// et UIModel à jour), AVANT l'enregistrement de la frame (BuildVisibleList).
		if (m_gameplayNetInitialized)
		{
			m_targetReticle.Update(m_uiModelBinding.GetModel(), out.camera, static_cast<float>(dt));
		}

		out.viewMatrix = out.camera.ComputeViewMatrix();
		{
			// La direction gaze est stockee en row 2 du view matrix corrige :
			// (m[2], m[6], m[10]) = (forward.x, forward.y, forward.z). Cf.
			// commentaire dans Camera::ComputeViewMatrix.
			const engine::math::Vec3 forward(out.viewMatrix.m[2], out.viewMatrix.m[6], out.viewMatrix.m[10]);
			m_streamingScheduler.PushRequests(m_world.GetPendingChunkRequests(), out.camera.position, forward);
			m_streamingScheduler.DropStaleFromAllQueues();
		}

		if (m_width > 0 && m_height > 0 && std::abs(out.camera.fovYDeg - readState.camera.fovYDeg) > 0.0001f)
			m_taaHistoryInvalid = true;

		// Monde en cellules : plan lointain = distance de vue du préréglage
		// « Qualité graphique » (le terrain, les bâtiments et les cascades d'ombre
		// s'arrêtent là ; le brouillard termine bien avant).
		if (m_cellWorld)
			out.camera.farZ = m_viewDistance.cameraFarM;
		out.projMatrix = out.camera.ComputeProjectionMatrix();

		// ViewProj non-jitterée (avant l'ajout du jitter TAA) : sert au raymarch des
		// nuages pour qu'ils soient stables temporellement (pas de tremblement
		// sous-pixel -> pas de scintillement via le TAA).
		out.viewProjMatrixUnjittered = out.projMatrix * out.viewMatrix;

		const uint32_t taaSampleIndex = m_currentFrame % engine::render::kTaaHaltonN;
		float jitterX = 0.0f, jitterY = 0.0f;
		if (!m_taaHistoryInvalid && m_width > 0 && m_height > 0)
			engine::render::GetJitterNdc(taaSampleIndex, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), jitterX, jitterY);
		out.projMatrix.m[8] += jitterX;
		out.projMatrix.m[9] += jitterY;

		out.viewProjMatrix = out.projMatrix * out.viewMatrix;
		out.jitterCurrNdc[0] = jitterX;
		out.jitterCurrNdc[1] = jitterY;
		out.prevViewProjMatrix = m_taaHistoryInvalid ? out.viewProjMatrix : readState.viewProjMatrix;
		if (m_taaHistoryInvalid) m_taaHistoryInvalid = false;

		if (m_editorMode)
		{
			m_editorMode->Update(m_input, m_window, out.camera, m_geometryMeshHandle.Get(), m_width, m_height, dt);
			// Demande utilisateur : afficher un humanoide de reference au centre du
			// terrain pour juger des reliefs. On override la matrice pour positionner
			// l'avatar (1.8m) au centre XZ du terrain courant a la hauteur du sol
			// echantillonnee. Si pas de terrain, fallback (0, 0, 0).
			out.objectVisible = true;
			float refX = 0.0f, refZ = 0.0f, refY = 0.0f;
			if (m_terrain.IsValid())
			{
				const float ox = m_terrain.GetTerrainOriginX();
				const float oz = m_terrain.GetTerrainOriginZ();
				const float ws = m_terrain.GetTerrainWorldSize();
				refX = ox + ws * 0.5f;
				refZ = oz + ws * 0.5f;
				refY = m_terrain.SampleHeightAtWorldXZ(refX, refZ);
			}
			// Matrice column-major : identite (rotation 0) + translation (refX, refY, refZ).
			// Avatar mesh y va de 0 (pieds) a 1.8 (tete) -> pieds au sol echantillonne.
			out.objectModelMatrix[0]  = 1.0f; out.objectModelMatrix[1]  = 0.0f; out.objectModelMatrix[2]  = 0.0f; out.objectModelMatrix[3]  = 0.0f;
			out.objectModelMatrix[4]  = 0.0f; out.objectModelMatrix[5]  = 1.0f; out.objectModelMatrix[6]  = 0.0f; out.objectModelMatrix[7]  = 0.0f;
			out.objectModelMatrix[8]  = 0.0f; out.objectModelMatrix[9]  = 0.0f; out.objectModelMatrix[10] = 1.0f; out.objectModelMatrix[11] = 0.0f;
			out.objectModelMatrix[12] = refX; out.objectModelMatrix[13] = refY; out.objectModelMatrix[14] = refZ; out.objectModelMatrix[15] = 1.0f;
		}
		else
		{
			out.objectVisible = true;
			// Etape 2 + 4 vue 3eme personne : position + orientation de l'avatar.
			//
			// Position : translation a la cible orbitale (= position joueur).
			// Le mesh 'avatar_placeholder.mesh' (chantier 3) est un humanoide
			// composite : torse + tete + 2 bras + 2 jambes (6 boites soudees,
			// 144 verts, 216 indices). Pieds a mesh-Y=0, sommet du crane a
			// mesh-Y=1.8 ; translation(target) sans offset.
			//
			// Orientation : rotation Y selon le yaw camera (etape 4). Standard MMO :
			// le perso fait face dans la meme direction horizontale que la camera,
			// ce qui revient a montrer son dos a la camera (3eme personne classique).
			// Le cube placeholder etant symetrique, ca n'a pas d'effet visuel pour
			// le moment ; le cablage est en place pour les futurs meshes textures.
			//
			// Matrice row-major M = T(target) * R_y(yaw) :
			//   | cos(y)   0  sin(y)  tx |
			//   | 0        1  0       ty |
			//   | -sin(y)  0  cos(y)  tz |
			//   | 0        0  0       1  |
			// On rend l'avatar systematiquement (meme si character_id == 0u, ex.
			// scenarios de test sans EnterWorld complet) pour que la 3eme personne
			// soit visible des l'entree dans la scene 3D.
			{
				const engine::math::Vec3& target = m_orbitalCameraController.GetTargetPosition();
				const float yaw = out.camera.yaw;
				const float c = std::cos(yaw);
				const float s = std::sin(yaw);
				// B.1 / Task 8 : le bob synthetique (placeholder anim) est supprime
				// avec la retrogradation d'OrbitalCameraController en camera pure.
				// L'animation visuelle (walk-bob, vraies anims squelettiques) sera
				// rebranchee plus tard depuis la state machine de locomotion.
				//
				// Plus de kTargetEyeHeight non plus : la cible orbitale n'est plus
				// systematiquement "yeux a 1.7 m du sol". CharacterController
				// (Task 9) decidera ou poser la cible (yeux, hanche, ou pieds) et
				// poussera la position correspondante via SetTargetPosition. Pour
				// l'instant target reste a (0,0,0) -> avatar pose les pieds en 0.
				const float feetY = target.y;
				// IMPORTANT : layout COLUMN-MAJOR. Le shader gbuffer_geometry.vert
				// reconstruit la mat4 via 4 vec4 (instanceRow0..3), chacun lu en sequence
				// dans le buffer d'instance ; GLSL mat4(c0,c1,c2,c3) place chaque vec4
				// comme COLONNE. Donc nos indices [0..3] = colonne 0, [4..7] = colonne 1,
				// etc. La translation va dans la 4eme COLONNE (indices 12, 13, 14).
				// Avant ce fix, le code etait ecrit en row-major, ce qui mettait la
				// translation dans la composante w des positions monde -> avatar
				// invisible (worldPos.w != 1, et translation nulle dans xyz).
				//
				// Matrice mathematique (colonne-major M = T * R_y(yaw)) :
				//   | cos(y)   0   sin(y)    tx |
				//   | 0        1   0         ty |
				//   | -sin(y)  0   cos(y)    tz |
				//   | 0        0   0         1  |
				// Stockage colonne-par-colonne :
				// Avatar a echelle 1:1 (1.8 m). Le scale x3 introduit pour "garantir
				// la visibilite" placait la camera A L'INTERIEUR du corps de l'avatar
				// (5.4 m de haut, camera a 1.81 m au-dessus du sol = inside the model)
				// -> ecran fige perceptuel. La camera 3eme personne (dist=8m, height=1.5m)
				// est largement assez genereuse a echelle reelle.
				// Avatar a echelle reelle 1.8m (pas de scale). Cible image 2 utilisateur :
				// avec camera a 5m de distance (cf. Camera.h kDistanceDefault), l'avatar
				// occupe 26% de hauteur ecran -> visible et identifiable comme humanoide.
				out.objectModelMatrix[0]  = c;     out.objectModelMatrix[1]  = 0.0f; out.objectModelMatrix[2]  = -s;   out.objectModelMatrix[3]  = 0.0f; // col0 : axe X local
				out.objectModelMatrix[4]  = 0.0f;  out.objectModelMatrix[5]  = 1.0f; out.objectModelMatrix[6]  = 0.0f; out.objectModelMatrix[7]  = 0.0f; // col1 : axe Y local
				out.objectModelMatrix[8]  = s;     out.objectModelMatrix[9]  = 0.0f; out.objectModelMatrix[10] = c;    out.objectModelMatrix[11] = 0.0f; // col2 : axe Z local
				out.objectModelMatrix[12] = target.x; out.objectModelMatrix[13] = feetY; out.objectModelMatrix[14] = target.z; out.objectModelMatrix[15] = 1.0f; // col3 : translation
			}
		}

#if defined(_WIN32)
		if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			UpdateWorldEditorViewportOverlay(out);
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_authImGui
			&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false) && m_authUi.GetVisualState().active)
		{
			RenderAuthImGuiFrame();
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_chatImGui
			&& m_chatUi.IsInitialized()
			&& m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
			&& !m_worldEditorExe
			&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible))
		{
			RenderInGameImGuiFrame(out);
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_editorHubImGui
			&& m_editorEnabled && !m_worldEditorExe)
		{
			// M43.4 — Rendu du panneau Editor Hub overlay quand --editor (sans world-editor).
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
			m_editorHubImGui->Render(dw, dh);
			ImGui::Render();
		}
#endif

		out.frustum.ExtractFromMatrix(out.viewProjMatrix);
		{
			const float maxDrawDist = static_cast<float>(m_cfg.GetDouble("world.max_draw_distance_m", 0.0));
			std::span<const engine::world::ChunkRequest> pending = m_streamingScheduler.GetPrioritizedRequests();
			out.hlodDebugText = engine::world::BuildChunkDrawList(pending.data(), pending.size(), out.camera.position, out.frustum, m_hlodRuntime, maxDrawDist, m_chunkDrawDecisions);
			if ((m_currentFrame % 60) == 0 && !out.hlodDebugText.empty())
				LOG_DEBUG(World, "M09.5 {}", out.hlodDebugText);
			if ((m_currentFrame % 60) == 0 && !out.profilerDebugText.empty())
				LOG_DEBUG(Core, "M18.1 {}", out.profilerDebugText);
			if ((m_currentFrame % 60) == 0 && !out.chatDebugText.empty())
				LOG_DEBUG(Core, "M29.1 {}", out.chatDebugText);
			if ((m_currentFrame % 60) == 0 && !out.gameplayHudDebugText.empty())
				LOG_DEBUG(Core, "M35.2 {}", out.gameplayHudDebugText);
			if ((m_currentFrame % 60) == 0 && !out.authHudText.empty())
				LOG_INFO(Core, "[AuthUi] {}", out.authHudText);
		}

		{
			const engine::math::Vec3 lightDirTowardLight(0.5774f, 0.5774f, 0.5774f);
			const float lambda = 0.7f;
			const uint32_t shadowMapResolution = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
			engine::render::ComputeCascades(out.camera, lightDirTowardLight, lambda, shadowMapResolution, out.cascades);
		}

		// Snapshot des point lights actives dans le RenderState (anti data-race).
		// COPIE ici (assemblage de `out`) ; le lambda Lighting lit rs.pointLights
		// et ne touche jamais m_dynamicLights. Tick() déjà appelé plus tôt (Update).
		// v1.0.118 — l'UBO n'a que 64 places : avec les torches des cellules, on garde les
		// 64 lumières les plus proches de la caméra.
		out.pointLights = engine::render::DynamicLightSystem::SelectNearest(m_dynamicLights.GetActiveLights(),
			out.camera.position.x, out.camera.position.y, out.camera.position.z, 64);
		out.ambianceDarkness = m_ambianceDarkness;

		for (int i = 0; i < 256; ++i)
			(void)m_frameArena.alloc(64, alignof(std::max_align_t), engine::core::memory::MemTag::Temp);
		out.drawItemCount = 256;
	}

	void Engine::Render()
	{
	    // PROFILE_FUNCTION();
	    if (!m_vkDeviceContext.IsValid() || !m_vkSwapchain.IsValid() || m_frameResources[0].cmdPool == VK_NULL_HANDLE)
	    {
	        LOG_WARN(Render, "[Engine] Render early return: device/swapchain not ready frame={}", m_currentFrame);
	        return;
	    }
	    // Skip render uniquement quand la fenetre est minimisee (m_width/m_height = 0).
	    // Ne PAS skipper sur m_swapchainResizeRequested : le code de resize est traite
	    // dans BeginFrame du frame suivant, et vkAcquireNextImageKHR retourne
	    // OUT_OF_DATE_KHR / SUBOPTIMAL_KHR qui sont gerees plus bas. Skipper en plus
	    // du flag risquait de figer le rendu de l'editeur monde dont la fenetre
	    // ne plante plus mais reste noire en permanence (signale par utilisateur).
	    if (m_width <= 0 || m_height <= 0)
	    {
	        LOG_DEBUG(Render, "[Engine] Render skipped (window minimized w={} h={})", m_width, m_height);
	        return;
	    }
	    LOG_DEBUG(Render, "[Engine] Render begin frame={}", m_currentFrame);
	    const uint32_t frameIndex          = m_currentFrame % 2;
	    engine::render::FrameResources& fr = m_frameResources[frameIndex];
	    ::VkDevice     device              = m_vkDeviceContext.GetDevice();
	    VkQueue        graphicsQueue       = m_vkDeviceContext.GetGraphicsQueue();
	    VkQueue        presentQueue        = m_vkDeviceContext.GetPresentQueue();
	    VkSwapchainKHR swapchain           = m_vkSwapchain.GetSwapchain();
	    // Utiliser l'extent réel de la swapchain pour que le FrameGraph alloue/recrée
	    // ses rendertargets avec les bonnes dimensions.
	    VkExtent2D extent = m_vkSwapchain.GetExtent();
	
	    LOG_DEBUG(Render, "[DIAG] vkWaitForFences begin frame={} frameIndex={}", m_currentFrame, frameIndex);
	    vkWaitForFences(device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
	    LOG_DEBUG(Render, "[DIAG] vkWaitForFences done frame={}", m_currentFrame);
	    if (m_profiler.IsInitialized() && m_profiler.ResolveGpuFrame(device, frameIndex))
	    {
	        if (m_profilerHud.IsInitialized())
	        {
	            m_profilerHud.ApplySnapshot(m_profiler.GetLatestSnapshot());
	            m_renderStates[0].profilerDebugText = m_profilerHud.GetState().debugText;
	            m_renderStates[1].profilerDebugText = m_profilerHud.GetState().debugText;
	        }
	    }
	    m_deferredDestroyQueue.Collect(device, m_currentFrame > 0 ? m_currentFrame - 1 : 0);
	    m_stagingAllocator.BeginFrame(frameIndex);
	    (void)m_gpuUploadQueue.PlanFrameUploads();
	
	    if (m_pipeline->GetAutoExposure().IsValid())
	    {
	        const float dt    = static_cast<float>(m_time.DeltaSeconds());
	        const float key   = static_cast<float>(m_cfg.GetDouble("exposure.key", 0.18));
	        const float speed = static_cast<float>(m_cfg.GetDouble("exposure.speed", 2.0));
	        // Bornes de l'exposition adaptée. Le plafond (défaut 3.0 au lieu de 10.0)
	        // empêche l'auto-exposition de surcompenser une scène assombrie par les
	        // ombres et de cramer le ciel/horizon en blanc. Réglable via config.
	        const float minExp = static_cast<float>(m_cfg.GetDouble("exposure.min", 0.1));
	        // Plafond d'exposition piloté par l'heure : bas la nuit (night_max),
	        // normal le jour (max), interpolé linéairement via le facteur jour
	        // (élévation solaire clampée [0,1]). À dayFactor=1, effectiveMax==dayMax
	        // (jour inchangé). Comme tout passe par l'exposition, l'ambient IBL
	        // nocturne est aussi assombri. Lu chaque frame → night_max réglable à chaud.
	        const float dayMax    = static_cast<float>(m_cfg.GetDouble("exposure.max", 3.0));
	        const float nightMax  = static_cast<float>(m_cfg.GetDouble("exposure.night_max", 0.8));
	        const float dayFactor = m_dayNight.GetState().dayFactor; // 0=nuit, 1=jour
	        float effectiveMax = nightMax + (dayMax - nightMax) * dayFactor;
	        // v1.0.118 — dans une zone d'ambiance sombre (fond du gouffre), plafond de nuit :
	        // l'auto-exposition ne doit pas « rallumer » un fond que l'on veut sombre.
	        effectiveMax += (std::min(effectiveMax, nightMax) - effectiveMax) * std::clamp(m_ambianceDarkness, 0.0f, 1.0f);
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, minExp, effectiveMax, frameIndex);
	    }
	
	    auto handleSuboptimal = [this](const char* phase)
	    {
	        int clientW = 0;
	        int clientH = 0;
	        m_window.GetClientSize(clientW, clientH);
	        clientW = std::max(1, clientW);
	        clientH = std::max(1, clientH);
	        if (clientW != m_width || clientH != m_height)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL and window client size changed {}x{} -> {}x{}",
	                phase, m_width, m_height, clientW, clientH);
	            m_width = clientW;
	            m_height = clientH;
	        }
	        const bool needsResize = m_vkSwapchain.NeedsRecreateForSurfaceExtent(
	            static_cast<uint32_t>(clientW),
	            static_cast<uint32_t>(clientH));
	        const bool degenerateSurfaceExtent = m_vkSwapchain.HasDegenerateSurfaceExtent(
	            static_cast<uint32_t>(clientW),
	            static_cast<uint32_t>(clientH));
	        if (m_suboptimalWidth == clientW && m_suboptimalHeight == clientH)
	        {
	            ++m_suboptimalStreak;
	        }
	        else
	        {
	            m_suboptimalWidth = clientW;
	            m_suboptimalHeight = clientH;
	            m_suboptimalStreak = 1;
	        }
	        if (needsResize)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL with extent mismatch -> recreate requested (client={}x{}, streak={})",
	                phase, clientW, clientH, m_suboptimalStreak);
	            m_swapchainResizeRequested = true;
	            return;
	        }
	        if (degenerateSurfaceExtent)
	        {
	            LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL with degenerate surface caps -> keep current swapchain (client={}x{}, streak={})",
	                phase, clientW, clientH, m_suboptimalStreak);
	            return;
	        }
	        LOG_INFO(Render, "[SWAPCHAIN] {} returned SUBOPTIMAL but extent is unchanged -> keep current swapchain (client={}x{}, streak={})",
	            phase, clientW, clientH, m_suboptimalStreak);
	    };

	    uint32_t imageIndex = 0;
	    LOG_DEBUG(Render, "[Engine] Render vkAcquireNextImageKHR begin frame={}", m_currentFrame);
	    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, fr.imageAvailable, VK_NULL_HANDLE, &imageIndex);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR) { m_swapchainResizeRequested = true; LOG_WARN(Render, "[Engine] Render vkAcquireNextImageKHR OUT_OF_DATE frame={}", m_currentFrame); return; }
	    if (result == VK_SUBOPTIMAL_KHR)
	    {
	        handleSuboptimal("Acquire");
	    }
	    else if (result != VK_SUCCESS)
	    {
	        LOG_WARN(Render, "[Engine] Render vkAcquireNextImageKHR failed result={} frame={}", static_cast<int>(result), m_currentFrame);
	        return;
	    }
	    else
	    {
	        m_suboptimalStreak = 0;
	    }
	    LOG_DEBUG(Render, "[Engine] Render vkAcquireNextImageKHR OK imageIndex={} frame={}", imageIndex, m_currentFrame);

	    vkResetCommandPool(device, fr.cmdPool, 0);
	
	    VkCommandBufferBeginInfo beginInfo{};
	    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	    if (vkBeginCommandBuffer(fr.cmdBuffer, &beginInfo) != VK_SUCCESS) return;
	    if (m_profiler.IsInitialized())
	    {
	        m_profiler.BeginGpuFrame(fr.cmdBuffer, frameIndex);
	    }

#if defined(_WIN32)
	    if (m_worldEditorExe && m_worldEditorTerrainTools.IsValid() && m_worldEditorTerrainTools.IsDirtyHeightmap()
	        && m_terrain.IsValid())
	    {
		    const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
		    const VkImage hmImg = m_terrain.GetHeightmapGpu().image;
		    if (hmImg != VK_NULL_HANDLE && hm.width > 0u && hm.height > 0u && !hm.heights.empty())
		    {
			    const size_t bytes = static_cast<size_t>(hm.width) * static_cast<size_t>(hm.height) * sizeof(uint16_t);
			    VkDeviceSize stagingOff = 0;
			    const VkBuffer stBuf = m_stagingAllocator.Allocate(bytes, stagingOff);
			    void* mappedBase = m_stagingAllocator.GetCurrentMappedBase();
			    if (stBuf != VK_NULL_HANDLE && mappedBase != nullptr)
			    {
				    std::memcpy(static_cast<char*>(mappedBase) + static_cast<size_t>(stagingOff), hm.heights.data(), bytes);
				    engine::render::terrain::RecordHeightmapR16UploadCommands(
				        fr.cmdBuffer, stBuf, stagingOff, hmImg, hm.width, hm.height);
				    m_worldEditorTerrainTools.AckHeightmapGpuUploaded();
			    }
			    else
			    {
				    (void)m_worldEditorTerrainTools.FlushHeightmap(device,
				        m_vkDeviceContext.GetPhysicalDevice(),
				        graphicsQueue,
				        m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
				        hmImg);
			    }
		    }
	    }
#endif

	    // M100.14 — Live update WaterMeshGpu si la WaterScene est dirty.
	    // No-op si :
	    //   • pas de WaterMeshGpu valide (typique : VMA disabled STAB.7),
	    //   • aucune scene (mode jeu sans m_clientWaterScene, ou éditeur sans shell),
	    //   • flag dirty == false (cas nominal régime établi).
	    if (m_waterMeshGpu.IsInitialized())
	    {
	        const engine::world::water::WaterScene* scene = nullptr;
	        bool dirty = false;
	        if (m_worldEditorExe && m_worldEditorShell)
	        {
	            scene = &m_worldEditorShell->GetWaterDocument().Get();
	            dirty = m_worldEditorShell->GetWaterDocument().IsDirty();
	        }
	        else if (m_clientWaterScene)
	        {
	            scene = m_clientWaterScene.get();
	            dirty = m_waterClientSceneDirty;
	        }
	        if (scene && dirty)
	        {
	            // Réutilise le pool long-lived créé au boot pour éviter un create/destroy par frame.
	            if (m_waterTransferPool == VK_NULL_HANDLE)
	            {
	                LOG_WARN(Render, "[Water] m_waterTransferPool non disponible — skipping rebuild this frame");
	            }
	            else
	            {
	                // Reset cheap : libère les command buffers passés sans détruire le pool.
	                vkResetCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, 0);
	                if (m_waterMeshGpu.Rebuild(m_waterTransferPool, m_vkDeviceContext.GetGraphicsQueue(), *scene))
	                {
	                    if (m_worldEditorExe && m_worldEditorShell)
	                        m_worldEditorShell->MutableWaterDocument().ClearDirty();
	                    else
	                        m_waterClientSceneDirty = false;
	                }
	            }
	        }
	    }

	    if (m_fgSceneColorHDRId != engine::render::kInvalidResourceId && m_fgBackbufferId != engine::render::kInvalidResourceId)
	    {
	        VkImage     backbufferImage = m_vkSwapchain.GetImage(imageIndex);
	        VkImageView backbufferView  = m_vkSwapchain.GetImageView(imageIndex);
	        m_fgRegistry.bindImage(m_fgBackbufferId, backbufferImage, backbufferView);
	        m_frameGraph.execute(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, fr.cmdBuffer, m_fgRegistry, frameIndex, extent, 2u, m_vkDeviceContext.SupportsSynchronization2(), m_profiler.IsInitialized() ? &m_profiler : nullptr);

	        // M100.34 incrément 2 — Pont SceneColor_LDR → EditorViewportRenderTarget.
	        // Copie la scène rendue (post-tonemap, post-TAA, AVANT ImGui — donc le
	        // ScenePanel n'affiche pas son propre overlay = pas de récursion) vers
	        // l'image offscreen. Barriers à la main parce qu'on est hors FrameGraph
	        // ici (juste après son execute()).
	        if (m_worldEditorExe && m_editorViewportTarget.IsValid()
	            && m_fgSceneColorLDRId != engine::render::kInvalidResourceId)
	        {
	            VkImage sceneLdr = m_fgRegistry.getImage(m_fgSceneColorLDRId);
	            VkImage dst      = m_editorViewportTarget.GetImage();
	            if (sceneLdr != VK_NULL_HANDLE && dst != VK_NULL_HANDLE)
	            {
	                // Step 1: transitions de layout.
	                //   src (SceneColor_LDR) : `TRANSFER_SRC_OPTIMAL` post-CopyPresent
	                //     (cf. ligne 4538 — le dernier consumer FrameGraph le lit
	                //     en TransferSrc) → on garde tel quel, pas de barrier.
	                //   dst (ma target) : `SHADER_READ_ONLY` (init PR 1) →
	                //     `TRANSFER_DST_OPTIMAL` pour le blit.
	                VkImageMemoryBarrier bDst{};
	                bDst.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	                bDst.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	                bDst.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	                bDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	                bDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	                bDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst.image            = dst;
	                bDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	                vkCmdPipelineBarrier(fr.cmdBuffer,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                    VK_PIPELINE_STAGE_TRANSFER_BIT,
	                    0, 0, nullptr, 0, nullptr, 1, &bDst);

	                // Step 2: vkCmdBlitImage (gère size + format mismatch via
	                // LINEAR filter — utile parce que SceneColor_LDR est au
	                // format swapchain (typiquement BGRA8) et ma target est RGBA8).
	                VkImageBlit region{};
	                region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	                region.srcOffsets[0]  = { 0, 0, 0 };
	                region.srcOffsets[1]  = {
	                    static_cast<int32_t>(extent.width),
	                    static_cast<int32_t>(extent.height),
	                    1
	                };
	                region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	                region.dstOffsets[0]  = { 0, 0, 0 };
	                region.dstOffsets[1]  = {
	                    static_cast<int32_t>(m_editorViewportTarget.GetWidth()),
	                    static_cast<int32_t>(m_editorViewportTarget.GetHeight()),
	                    1
	                };
	                vkCmdBlitImage(fr.cmdBuffer,
	                    sceneLdr,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                    dst,       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                    1, &region, VK_FILTER_LINEAR);

	                // Step 3: transition retour de dst → SHADER_READ_ONLY pour
	                // qu'ImGui::Image puisse sampler dans le ScenePanel cette
	                // même frame. SceneColor_LDR reste en TRANSFER_SRC ; le
	                // FrameGraph re-transitionnera au prochain frame selon
	                // ses propres `read/write` declarations (auto-géré).
	                VkImageMemoryBarrier bDst2{};
	                bDst2.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	                bDst2.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	                bDst2.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	                bDst2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	                bDst2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	                bDst2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	                bDst2.image            = dst;
	                bDst2.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	                vkCmdPipelineBarrier(fr.cmdBuffer,
	                    VK_PIPELINE_STAGE_TRANSFER_BIT,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                    0, 0, nullptr, 0, nullptr, 1, &bDst2);
	            }
	        }
	    }
	    LOG_DEBUG(Render, "[DIAG] FrameGraph execute returned frame={}", m_currentFrame);

	    LOG_DEBUG(Render, "[DIAG] vkEndCommandBuffer begin frame={}", m_currentFrame);
	    if (vkEndCommandBuffer(fr.cmdBuffer) != VK_SUCCESS) return;
	    LOG_DEBUG(Render, "[DIAG] vkEndCommandBuffer OK frame={}", m_currentFrame);

	    VkSemaphore          waitSemaphores[]   = { fr.imageAvailable };
	    VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	    VkSemaphore          signalSemaphores[] = { fr.renderFinished };
	    VkSubmitInfo submitInfo{};
	    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	    submitInfo.waitSemaphoreCount   = 1;
	    submitInfo.pWaitSemaphores      = waitSemaphores;
	    submitInfo.pWaitDstStageMask    = waitStages;
	    submitInfo.commandBufferCount   = 1;
	    submitInfo.pCommandBuffers      = &fr.cmdBuffer;
	    submitInfo.signalSemaphoreCount = 1;
	    submitInfo.pSignalSemaphores    = signalSemaphores;
	    LOG_DEBUG(Render, "[DIAG] vkResetFences begin frame={}", m_currentFrame);
	    vkResetFences(device, 1, &fr.fence);
	    LOG_DEBUG(Render, "[DIAG] vkQueueSubmit begin frame={}", m_currentFrame);
	    VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fr.fence);
	    LOG_DEBUG(Render, "[DIAG] vkQueueSubmit result={} frame={}", static_cast<int>(submitResult), m_currentFrame);
	    if (submitResult != VK_SUCCESS) return;

	    VkPresentInfoKHR presentInfo{};
	    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	    presentInfo.waitSemaphoreCount = 1;
	    presentInfo.pWaitSemaphores    = signalSemaphores;
	    presentInfo.swapchainCount     = 1;
	    presentInfo.pSwapchains        = &swapchain;
	    presentInfo.pImageIndices      = &imageIndex;
	    LOG_DEBUG(Render, "[DIAG] vkQueuePresentKHR begin frame={}", m_currentFrame);
	    result = vkQueuePresentKHR(presentQueue, &presentInfo);
	    LOG_DEBUG(Render, "[DIAG] vkQueuePresentKHR result={} frame={}", static_cast<int>(result), m_currentFrame);
	    if (result == VK_ERROR_OUT_OF_DATE_KHR)
	    {
	        m_swapchainResizeRequested = true;
	    }
	    else if (result == VK_SUBOPTIMAL_KHR)
	    {
	        handleSuboptimal("Present");
	    }
	    else
	    {
	        m_suboptimalStreak = 0;
	    }

	    LOG_DEBUG(Render, "[DIAG] Render() complete frame={}", m_currentFrame);
	    m_currentFrame++;
	}

	void Engine::EndFrame()
	{
		// PROFILE_FUNCTION();
		LOG_DEBUG(Render, "[DIAG] EndFrame enter frame={}", m_currentFrame);
		if (m_profiler.IsInitialized())
		{
			m_profiler.EndFrame();
		}
		if (m_currentFrame > 0 && (m_currentFrame % 60) == 0)
			m_chunkStats.LogStats();
		LOG_DEBUG(Render, "[DIAG] EndFrame done frame={}", m_currentFrame);
	}

	void Engine::SwapRenderState()
	{
		const uint32_t readIdx  = m_renderReadIndex.load(std::memory_order_acquire);
		const uint32_t writeIdx = 1u - (readIdx & 1u);
		m_renderReadIndex.store(writeIdx, std::memory_order_release);
	}

	engine::render::ResourceId Engine::GetTaaHistoryPrevId() const
	{
		const uint32_t nextIdx = m_currentFrame % 2u;
		const uint32_t prevIdx = nextIdx ^ 1u;
		return prevIdx == 0u ? m_fgHistoryAId : m_fgHistoryBId;
	}

	engine::render::ResourceId Engine::GetTaaHistoryNextId() const
	{
		const uint32_t nextIdx = m_currentFrame % 2u;
		return nextIdx == 0u ? m_fgHistoryAId : m_fgHistoryBId;
	}

	/// Sous-projet C MVP — Resout race_str (string DB) en pointeur SkinnedMesh*
	/// depuis m_raceMeshes. Si la race demandee n'est pas chargee (asset manquant
	/// ou race hors-MVP), fallback sur "humains" avec un LOG_WARN. Si meme
	/// "humains" est absent (boot rate : SkinnedMeshLoader::Load a echoue pour
	/// toutes les races), retourne nullptr — le caller doit alors retomber sur
	/// le cube placeholder via m_skinnedAvatarReady = false.
	///
	/// Effet de bord : aucun (lecture seule sur m_raceMeshes + log).
	/// Thread : main thread uniquement (m_raceMeshes n'est ni mute ni protege).
	engine::render::skinned::SkinnedMesh* Engine::GetRaceMesh(const std::string& raceId)
	{
		return GetRaceMesh(raceId, m_avatarGender);
	}

	engine::render::skinned::SkinnedMesh* Engine::GetRaceMesh(const std::string& raceId,
	                                                          const std::string& gender)
	{
		// 1. Mesh exact race+genre demande.
		auto it = m_raceMeshes.find(RaceMeshKey(raceId, gender));
		if (it != m_raceMeshes.end()) return &it->second;
		// 2. Pas de variante pour ce genre (ex. femelle absente pour nains/orcs)
		//    -> male de la meme race (toujours charge).
		if (gender != "male") {
			auto maleIt = m_raceMeshes.find(RaceMeshKey(raceId, "male"));
			if (maleIt != m_raceMeshes.end()) {
				return &maleIt->second;
			}
		}
		// 3. Race inconnue / non chargee -> humains male.
		auto humansIt = m_raceMeshes.find(RaceMeshKey("humains", "male"));
		if (humansIt != m_raceMeshes.end()) {
			LOG_WARN(Render, "[Engine] GetRaceMesh('{}','{}') fallback humains|male", raceId, gender);
			return &humansIt->second;
		}
		// 4. Boot rate : meme humains|male absent -> cube placeholder.
		LOG_ERROR(Render, "[Engine] GetRaceMesh('{}','{}') ECHEC (humains|male absent)", raceId, gender);
		return nullptr;
	}

	void Engine::LoadRespawnMarkers()
	{
		m_respawnMarkers.clear();
		// Monde en cellules : marqueurs chargés avec chaque cellule (EnsureCellHeights).
		if (m_cellWorld)
			return;
		// Marqueurs de la zone active (game/data/zones/<zone>/respawn_points.txt) si
		// world.active_zone est défini, sinon repli sur l'ancien chemin global.
		// Le parseur ci-dessous ignore les colonnes faction/rayon (réservées au shard).
		const std::string activeZone = m_cfg.GetString("world.active_zone", "");
		const std::string markersPath = activeZone.empty()
			? m_cfg.GetString("server.respawn_points_path", "respawn/respawn_points.txt")
			: ("zones/" + activeZone + "/respawn_points.txt");
		const std::string markersText = engine::platform::FileSystem::ReadAllTextContent(m_cfg, markersPath);
		if (markersText.empty())
		{
			LOG_WARN(Core, "[Engine] Marqueurs de réapparition absents ({})", markersPath);
			return;
		}
		std::istringstream input(markersText);
		std::string line;
		while (std::getline(input, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			std::istringstream lineStream(line);
			RespawnMarker marker{};
			std::string typeToken;
			float unusedY = 0.0f;
			if (!(lineStream >> marker.zoneId >> typeToken >> marker.x >> unusedY >> marker.z))
				continue;
			if (typeToken == "graveyard")
				marker.destinationType = 0;
			else if (typeToken == "inn")
				marker.destinationType = 1;
			else
				continue;
			m_respawnMarkers.push_back(marker);
		}
		LOG_INFO(Core, "[Engine] Marqueurs de réapparition charges ({})", m_respawnMarkers.size());
	}

	float Engine::ResolveRemoteDisplayCenterY(bool isPlayer, float serverY,
	                                          float worldX, float worldZ) const
	{
		if (isPlayer)
		{
			return serverY; // joueur : centre capsule fiable répliqué par le shard
		}
		// Mob / node / loot bag : Y de spawn brut (souvent 0) → snap visuel au sol
		// client. SampleHeightAtWorldXZ retombe sur 0 si la heightmap n'est pas
		// chargée — comportement identique à l'ancien rendu dans ce cas.
		return SampleGroundHeight(worldX, worldZ) + 0.9f;
	}

	void Engine::SetAvatarGender(const std::string& gender)
	{
		if (gender != "male" && gender != "female") {
			LOG_WARN(Core, "[Avatar] SetAvatarGender('{}') ignore (attendu male/female)", gender);
			return;
		}
		if (gender == m_avatarGender)
			return;
		m_avatarGender = gender;
		// Runtime : la valeur sert au prochain EnterWorld (mesh) et aux apercus.
		m_cfg.SetValue("client.character_creation.gender", gender);
		// Persistance : fichier dedie merge au boot (comme keybinds.json) -> le
		// choix survit au relog. JSON imbrique = aplati en client.character_creation.gender.
		const std::string json =
			std::string("{\n  \"client\": {\n    \"character_creation\": {\n")
			+ "      \"gender\": \"" + gender + "\"\n"
			+ "    }\n  }\n}\n";
		if (!engine::platform::FileSystem::WriteAllText("character_appearance.json", json))
			LOG_WARN(Core, "[Avatar] character_appearance.json non ecrit (genre non persiste)");
		LOG_INFO(Core, "[Avatar] Genre actif -> {}", gender);
	}

	void Engine::SetCharacterGender(const std::string& characterName, const std::string& gender)
	{
		if (gender != "male" && gender != "female") {
			LOG_WARN(Core, "[Avatar] SetCharacterGender('{}','{}') ignore (genre attendu male/female)",
				characterName, gender);
			return;
		}
		if (characterName.empty()) {
			// Cas limite (nom vide) : repli sur le genre global de session.
			SetAvatarGender(gender);
			return;
		}
		// Session : applique tout de suite. Le 1er EnterWorld apres creation lit
		// m_avatarGender (la persistance ci-dessous sert surtout au relog ulterieur).
		if (gender != m_avatarGender) {
			m_avatarGender = gender;
			m_avatarSkinDiagLoggedGender.clear();  // force le re-log du diagnostic peau
		}
		m_cfg.SetValue("client.character_creation.gender", gender);
		m_cfg.SetValue("characters." + characterName + ".gender", gender);

		// Persistance : rebatit character_appearance.json = genre global (defaut) +
		// map genre-par-personnage (characters.<nom>.gender), pour que l'EnterWorld
		// d'un perso existant retrouve son genre au relog. Fix client interim ; le
		// stockage serveur (DB) remplacera ce fichier le moment venu.
		std::unordered_map<std::string, std::string> genders;
		for (const auto& kv : m_cfg.GetStringMapUnderPrefix("characters")) {
			const std::string::size_type dot = kv.first.rfind('.');
			if (dot == std::string::npos) continue;
			if (kv.first.substr(dot + 1) != "gender") continue;
			genders[kv.first.substr(0, dot)] = kv.second;
		}
		genders[characterName] = gender;

		auto jsonEscape = [](const std::string& s) {
			std::string out;
			out.reserve(s.size());
			for (char c : s) {
				if (c == '\\' || c == '"') out.push_back('\\');
				out.push_back(c);
			}
			return out;
		};

		std::string json =
			"{\n  \"client\": {\n    \"character_creation\": {\n      \"gender\": \""
			+ gender + "\"\n    }\n  },\n  \"characters\": {\n";
		std::size_t i = 0;
		for (const auto& kv : genders) {
			json += "    \"" + jsonEscape(kv.first) + "\": { \"gender\": \"" + kv.second + "\" }";
			json += (++i < genders.size()) ? ",\n" : "\n";
		}
		json += "  }\n}\n";

		if (!engine::platform::FileSystem::WriteAllText("character_appearance.json", json))
			LOG_WARN(Core, "[Avatar] character_appearance.json non ecrit (genre perso non persiste)");
		LOG_INFO(Core, "[Avatar] Genre perso '{}' -> {} (persiste, {} perso(s))",
			characterName, gender, genders.size());
	}

	void Engine::LoadInteractableProps()
	{
		m_props.clear();
		m_trimMatCache.clear();
		// Le collisionneur composite est (re)construit ici : terrain + cylindres des
		// props interactifs (coffre, villageois) puis du décor (LoadScenery, appelé après).
		m_worldCollider.SetTerrain(&m_terrainCollider);
		m_worldCollider.ClearCylinders();
		if (!m_pipeline) return;

		for (std::size_t ii = 0; ii < m_interactables.size(); ++ii)
		{
			const InteractableEntity& e = m_interactables[ii];
			if (e.meshPath.empty()) continue;
			// Le coffre anime (LoadAnimatedChest) est rendu via le pipeline skinne ; on
			// saute son rendu statique (mais on lui pose un cylindre de collision)
			// UNIQUEMENT pour l'interactible EXACT charge en anime (m_chestInteractableIndex).
			// Les autres coffres eventuels restent rendus en statique normalement.
			if (m_chestLoaded && static_cast<int>(ii) == m_chestInteractableIndex)
			{
				const float gy = m_terrainCollider.GroundHeightAt(e.position.x, e.position.z);
				m_worldCollider.AddCylinder(engine::gameplay::PropCylinder{
					e.position.x, e.position.z, 0.6f, gy, gy + 1.0f });
				continue;
			}
			// Interactibles solides (coffre, PNJ...) : collision auto (empreinte XZ du mesh).
			BuildPropFromMesh(e.meshPath, e.position.x, e.position.z, e.meshYawDeg,
			                  e.meshRotXDeg, e.meshScale, static_cast<int>(ii),
			                  /*solid*/ true, /*collisionRadius*/ 0.0f);
		}
		LOG_INFO(Render, "[Props] {} prop(s) interactif(s) rendus", m_props.size());
	}

	void Engine::LoadScenery()
	{
		m_scenery.clear();
		if (!m_pipeline) return;
		const int n = static_cast<int>(m_cfg.GetInt("world.scenery.count", 0));
		for (int i = 0; i < n; ++i)
		{
			const std::string base = "world.scenery." + std::to_string(i) + ".";
			SceneryInstance s;
			s.meshPath = m_cfg.GetString(base + "mesh", "");
			if (s.meshPath.empty()) continue;
			s.x = static_cast<float>(m_cfg.GetDouble(base + "x", 0.0));
			s.z = static_cast<float>(m_cfg.GetDouble(base + "z", 0.0));
			s.yawDeg = static_cast<float>(m_cfg.GetDouble(base + "yaw_deg", 0.0));
			s.scale = static_cast<float>(m_cfg.GetDouble(base + "scale", 1.0));
			s.collisionRadius = static_cast<float>(m_cfg.GetDouble(base + "collision_radius", 0.0));
			s.solid = m_cfg.GetBool(base + "solid", true);
			m_scenery.push_back(s);
		}
		for (const auto& s : m_scenery)
			BuildPropFromMesh(s.meshPath, s.x, s.z, s.yawDeg, /*rotXDeg*/ 0.0f, s.scale,
			                  /*interactableIndex*/ -1, /*solid*/ s.solid, s.collisionRadius);
		LOG_INFO(Render, "[Scenery] {} element(s) decor charge(s) ; {} cylindre(s) collision",
		         static_cast<int>(m_scenery.size()), static_cast<int>(m_worldCollider.CylinderCount()));
	}

	void Engine::LoadAnimatedChest()
	{
		m_chestLoaded = false;
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		const InteractableEntity* chest = nullptr;
		int chestIdx = -1;
		for (std::size_t i = 0; i < m_interactables.size(); ++i)
			if (m_interactables[i].meshPath.find("Chest_Wood") != std::string::npos)
			{
				chest = &m_interactables[i];
				chestIdx = static_cast<int>(i);
				break;
			}
		if (!chest) return;

		const std::string full = contentRoot + "/" + chest->meshPath;
		auto loaded = engine::render::skinned::SkinnedMeshLoader::Load(
			m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), full);
		if (!loaded)
		{
			LOG_WARN(Render, "[Chest] chargement skinne ECHEC '{}' (coffre non anime)", full);
			return;
		}
		m_chestMesh = std::move(*loaded);
		m_chestInteractableIndex = chestIdx;
		m_chestPos = engine::math::Vec3{ chest->position.x,
			m_terrainCollider.GroundHeightAt(chest->position.x, chest->position.z), chest->position.z };
		m_chestYawDeg = chest->meshYawDeg;
		m_chestScale = chest->meshScale;
		m_chestRotXDeg = chest->meshRotXDeg;

		std::string meshDir;
		{
			const auto sl = chest->meshPath.find_last_of('/');
			if (sl != std::string::npos) meshDir = chest->meshPath.substr(0, sl + 1);
		}
		auto makeTrim = [&](const std::string& trimType) -> uint32_t
		{
			const engine::render::TextureHandle bc =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_BaseColor.png", true);
			if (!bc.IsValid()) return materialCache.GetDefaultMaterialIndex();
			engine::render::Material mat{};
			mat.baseColor = bc;
			const engine::render::TextureHandle n =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_Normal.png", false);
			if (n.IsValid()) mat.normal = n;
			const engine::render::TextureHandle o =
				m_assetRegistry.LoadTexture(meshDir + "T_Trim_" + trimType + "_ORM.png", false);
			if (o.IsValid()) mat.orm = o;
			return materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
		};
		m_chestMatFurniture = makeTrim("Furniture");
		m_chestMatMetal = makeTrim("Metal");

		m_chestSubmeshMat.clear();
		for (const auto& sm : m_chestMesh.submeshes)
			m_chestSubmeshMat.push_back(
				sm.materialName.find("Metal") != std::string::npos ? m_chestMatMetal : m_chestMatFurniture);

		m_chestLoaded = true;
		LOG_INFO(Render,
			"[Chest] coffre anime charge ({} bones, {} clips, {} sous-maillages)",
			static_cast<int>(m_chestMesh.skeleton.bones.size()),
			static_cast<int>(m_chestMesh.clips.size()),
			static_cast<int>(m_chestMesh.submeshes.size()));
	}

	void Engine::RecordAnimatedChest(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		if (!m_chestLoaded || !m_pipeline || !m_skinnedRenderer.IsValid()) return;
		if (m_chestMesh.skeleton.bones.empty()) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;

		const float nowSec = EngineNowSec();
		auto locals  = m_chestCrossfade.Sample(m_chestMesh.skeleton, nowSec);
		auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(m_chestMesh.skeleton, locals);
		auto finals  = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(m_chestMesh.skeleton, globals);

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		// Meme composition que BuildPropFromMesh : T * Ry(yaw) * Rx(rotX) * Scale * import,
		// pour honorer les champs scale / rot_x_deg de l'interactable coffre.
		engine::math::Mat4 scaleM = engine::math::Mat4::Identity();
		scaleM.m[0] = m_chestScale; scaleM.m[5] = m_chestScale; scaleM.m[10] = m_chestScale;
		const engine::math::Mat4 model =
			engine::math::Mat4::Translate(m_chestPos) *
			engine::math::Mat4::RotateY(m_chestYawDeg * kDeg2Rad) *
			engine::math::Mat4::RotateX(m_chestRotXDeg * kDeg2Rad) *
			scaleM *
			m_chestMesh.importTransform;

		std::vector<uint32_t> subMat = m_chestSubmeshMat;
		m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
			m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
			m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
			[this, &rs, finals = std::move(finals), &materialCache, model,
			 subMat = std::move(subMat)](VkCommandBuffer innerCmd) {
				m_skinnedRenderer.Record(
					m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
					m_pipeline->GetGeometryPass().GetRenderPassLoad(), VK_NULL_HANDLE,
					rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
					m_chestMesh, finals, materialCache.GetDescriptorSet(),
					model.m, m_chestMatFurniture, subMat, m_chestMatMetal, 0.0f, 0.0f);
			});
	}

	void Engine::LoadBoatMesh()
	{
		if (m_boatMeshTried || !m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		m_boatMeshTried = true;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		const std::string meshDir = "models/props/ships/dromon/";
		auto loaded = engine::render::skinned::SkinnedMeshLoader::Load(
			m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), contentRoot + "/" + meshDir + "dromon.glb");
		if (!loaded)
		{
			LOG_WARN(Render, "[Boats] dromon introuvable ou illisible ({}dromon.glb) : bateaux invisibles", meshDir);
			return;
		}
		m_boatMesh = std::move(*loaded);
		engine::render::Material mat{};
		mat.baseColor = m_assetRegistry.LoadTexture(meshDir + "dromon_basecolor.png", true);
		if (const engine::render::TextureHandle n = m_assetRegistry.LoadTexture(meshDir + "dromon_normal.png", false); n.IsValid())
			mat.normal = n;
		if (const engine::render::TextureHandle o = m_assetRegistry.LoadTexture(meshDir + "dromon_orm.png", false); o.IsValid())
			mat.orm = o;
		const uint32_t material = mat.baseColor.IsValid()
			? materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat)
			: materialCache.GetDefaultMaterialIndex();
		m_boatSubmeshMat.assign(m_boatMesh.submeshes.size(), material);
		m_boatMeshLoaded = true;
		LOG_INFO(Render, "[Boats] dromon charge ({} sous-maillage(s), {} os)",
			m_boatMesh.submeshes.size(), m_boatMesh.skeleton.bones.size());
	}

	bool Engine::BoatRenderPose(uint64_t boatEntityId, float& outX, float& outY, float& outZ, float& outYaw, bool& outDocked) const
	{
		for (const engine::client::UIRemoteEntity& re : m_uiModelBinding.GetModel().remoteEntities)
		{
			if (re.entityId != boatEntityId || !engine::server::IsBoatArchetype(re.archetypeId))
				continue;
			outX = re.positionX; outY = re.positionY; outZ = re.positionZ; outYaw = re.yawRadians;
			const auto it = m_remoteSmoothed.find(re.entityId);
			if (it != m_remoteSmoothed.end() && it->second.valid)
			{
				outX = it->second.x; outY = it->second.y; outZ = it->second.z; outYaw = it->second.yaw;
			}
			outDocked = (re.stateFlags & engine::server::kEntityStateBoatDocked) != 0u;
			return true;
		}
		return false;
	}

	void Engine::RecordBoats(VkCommandBuffer cmd, engine::render::Registry& reg, const engine::RenderState& rs)
	{
		if (!m_pipeline || !m_skinnedRenderer.IsValid()) return;
		const std::vector<engine::client::UIRemoteEntity>& remotes = m_uiModelBinding.GetModel().remoteEntities;
		bool any = false;
		for (const engine::client::UIRemoteEntity& re : remotes)
			any = any || engine::server::IsBoatArchetype(re.archetypeId);
		if (!any) return;
		LoadBoatMesh();
		if (!m_boatMeshLoaded) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		const float maxDist = static_cast<float>(m_cfg.GetDouble("client.boats.render_distance_m", 2500.0));
		const engine::world::scatter::Frustum frustum = engine::world::scatter::Frustum::FromViewProj(rs.viewProjMatrix.m);
		// Un os, pose de repos : matrices finales identité (os et liaison à l'identité).
		const std::vector<engine::math::Mat4> finals((std::max)(m_boatMesh.skeleton.bones.size(), static_cast<size_t>(1)),
			engine::math::Mat4::Identity());
		for (const engine::client::UIRemoteEntity& re : remotes)
		{
			if (!engine::server::IsBoatArchetype(re.archetypeId)) continue;
			float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f;
			bool docked = false;
			if (!BoatRenderPose(re.entityId, x, y, z, yaw, docked)) continue;
			const float dx = x - rs.camera.position.x, dz = z - rs.camera.position.z;
			if (dx * dx + dz * dz > maxDist * maxDist) continue;
			// Sphère englobante du dromon (30 m de long, mât de 19 m).
			if (!frustum.SphereVisible(engine::math::Vec3{ x, y + 6.0f, z }, 20.0f)) continue;
			const engine::math::Mat4 model =
				engine::math::Mat4::Translate(engine::math::Vec3{ x, y, z }) *
				engine::math::Mat4::RotateY(yaw) *
				m_boatMesh.importTransform;
			std::vector<uint32_t> subMat = m_boatSubmeshMat;
			m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
				m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
				m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
				[this, &rs, finals, &materialCache, model, subMat = std::move(subMat)](VkCommandBuffer innerCmd) {
					m_skinnedRenderer.Record(
						m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
						m_pipeline->GetGeometryPass().GetRenderPassLoad(), VK_NULL_HANDLE,
						rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
						m_boatMesh, finals, materialCache.GetDescriptorSet(),
						model.m, subMat.empty() ? 0u : subMat.front(), subMat, subMat.empty() ? 0u : subMat.front(), 0.0f, 0.0f);
				});
		}
	}

	// Charge un mesh statique (prop/décor), CUIT sa transformation monde dans les
	// sommets (contournement du buffer d'instance partagé, cf. plus bas), l'ancre au
	// sol, crée ses matériaux et l'ajoute à m_props. Si \p solid, enregistre un
	// cylindre de collision dans m_worldCollider.
	// \param wx,wz             Position monde (m) ; Y posé au sol via GroundHeightAt.
	// \param yawDeg,rotXDeg    Rotations (degrés) ; \param scale échelle uniforme.
	// \param interactableIndex Index dans m_interactables (-1 pour le décor non interactif).
	// \param solid             Si true, ajoute un cylindre de collision.
	// \param collisionRadius   Rayon du cylindre (m) ; 0 = empreinte XZ auto du mesh.
	void Engine::BuildPropFromMesh(const std::string& meshPath, float wx, float wz,
		float yawDeg, float rotXDeg, float scale, int interactableIndex,
		bool solid, float collisionRadius)
	{
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		constexpr float kDeg2Rad = 3.14159265f / 180.f;

		const std::string full = contentRoot + "/" + meshPath;
		auto cpu = engine::render::staticmesh::StaticMeshLoader::LoadCpuOnlyForTests(full);
		if (!cpu || cpu->vertices.empty() || cpu->submeshes.empty())
		{
			LOG_WARN(Render, "[Props] mesh load FAIL '{}'", full);
			return;
		}
		// Dossier du mesh : les URI de textures sont relatives au .gltf.
		std::string meshDir;
		{
			const auto slash = meshPath.find_last_of('/');
			if (slash != std::string::npos) meshDir = meshPath.substr(0, slash + 1);
		}
		// Groupe les indices par nom de materiau + un sous-maillage representatif (URIs).
		std::unordered_map<std::string, std::vector<uint32_t>> idxByMat;
		std::unordered_map<std::string, const engine::render::staticmesh::StaticSubMesh*> repByMat;
		for (const auto& sub : cpu->submeshes)
		{
			std::vector<uint32_t>& v = idxByMat[sub.materialName];
			v.insert(v.end(), cpu->indices.begin() + sub.firstIndex,
			         cpu->indices.begin() + sub.firstIndex + sub.indexCount);
			if (repByMat.find(sub.materialName) == repByMat.end()) repByMat[sub.materialName] = &sub;
		}

		PropRenderable prop;
		const float groundY = m_terrainCollider.GroundHeightAt(wx, wz);
		engine::math::Mat4 scaleM = engine::math::Mat4::Identity();
		scaleM.m[0] = scale; scaleM.m[5] = scale; scaleM.m[10] = scale;
		// Matrice modèle monde du prop (translation au sol + yaw + rotX + échelle).
		const engine::math::Mat4 bakeM =
			engine::math::Mat4::Translate(engine::math::Vec3{ wx, groundY, wz }) *
			engine::math::Mat4::RotateY(yawDeg * kDeg2Rad) *
			engine::math::Mat4::RotateX(rotXDeg * kDeg2Rad) *
			scaleM;
		// CONTOURNEMENT bug moteur : GeometryPass::Record applique la matrice d'instance
		// via un buffer GPU PARTAGE (m_identityInstanceBuffer) reecrit a chaque draw au
		// moment du RECORD. Avec plusieurs props dans le meme command buffer, seule la
		// DERNIERE matrice survit a l'execution -> tous les props se dessinent au meme
		// endroit (empiles). On contourne en CUISANT la transformation monde directement
		// dans les sommets (chaque prop a deja son propre mesh CPU), puis on laisse la
		// matrice d'instance a l'identite.
		float topY = groundY;     // haut du cylindre de collision (monde)
		float radiusAuto = 0.0f;  // empreinte XZ max autour de (wx, wz)
		{
			const float* M = bakeM.m;  // column-major : M[col*4+row]
			float minY = 1e30f, maxY = -1e30f;
			// AABB monde complète (anti-occlusion caméra) — XZ figés ici (le lift
			// ne décale que Y, appliqué plus bas aux bornes minY/maxY).
			float bbMinX = 1e30f, bbMaxX = -1e30f, bbMinZ = 1e30f, bbMaxZ = -1e30f;
			for (auto& v : cpu->vertices)
			{
				const float px = v.pos[0], py = v.pos[1], pz = v.pos[2];
				v.pos[0] = M[0]*px + M[4]*py + M[8]*pz  + M[12];
				v.pos[1] = M[1]*px + M[5]*py + M[9]*pz  + M[13];
				v.pos[2] = M[2]*px + M[6]*py + M[10]*pz + M[14];
				if (v.pos[1] < minY) minY = v.pos[1];
				if (v.pos[1] > maxY) maxY = v.pos[1];
				if (v.pos[0] < bbMinX) bbMinX = v.pos[0];
				if (v.pos[0] > bbMaxX) bbMaxX = v.pos[0];
				if (v.pos[2] < bbMinZ) bbMinZ = v.pos[2];
				if (v.pos[2] > bbMaxZ) bbMaxZ = v.pos[2];
				const float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
				float rnx = M[0]*nx + M[4]*ny + M[8]*nz;
				float rny = M[1]*nx + M[5]*ny + M[9]*nz;
				float rnz = M[2]*nx + M[6]*ny + M[10]*nz;
				const float nlen = std::sqrt(rnx*rnx + rny*rny + rnz*rnz);
				if (nlen > 1e-6f) { rnx /= nlen; rny /= nlen; rnz /= nlen; }
				v.normal[0] = rnx; v.normal[1] = rny; v.normal[2] = rnz;
			}
			// Ancrage au sol : selon le modele, le pivot n'est pas a la base
			// -> le prop s'enterre (ou flotte). On decale tout le mesh pour que
			// son point le PLUS BAS repose exactement sur le sol (groundY).
			const float lift = groundY - minY;
			if (std::fabs(lift) > 1e-4f)
				for (auto& v : cpu->vertices)
					v.pos[1] += lift;
			topY = maxY + lift;
			// Anti-occlusion caméra — sphère englobante monde (AABB des sommets
			// bakés, Y décalé du lift). Sert d'occulteur dans CameraOcclusionFade.
			{
				const float wMinY = minY + lift, wMaxY = maxY + lift;
				prop.occluderCenter = engine::math::Vec3{
					0.5f * (bbMinX + bbMaxX), 0.5f * (wMinY + wMaxY),
					0.5f * (bbMinZ + bbMaxZ) };
				const engine::math::Vec3 ext{
					bbMaxX - bbMinX, wMaxY - wMinY, bbMaxZ - bbMinZ };
				prop.occluderRadius =
					0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
			}
			// Empreinte XZ auto (rayon max des sommets autour de l'axe (wx,wz)).
			for (const auto& v : cpu->vertices)
			{
				const float ddx = v.pos[0] - wx, ddz = v.pos[2] - wz;
				const float r = std::sqrt(ddx*ddx + ddz*ddz);
				if (r > radiusAuto) radiusAuto = r;
			}
		}
		prop.modelMatrix = engine::math::Mat4::Identity();  // sommets deja en espace monde

		// M45.5 — sphère englobante monde du prop (centre du billboard impostor +
		// rayon = demi-taille). Le centre XZ est (wx,wz) ; en Y, milieu entre le
		// sol (groundY) et le sommet (topY). Le rayon couvre largeur (radiusAuto)
		// ET hauteur (demi-hauteur), pour un quad qui englobe tout le mesh.
		prop.meshPath = meshPath;
		{
			const float halfHeight = 0.5f * (topY - groundY);
			prop.impostorCenter = engine::math::Vec3{ wx, groundY + halfHeight, wz };
			prop.impostorRadius = std::max(radiusAuto, halfHeight);
			if (prop.impostorRadius < 0.05f) prop.impostorRadius = 0.5f;
		}

		for (const auto& kv : idxByMat)
		{
			const std::string& matName = kv.first;
			const std::vector<uint32_t>& idxs = kv.second;
			if (idxs.empty()) continue;
			engine::render::MeshHandle mh = m_assetRegistry.CreateMeshFromData(
				cpu->vertices.data(), static_cast<uint32_t>(cpu->vertices.size()),
				idxs.data(), static_cast<uint32_t>(idxs.size()));
			if (!mh.IsValid()) continue;

			uint32_t matIdx = materialCache.GetDefaultMaterialIndex();
			uint32_t hlIdx  = matIdx;
			auto cached = m_trimMatCache.find(matName);
			if (cached != m_trimMatCache.end())
			{
				matIdx = cached->second.first;
				hlIdx  = cached->second.second;
			}
			else
			{
				const engine::render::staticmesh::StaticSubMesh* rep = repByMat[matName];
				if (rep && !rep->baseColorUri.empty())
				{
					const engine::render::TextureHandle bc =
						m_assetRegistry.LoadTexture(meshDir + rep->baseColorUri, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (!rep->normalUri.empty())
						{
							const engine::render::TextureHandle n = m_assetRegistry.LoadTexture(meshDir + rep->normalUri, false);
							if (n.IsValid()) mat.normal = n;
						}
						if (!rep->ormUri.empty())
						{
							const engine::render::TextureHandle o = m_assetRegistry.LoadTexture(meshDir + rep->ormUri, false);
							if (o.IsValid()) mat.orm = o;
						}
						// Feuillages (alphaMode != opaque) : decoupe alpha au fragment shader.
						if (rep->alphaCutout)
							mat.flags = engine::render::MaterialFlags::AlphaCutout;
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						// Variante surbrillance : memes textures + Highlight (+ cutout eventuel).
						mat.flags = static_cast<engine::render::MaterialFlags>(
							static_cast<uint32_t>(mat.flags) | static_cast<uint32_t>(engine::render::MaterialFlags::Highlight));
						hlIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					}
				}
				else
				{
					// Pas de texture baseColor (props « nature » : arbres, herbe...) :
					// l'albedo provient de la couleur de sommet COLOR_0 (flag VertexColorAlbedo).
					engine::render::Material mat{};
					mat.flags = engine::render::MaterialFlags::VertexColorAlbedo;
					matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					hlIdx = matIdx;
				}
				m_trimMatCache[matName] = { matIdx, hlIdx };
			}

			PropPart part;
			part.mesh = mh;
			part.materialIndex = matIdx;
			part.highlightMaterialIndex = hlIdx;
			prop.parts.push_back(part);
		}

		prop.interactableIndex = interactableIndex;
		prop.worldPos = engine::math::Vec3{ wx, groundY, wz };
		if (!prop.parts.empty())
		{
			if (solid)
			{
				float radius = collisionRadius > 0.0f ? collisionRadius : radiusAuto;
				if (radius < 0.05f) radius = 0.5f;
				// Porte/escalier reconnus par le nom du mesh (insensible à la casse) :
				// « door » → passage franchissable (aucune collision) ; « escalier »
				// (ou « stair ») → surface gravissable. Le décor normal reste un mur plein.
				std::string meshLower = meshPath;
				for (char& ch : meshLower)
				{
					if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
				}
				engine::gameplay::PropCylinder cyl{ wx, wz, radius, groundY, topY };
				cyl.passable = meshLower.find("door") != std::string::npos;
				cyl.stair = (meshLower.find("escalier") != std::string::npos)
					|| (meshLower.find("stair") != std::string::npos);
				m_worldCollider.AddCylinder(cyl);
			}
			m_props.push_back(std::move(prop));
		}
	}

	void Engine::LoadBuildingCollisionCatalog()
	{
		// Absent -> catalogue vide -> fallback cylindre (rétro-compatible).
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		const std::string catPath = contentRoot + "/collision/building_pieces.json";
		const std::string catJson = engine::platform::FileSystem::ReadAllText(catPath);
		if (!catJson.empty() && m_buildingCollisionCatalog.LoadFromJson(catJson))
			LOG_INFO(Render, "[Buildings] catalogue collision chargé ('{}')", catPath);
		else
			LOG_DEBUG(Render, "[Buildings] pas de catalogue collision ('{}') -> fallback cylindre", catPath);
	}

	void Engine::LoadBuildings()
	{
		if (!m_pipeline) return;
		namespace fs = std::filesystem;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		// Catalogue de collision des pièces : chargé au boot AVANT le décor instancié
		// (LoadBuildingCollisionCatalog), partagé avec les bâtiments ci-dessous.

		const std::string zoneId = m_cfg.GetString("world.active_zone", "");

		// Résolution du chemin : namespacé par zone si présent, sinon legacy plat.
		fs::path path;
		if (!zoneId.empty())
		{
			const fs::path ns = fs::path(contentRoot) / "instances" /
				("zone_" + zoneId) / "buildings.bin";
			std::error_code ec;
			path = fs::exists(ns, ec) ? ns
				: fs::path(contentRoot) / "instances" / "buildings.bin";
		}
		else
		{
			path = fs::path(contentRoot) / "instances" / "buildings.bin";
		}
		(void)LoadBuildingsInto(path.string(), LegacyInstanceTarget());
	}

	int Engine::LoadBuildingsInto(const std::string& fullPath, const InstanceTarget& target)
	{
		if (!m_pipeline || target.field == nullptr || target.collider == nullptr || !target.ground) return 0;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		const std::string& path = fullPath;
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			// Absence de fichier = cas normal (zone sans bâtiment) -> Debug.
			LOG_DEBUG(Render, "[Buildings] aucun buildings.bin ('{}')", path);
			return 0;
		}
		const std::streamsize size = f.tellg();
		if (size <= 0) return 0;
		f.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);

		std::vector<engine::world::instances::BuildingPlacement> placements;
		std::string err;
		if (!engine::world::instances::LoadBuildingsBin(
				std::span<const uint8_t>(bytes), placements, err))
		{
			LOG_WARN(Render, "[Buildings] buildings.bin invalide : {}", err);
			return 0;
		}

		// Bibliothèque des types de bâtiments : la carte ne stocke que des
		// références (type + variante) ; on résout ici contre les fichiers
		// `buildings/templates/<type>.json` pour obtenir les pièces.
		engine::world::instances::BuildingTemplateLibrary library;
		std::string libErr;
		if (!library.LoadFromContent(contentRoot, libErr) && !libErr.empty())
			LOG_WARN(Render, "[Buildings] bibliotheque partiellement chargee : {}", libErr);
		return PlaceBuildings(placements, library, target, path);
	}

	int Engine::PlaceBuildings(const std::vector<engine::world::instances::BuildingPlacement>& placements,
		const engine::world::instances::BuildingTemplateLibrary& library,
		const InstanceTarget& target, const std::string& label, size_t first, size_t count)
	{
		if (!m_pipeline || target.field == nullptr || target.collider == nullptr || !target.ground) return 0;
		const std::string& path = label;

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		// Helpers de composition (Math.h ne fournit ni Scale ni RotateZ).
		auto scaleMat = [](float s) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			m.m[0] = s; m.m[5] = s; m.m[10] = s; return m;
		};
		auto rotZ = [](float r) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			const float c = std::cos(r), s = std::sin(r);
			m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c; return m;
		};

		int partCount = 0, resolved = 0;
		const size_t end = (first >= placements.size()) ? first
			: first + std::min(count, placements.size() - first);
		for (size_t k = first; k < end; ++k)
		{
			const auto& pl = placements[k];
			const engine::world::instances::BuildingVariant* variant =
				library.Resolve(pl.templateType, pl.variantId);
			if (!variant)
			{
				LOG_WARN(Render, "[Buildings] reference non resolue: type='{}' variante='{}'",
					pl.templateType, pl.variantId);
				continue;
			}
			++resolved;
			// L'origine du bâtiment est ancrée au sol UNE fois ; les pièces
			// conservent ensuite leur Y local (toit/étage empilés). Position dans le
			// repère de la cible (globale − origine : cellule du monde « Royaumes »).
			const float bx = static_cast<float>(static_cast<double>(pl.worldPosition.x) - target.originX);
			const float bz = static_cast<float>(static_cast<double>(pl.worldPosition.z) - target.originZ);
			const float groundY = target.ground(bx, bz);
			const engine::math::Mat4 groupM =
				engine::math::Mat4::Translate(engine::math::Vec3{
					bx, groundY + pl.worldPosition.y, bz }) *
				engine::math::Mat4::RotateY(pl.worldYawDeg * kDeg2Rad) *
				scaleMat(pl.worldScale);

			for (const auto& pt : variant->parts)
			{
				if (pt.gltfRelativePath.empty()) continue;
				const engine::math::Mat4 localM =
					engine::math::Mat4::Translate(pt.localPosition) *
					engine::math::Mat4::RotateY(pt.localEulerDeg.y * kDeg2Rad) *
					engine::math::Mat4::RotateX(pt.localEulerDeg.x * kDeg2Rad) *
					rotZ(pt.localEulerDeg.z * kDeg2Rad) *
					scaleMat(pt.localScale);
				const engine::math::Mat4 worldM = groupM * localM;
				// Monde ouvert : pièce rendue en INSTANCIÉ (mesh GPU partagé entre
				// tous les bâtiments du même modèle) — l'ancien chemin cuisait un
				// mesh par pièce, intenable avec des centaines de bâtiments.
				const uint32_t meshId = m_scatterField.InternMesh(pt.gltfRelativePath);
				if (!EnsureScatterMesh(meshId)) continue;
				AddScatterInstance(meshId, worldM, /*cullClass*/ 0u,
					static_cast<uint8_t>(engine::world::instances::kScatterFlagBuildingPiece
						| engine::world::instances::kScatterFlagOccluder),
					pt.solid, /*buildingPiece*/ true, pt.collisionRadius, target);
				++partCount;
			}
		}
		LOG_DEBUG(Render, "[Buildings] '{}' : {} placement(s) [{}..{}[, {} resolu(s), {} piece(s) rendue(s)",
			path, static_cast<int>(placements.size()), first, end, resolved, partCount);
		return partCount;
	}

	engine::render::TextureHandle Engine::SolidColorTexture(uint8_t r, uint8_t g, uint8_t b)
	{
		const uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
		auto it = m_solidColorTextures.find(key);
		if (it != m_solidColorTextures.end()) return it->second;
		const uint8_t rgba[4] = { r, g, b, 255 };
		engine::render::TextureHandle h =
			m_assetRegistry.CreateTextureFromMemory(rgba, 1, 1, /*useSrgb*/ true);
		m_solidColorTextures[key] = h;
		return h;
	}

	void Engine::BuildPropFromMeshMatrix(const std::string& meshPath,
		const engine::math::Mat4& worldM, int interactableIndex,
		bool solid, float collisionRadius)
	{
		if (!m_pipeline) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return;
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");

		const std::string full = contentRoot + "/" + meshPath;
		auto cpu = engine::render::staticmesh::StaticMeshLoader::LoadCpuOnlyForTests(full);
		if (!cpu || cpu->vertices.empty() || cpu->submeshes.empty())
		{
			LOG_WARN(Render, "[Buildings] mesh load FAIL '{}'", full);
			return;
		}
		std::string meshDir;
		{
			const auto slash = meshPath.find_last_of('/');
			if (slash != std::string::npos) meshDir = meshPath.substr(0, slash + 1);
		}
		std::unordered_map<std::string, std::vector<uint32_t>> idxByMat;
		std::unordered_map<std::string, const engine::render::staticmesh::StaticSubMesh*> repByMat;
		for (const auto& sub : cpu->submeshes)
		{
			std::vector<uint32_t>& v = idxByMat[sub.materialName];
			v.insert(v.end(), cpu->indices.begin() + sub.firstIndex,
			         cpu->indices.begin() + sub.firstIndex + sub.indexCount);
			if (repByMat.find(sub.materialName) == repByMat.end()) repByMat[sub.materialName] = &sub;
		}

		PropRenderable prop;
		// Cuisson de worldM dans les sommets (même contournement que BuildPropFromMesh :
		// le buffer d'instance GPU partagé n'autorise pas de matrice par-prop), mais
		// SANS le lift au sol : le Y de la matrice est autoritaire (pièce empilable).
		float minY = 1e30f, maxY = -1e30f;
		float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
		{
			const float* M = worldM.m; // column-major : M[col*4+row]
			for (auto& v : cpu->vertices)
			{
				const float px = v.pos[0], py = v.pos[1], pz = v.pos[2];
				v.pos[0] = M[0]*px + M[4]*py + M[8]*pz  + M[12];
				v.pos[1] = M[1]*px + M[5]*py + M[9]*pz  + M[13];
				v.pos[2] = M[2]*px + M[6]*py + M[10]*pz + M[14];
				if (v.pos[1] < minY) minY = v.pos[1];
				if (v.pos[1] > maxY) maxY = v.pos[1];
				if (v.pos[0] < minX) minX = v.pos[0];
				if (v.pos[0] > maxX) maxX = v.pos[0];
				if (v.pos[2] < minZ) minZ = v.pos[2];
				if (v.pos[2] > maxZ) maxZ = v.pos[2];
				const float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
				float rnx = M[0]*nx + M[4]*ny + M[8]*nz;
				float rny = M[1]*nx + M[5]*ny + M[9]*nz;
				float rnz = M[2]*nx + M[6]*ny + M[10]*nz;
				const float nlen = std::sqrt(rnx*rnx + rny*rny + rnz*rnz);
				if (nlen > 1e-6f) { rnx /= nlen; rny /= nlen; rnz /= nlen; }
				v.normal[0] = rnx; v.normal[1] = rny; v.normal[2] = rnz;
			}
		}
		prop.modelMatrix = engine::math::Mat4::Identity(); // sommets déjà en espace monde

		// Centre XZ = milieu de l'empreinte des sommets bakés (pour collision + impostor).
		const float cx = 0.5f * (minX + maxX);
		const float cz = 0.5f * (minZ + maxZ);
		float radiusAuto = 0.0f;
		for (const auto& v : cpu->vertices)
		{
			const float ddx = v.pos[0] - cx, ddz = v.pos[2] - cz;
			const float r = std::sqrt(ddx*ddx + ddz*ddz);
			if (r > radiusAuto) radiusAuto = r;
		}

		prop.meshPath = meshPath;
		{
			const float halfHeight = 0.5f * (maxY - minY);
			prop.impostorCenter = engine::math::Vec3{ cx, minY + halfHeight, cz };
			prop.impostorRadius = std::max(radiusAuto, halfHeight);
			if (prop.impostorRadius < 0.05f) prop.impostorRadius = 0.5f;
		}

		// Anti-occlusion caméra — sphère englobante monde (AABB des sommets bakés,
		// déjà en espace monde car modelMatrix = identité). Sert d'occulteur dans
		// CameraOcclusionFade. Si l'empreinte est dégénérée, le rayon reste positif.
		{
			prop.occluderCenter = engine::math::Vec3{
				0.5f * (minX + maxX), 0.5f * (minY + maxY), 0.5f * (minZ + maxZ) };
			const engine::math::Vec3 ext{ maxX - minX, maxY - minY, maxZ - minZ };
			prop.occluderRadius =
				0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
		}

		for (const auto& kv : idxByMat)
		{
			const std::string& matName = kv.first;
			const std::vector<uint32_t>& idxs = kv.second;
			if (idxs.empty()) continue;
			engine::render::MeshHandle mh = m_assetRegistry.CreateMeshFromData(
				cpu->vertices.data(), static_cast<uint32_t>(cpu->vertices.size()),
				idxs.data(), static_cast<uint32_t>(idxs.size()));
			if (!mh.IsValid()) continue;

			uint32_t matIdx = materialCache.GetDefaultMaterialIndex();
			uint32_t hlIdx  = matIdx;
			auto cached = m_trimMatCache.find(matName);
			if (cached != m_trimMatCache.end())
			{
				matIdx = cached->second.first;
				hlIdx  = cached->second.second;
			}
			else
			{
				const engine::render::staticmesh::StaticSubMesh* rep = repByMat[matName];
				if (rep && !rep->baseColorUri.empty())
				{
					const engine::render::TextureHandle bc =
						m_assetRegistry.LoadTexture(meshDir + rep->baseColorUri, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (!rep->normalUri.empty())
						{
							const engine::render::TextureHandle n = m_assetRegistry.LoadTexture(meshDir + rep->normalUri, false);
							if (n.IsValid()) mat.normal = n;
						}
						if (!rep->ormUri.empty())
						{
							const engine::render::TextureHandle o = m_assetRegistry.LoadTexture(meshDir + rep->ormUri, false);
							if (o.IsValid()) mat.orm = o;
						}
						if (rep->alphaCutout)
							mat.flags = engine::render::MaterialFlags::AlphaCutout;
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						mat.flags = static_cast<engine::render::MaterialFlags>(
							static_cast<uint32_t>(mat.flags) | static_cast<uint32_t>(engine::render::MaterialFlags::Highlight));
						hlIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
					}
				}
				else
				{
					// Pièces de structure (murs, sols, coins…) : le glTF du projet
					// ne référence aucune texture, mais a GARDÉ le nom du matériau
					// (MI_Plaster, MI_WoodTrim, MI_Brick…). On retrouve la texture du
					// kit « Medieval Village » (copiée dans meshes/props/) par ce nom.
					// Priorité au NOM DU MATÉRIAU (sinon un mur « Wall_Plaster_WoodGrid »
					// donnerait du plâtre à sa partie bois) ; le chemin ne sert qu'à
					// distinguer la brique générique (rouge / irrégulière).
					// Règles partagées avec le rendu instancié du monde (PropMaterialRules.h).
					engine::world::scatter::TrimTextureSet trim;
					engine::world::scatter::ResolveTrimTextures(matName, meshPath, trim);
					const char* baseTex = trim.baseColor;
					const char* nrmTex = trim.normal;
					const char* ormTex = trim.orm;

					engine::render::TextureHandle bc;
					if (baseTex) bc = m_assetRegistry.LoadTexture(meshDir + baseTex, /*useSrgb*/ true);
					if (bc.IsValid())
					{
						engine::render::Material mat{};
						mat.baseColor = bc;
						if (nrmTex) { const auto n = m_assetRegistry.LoadTexture(meshDir + nrmTex, false); if (n.IsValid()) mat.normal = n; }
						if (ormTex) { const auto o = m_assetRegistry.LoadTexture(meshDir + ormTex, false); if (o.IsValid()) mat.orm = o; }
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						hlIdx = matIdx;
					}
					else
					{
						// Repli (matériau non mappé OU texture absente) : couleur plate
						// plutôt que blanc. Et on LOGGE le matériau pour pouvoir le
						// mapper précisément ensuite (diagnostic des pièces blanches).
						uint8_t cr = 200, cg = 195, cb = 185;
						engine::world::scatter::FallbackFlatColor(matName, meshPath, cr, cg, cb);
						engine::render::Material mat{};
						mat.baseColor = SolidColorTexture(cr, cg, cb);
						matIdx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
						hlIdx = matIdx;
						LOG_WARN(Render, "[Buildings] materiau sans texture '{}' (mesh '{}', baseTex tente='{}') -> couleur plate ({},{},{})",
							matName, meshPath, baseTex ? baseTex : "(aucun)", cr, cg, cb);
					}
				}
				m_trimMatCache[matName] = { matIdx, hlIdx };
			}

			PropPart part;
			part.mesh = mh;
			part.materialIndex = matIdx;
			part.highlightMaterialIndex = hlIdx;
			prop.parts.push_back(part);
		}

		prop.interactableIndex = interactableIndex;
		prop.worldPos = engine::math::Vec3{ cx, minY, cz };
		if (!prop.parts.empty())
		{
			if (solid)
				RegisterBuildingPieceCollision(meshPath, worldM, cx, cz, radiusAuto, minY, maxY, collisionRadius, m_worldCollider);
			m_props.push_back(std::move(prop));
		}
	}

	// Enregistre la collision d'une pièce de bâtiment (mur, sol, escalier…)
	// posée par la matrice monde `worldM` : boîtes orientées du catalogue
	// building_pieces.json si la pièce y figure, sinon cylindre englobant de
	// « mur » (barrière latérale pure). Partagé par le rendu historique
	// (BuildPropFromMeshMatrix) et le rendu instancié du monde (LoadScatterWorld).
	// \param cx,cz,radiusAuto  Empreinte XZ (centre + rayon auto) du mesh posé (m).
	// \param minY,maxY         Bornes verticales monde du mesh posé (m).
	// \param collisionRadius   Rayon imposé (m) ; 0 = radiusAuto.
	// \param target          Collisionneur qui reçoit les primitives (m_worldCollider, ou
	//                         celui d'une cellule du monde « Royaumes », en coordonnées locales).
	// Effet de bord : ajoute des primitives dans \p target (index spatial invalidé).
	void Engine::RegisterBuildingPieceCollision(const std::string& meshPath, const engine::math::Mat4& worldM,
		float cx, float cz, float radiusAuto, float minY, float maxY, float collisionRadius,
		engine::gameplay::CompositeWorldCollider& target)
	{
		float radius = collisionRadius > 0.0f ? collisionRadius : radiusAuto;
		if (radius < 0.05f) radius = 0.5f;
		// Pièces de bâtiment : une « door » devient un passage franchissable,
		// un « escalier » une surface gravissable (cf. PropCylinder). Détection
		// par le nom du mesh, insensible à la casse. Les murs restent pleins.
		std::string meshLower = meshPath;
		for (char& ch : meshLower)
		{
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
		}
		// Basename du mesh (sans dossier ni extension) pour le catalogue.
		std::string meshBase = meshPath;
		{
			const auto sl = meshBase.find_last_of("/\\");
			if (sl != std::string::npos) meshBase = meshBase.substr(sl + 1);
			const auto dot = meshBase.find_last_of('.');
			if (dot != std::string::npos) meshBase = meshBase.substr(0, dot);
		}

		const auto* piece = m_buildingCollisionCatalog.Lookup(meshBase);
		const bool isStair = (meshLower.find("escalier") != std::string::npos)
			|| (meshLower.find("stair") != std::string::npos);
		if (piece == nullptr)
		{
			// Fallback : comportement actuel (cylindre englobant), rétro-compatible.
			// Pièce de bâtiment = MUR (barrière latérale pure, pas de dessus
			// marchable), SAUF un escalier qui doit garder son dessus pour être
			// gravi. Évite le « vol » : la sonde anti-encastrement ne peut plus
			// remonter le perso au sommet du gros cylindre englobant de la pièce.
			engine::gameplay::PropCylinder cyl{ cx, cz, radius, minY, maxY };
			cyl.passable = meshLower.find("door") != std::string::npos;
			cyl.stair = isStair;
			cyl.wall = !cyl.stair;
			target.AddCylinder(cyl);
		}
		else if (!piece->passable)
		{
			// Boîtes du catalogue : transformer chaque boîte LOCALE par la
			// matrice monde de la pièce (worldM, column-major M[col*4+row]).
			// Colonnes 3x3 = axes + échelle.
			const float* M = worldM.m;
			const engine::math::Vec3 colX{ M[0], M[1], M[2] };
			const engine::math::Vec3 colY{ M[4], M[5], M[6] };
			const engine::math::Vec3 colZ{ M[8], M[9], M[10] };
			const float sX = std::sqrt(colX.x*colX.x + colX.y*colX.y + colX.z*colX.z);
			const float sY = std::sqrt(colY.x*colY.x + colY.y*colY.y + colY.z*colY.z);
			const float sZ = std::sqrt(colZ.x*colZ.x + colZ.y*colZ.y + colZ.z*colZ.z);
			for (const auto& lb : piece->boxes)
			{
				// Centre local -> monde.
				const float wx = M[0]*lb.cx + M[4]*lb.cy + M[8]*lb.cz  + M[12];
				const float wy = M[1]*lb.cx + M[5]*lb.cy + M[9]*lb.cz  + M[13];
				const float wz = M[2]*lb.cx + M[6]*lb.cy + M[10]*lb.cz + M[14];
				engine::gameplay::PropBox box;
				box.cx = wx; box.cz = wz;
				box.halfX = lb.hx * sX; box.halfZ = lb.hz * sZ;
				box.axisX = (sX > 1e-6f) ? engine::math::Vec3{ colX.x/sX, 0.0f, colX.z/sX } : engine::math::Vec3{ 1, 0, 0 };
				box.axisZ = (sZ > 1e-6f) ? engine::math::Vec3{ colZ.x/sZ, 0.0f, colZ.z/sZ } : engine::math::Vec3{ 0, 0, 1 };
				box.loY = wy - lb.hy * sY; box.hiY = wy + lb.hy * sY;
				box.stair = isStair;
				// Roadmap-5 (2026-07-19) — dessus marchable pour les
				// sols (tag JSON "walkable") et les marches d'escalier ;
				// les murs restent des barrières latérales pures.
				box.walkableTop = piece->walkable || isStair;
				box.wall = !box.walkableTop;
				target.AddBox(box);
			}
		}
		// piece->passable : on n'ajoute rien (battant franchissable).
	}

	bool Engine::EnsureImpostorAtlas(const std::string& meshPath)
	{
		if (!m_impostorEnabled || meshPath.empty()) return false;
		auto it = m_impostorAtlases.find(meshPath);
		if (it != m_impostorAtlases.end())
			return it->second.IsValid();

		// Chemin de l'atlas : à côté du .gltf, même nom, extension `.mipo`.
		// Ex. meshes/props/DeadTree_2.gltf -> <content>/meshes/props/DeadTree_2.mipo.
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		std::string base = meshPath;
		const auto dot = base.find_last_of('.');
		if (dot != std::string::npos) base = base.substr(0, dot);
		const std::string mipoPath = contentRoot + "/" + base + ".mipo";

		engine::render::ImpostorAsset asset;
		std::string err;
		const bool ok = asset.LoadFromFile(mipoPath, m_assetRegistry, err);
		if (!ok)
		{
			// Absence d'atlas = cas normal (tous les meshes n'en ont pas) -> Debug, pas Warn.
			LOG_DEBUG(Render, "[Impostor] pas d'atlas pour '{}' ({})", meshPath, err);
		}
		// On insère même un asset invalide pour mémoriser l'échec (évite de retenter
		// le chargement disque à chaque frame/prop partageant ce mesh).
		auto [insIt, inserted] = m_impostorAtlases.emplace(meshPath, std::move(asset));
		(void)inserted;
		return insIt->second.IsValid();
	}

	void Engine::RecordPropsGeometry(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		if (m_props.empty() || !m_pipeline) return;
		auto& geom = m_pipeline->GetGeometryPass();
		// loadOp=LOAD requis pour se superposer au GBuffer (terrain + avatar) sans clear.
		if (!geom.HasLoadPass()) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		// Culling de distance des props de DECOR. Le profiler (M18.1) montre que la
		// passe Geometry GPU (overdraw de feuillages a 6,2 Mpx) est le poste de cout
		// n1 en foret dense : au-dela de ce rayon, on ne dessine plus le decor lointain
		// VISIBLE. Distance lue a CHAQUE frame depuis config.json (reglage a chaud, sans
		// recompilation) ; repli 80 m si la cle est absente (sinon le correctif perf
		// serait silencieusement desactive sur une config plus ancienne). <= 0 =>
		// desactive. Les INTERACTIBLES (coffre, caisses, PNJ) sont toujours dessines.
		const engine::math::Vec3 camPos = rs.camera.position;
		const float cullDist = static_cast<float>(m_cfg.GetDouble("world.props.cull_distance_m", 80.0))
			* (m_cellWorld ? m_viewDistance.scatterScale : 1.0f);
		const float cullDist2 = cullDist * cullDist;
		const bool cullEnabled = cullDist > 0.0f;

		// M45.5 — bascule mesh -> impostor. GATED world.impostor.enabled : par défaut
		// false => `impostorActive` reste false et le chemin ci-dessous est STRICTEMENT
		// l'historique (aucun changement de rendu). Quand actif : un prop de DÉCOR
		// (interactableIndex<0) dont la distance XZ ∈ [impostorDist, cullDist] ET qui a
		// un atlas chargé est rendu en impostor au lieu de son mesh ; en deçà
		// d'impostorDist, mesh normal (inchangé).
		const bool impostorPassValid = m_pipeline->GetImpostorPass().IsValid();
		const bool impostorActive = m_impostorEnabled && impostorPassValid;
		// Seuil de bascule mesh -> impostor : centralisé dans LodConfig (M45.5b),
		// plus de duplication avec world.impostor.distance_m. Gated : si inactif,
		// D = +inf => aucune des branches impostor ne s'active (historique strict).
		const float impostorDist = impostorActive
			? m_lodConfig.GetImpostorDistanceMax() : 1e30f;
		// Largeur de la bande de cross-fade (m) juste après le seuil : le mesh et
		// l'impostor coexistent, l'impostor monte en dither 0->1 (anti-popping).
		const float impostorBand = impostorActive
			? static_cast<float>(m_cfg.GetDouble("world.impostor.fade_band_m", 10.0)) : 0.0f;
		const float impostorDist2 = impostorDist * impostorDist;
		// Borne haute de la bande de fondu (au-delà : impostor seul, fadeAlpha=1).
		const float impostorFadeEnd = impostorDist + impostorBand;
		// Instances d'impostors accumulées, groupées par chemin de mesh (= clé atlas).
		std::unordered_map<std::string, std::vector<engine::render::ImpostorInstance>> impostorBatches;

		int drawn = 0, culled = 0, impostored = 0;
		// Boucle indexée : propIndex sert de clé FadeFor (anti-occlusion caméra). Cet
		// index DOIT correspondre à l'id d'occulteur poussé dans la boucle Update
		// (même conteneur m_props, même ordre → même frame).
		for (std::size_t propIndex = 0; propIndex < m_props.size(); ++propIndex)
		{
			const auto& prop = m_props[propIndex];
			const float dxp = prop.worldPos.x - camPos.x;
			const float dzp = prop.worldPos.z - camPos.z;
			const float dist2 = dxp * dxp + dzp * dzp;

			if (cullEnabled && prop.interactableIndex < 0)
			{
				if (dist2 > cullDist2)
					{ ++culled; continue; }  // decor trop loin -> non dessine
			}

			// Cross-fade mesh <-> impostor (M45.5b, décor uniquement, atlas dispo).
			// Trois régimes selon la distance XZ `dist` :
			//   dist < D                : mesh seul (drawImpostorOnly=false, pas d'impostor).
			//   D <= dist < D+band      : mesh PLEIN + impostor en dither montant (fadeAlpha
			//                             = smoothstep(D, D+band, dist)) -> aucun pop.
			//   dist >= D+band          : impostor SEUL (fadeAlpha=1), pas de mesh.
			// Gated : si impostorActive est false, D = +inf => on n'entre jamais ici.
			bool drawImpostorOnly = false;
			if (impostorActive && prop.interactableIndex < 0 && dist2 >= impostorDist2)
			{
				if (EnsureImpostorAtlas(prop.meshPath))
				{
					const float dist = std::sqrt(dist2);
					// smoothstep(D, D+band, dist) : 0 au seuil, 1 en fin de bande.
					float fade = 1.0f;
					if (impostorBand > 0.0f && dist < impostorFadeEnd)
					{
						const float t = std::clamp((dist - impostorDist) / impostorBand, 0.0f, 1.0f);
						fade = t * t * (3.0f - 2.0f * t); // smoothstep
					}
					engine::render::ImpostorInstance inst;
					inst.worldPos[0] = prop.impostorCenter.x;
					inst.worldPos[1] = prop.impostorCenter.y;
					inst.worldPos[2] = prop.impostorCenter.z;
					inst.radius      = prop.impostorRadius;
					inst.fadeAlpha   = fade;
					impostorBatches[prop.meshPath].push_back(inst);
					++impostored;
					// Hors de la bande de fondu : l'impostor REMPLACE le mesh (pas de mesh).
					// Dans la bande : on dessine AUSSI le mesh par-dessous (drawImpostorOnly
					// reste false) pour que l'impostor monte en fondu sans pop.
					if (dist >= impostorFadeEnd)
						drawImpostorOnly = true;
				}
				// Pas d'atlas pour ce mesh -> fallback mesh normal (dessin plus bas).
			}
			if (drawImpostorOnly)
				continue; // NE PAS dessiner le mesh : l'impostor le remplace entièrement.

			++drawn;
			// Surbrillance (chantier C) : si ce prop est l'interactible a portee, on
			// dessine ses parties avec la variante de materiau Highlight (teinte).
			const bool highlight =
				(prop.interactableIndex >= 0 && prop.interactableIndex == m_interactableInRange);
			// Fondu d'occlusion de ce prop (1.0 = opaque si non occulteur / inconnu).
			const float occFade =
				m_cameraOcclusionFade.FadeFor(static_cast<std::uint32_t>(propIndex));
			for (const auto& part : prop.parts)
			{
				engine::render::MeshAsset* mesh = part.mesh.Get();
				if (mesh == nullptr) continue;
				geom.Record(
					m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
					m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
					rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
					0u,
					materialCache.GetDescriptorSet(),
					prop.modelMatrix.m,
					highlight ? part.highlightMaterialIndex : part.materialIndex,
					true,
					occFade);
			}
		}

		// M45.5 — dessine les impostors accumulés DANS le GBuffer, via le render pass
		// loadOp=LOAD de la GeometryPass (RecordTerrainChunkBatch ouvre exactement ce
		// render pass + framebuffer + viewport/scissor). Un RecordInstances par atlas
		// distinct (le descriptor de l'ImpostorPass est réécrit à chaque appel).
		if (impostorActive && !impostorBatches.empty())
		{
			auto& impostorPass = m_pipeline->GetImpostorPass();
			geom.RecordTerrainChunkBatch(
				m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
				m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
				[this, &impostorPass, &impostorBatches, &rs](VkCommandBuffer innerCmd) {
					const float camPos3[3] = {
						rs.camera.position.x, rs.camera.position.y, rs.camera.position.z };
					// Échelle de parallax single-step (frag v2) ; gated par enabled.
					const float parallaxScale = static_cast<float>(
						m_cfg.GetDouble("world.impostor.parallax_scale", 0.08));
					for (auto& kv : impostorBatches)
					{
						auto atlasIt = m_impostorAtlases.find(kv.first);
						if (atlasIt == m_impostorAtlases.end() || !atlasIt->second.IsValid())
							continue;
						const engine::render::ImpostorAsset& atlas = atlasIt->second;
						engine::render::TextureAsset* albedo = atlas.Albedo().Get();
						engine::render::TextureAsset* normal = atlas.Normal().Get();
						engine::render::TextureAsset* orm    = atlas.Orm().Get();
						if (albedo == nullptr || normal == nullptr || orm == nullptr
							|| albedo->view == VK_NULL_HANDLE || normal->view == VK_NULL_HANDLE
							|| orm->view == VK_NULL_HANDLE)
							continue;
						const VkSampler samp = impostorPass.GetSampler();
						impostorPass.RecordInstances(
							m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
							kv.second.data(), static_cast<uint32_t>(kv.second.size()),
							albedo->view, samp, normal->view, samp, orm->view, samp,
							rs.viewProjMatrix.m, rs.prevViewProjMatrix.m, camPos3,
							atlas.Info().viewsPerAxis, atlas.Info().tileSize, parallaxScale);
					}
				});
		}

		// Diag throttle (1/60 frames) : visible uniquement en log.level=Debug.
		if ((m_currentFrame % 60) == 0)
			LOG_DEBUG(Render, "[Props] cull_dist={:.0f}m dessines={} impostors={} coupes={} (total={})",
			          cullDist, drawn, impostored, culled, static_cast<int>(m_props.size()));
	}

	void Engine::RecordRemoteAvatars(VkCommandBuffer cmd, engine::render::Registry& reg,
	                                 const engine::RenderState& rs)
	{
		(void)reg; // reg n'est plus utilisé depuis le passage à SkinnedRenderer (TD.7).
		if (!m_pipeline) return;
		const std::vector<engine::client::UIRemoteEntity>& remotes = m_uiModelBinding.GetModel().remoteEntities;
		if (remotes.empty()) return;
		// TD.7 — bascule du placeholder (GeometryPass, mesh cube statique) au rendu skinné
		// (SkinnedRenderer, mesh humanoïde + animation dérivée de la vélocité serveur). Les
		// pré-requis : (1) SkinnedRenderer initialisé + ses meshes humains chargés ;
		// (2) GeometryPass dispose d'un load pass (on partage le render pass pour superposer
		// au G-buffer terrain + props + avatar local sans clear).
		static bool diagLoggedReason = false;
		if (!m_skinnedRenderer.IsValid())
		{
			if (!diagLoggedReason)
			{
				LOG_WARN(Render, "[RecordRemoteAvatars] SKIP : SkinnedRenderer non initialise (remotes={})", remotes.size());
				diagLoggedReason = true;
			}
			return;
		}
		auto& geom = m_pipeline->GetGeometryPass();
		if (!geom.HasLoadPass())
		{
			if (!diagLoggedReason)
			{
				LOG_WARN(Render, "[RecordRemoteAvatars] SKIP : GeometryPass sans load pass (remotes={})", remotes.size());
				diagLoggedReason = true;
			}
			return;
		}
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		const float nowSec = EngineNowSec();
		// TD.7 — seuils dérivation Idle/Walk depuis la vélocité serveur. Le seuil bas est
		// volontairement laxiste (0.1 m/s) pour éviter de flicker entre Idle et Walk autour
		// du stationnement (déplacements d'inertie / sub-tick). Au-delà, on joue Walk en
		// boucle. Pas encore de distinction Walk/Run/Sprint côté wire — TD.7 V1 suffit.
		constexpr float kIdleSpeedThresholdMps = 0.1f;
		static bool diagLoggedSuccess = false;
		// Anomalie perf (retour de test 2026-07-20) : ~26 avatars skinnés (65 os
		// chacun) échantillonnés + dessinés CHAQUE frame faisaient chuter 60→12 fps,
		// et dépassaient la limite sûre du ring SSBO (~16 draws skinnés/frame, cf.
		// SkinnedRenderer.h — au-delà le ring se réécrit pendant une frame en vol).
		// Triple garde-fou, distances configurables :
		//   1) cull : au-delà de render_distance_m, pas de mesh (labels flottants et
		//      points radar suivent leur propre chemin et restent visibles) ;
		//   2) budget : au plus max_skinned_draws avatars dessinés par frame, les
		//      PLUS PROCHES d'abord (tri par distance caméra croissante) ;
		//   3) LOD d'animation : au-delà de anim_lod_distance_m, la pose n'est
		//      ré-échantillonnée qu'à ~10 Hz (pose cachée entre deux, cf. boucle).
		const float remoteRenderDist = static_cast<float>(
			m_cfg.GetDouble("client.remote_avatars.render_distance_m", 60.0));
		const float animLodDist = static_cast<float>(
			m_cfg.GetDouble("client.remote_avatars.anim_lod_distance_m", 25.0));
		// 32 (monde « Royaumes », villes denses) : borné par SkinnedRenderer::kFrameSlots
		// (128 slots = 64 draws/frame à FIF=2, marge pour l'avatar local et l'aperçu).
		const size_t maxSkinnedDraws = std::min<size_t>(48u, static_cast<size_t>(
			m_cfg.GetDouble("client.remote_avatars.max_skinned_draws", 32.0)));
		// Seuls les avatars DANS le champ de la caméra consomment le budget : un PNJ
		// dans le dos du joueur n'est ni animé ni dessiné (sa plaque suit son chemin).
		const engine::world::scatter::Frustum avatarFrustum =
			engine::world::scatter::Frustum::FromViewProj(rs.viewProjMatrix.m);
		const float remoteRenderDist2 = remoteRenderDist * remoteRenderDist;
		// Passe 1 — candidats triés par distance. Les entités sans mesh (nodes de
		// récolte, mobs morts, loot bags) sont écartées ICI pour ne pas consommer
		// de slot du budget de draws.
		struct RemoteDrawCandidate { size_t index; float dist2; };
		std::vector<RemoteDrawCandidate> candidates;
		candidates.reserve(remotes.size());
		for (size_t ri = 0; ri < remotes.size(); ++ri)
		{
			const engine::client::UIRemoteEntity& rc = remotes[ri];
			if (rc.playerClientId == 0u)
			{
				if (rc.archetypeId == 0u) continue;                                          // loot bag : pipeline dédié
				if (rc.archetypeId >= engine::server::kGatheringNodeArchetypeBase) continue; // node de récolte : label seul
				if ((rc.stateFlags & 1u) != 0u) continue;                                    // mob mort en attente de despawn
			}
			float cx = rc.positionX, cy = rc.positionY, cz = rc.positionZ;
			const auto csit = m_remoteSmoothed.find(rc.entityId);
			if (csit != m_remoteSmoothed.end() && csit->second.valid)
			{
				cx = csit->second.x; cy = csit->second.y; cz = csit->second.z;
			}
			const float ddx = cx - rs.camera.position.x;
			const float ddy = cy - rs.camera.position.y;
			const float ddz = cz - rs.camera.position.z;
			const float dist2 = ddx * ddx + ddy * ddy + ddz * ddz;
			if (dist2 > remoteRenderDist2)
				continue;
			// Sphère englobante d'un humanoïde (centre ~1 m au-dessus des pieds, rayon 1,6 m).
			if (!avatarFrustum.SphereVisible(engine::math::Vec3{ cx, cy + 1.0f, cz }, 1.6f))
				continue;
			candidates.push_back(RemoteDrawCandidate{ ri, dist2 });
		}
		std::sort(candidates.begin(), candidates.end(),
			[](const RemoteDrawCandidate& a, const RemoteDrawCandidate& b) { return a.dist2 < b.dist2; });
		if (candidates.size() > maxSkinnedDraws)
			candidates.resize(maxSkinnedDraws);
		for (const RemoteDrawCandidate& cand : candidates)
		{
			const engine::client::UIRemoteEntity& re = remotes[cand.index];
			// remoteEntities contient TOUTES les entités distantes (joueurs + mobs + lootbags).
			// Combat SP1 : les mobs (archetypeId != 0) réutilisent un mesh de race
			// existant déclaré par le catalogue (cf. CreatureCatalog) ; seuls les loot
			// bags (les deux ids à 0) gardent leur pipeline dédié (pas de mesh humanoïde).
			// Attention budget : chaque mob = 1 draw skinné dans le ring SSBO partagé
			// (kFrameSlots=128, cf. SkinnedRenderer.h, soit 64 draws/frame à FIF=2) ;
			// maxSkinnedDraws est plafonné à 48 pour garder la marge.
			std::string meshRace;
			std::string gender;
			float meshScale = 1.0f;
			if (re.playerClientId != 0u)
			{
				// TD.6 — genre du personnage propagé par le snapshot. Fallback "male" si vide
				// (master sans migration 0067 ou avatar legacy). Récupère le mesh correspondant
				// dans le registre m_raceMeshes (clé "humains|male" / "humains|female").
				meshRace = "humains";
				gender = (re.gender == "male" || re.gender == "female") ? re.gender : std::string{"male"};
			}
			else if (re.archetypeId >= engine::server::kGatheringNodeArchetypeBase)
			{
				// Métiers SP1 — node de récolte : pas de mesh (label flottant seul).
				continue;
			}
			else if (re.archetypeId != 0u)
			{
				// Mob mort en attente de despawn : pas de rendu (kEntityStateDead).
				if ((re.stateFlags & 1u) != 0u)
					continue;
				const engine::client::CreatureAppearance* appearance = m_creatureCatalog.Find(re.archetypeId);
				meshRace = (appearance != nullptr && !appearance->meshKey.empty())
					? appearance->meshKey
					: std::string{"humains"};
				// Monde vivant : villageoises, sentinelles… (variante Female_ si chargée).
				gender = (appearance != nullptr) ? appearance->gender : std::string{"male"};
				meshScale = (appearance != nullptr) ? appearance->scale : 1.0f;
			}
			else
			{
				continue; // loot bag : pipeline dédié, pas de mesh humanoïde
			}
			engine::render::skinned::SkinnedMesh* remoteMesh = GetRaceMesh(meshRace, gender);
			if (remoteMesh == nullptr && re.archetypeId != 0u)
			{
				// Fallback mob : la race du catalogue n'est pas chargée dans
				// m_raceMeshes (asset absent) → on retombe sur le mesh humain.
				remoteMesh = GetRaceMesh("humains", "male");
			}
			if (remoteMesh == nullptr)
			{
				// Aucun mesh chargé pour cette combo race+genre → on saute. Pas de fallback
				// vers le cube placeholder ici : si le mesh humain n'est pas chargé, l'avatar
				// local non plus ne s'afficherait pas correctement.
				continue;
			}
			// TD.3 : position lissée (interpolation, cf. UpdateGameplayNet) ; repli sur la
			// position snapshot brute si pas encore d'état lissé (graceful).
			float px = re.positionX, py = re.positionY, pz = re.positionZ, yaw = re.yawRadians;
			const auto sit = m_remoteSmoothed.find(re.entityId);
			if (sit != m_remoteSmoothed.end() && sit->second.valid)
			{
				px = sit->second.x; py = sit->second.y; pz = sit->second.z; yaw = sit->second.yaw;
			}
			// Combat SP1 fix — mobs : le serveur réplique le Y BRUT du spawner
			// (souvent 0.0, jamais collé au terrain — pas de heightfield serveur).
			// Avec l'offset -0.9 « centre → pieds » ci-dessous, le mesh se dessinait
			// SOUS le terrain (sanglier invisible, plaque flottante). Snap visuel au
			// sol client : ResolveRemoteDisplayCenterY renvoie sol + 0.9 pour les
			// non-joueurs, que le -0.9 ramène exactement au niveau des pieds.
			py = ResolveRemoteDisplayCenterY(re.playerClientId != 0u, py, px, pz);
			// TD.7 — état d'animation par avatar distant. Crée l'entrée à la première frame
			// où on voit cet entityId. L'horloge utilisée pour Sample() / Play() est la même
			// (nowSec) pour tous les avatars : crossfade et boucle restent en phase.
			RemoteAvatarAnim& anim = m_remoteAnims[re.entityId];
			// TD.8 — clip dérivé de l'état d'animation RÉPLIQUÉ (emote/roulade/run/sprint/saut/…),
			// plus seulement Idle/Walk dérivé de la vélocité. C'est ce qui rend /dance et la
			// roulade visibles aux autres joueurs.
			const float speedXZ = std::sqrt(re.velocityX * re.velocityX + re.velocityZ * re.velocityZ);
			AvatarLocomotionState effState = FromWireAnimState(re.animationState);
			// Repli : état Idle reçu mais l'avatar glisse encore (latence de propagation de
			// l'état) → on joue Walk pour éviter une pose Idle qui dérape.
			if (effState == AvatarLocomotionState::Idle && speedXZ >= kIdleSpeedThresholdMps)
				effState = AvatarLocomotionState::Walk;
			// L'état Emote ne désigne pas un clip unique : plusieurs emotes existent
			// (cf. kEmotes : /dance, /sit, /talk, /torch, /kneel, /sittalk, /push) mais le wire
			// ne porte pas le rôle précis (1 octet d'état seulement). V1 : on joue l'emote
			// primaire "Dance" pour TOUTES les emotes distantes — /dance est donc exact, les
			// autres s'animent visuellement comme Dance (mieux que l'ancien Idle/Walk figé).
			// Réplication du rôle d'emote exact = travail ultérieur (porter le rôle sur le wire).
			const char* desiredClipName = (effState == AvatarLocomotionState::Emote)
				? "Dance"
				: StateToClipName(effState);
			if (anim.lastClipName != desiredClipName)
			{
				const engine::render::skinned::AnimationClip* clip = remoteMesh->FindClip(desiredClipName);
				if (clip != nullptr)
				{
					anim.crossfade.Play(*clip, /*loops=*/ ClipLoops(effState), nowSec);
					anim.lastClipName = desiredClipName;
				}
				// Si le clip n'existe pas sur ce mesh (asset manquant), on garde l'anim
				// précédente (graceful) — pas de switch, pas de crash.
			}
			// Sample → globals → finals : même chaîne que l'avatar local (cf. ligne ~4723).
			// LOD d'animation : au-delà de animLodDist, la pose n'est ré-échantillonnée
			// qu'à ~10 Hz — entre deux échantillons on redessine la pose cachée
			// (imperceptible à distance, divise le coût CPU par ~6 à 60 fps). En deçà,
			// plein débit comme avant. Un changement de clip repasse par le cache au
			// prochain échantillon (≤ 100 ms), le crossfade lisse la transition.
			const bool animFullRate = cand.dist2 <= animLodDist * animLodDist;
			const bool animCacheUsable = !anim.cachedFinals.empty()
				&& anim.lastSampleSec >= 0.0f
				&& (nowSec - anim.lastSampleSec) < 0.1f;
			if (animFullRate || !animCacheUsable)
			{
				auto locals  = anim.crossfade.Sample(remoteMesh->skeleton, nowSec);
				auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(
					remoteMesh->skeleton, locals);
				anim.cachedFinals = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(
					remoteMesh->skeleton, globals);
				anim.lastSampleSec = nowSec;
			}
			const std::vector<engine::math::Mat4>& finals = anim.cachedFinals;
			// Pieds au sol : le serveur réplique la position « centre capsule » comme pour
			// l'avatar local (feetPos = ccPos.y - 0.9). Même offset ici pour la cohérence.
			// Combat SP1 : échelle uniforme d'archétype (1.0 pour les joueurs) appliquée
			// entre la rotation et l'importTransform — Mat4 n'a pas de helper Scale,
			// on construit la diagonale (column-major) à la main.
			engine::math::Mat4 scaleMat;
			scaleMat.m[0] = meshScale;
			scaleMat.m[5] = meshScale;
			scaleMat.m[10] = meshScale;
			const engine::math::Mat4 model =
				engine::math::Mat4::Translate(engine::math::Vec3{ px, py - 0.9f, pz }) *
				engine::math::Mat4::RotateY(yaw) *
				scaleMat *
				(remoteMesh->importTransform);
			// Matériau : on prend le même matériau habit (m_avatarMaterialId) que l'avatar
			// local. Pas encore de skin par-personnage côté serveur — toute la flotte distante
			// partage le même habit pour V1. Idem pour la peau (m_avatarBodyMaterialId{Male,Female}).
			const uint32_t outfitMaterialIndex = m_avatarMaterialId;
			const uint32_t skinMaterialIndex = (gender == "female")
				? m_avatarBodyMaterialIdFemale
				: m_avatarBodyMaterialIdMale;
			// Per-submesh materials : skin pour les sous-maillages dont le nom matche un body
			// material name (cf. Engine::BuildAvatarSubmeshMaterialIndices pour la logique).
			// Pour V1, on passe un vecteur vide → SkinnedRenderer dessine en un seul draw avec
			// outfitMaterialIndex. C'est cohérent avec l'avatar local pré-mat-mat (régression
			// acceptée pour V1, sera amélioré quand les matériaux par submesh seront décorrélés
			// de l'état local).
			const std::vector<uint32_t> submeshMaterialIndices;
			m_skinnedRenderer.Record(
				m_vkDeviceContext.GetDevice(), cmd,
				m_vkSwapchain.GetExtent(),
				geom.GetRenderPassLoad(),
				VK_NULL_HANDLE,
				rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
				*remoteMesh,
				finals,
				materialCache.GetDescriptorSet(),
				model.m,
				outfitMaterialIndex,
				submeshMaterialIndices,
				skinMaterialIndex,
				m_avatarSkinDepthBiasConstant,
				m_avatarSkinDepthBiasSlope);
			if (!diagLoggedSuccess)
			{
				LOG_INFO(Render,
					"[RecordRemoteAvatars] OK : rendu skinne demarre (remotes={}, premier entity_id={}, gender='{}', clip='{}', mesh.bones={}, skin_mat={})",
					remotes.size(), re.entityId, gender, desiredClipName,
					remoteMesh->skeleton.bones.size(), skinMaterialIndex);
				diagLoggedSuccess = true;
			}
		}
	}

	void Engine::OnResize(int w, int h)
	{
    	LOG_INFO(Platform, "[Resize] OnResize");
		m_width  = w;
		m_height = h;
		m_suboptimalStreak = 0;
		m_suboptimalWidth = w;
		m_suboptimalHeight = h;
		m_taaHistoryInvalid        = true;
		m_swapchainResizeRequested = true;
		if (m_chatUi.IsInitialized())
		{
			(void)m_chatUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
		}
		if (m_authUi.IsInitialized())
		{
			(void)m_authUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
		}
		// SP2 Task 5 — Relayout journal/tracker (QuestUiPresenter). SetViewportSize
		// est un no-op silencieux (log + false) si le presenter n'est pas initialise
		// (echec Init au boot), donc pas besoin d'un garde IsInitialized ici.
		(void)m_questUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
		if (m_gameplayNetInitialized)
		{
			(void)m_shopUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			(void)m_auctionUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			(void)m_invUi.SetViewportSize(static_cast<uint32_t>(std::max(1, w)), static_cast<uint32_t>(std::max(1, h)));
			const engine::client::UIModel& mdl = m_uiModelBinding.GetModel();
			(void)m_shopUi.ApplyModel(mdl, engine::client::UIModelChangeShop);
			(void)m_auctionUi.ApplyModel(mdl, engine::client::UIModelChangeAuction);
			(void)m_invUi.ApplyModel(mdl, engine::client::UIModelChangeInventory);
		}
	}

	void Engine::OnQuit()
	{
		m_quitRequested = true;
	}

	void Engine::ToggleInGamePauseMenu()
	{
		m_inGamePauseMenuVisible = !m_inGamePauseMenuVisible;
		LOG_INFO(Core, "[InGamePauseMenu] toggled visible={}", m_inGamePauseMenuVisible);
	}

	void Engine::ReloadKeybindsFromDisk()
	{
		// Même mécanisme qu'au boot (ApplyUserSettingsOverrides) : Config::LoadFromFile
		// merge (override) les clés du fichier par-dessus m_cfg. Re-charger keybinds.json
		// réécrit donc controls.keybind.* dans m_cfg -> les binds remappés via l'écran
		// d'options prennent effet à la frame suivante (BuildMoveInput lit m_cfg).
		// Fichier absent/malformé -> LoadFromFile renvoie false : on garde les binds
		// courants (jamais de corruption).
		if (m_cfg.LoadFromFile("keybinds.json"))
			LOG_INFO(Core, "[Options] keybinds.json rechargé en jeu (binds clavier live)");
	}

	void Engine::RequestLogoutToLoginScreen()
	{
		LOG_INFO(Core, "[InGamePauseMenu] logout requested -> Login screen");
		// 1) Coupe la connexion gameplay UDP + presenters in-game.
		if (m_gameplayNetInitialized)
		{
			ShutdownGameplayNet();
		}
		// ETAPE3.1 — la clé de session UDP de cet EnterWorld ne doit plus servir.
		m_gameplayUdp.ClearSessionKey();
		m_authUi.ClearPostEnterWorldUdpKey();
		// 2) Reset auth UI : flowComplete repasse a false, phase Login. Le presenter
		//    relancera MasterShardClientFlow au prochain clic Se connecter.
		m_authUi.RequestReturnToLogin();
		// 3) Cache le menu pause (pour qu'il ne reste pas visible sur l'ecran auth).
		m_inGamePauseMenuVisible = false;
		// 4) Oublie le character_id memorise (sinon SavePositionAsync continuerait
		//    a essayer d'envoyer la position d'un perso pour lequel la session est
		//    fermee).
		m_currentCharacterId = 0;
	}

	void Engine::WatchShader(std::string_view relativePath, engine::render::ShaderStage stage, std::string_view defines)
	{
		m_shaderHotReload.Watch(relativePath, stage, defines);
	}

	void Engine::InitGameplayNet()
	{
		if (!m_cfg.GetBool("client.gameplay_udp.enabled", false))
		{
			LOG_INFO(Core, "[GameplayNet] Disabled (set client.gameplay_udp.enabled=true with shard server running)");
			return;
		}

		if (!m_uiModelBinding.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UIModelBinding");
			return;
		}

		if (!m_shopUi.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: ShopUiPresenter");
			m_uiModelBinding.Shutdown();
			return;
		}

		if (!m_auctionUi.Init())
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: AuctionUiPresenter");
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		if (!m_invUi.Init(m_cfg))
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: InventoryUiPresenter");
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		// Combat SP2 — présentateurs combat (non bloquants : un échec d'Init
		// désactive juste le HUD combat, le gameplay réseau reste fonctionnel).
		if (!m_combatHud.Init())
		{
			LOG_WARN(Core, "[GameplayNet] CombatHudPresenter Init FAILED — HUD combat désactivé");
		}
		if (!m_advancedCombat.Init())
		{
			LOG_WARN(Core, "[GameplayNet] AdvancedCombatPresenter Init FAILED — panneau combat désactivé");
		}
		// Combat SP3 — BuffBar (auras du joueur / de la cible).
		if (!m_buffBar.Init())
		{
			LOG_WARN(Core, "[GameplayNet] BuffBarPresenter Init FAILED — BuffBar désactivée");
		}
		// Combat SP4 — FX d'auras (halo aux pieds des entités sous aura).
		if (!m_auraFx.Init())
		{
			LOG_WARN(Core, "[GameplayNet] AuraFXSystem Init FAILED — FX d'auras désactivés");
		}
		// Groupes SP1 — cadres de groupe (M32.2).
		if (!m_partyHud.Init())
		{
			LOG_WARN(Core, "[GameplayNet] PartyHudPresenter Init FAILED — cadres de groupe désactivés");
		}
		// Métiers SP1 — barre de récolte + panneau d'artisanat.
		if (!m_harvestBar.Init())
		{
			LOG_WARN(Core, "[GameplayNet] HarvestCastBarPresenter Init FAILED — barre de récolte désactivée");
		}
		if (!m_craftingUi.Init())
		{
			LOG_WARN(Core, "[GameplayNet] CraftingUiPresenter Init FAILED — panneau d'artisanat désactivé");
		}

		const uint32_t vw = static_cast<uint32_t>(std::max(1, m_width));
		const uint32_t vh = static_cast<uint32_t>(std::max(1, m_height));
		if (!m_shopUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] ShopUiPresenter viewport FAILED — using fallback layout");
		}
		if (!m_auctionUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] AuctionUiPresenter viewport FAILED — using fallback layout");
		}
		if (!m_invUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] InventoryUiPresenter viewport FAILED — using fallback layout");
		}
		// Combat SP2 — layout pixel du HUD combat (cadre cible, log, cooldowns).
		if (!m_combatHud.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] CombatHudPresenter viewport FAILED — using fallback layout");
		}
		// Combat SP3 — layout pixel de la BuffBar.
		if (!m_buffBar.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] BuffBarPresenter viewport FAILED — using fallback layout");
		}
		// Groupes SP1 — layout pixel des cadres de groupe.
		if (!m_partyHud.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] PartyHudPresenter viewport FAILED — using fallback layout");
		}
		// Métiers SP1 — layouts récolte + artisanat.
		if (!m_harvestBar.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] HarvestCastBarPresenter viewport FAILED — using fallback layout");
		}
		if (!m_craftingUi.SetViewportSize(vw, vh))
		{
			LOG_WARN(Core, "[GameplayNet] CraftingUiPresenter viewport FAILED — using fallback layout");
		}

		m_uiObserverHandle = m_uiModelBinding.AddObserver(
			[this](const engine::client::UIModel& model, uint32_t changeMask)
			{
				(void)m_shopUi.ApplyModel(model, changeMask);
				(void)m_auctionUi.ApplyModel(model, changeMask);
				(void)m_invUi.ApplyModel(model, changeMask);
				// Combat SP2 — les présentateurs combat consomment UIModelChangeCombat
				// (combat log, cadre cible, DPS meter) et Stats (PV joueur).
				(void)m_combatHud.ApplyModel(model, changeMask);
				m_advancedCombat.ApplyModel(model, changeMask);
				// Groupes SP1 — cadres de groupe (UIModelChangeParty).
				(void)m_partyHud.ApplyModel(model, changeMask);
				// Métiers SP1 — récolte (UIModelChangeHarvest) + artisanat (Crafting).
				(void)m_harvestBar.ApplyModel(model, changeMask);
				(void)m_craftingUi.ApplyModel(model, changeMask);
				// SP2 Task 5 — journal + tracker (QuestUiPresenter reconstruit sa vue
				// depuis model.quests a chaque changement). Le panneau donneur, lui,
				// lit directement model.giverList depuis QuestImGuiRenderer (pas besoin
				// de passer par le presenter).
				(void)m_questUi.ApplyModel(model, changeMask);
			});
		if (m_uiObserverHandle == 0u)
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UI observer not registered");
			m_invUi.Shutdown();
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		const std::string host = m_cfg.GetString("client.gameplay_udp.host", "127.0.0.1");
		const int64_t portCfg = m_cfg.GetInt("client.gameplay_udp.port", 27015);
		const uint16_t port = static_cast<uint16_t>(
			std::clamp(portCfg, static_cast<int64_t>(1), static_cast<int64_t>(65535)));
		m_gameplayVendorTalkTarget = m_cfg.GetString("client.gameplay_udp.vendor_talk_target", "vendor:1");
		m_gameplayAuctionTalkTarget = m_cfg.GetString("client.gameplay_udp.auction_talk_target", "auction");
		if (!m_gameplayUdp.Init(host, port))
		{
			LOG_ERROR(Core, "[GameplayNet] Init FAILED: UDP connect {}", host);
			(void)m_uiModelBinding.RemoveObserver(m_uiObserverHandle);
			m_uiObserverHandle = 0;
			m_invUi.Shutdown();
			m_auctionUi.Shutdown();
			m_shopUi.Shutdown();
			m_uiModelBinding.Shutdown();
			return;
		}

		const int64_t tickHzCfg = m_cfg.GetInt("client.gameplay_udp.request_tick_hz", 20);
		const uint16_t reqTick = static_cast<uint16_t>(
			std::clamp(tickHzCfg, static_cast<int64_t>(1), static_cast<int64_t>(120)));
		const int64_t snapHzCfg = m_cfg.GetInt("client.gameplay_udp.request_snapshot_hz", 10);
		const uint16_t reqSnap = static_cast<uint16_t>(
			std::clamp(snapHzCfg, static_cast<int64_t>(1), static_cast<int64_t>(60)));
		// Phase 3.7.5 — character_key élargi à uint64. La config stocke un int64_t signé ;
		// on bit-cast pour préserver la valeur uint64 quand le bit 63 serait positionné
		// (reinterpret par bits, pas conversion arithmétique).
		const int64_t charKeyCfg = m_cfg.GetInt("client.gameplay_udp.character_key", 1);
		const uint64_t charKey = (charKeyCfg <= 0)
			? static_cast<uint64_t>(1u)
			: static_cast<uint64_t>(charKeyCfg);
		// ETAPE3.3 — nouvelle session : époque 0 (celle du shard à l'admission), plus
		// de positions envoyées connues, aucune correction antérieure acceptée.
		m_moveEpoch = 0;
		m_moveHistory.Clear();
		m_moveLastCorrectedSeq = m_gameplayInputSeq;
		m_uiModelBinding.ClearMoveCorrection();
		m_aboardBoatEntityId = 0; // v1.0.125 : nouvelle session, à terre
		m_uiModelBinding.ClearBoatAboard();
		m_reliableFailureNotified = false; // ETAPE3.4 : nouvelle session, nouveau canal
		(void)m_gameplayUdp.SendHello(reqTick, reqSnap, charKey);

		m_gameplayNetInitialized = true;
		LOG_INFO(Core,
			"[GameplayNet] Init OK (host={}, port={}, vendor_target='{}', auction_target='{}')",
			host,
			port,
			m_gameplayVendorTalkTarget,
			m_gameplayAuctionTalkTarget);

		// SP2 Task 5 — Panneau donneur (QuestImGuiRenderer) : boutons Accepter/
		// Terminer. Contrairement au callback d'action du dialogue (qui cible
		// m_currentDialogueNpcTargetId), le panneau donneur cible
		// giverList.npcTargetId : il n'apparaît qu'après une réponse
		// QuestGiverList (donc npcTargetId est forcément à jour dans ce contexte).
		if (m_questImGui)
		{
			// Callback partagé Accepter/Rendre (panneau donneur ET boutons injectés
			// dans le dialogue, PR-B) : même logique, npcTargetId = celui du dernier
			// QuestGiverList (à jour dans ce contexte).
			auto giverActionCb = [this](const std::string& questId, uint8_t role)
			{
				if (!m_gameplayNetInitialized)
					return;
				const uint32_t gameplayClientId = m_gameplayUdp.ServerClientId();
				const std::string& npcTargetId = m_uiModelBinding.GetModel().giverList.npcTargetId;
				if (role == 0)
					(void)m_gameplayUdp.SendQuestAcceptRequest(gameplayClientId, questId, npcTargetId);
				else
					(void)m_gameplayUdp.SendQuestTurnInRequest(gameplayClientId, questId, npcTargetId);
			};
			m_questImGui->SetGiverActionCallback(giverActionCb);
			// PR-B — même callback pour les boutons Accepter/Rendre injectés dans la
			// fenêtre de dialogue (acceptation « dans la conversation »).
			if (m_dialogueImGui)
				m_dialogueImGui->SetGiverActionCallback(giverActionCb);
		}
	}

	void Engine::ShutdownGameplayNet()
	{
		if (!m_gameplayNetInitialized)
		{
			return;
		}

		if (m_uiObserverHandle != 0u)
		{
			(void)m_uiModelBinding.RemoveObserver(m_uiObserverHandle);
			m_uiObserverHandle = 0;
		}

		// Combat SP2/SP3/SP4 + Groupes/Métiers SP1 — symétrie d'Init.
		m_craftingUi.Shutdown();
		m_harvestBar.Shutdown();
		m_craftingVisible = false;
		m_partyHud.Shutdown();
		m_selectedAllyEntityId = 0;
		m_auraFx.Shutdown();
		m_buffBar.Shutdown();
		m_spellCooldownUiUntilSec.clear();
		m_advancedCombat.Shutdown();
		m_combatHud.Shutdown();
		m_advancedCombatVisible = false;
		m_attackSendCooldownSec = 0.0f;
		m_outOfRangeHintSec = 0.0f;
		m_rmbClickCandidate = false;
		m_invUi.Shutdown();
		m_auctionUi.Shutdown();
		m_shopUi.Shutdown();
		m_uiModelBinding.Shutdown();
		m_gameplayUdp.Shutdown();
		m_gameplayNetInitialized = false;
		m_pendingSellActive = false;
		m_pendingSellVendorId = 0;
		m_pendingSellItemId = 0;
		m_pendingSellQty = 0;
		m_pendingSellUnitGold = 0;
		m_gameplayVendorTalkTarget.clear();
		m_gameplayAuctionTalkTarget.clear();
		LOG_INFO(Core, "[GameplayNet] Shutdown complete");
	}

	// Refactor B2 (ST1) — Consomme et applique les commandes de réglages stagées par
	// l'écran d'options (AuthUiPresenter). Idempotent : appelable chaque frame.
	// Extrait verbatim de l'ancien bloc gardé par authGateActive dans la boucle de rendu,
	// pour que l'application des options tourne aussi in-game (écran d'options réutilisé).
	void Engine::ApplyConsumedSettingsCommands(bool authGateActive)
	{
		const engine::client::AuthUiPresenter::VideoSettingsCommand videoCmd = m_authUi.ConsumePendingVideoSettings();
		const engine::client::AuthUiPresenter::AudioSettingsCommand audioCmd = m_authUi.ConsumePendingAudioSettings();
		const engine::client::AuthUiPresenter::ControlSettingsCommand controlCmd = m_authUi.ConsumePendingControlSettings();
		const engine::client::AuthUiPresenter::GameSettingsCommand gameCmd = m_authUi.ConsumePendingGameSettings();
		if (videoCmd.applyRequested)
		{
			const bool fullscreenChanged = (videoCmd.fullscreen != m_window.IsFullscreen());
			const bool vsyncChanged = (videoCmd.vsync != m_vsync);
			m_cfg.SetValue("render.fullscreen", videoCmd.fullscreen);
			m_cfg.SetValue("render.vsync", videoCmd.vsync);
			m_cfg.SetValue("render.resolution_width", static_cast<int64_t>(videoCmd.resolutionWidth));
			m_cfg.SetValue("render.resolution_height", static_cast<int64_t>(videoCmd.resolutionHeight));
			m_cfg.SetValue("render.quality_preset", static_cast<int64_t>(videoCmd.qualityPreset));
			ApplyViewDistancePreset();
			m_cfg.SetValue("render.fov", static_cast<double>(videoCmd.fovDegrees));
			m_vsync = videoCmd.vsync;
			if (fullscreenChanged)
			{
				m_window.ToggleFullscreen();
				LOG_INFO(Core, "[Options] Fullscreen applied ({})", videoCmd.fullscreen ? "on" : "off");
			}
			if (vsyncChanged)
			{
				m_swapchainResizeRequested = true;
				LOG_INFO(Core, "[Options] VSync applied ({}) -> swapchain recreate requested", videoCmd.vsync ? "on" : "off");
			}
			int cw = 0, ch = 0;
			m_window.GetClientSize(cw, ch);
			const bool resChanged = (videoCmd.resolutionWidth > 0 && videoCmd.resolutionHeight > 0
				&& (videoCmd.resolutionWidth != cw || videoCmd.resolutionHeight != ch));
			if (resChanged)
			{
				m_swapchainResizeRequested = true;
				LOG_INFO(Core, "[Options] Resolution persisted {}x{} (fenêtre actuelle {}x{} ; redimensionnement natif à brancher)",
					videoCmd.resolutionWidth, videoCmd.resolutionHeight, cw, ch);
			}
			if (!fullscreenChanged && !vsyncChanged && !resChanged)
			{
				LOG_INFO(Core, "[Options] Video apply requested but window flags unchanged (qualité / FOV enregistrés dans la config)");
			}
		}
		if (audioCmd.applyRequested)
		{
			m_cfg.SetValue("audio.master_volume", static_cast<double>(audioCmd.masterVolume));
			m_cfg.SetValue("audio.music_volume", static_cast<double>(audioCmd.musicVolume));
			m_cfg.SetValue("audio.sfx_volume", static_cast<double>(audioCmd.sfxVolume));
			m_cfg.SetValue("audio.ui_volume", static_cast<double>(audioCmd.uiVolume));
			const bool masterOk = m_audioEngine.SetMasterVolume(audioCmd.masterVolume);
			const bool musicOk = m_audioEngine.SetBusVolume("Music", audioCmd.musicVolume);
			const bool sfxOk = m_audioEngine.SetBusVolume("SFX", audioCmd.sfxVolume);
			const bool uiOk = m_audioEngine.SetBusVolume("UI", audioCmd.uiVolume);
			LOG_INFO(Core, "[Options] Audio applied (master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, ok={})",
				audioCmd.masterVolume, audioCmd.musicVolume, audioCmd.sfxVolume, audioCmd.uiVolume,
				(masterOk && musicOk && sfxOk && uiOk) ? "yes" : "partial");
		}
		if (controlCmd.applyRequested)
		{
			m_cfg.SetValue("camera.mouse_sensitivity", static_cast<double>(controlCmd.mouseSensitivity));
			m_cfg.SetValue("controls.invert_y", controlCmd.invertY);
			m_cfg.SetValue("controls.movement_layout", controlCmd.useZqsd ? std::string("zqsd") : std::string("wasd"));
			LOG_INFO(Core, "[Options] Controls applied (sens={:.4f}, invert_y={}, layout={})",
				controlCmd.mouseSensitivity, controlCmd.invertY, controlCmd.useZqsd ? "zqsd" : "wasd");
		}
		if (gameCmd.applyRequested)
		{
			const bool gameplayWasEnabled = m_cfg.GetBool("client.gameplay_udp.enabled", false);
			m_cfg.SetValue("client.gameplay_udp.enabled", gameCmd.gameplayUdpEnabled);
			m_cfg.SetValue("client.allow_insecure_dev", gameCmd.allowInsecureDev);
			m_cfg.SetValue("client.auth_ui.timeout_ms", static_cast<int64_t>(gameCmd.authTimeoutMs));
			// Garde-fou réseau (ST1, étape 4) : on ne (ré)initialise / n'arrête le réseau
			// gameplay QUE pendant l'auth. En jeu (session active), appliquer un réglage
			// "game settings" ne doit PAS couper la session UDP de façon intempestive via
			// ShutdownGameplayNet (ni rouvrir un socket via InitGameplayNet hors handshake).
			// La config est tout de même persistée ci-dessus ; le changement effectif de
			// l'état réseau in-game sera traité par un chemin dédié dans une sous-tâche
			// ultérieure du refactor B2.
			if (authGateActive && gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
			{
				if (gameCmd.gameplayUdpEnabled)
					InitGameplayNet();
				else
					ShutdownGameplayNet();
			}
			else if (gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
			{
				LOG_INFO(Core, "[Options] Game: changement gameplay_udp.enabled={} persisté en config mais état réseau inchangé (hors auth, session préservée)",
					gameCmd.gameplayUdpEnabled);
			}
			LOG_INFO(Core, "[Options] Game applied (gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
				gameCmd.gameplayUdpEnabled, gameCmd.allowInsecureDev, gameCmd.authTimeoutMs);
		}
	}

	void Engine::PumpGameplayPackets()
	{
		if (!m_gameplayNetInitialized || !m_gameplayUdp.IsActive())
		{
			return;
		}

		// ETAPE3.1 — Hello répété (1 s) tant que le shard n'a pas répondu par un Welcome :
		// le Hello part souvent avant que le master ait poussé l'admission et la clé.
		m_gameplayUdp.MaybeResendHello(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count()));

		std::vector<std::vector<std::byte>> packets = m_gameplayUdp.PollIncoming();
		for (std::vector<std::byte>& packet : packets)
		{
			engine::server::MessageKind kind{};
			if (engine::server::PeekMessageKind(packet, kind) && kind == engine::server::MessageKind::Welcome)
			{
				// ADM.1 — nouvelle session de jeu : le shard ne garde aucun pouvoir de MJ.
				m_uiModelBinding.ClearGmState();
				continue;
			}
			// ETAPE5.1 — chat de proximité (Dire, Crier) relayé par le shard : affiché
			// dans le panneau de chat comme les lignes du master. ApplyPacket continue
			// d'alimenter les bulles au-dessus des personnages.
			if (kind == engine::server::MessageKind::ChatRelay)
			{
				engine::server::ChatRelayMessage relay{};
				if (engine::server::DecodeChatRelay(packet, relay))
				{
					engine::net::ChatMessage line{};
					line.timestampUnixMs = relay.timestampUnixMs;
					if (!engine::net::TryDecodeChannelWire(relay.channel, line.channel))
					{
						line.channel = engine::net::ChatChannel::Say;
					}
					line.sender = relay.senderDisplay;
					line.text = relay.text;
					m_chatUi.PushNetworkLine(line);
				}
			}
			(void)m_uiModelBinding.ApplyPacket(packet);
		}

		// Lore v1.0.114 — exploration : message système discret dans le chat (cimetière
		// visité avec compteur, lieu secret découvert, exploit débloqué). Un exploit
		// débloqué relance la liste du master pour l'onglet Exploits.
		for (const engine::server::DiscoveryEventMessage& event : m_uiModelBinding.TakeDiscoveryEvents())
		{
			engine::net::ChatMessage line{};
			line.timestampUnixMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
			line.channel = engine::net::ChatChannel::Server;
			line.sender = "Système";
			line.text = engine::client::FormatDiscoveryEventLine(event);
			m_chatUi.PushNetworkLine(line);
			if (event.kind == engine::server::DiscoveryEventKind::ExploitUnlocked)
				m_exploitsUi.RequestExploitList();
		}

		// ETAPE3.4 — canal fiable en échec (aucun accusé du shard depuis 30 s) : les
		// actions importantes (butin, échange, chat…) ne passent plus. Le joueur est
		// prévenu une fois par session ; le shard coupe la session de son côté.
		if (m_gameplayUdp.ReliableChannelFailed() && !m_reliableFailureNotified)
		{
			m_reliableFailureNotified = true;
			engine::net::ChatMessage line{};
			line.timestampUnixMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
			line.channel = engine::net::ChatChannel::Server;
			line.sender = "Système";
			line.text = "Liaison avec le serveur de jeu perdue : tes actions ne sont plus transmises. Reconnecte-toi.";
			m_chatUi.PushNetworkLine(line);
		}
	}

	void Engine::UpdateGameplayNet(float deltaSeconds)
	{
		(void)deltaSeconds;
		if (!m_gameplayNetInitialized || m_editorEnabled)
		{
			return;
		}

		const uint32_t clientId = m_gameplayUdp.ServerClientId();
		if (clientId == 0u)
		{
			return;
		}

		// Combat SP2 — avance les timers des présentateurs combat (cooldowns HUD,
		// fenêtre DPS) et le throttle local d'envoi d'attaque.
		(void)m_combatHud.Tick(deltaSeconds);
		m_advancedCombat.Tick(deltaSeconds);
		// Métiers SP1 — progression des cast bars récolte/artisanat.
		(void)m_harvestBar.Tick(deltaSeconds);
		(void)m_craftingUi.Tick(deltaSeconds);

		// Correction SP1 — position imposée par le serveur (respawn, rejet
		// anti-triche, téléport) : téléporte le CharacterController AVANT
		// l'envoi du prochain Input — le mouvement client-autoritaire reprend
		// depuis la position imposée au lieu d'écraser la téléportation serveur
		// (c'était le bug du respawn SP2 : « Réapparaître » soignait sur place).
		{
			// Seconde carte (Les Enfers) — le shard a annoncé une autre carte (ZoneChange,
			// canal fiable) : elle devient résidente ici. Le ForcePosition (non fiable) peut
			// arriver avant ou après : sans lui, le joueur est posé au point d'arrivée du
			// ZoneChange ; avec lui, le bloc ci-dessous le place (mêmes coordonnées).
			if (FollowServerCellMap() && !m_uiModelBinding.GetModel().forcedPosition.pending)
			{
				const engine::server::ZoneChangeMessage& arrival = m_uiModelBinding.LastZoneChange();
				EnterCellWorldAt(arrival.spawnPositionX, arrival.spawnPositionZ);
				const float arrivalX = m_renderFrame.ToRenderX(arrival.spawnPositionX);
				const float arrivalZ = m_renderFrame.ToRenderZ(arrival.spawnPositionZ);
				const engine::math::Vec3 arrivalPos{ arrivalX,
					std::max(arrival.spawnPositionY, SampleGroundHeight(arrivalX, arrivalZ) + 0.9f), arrivalZ };
				(void)m_characterController.Init(arrivalPos);
				m_orbitalCameraController.SetTargetPosition(arrivalPos);
				m_lastSyncedPosition = arrivalPos;
				LOG_INFO(Core, "[Engine] Changement de carte : arrivee ({:.1f}, {:.1f}, {:.1f})",
					arrival.spawnPositionX, arrival.spawnPositionY, arrival.spawnPositionZ);
			}
			// v1.0.125 — embarquement (BoatAboard) : le joueur est sur le pont du bateau, en
			// (x, y, z) dans le repère du bateau ; nouvelle époque de mouvement (comme un
			// ForcePosition). Le personnage suit ensuite la pose du bateau (bloc de mouvement).
			const engine::client::UIBoatAboard& boarding = m_uiModelBinding.GetModel().boatAboard;
			if (boarding.pending)
			{
				m_aboardBoatEntityId = boarding.boatEntityId;
				float lx = boarding.localX, lz = boarding.localZ;
				(void)engine::world::boat::ClampToDeck(lx, lz);
				m_aboardLocal = engine::math::Vec3{ lx, engine::world::boat::kDeckY + engine::world::boat::kStandHeight, lz };
				float bx = 0.0f, by = 0.0f, bz = 0.0f, byaw = 0.0f;
				bool bdocked = true;
				if (BoatRenderPose(m_aboardBoatEntityId, bx, by, bz, byaw, bdocked))
				{
					m_aboardLastX = bx; m_aboardLastY = by; m_aboardLastZ = bz; m_aboardLastYaw = byaw;
					m_aboardLastDocked = bdocked;
					double wx = 0.0, wz = 0.0;
					engine::world::boat::LocalToWorld(bx, bz, byaw, lx, lz, wx, wz);
					m_characterController.PlaceOnPlatform(engine::math::Vec3{ static_cast<float>(wx), by + m_aboardLocal.y, static_cast<float>(wz) });
					m_orbitalCameraController.SetTargetPosition(m_characterController.GetPosition());
				}
				m_avatarYaw = std::remainder(m_aboardLastYaw + boarding.localYaw, 6.28318530718f);
				m_moveEpoch = boarding.moveEpoch;
				m_moveHistory.Clear();
				m_moveLastCorrectedSeq = m_gameplayInputSeq;
				LOG_INFO(Core, "[Engine] A bord du bateau {} (pont=({:.1f},{:.1f}))", m_aboardBoatEntityId, lx, lz);
				m_uiModelBinding.ClearBoatAboard();
			}
			const engine::client::UIForcedPosition& forced = m_uiModelBinding.GetModel().forcedPosition;
			if (forced.pending)
			{
				// v1.0.125 — une position imposée (monde) signifie « à terre » (débarquement).
				m_aboardBoatEntityId = 0;
				// Validation v12 — collage au sol : le Y des points de réapparition
				// (data) ou du serveur (sans heightfield) peut être SOUS le terrain
				// local → le joueur réapparaissait sous la carte. On garantit au
				// minimum sol + 0.9 (centre capsule) ; un Y data plus haut (tour,
				// étage) reste respecté.
				// Monde « Royaumes » : position GLOBALE → cellule chargée (rebase si
				// téléportation lointaine) puis repère de rendu.
				float forcedX = forced.x;
				float forcedZ = forced.z;
				if (m_cellWorld)
				{
					EnterCellWorldAt(forced.x, forced.z);
					forcedX = m_renderFrame.ToRenderX(forced.x);
					forcedZ = m_renderFrame.ToRenderZ(forced.z);
				}
				const float forcedGroundY = SampleGroundHeight(forcedX, forcedZ) + 0.9f;
				const engine::math::Vec3 forcedPos{
					forcedX, std::max(forced.y, forcedGroundY), forcedZ };
				(void)m_characterController.Init(forcedPos);
				m_orbitalCameraController.SetTargetPosition(forcedPos);
				m_avatarYaw = forced.yawRadians;
				LOG_INFO(Core,
					"[Engine] Position imposée par le serveur appliquée (pos=({:.1f},{:.1f},{:.1f}), reason={})",
					forced.x, forced.y, forced.z, forced.reason);
				// Validation v12 — un ForcePosition « respawn » prouve la
				// résurrection : on ferme l'écran de mort même si l'événement
				// de résurrection se perdait (UDP) ou si le shardd déployé ne
				// l'émettait pas encore (ceinture-bretelles).
				if (forced.reason == engine::server::kForcePositionReasonRespawn)
				{
					m_uiModelBinding.MarkLocalPlayerResurrected();
				}
				// ETAPE3.3 — nouvelle époque de mouvement : les images d'avant le téléport
				// ne seront plus jamais rejouées, les corrections plus anciennes ignorées.
				m_moveEpoch = forced.moveEpoch;
				m_moveHistory.Clear();
				m_moveLastCorrectedSeq = m_gameplayInputSeq;
				m_uiModelBinding.ClearForcedPosition();
			}
		}

		// ETAPE3.3 (chantier 4.1) — position officielle du shard pour une commande de
		// mouvement : le personnage est décalé de l'écart entre la position officielle et
		// la position envoyée pour cette commande (MoveReconciler) ; sa vitesse, son saut
		// et sa hauteur locale sont conservés, le mouvement fait depuis reste acquis.
		// Correction d'une autre époque ou plus ancienne que la dernière appliquée :
		// ignorée.
		{
			const engine::client::UIMoveCorrection correction = m_uiModelBinding.GetModel().moveCorrection;
			if (correction.pending)
			{
				m_uiModelBinding.ClearMoveCorrection();
				if (correction.moveEpoch == m_moveEpoch && correction.inputSequence > m_moveLastCorrectedSeq
					&& m_aboardBoatEntityId != 0u)
				{
					// v1.0.125 — à bord : position officielle sur le pont (repère du bateau).
					m_moveLastCorrectedSeq = correction.inputSequence;
					engine::gameplay::MoveReconciler::Offset offset{};
					float lx = correction.x, lz = correction.z;
					if (m_moveHistory.ApplyCorrection(correction.inputSequence, correction.x, correction.z, offset))
					{
						lx = m_aboardLocal.x + static_cast<float>(offset.dx);
						lz = m_aboardLocal.z + static_cast<float>(offset.dz);
					}
					(void)engine::world::boat::ClampToDeck(lx, lz);
					m_aboardLocal.x = lx;
					m_aboardLocal.z = lz;
				}
				else if (correction.moveEpoch == m_moveEpoch && correction.inputSequence > m_moveLastCorrectedSeq)
				{
					m_moveLastCorrectedSeq = correction.inputSequence;
					engine::gameplay::MoveReconciler::Offset offset{};
					if (m_moveHistory.ApplyCorrection(correction.inputSequence, correction.x, correction.z, offset))
					{
						m_characterController.Translate(static_cast<float>(offset.dx), static_cast<float>(offset.dz));
						m_orbitalCameraController.SetTargetPosition(m_characterController.GetPosition());
						LOG_INFO(Core, "[Engine] Position officielle appliquée (commande {}, écart=({:.2f},{:.2f}) m)",
							correction.inputSequence, offset.dx, offset.dz);
					}
					else
					{
						// Commande plus connue (historique dépassé) : recalage direct sur la
						// position officielle, hauteur locale gardée (jamais sous le sol).
						const float renderX = m_cellWorld ? m_renderFrame.ToRenderX(correction.x) : correction.x;
						const float renderZ = m_cellWorld ? m_renderFrame.ToRenderZ(correction.z) : correction.z;
						const engine::math::Vec3 current = m_characterController.GetPosition();
						const float groundY = SampleGroundHeight(renderX, renderZ) + 0.9f;
						(void)m_characterController.Init(engine::math::Vec3{ renderX, (std::max)(current.y, groundY), renderZ });
						m_orbitalCameraController.SetTargetPosition(m_characterController.GetPosition());
						LOG_INFO(Core, "[Engine] Position officielle appliquée (commande {} hors historique)",
							correction.inputSequence);
					}
				}
			}
		}
		if (m_attackSendCooldownSec > 0.0f)
		{
			m_attackSendCooldownSec -= deltaSeconds;
		}
		// Validation v12 — fait expirer l'indication « Hors de portee ».
		if (m_outOfRangeHintSec > 0.0f)
		{
			m_outOfRangeHintSec -= deltaSeconds;
		}

		// TC.2 — émet la position + orientation de l'avatar local au shard, à la cadence
		// client.gameplay_udp.request_tick_hz (défaut 20 Hz), avec une séquence monotone.
		// C'est ce qui permet aux autres joueurs de voir bouger ce client (réplication AoI).
		{
			const int64_t hzCfg = m_cfg.GetInt("client.gameplay_udp.request_tick_hz", 20);
			const float hz = static_cast<float>(std::clamp<int64_t>(hzCfg, 1, 120));
			m_gameplayInputAccumSec += deltaSeconds;
			if (m_gameplayInputAccumSec >= (1.0f / hz))
			{
				m_gameplayInputAccumSec = 0.0f;
				const engine::math::Vec3 avatarPos = m_characterController.GetPosition();
				// Monde « Royaumes » : le shard attend des positions GLOBALES.
				// v1.0.125 — à bord : position sur le pont, dans le repère du bateau.
				const bool wireAboard = m_aboardBoatEntityId != 0u;
				const float wireX = wireAboard ? m_aboardLocal.x
					: (m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalX(avatarPos.x)) : avatarPos.x);
				const float wireZ = wireAboard ? m_aboardLocal.z
					: (m_cellWorld ? static_cast<float>(m_renderFrame.ToGlobalZ(avatarPos.z)) : avatarPos.z);
				const float wireY = wireAboard ? m_aboardLocal.y : avatarPos.y;
				// TD.8 — propage l'état d'animation local pour que les autres joueurs voient
				// emotes/roulades/run/sprint/etc. (pas seulement Idle/Walk dérivé de la vélocité).
				// ETAPE3.2 — chaque Input acquitte le dernier snapshot reconstruit.
				// ETAPE3.3 — commande de mouvement : horloge de simulation, instants du
				// dernier saut / de la dernière esquive, époque (dernier ForcePosition).
				engine::client::GameplayUdpClient::MoveCommandFields move{};
				move.clientTimeMs = static_cast<uint32_t>(m_moveClientTimeMs);
				move.lastJumpMs = m_moveLastJumpMs;
				move.lastDodgeMs = m_moveLastDodgeMs;
				move.moveEpoch = m_moveEpoch;
				(void)m_gameplayUdp.SendInput(clientId, ++m_gameplayInputSeq,
					wireX, wireY, wireZ, m_avatarYaw,
					ToWireAnimState(m_avatarLocoState), m_uiModelBinding.LatestSnapshotSeq(), move);
				// ETAPE3.3 — position envoyée, pour mesurer l'écart d'une correction.
				m_moveHistory.RecordCommand(m_gameplayInputSeq, wireX, wireZ);
			}
		}

		// TD.3 — lissage exponentiel des positions des avatars distants vers la cible
		// snapshot (~10 Hz) pour un rendu fluide entre snapshots. RecordRemoteAvatars lit
		// ces valeurs (et retombe sur la position snapshot brute si une entité manque ici).
		{
			const std::vector<engine::client::UIRemoteEntity>& remotes = m_uiModelBinding.GetModel().remoteEntities;
			const float k = std::clamp(deltaSeconds * 12.0f, 0.0f, 1.0f); // ~0.1-0.2 s pour converger
			for (const engine::client::UIRemoteEntity& re : remotes)
			{
				RemoteAvatarSmoothed& s = m_remoteSmoothed[re.entityId];
				if (!s.valid)
				{
					s.x = re.positionX; s.y = re.positionY; s.z = re.positionZ; s.yaw = re.yawRadians; s.valid = true;
				}
				else
				{
					s.x += (re.positionX - s.x) * k;
					s.y += (re.positionY - s.y) * k;
					s.z += (re.positionZ - s.z) * k;
					// Interpolation par le plus COURT chemin angulaire : sans repli dans
					// [-π, π], un PNJ dont le cap traverse ±π tournait d'un tour complet.
					float dyaw = std::remainder(re.yawRadians - s.yaw, 6.28318530718f);
					s.yaw += dyaw * k;
					s.yaw = std::remainder(s.yaw, 6.28318530718f);
				}
			}
			// Purge des entités sorties d'AoI (borne la map ; erase renvoie l'itérateur suivant).
			for (auto it = m_remoteSmoothed.begin(); it != m_remoteSmoothed.end(); )
			{
				bool present = false;
				for (const engine::client::UIRemoteEntity& re : remotes)
					if (re.entityId == it->first) { present = true; break; }
				if (present) ++it; else it = m_remoteSmoothed.erase(it);
			}
			// TD.7 — purge symétrique des états d'animation. Si un avatar sort d'AoI ou se
			// déconnecte, on libère son AnimationCrossfade (sinon m_remoteAnims grossit sans
			// borne pour les entités qu'on a vues une fois).
			for (auto it = m_remoteAnims.begin(); it != m_remoteAnims.end(); )
			{
				bool present = false;
				for (const engine::client::UIRemoteEntity& re : remotes)
					if (re.entityId == it->first) { present = true; break; }
				if (present) ++it; else it = m_remoteAnims.erase(it);
			}
		}

		const float mx = static_cast<float>(m_input.MouseX());
		const float my = static_cast<float>(m_input.MouseY());
		(void)m_invUi.UpdateHover(mx, my);

		const engine::client::UIModel& ui = m_uiModelBinding.GetModel();
		const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();

		if (m_pendingSellActive && !chatBlocks)
		{
			if (m_input.WasPressed(engine::platform::Key::Y))
			{
				(void)m_gameplayUdp.SendShopSellRequest(
					clientId,
					m_pendingSellVendorId,
					m_pendingSellItemId,
					m_pendingSellQty);
				m_pendingSellActive = false;
				LOG_INFO(Core,
					"[GameplayNet] Shop sell confirmed (vendor_id={}, item_id={}, qty={})",
					m_pendingSellVendorId,
					m_pendingSellItemId,
					m_pendingSellQty);
			}
			else if (m_input.WasPressed(engine::platform::Key::N))
			{
				m_pendingSellActive = false;
				LOG_INFO(Core, "[GameplayNet] Shop sell cancelled by player");
			}
			return;
		}

		auto sendAuctionBrowseFromModel = [&]()
		{
			const engine::client::UIModel& m = m_uiModelBinding.GetModel();
			engine::server::AuctionBrowseRequestMessage req{};
			req.clientId = clientId;
			req.minPrice = m.auction.filterMinPrice;
			req.maxPrice = m.auction.filterMaxPrice;
			req.itemIdFilter = m.auction.filterItemId;
			req.sortMode = m.auction.sortMode;
			req.maxRows = engine::server::kMaxAuctionBrowseRowsWire;
			(void)m_gameplayUdp.SendAuctionBrowseRequest(req);
		};

		auto clientAuctionMinNextBid = [](uint32_t startBid, uint32_t currentBid) -> uint32_t
		{
			if (currentBid == 0u)
			{
				return startBid;
			}
			const uint32_t inc = std::max(1u, (currentBid * 5u) / 100u);
			const uint64_t sum = static_cast<uint64_t>(currentBid) + static_cast<uint64_t>(inc);
			return static_cast<uint32_t>(std::min<uint64_t>(
				sum,
				static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
		};

		if (!chatBlocks && m_input.WasPressed(engine::platform::Key::V))
		{
			(void)m_gameplayUdp.SendTalkRequest(clientId, m_gameplayVendorTalkTarget);
			LOG_INFO(Core, "[GameplayNet] Vendor talk requested ({})", m_gameplayVendorTalkTarget);
		}

		if (!chatBlocks && m_input.WasPressed(engine::platform::Key::H))
		{
			(void)m_gameplayUdp.SendTalkRequest(clientId, m_gameplayAuctionTalkTarget);
			LOG_INFO(Core, "[GameplayNet] Auction talk requested ({})", m_gameplayAuctionTalkTarget);
		}

		auto tryBuyIndex = [&](size_t offerIndex)
		{
			if (!ui.shop.isOpen || offerIndex >= ui.shop.offers.size())
			{
				return;
			}
			const uint32_t itemId = ui.shop.offers[offerIndex].itemId;
			const uint32_t vendorId = ui.shop.vendorId;
			(void)m_gameplayUdp.SendShopBuyRequest(clientId, vendorId, itemId, 1u);
		};

		const engine::platform::Key digitKeys[9] = {
			engine::platform::Key::Digit1,
			engine::platform::Key::Digit2,
			engine::platform::Key::Digit3,
			engine::platform::Key::Digit4,
			engine::platform::Key::Digit5,
			engine::platform::Key::Digit6,
			engine::platform::Key::Digit7,
			engine::platform::Key::Digit8,
			engine::platform::Key::Digit9
		};

		if (!chatBlocks && ui.auction.isOpen && !ui.auction.listings.empty())
		{
			for (int d = 0; d < 9; ++d)
			{
				if (m_input.WasPressed(digitKeys[d]))
				{
					const size_t idx = static_cast<size_t>(d);
					if (idx < ui.auction.listings.size())
					{
						(void)m_uiModelBinding.SelectAuctionRow(static_cast<uint32_t>(idx));
						LOG_INFO(Core, "[GameplayNet] Auction row selected ({})", idx);
					}
					break;
				}
			}
		}

		if (!chatBlocks && ui.auction.isOpen)
		{
			if (m_input.WasPressed(engine::platform::Key::G))
			{
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction browse refresh (G)");
			}
			if (m_input.WasPressed(engine::platform::Key::F))
			{
				const uint32_t nextSort = (ui.auction.sortMode + 1u) % 3u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					nextSort);
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction sort mode -> {}", nextSort);
			}
			if (m_input.WasPressed(engine::platform::Key::Q))
			{
				const uint32_t nmin =
					ui.auction.filterMinPrice > 100u ? ui.auction.filterMinPrice - 100u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					nmin,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::E))
			{
				const uint32_t nmin = ui.auction.filterMinPrice + 100u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					nmin,
					ui.auction.filterMaxPrice,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::PageUp))
			{
				const uint32_t nmax = ui.auction.filterMaxPrice + 500u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					nmax,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::PageDown))
			{
				const uint32_t nmax =
					ui.auction.filterMaxPrice > 500u ? ui.auction.filterMaxPrice - 500u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					nmax,
					ui.auction.filterItemId,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
			}
			if (m_input.WasPressed(engine::platform::Key::M))
			{
				const uint32_t nextFilter = ui.auction.filterItemId == 0u ? 1u : 0u;
				(void)m_uiModelBinding.ConfigureAuctionBrowse(
					ui.auction.filterMinPrice,
					ui.auction.filterMaxPrice,
					nextFilter,
					ui.auction.sortMode);
				sendAuctionBrowseFromModel();
				LOG_INFO(Core, "[GameplayNet] Auction item_id filter -> {}", nextFilter);
			}
			if (m_input.WasPressed(engine::platform::Key::B) && !ui.auction.listings.empty())
			{
				const uint32_t sel = std::min(ui.auction.selectedRow,
					static_cast<uint32_t>(ui.auction.listings.size() - 1u));
				const engine::client::UIAuctionListingLine& line = ui.auction.listings[sel];
				const uint32_t bidAmt = clientAuctionMinNextBid(line.startBid, line.currentBid);
				engine::server::AuctionBidRequestMessage msg{};
				msg.clientId = clientId;
				msg.listingId = line.listingId;
				msg.bidAmount = bidAmt;
				(void)m_gameplayUdp.SendAuctionBidRequest(msg);
			}
			if (m_input.WasPressed(engine::platform::Key::O) && !ui.auction.listings.empty())
			{
				const uint32_t sel = std::min(ui.auction.selectedRow,
					static_cast<uint32_t>(ui.auction.listings.size() - 1u));
				const engine::client::UIAuctionListingLine& line = ui.auction.listings[sel];
				if (line.buyoutPrice == 0u)
				{
					LOG_WARN(Core, "[GameplayNet] Buyout ignored: no buyout on row");
				}
				else
				{
					engine::server::AuctionBuyoutRequestMessage msg{};
					msg.clientId = clientId;
					msg.listingId = line.listingId;
					(void)m_gameplayUdp.SendAuctionBuyoutRequest(msg);
				}
			}
			if (m_input.WasPressed(engine::platform::Key::L))
			{
				for (const engine::client::InventorySlotState& slot : m_invUi.GetState().slots)
				{
					if (slot.hovered && slot.occupied && slot.itemId != 0u && slot.quantity > 0u)
					{
						engine::server::AuctionListItemRequestMessage msg{};
						msg.clientId = clientId;
						msg.itemId = slot.itemId;
						msg.quantity = 1u;
						msg.startBid = 10u;
						msg.buyoutPrice = 0u;
						msg.durationHours = 24u;
						(void)m_gameplayUdp.SendAuctionListItemRequest(msg);
						LOG_INFO(Core, "[GameplayNet] Auction list from hover (item_id={}, qty=1)", slot.itemId);
						break;
					}
				}
			}
		}

		if (!chatBlocks && ui.shop.isOpen)
		{
			for (int d = 0; d < 9; ++d)
			{
				if (m_input.WasPressed(digitKeys[d]))
				{
					tryBuyIndex(static_cast<size_t>(d));
					break;
				}
			}
		}

		if ((ui.shop.isOpen || ui.auction.isOpen) && m_input.WasMousePressed(engine::platform::MouseButton::Right))
		{
			(void)m_invUi.TryBeginDrag(mx, my);
		}

		if (m_input.WasMouseReleased(engine::platform::MouseButton::Left))
		{
			if (m_invUi.IsDragging() && ui.shop.isOpen && m_shopUi.HitSellDropZone(mx, my))
			{
				uint32_t slot = 0;
				uint32_t itemId = 0;
				uint32_t qty = 0;
				if (m_invUi.GetDragSource(slot, itemId, qty))
				{
					uint32_t buyPrice = 0;
					bool offerFound = false;
					for (const engine::client::UIShopOfferLine& line : ui.shop.offers)
					{
						if (line.itemId == itemId)
						{
							buyPrice = line.buyPrice;
							offerFound = true;
							break;
						}
					}
					if (!offerFound)
					{
						LOG_WARN(Core, "[GameplayNet] Sell-back rejected: item_id={} not listed by vendor", itemId);
					}
					else
					{
						m_pendingSellVendorId = ui.shop.vendorId;
						m_pendingSellItemId = itemId;
						m_pendingSellQty = qty;
						m_pendingSellUnitGold = ClientVendorSellUnitGold(buyPrice);
						m_pendingSellActive = true;
						LOG_INFO(Core,
							"[GameplayNet] Pending sell (item_id={}, qty={}, unit_gold={}) — confirm Y/N",
							itemId,
							qty,
							m_pendingSellUnitGold);
					}
				}
				m_invUi.CancelDrag();
			}
			else if (m_invUi.IsDragging() && ui.auction.isOpen && m_auctionUi.HitPostDropZone(mx, my))
			{
				uint32_t slot = 0;
				uint32_t itemId = 0;
				uint32_t qty = 0;
				if (m_invUi.GetDragSource(slot, itemId, qty) && itemId != 0u && qty > 0u)
				{
					engine::server::AuctionListItemRequestMessage msg{};
					msg.clientId = clientId;
					msg.itemId = itemId;
					msg.quantity = std::min(qty, 100u);
					msg.startBid = 10u;
					msg.buyoutPrice = 0u;
					msg.durationHours = 24u;
					(void)m_gameplayUdp.SendAuctionListItemRequest(msg);
					LOG_INFO(Core, "[GameplayNet] Auction list drag-drop (item_id={}, qty={})", itemId, msg.quantity);
				}
				m_invUi.CancelDrag();
			}
			else if (m_invUi.IsDragging())
			{
				m_invUi.CancelDrag();
			}
			else if (ui.auction.isOpen)
			{
				const int ahHit = m_auctionUi.HitTestRow(mx, my);
				if (ahHit >= 0)
				{
					(void)m_uiModelBinding.SelectAuctionRow(static_cast<uint32_t>(ahHit));
				}
			}
			else if (ui.shop.isOpen)
			{
				const int hit = m_shopUi.HitTestOfferLine(mx, my);
				if (hit >= 0)
				{
					tryBuyIndex(static_cast<size_t>(hit));
				}
			}
		}
	}

	std::vector<uint32_t> Engine::LoadTerrainSpirvWords(const char* relativeSpvPath)
	{
		std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, relativeSpvPath);
		if (bytes.size() % 4u == 0u && !bytes.empty())
		{
			std::vector<uint32_t> out(bytes.size() / 4u);
			std::memcpy(out.data(), bytes.data(), bytes.size());
			return out;
		}
		LOG_WARN(Render, "[Terrain] Shader SPIR-V manquant ou invalide: {}", relativeSpvPath);
		return {};
	}

	// =================================================================
	// M100 — Task 12 : ressources caméra (set 0) du TerrainChunkPipeline.
	// =================================================================

	bool Engine::CreateTerrainChunkCameraResources(std::string& outError)
	{
		VkDevice device = m_vkDeviceContext.GetDevice();
		VkPhysicalDevice physDev = m_vkDeviceContext.GetPhysicalDevice();
		if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE)
		{
			outError = "VkDeviceContext non initialisé";
			return false;
		}

		// 1. Set layout : 1 UBO pour CameraUBO { mat4 viewProj; }.
		VkDescriptorSetLayoutBinding binding{};
		binding.binding         = 0u;
		binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		binding.descriptorCount = 1u;
		binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
		VkDescriptorSetLayoutCreateInfo lci{};
		lci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 1u;
		lci.pBindings    = &binding;
		if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_terrainChunkCameraSetLayout) != VK_SUCCESS)
		{
			outError = "vkCreateDescriptorSetLayout (camera) failed";
			return false;
		}

		// 2. Pool : 1 set, 1 UBO.
		VkDescriptorPoolSize poolSize{};
		poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSize.descriptorCount = 1u;
		VkDescriptorPoolCreateInfo pci{};
		pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets       = 1u;
		pci.poolSizeCount = 1u;
		pci.pPoolSizes    = &poolSize;
		if (vkCreateDescriptorPool(device, &pci, nullptr, &m_terrainChunkCameraPool) != VK_SUCCESS)
		{
			outError = "vkCreateDescriptorPool (camera) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}

		// 3. Buffer host-visible 64 octets (mat4 viewProj std140).
		constexpr VkDeviceSize kUboSize = 64u;
		VkBufferCreateInfo bci{};
		bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size        = kUboSize;
		bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bci, nullptr, &m_terrainChunkCameraUbo) != VK_SUCCESS)
		{
			outError = "vkCreateBuffer (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkMemoryRequirements req{};
		vkGetBufferMemoryRequirements(device, m_terrainChunkCameraUbo, &req);

		// Cherche un memory type host-visible + host-coherent.
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
		uint32_t memType = UINT32_MAX;
		const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((req.memoryTypeBits & (1u << i)) != 0u
				&& (memProps.memoryTypes[i].propertyFlags & wanted) == wanted)
			{
				memType = i;
				break;
			}
		}
		if (memType == UINT32_MAX)
		{
			outError = "Aucun memory type host-visible+coherent trouvé pour camera UBO";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkMemoryAllocateInfo mai{};
		mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize  = req.size;
		mai.memoryTypeIndex = memType;
		if (vkAllocateMemory(device, &mai, nullptr, &m_terrainChunkCameraUboMem) != VK_SUCCESS)
		{
			outError = "vkAllocateMemory (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		if (vkBindBufferMemory(device, m_terrainChunkCameraUbo, m_terrainChunkCameraUboMem, 0) != VK_SUCCESS)
		{
			outError = "vkBindBufferMemory (camera UBO) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		if (vkMapMemory(device, m_terrainChunkCameraUboMem, 0, kUboSize, 0, &m_terrainChunkCameraUboMapped) != VK_SUCCESS)
		{
			outError = "vkMapMemory (camera UBO) failed";
			m_terrainChunkCameraUboMapped = nullptr;
			DestroyTerrainChunkCameraResources();
			return false;
		}

		// 4. Allocation du descriptor set + écriture initiale.
		VkDescriptorSetAllocateInfo dsai{};
		dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool     = m_terrainChunkCameraPool;
		dsai.descriptorSetCount = 1u;
		dsai.pSetLayouts        = &m_terrainChunkCameraSetLayout;
		if (vkAllocateDescriptorSets(device, &dsai, &m_terrainChunkCameraSet) != VK_SUCCESS)
		{
			outError = "vkAllocateDescriptorSets (camera) failed";
			DestroyTerrainChunkCameraResources();
			return false;
		}
		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = m_terrainChunkCameraUbo;
		bufInfo.offset = 0u;
		bufInfo.range  = kUboSize;
		VkWriteDescriptorSet write{};
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_terrainChunkCameraSet;
		write.dstBinding      = 0u;
		write.descriptorCount = 1u;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo     = &bufInfo;
		vkUpdateDescriptorSets(device, 1u, &write, 0, nullptr);
		return true;
	}

	void Engine::DestroyTerrainChunkCameraResources()
	{
		VkDevice device = m_vkDeviceContext.GetDevice();
		if (device == VK_NULL_HANDLE) return;
		// Note : le descriptor set est libéré implicitement par vkDestroyDescriptorPool.
		m_terrainChunkCameraSet = VK_NULL_HANDLE;
		if (m_terrainChunkCameraUboMapped != nullptr && m_terrainChunkCameraUboMem != VK_NULL_HANDLE)
		{
			vkUnmapMemory(device, m_terrainChunkCameraUboMem);
			m_terrainChunkCameraUboMapped = nullptr;
		}
		if (m_terrainChunkCameraUbo != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_terrainChunkCameraUbo, nullptr);
			m_terrainChunkCameraUbo = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraUboMem != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_terrainChunkCameraUboMem, nullptr);
			m_terrainChunkCameraUboMem = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, m_terrainChunkCameraPool, nullptr);
			m_terrainChunkCameraPool = VK_NULL_HANDLE;
		}
		if (m_terrainChunkCameraSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, m_terrainChunkCameraSetLayout, nullptr);
			m_terrainChunkCameraSetLayout = VK_NULL_HANDLE;
		}
	}

	void Engine::UpdateTerrainChunkCameraUbo(const float* viewProjMat4)
	{
		if (m_terrainChunkCameraUboMapped == nullptr || viewProjMat4 == nullptr)
			return;
		std::memcpy(m_terrainChunkCameraUboMapped, viewProjMat4, 64u);
		// Memory host-coherent : pas de flush nécessaire, la GPU verra l'update
		// avant le prochain submit.
	}

} // namespace engine
