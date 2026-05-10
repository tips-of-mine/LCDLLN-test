#include "src/client/app/Engine.h"

#include "src/shared/core/Log.h"
#include "src/world_editor/ui/EditorMode.h"
#include "src/world_editor/ui/WorldEditorImGui.h"
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/shared/core/memory/Memory.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/IgnoreListPayloads.h"
#include "src/shared/network/MailPayloads.h"
#include "src/shared/network/QuestPayloads.h"
#include "src/shared/network/GmTicketPayloads.h"
#include "src/shared/network/ReputationPayloads.h"
#include "src/shared/network/ArenaPayloads.h"
#include "src/shared/network/BattleGroundPayloads.h"
#include "src/shared/network/OutdoorPvpPayloads.h"
#include "src/shared/network/WeatherPayloads.h"
#include "src/shared/network/GameEventPayloads.h"
#include "src/shared/network/GuildPayloads.h"
#include "src/shared/network/AuctionPayloads.h"
#include "src/shared/network/LootPayloads.h"
#include "src/shared/network/LunarPayloads.h"
#include "src/shared/network/AdminCommandPayloads.h"
#include "src/shared/network/LfgPayloads.h"
#include "src/shared/network/CinematicPayloads.h"
#include "src/shared/network/SkillPayloads.h"
#include "src/shared/network/TradePayloads.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/ChatImGuiRenderer.h"
#include "src/client/render/MailImGuiRenderer.h"
#include "src/client/render/GmTicketImGuiRenderer.h"
#include "src/client/render/ReputationImGuiRenderer.h"
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
#include "src/client/render/EditorHubImGuiRenderer.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/client/render/DeferredPipeline.h"
#include "src/client/render/ShaderCompiler.h"
#include "src/client/render/terrain/HeightmapLoader.h"
#include "src/client/render/terrain/TerrainEditingTools.h"
#include "src/shared/network/ServerProtocol.h"

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
#include <optional>
#include <span>
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
#	include "imgui.h"
#endif

namespace engine
{
	namespace
	{
		constexpr float kWorldEditorPickPi = 3.14159265f;

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

		bool CameraViewportWorldDirection(const engine::render::Camera& camera, int viewportWidth, int viewportHeight,
			int mouseX, int mouseY, engine::math::Vec3& outDirection)
		{
			if (viewportWidth <= 0 || viewportHeight <= 0)
			{
				return false;
			}
			const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
			const float halfTan = std::tan((camera.fovYDeg * kWorldEditorPickPi / 180.0f) * 0.5f);
			const float ndcX = ((static_cast<float>(mouseX) + 0.5f) / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
			const float ndcY = 1.0f - ((static_cast<float>(mouseY) + 0.5f) / static_cast<float>(viewportHeight)) * 2.0f;

			const float cy = std::cos(camera.yaw);
			const float sy = std::sin(camera.yaw);
			const float cp = std::cos(camera.pitch);
			const float sp = std::sin(camera.pitch);

			engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
			// PR26.5 (M??.?) : alignement avec le fix Camera.cpp:22. La fonction
			// CameraViewportWorldDirection sert a calculer la direction du ray
			// pour le raycast camera->terrain (RaycastTerrainFromCamera). Doit
			// utiliser exactement les memes conventions que ComputeViewMatrix
			// pour que le raycast aligne avec ce que la camera voit a l'ecran.
			// Sans ce fix, le raycast utilisait un right_inverse alors que la
			// matrice view (post-PR26.5) a un right_standard, donc le pickX/Z
			// retourne par le raycast etait decale en X (souris a droite ->
			// pickX a gauche du sol). Possible cause partielle des items 6+7
			// (sculpt + splat ne fonctionnent pas) — a confirmer en PR27.
			engine::math::Vec3 right(-forward.z, 0.0f, forward.x);
			const float rightLen = right.Length();
			right = rightLen > 0.0f ? right * (1.0f / rightLen) : engine::math::Vec3(1.0f, 0.0f, 0.0f);

			engine::math::Vec3 up(
				right.y * forward.z - right.z * forward.y,
				right.z * forward.x - right.x * forward.z,
				right.x * forward.y - right.y * forward.x);
			const float upLen = up.Length();
			up = upLen > 0.0f ? up * (1.0f / upLen) : engine::math::Vec3(0.0f, 1.0f, 0.0f);

			outDirection = (forward + right * (ndcX * aspect * halfTan) + up * (ndcY * halfTan)).Normalized();
			return outDirection.LengthSq() > 1e-8f;
		}

		bool TryTerrainWorldY(const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale, float wx, float wz, float& yOut)
		{
			if (hm.width == 0 || hm.height == 0 || ws <= 0.0f)
			{
				return false;
			}
			const float u = (wx - ox) / ws;
			const float v = (wz - oz) / ws;
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				return false;
			}
			yOut = hm.SampleBilinearNorm(u, v) * hScale;
			return true;
		}

		bool RaycastTerrainHeightmap(const engine::math::Vec3& O, const engine::math::Vec3& D,
			const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale, float maxDistance,
			float& outHitX, float& outHitZ)
		{
			if (hm.width == 0 || maxDistance <= 0.0f)
			{
				return false;
			}
			constexpr int kSegments = 192;
			float prevT = 0.0f;
			float prevDiff = 0.0f;
			bool prevValid = false;
			{
				float h0 = 0.0f;
				const bool ok0 = TryTerrainWorldY(hm, ox, oz, ws, hScale, O.x, O.z, h0);
				if (ok0)
				{
					prevDiff = O.y - h0;
					prevValid = true;
				}
			}
			for (int i = 1; i <= kSegments; ++i)
			{
				const float t = maxDistance * (static_cast<float>(i) / static_cast<float>(kSegments));
				const float px = O.x + D.x * t;
				const float py = O.y + D.y * t;
				const float pz = O.z + D.z * t;
				float h = 0.0f;
				if (!TryTerrainWorldY(hm, ox, oz, ws, hScale, px, pz, h))
				{
					prevValid = false;
					continue;
				}
				const float diff = py - h;
				if (prevValid && prevDiff > 0.015f && diff <= 0.015f)
				{
					float t0 = prevT;
					float t1 = t;
					for (int b = 0; b < 14; ++b)
					{
						const float tm = 0.5f * (t0 + t1);
						const float mxp = O.x + D.x * tm;
						const float myp = O.y + D.y * tm;
						const float mzp = O.z + D.z * tm;
						float mh = 0.0f;
						if (!TryTerrainWorldY(hm, ox, oz, ws, hScale, mxp, mzp, mh))
						{
							t1 = tm;
							continue;
						}
						if (myp > mh)
						{
							t0 = tm;
						}
						else
						{
							t1 = tm;
						}
					}
					const float tf = 0.5f * (t0 + t1);
					outHitX = O.x + D.x * tf;
					outHitZ = O.z + D.z * tf;
					return true;
				}
				prevT = t;
				prevDiff = diff;
				prevValid = true;
			}
			return false;
		}

		bool RaycastTerrainFromCamera(const engine::render::Camera& camera, int vw, int vh, int mx, int my,
			const engine::render::terrain::HeightmapData& hm,
			float ox, float oz, float ws, float hScale,
			float& outX, float& outZ)
		{
			engine::math::Vec3 dir{};
			if (!CameraViewportWorldDirection(camera, vw, vh, mx, my, dir))
			{
				return false;
			}
			const float maxDist = std::max(ws * 4.0f, 8192.0f);
			return RaycastTerrainHeightmap(camera.position, dir, hm, ox, oz, ws, hScale, maxDist, outX, outZ);
		}

		void ApplyUserSettingsOverrides(engine::core::Config& cfg)
		{
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
				cfg.SetValue("render.quality_preset", persisted.GetInt("render.quality_preset", cfg.GetInt("render.quality_preset", 2)));
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

		enum class AuthBackgroundBlitFit
		{
			Stretch,
			Contain,
			Cover,
			CoverHeight,
		};

		AuthBackgroundBlitFit ParseAuthBackgroundBlitFit(std::string_view s)
		{
			if (s == "contain")
			{
				return AuthBackgroundBlitFit::Contain;
			}
			if (s == "cover_height" || s == "height")
			{
				return AuthBackgroundBlitFit::CoverHeight;
			}
			if (s == "cover")
			{
				return AuthBackgroundBlitFit::Cover;
			}
			if (s == "stretch")
			{
				return AuthBackgroundBlitFit::Stretch;
			}
			return AuthBackgroundBlitFit::CoverHeight;
		}

		/// Remplit \p blit pour \c vkCmdBlitImage : \c stretch (étire), \c contain (image entière, bandes),
		/// \c cover (remplit la surface, rogne l’excédent au centre),
		/// \c cover_height (remplit la hauteur, rogne la largeur de la source si l’écran est plus large ; sinon bandes latérales).
		void BuildAuthBackgroundBlit(
			AuthBackgroundBlitFit fit,
			uint32_t srcW,
			uint32_t srcH,
			uint32_t dstW,
			uint32_t dstH,
			VkImageBlit& blit)
		{
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = 0;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstSubresource = blit.srcSubresource;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { static_cast<int32_t>(srcW), static_cast<int32_t>(srcH), 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { static_cast<int32_t>(dstW), static_cast<int32_t>(dstH), 1 };

			const float sw = static_cast<float>(srcW);
			const float sh = static_cast<float>(srcH);
			const float dw = static_cast<float>(dstW);
			const float dh = static_cast<float>(dstH);
			if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f || fit == AuthBackgroundBlitFit::Stretch)
			{
				return;
			}

			if (fit == AuthBackgroundBlitFit::Contain)
			{
				const float scale = std::min(dw / sw, dh / sh);
				const int32_t outW = static_cast<int32_t>(std::lround(sw * scale));
				const int32_t outH = static_cast<int32_t>(std::lround(sh * scale));
				const int32_t dx = (static_cast<int32_t>(dstW) - outW) / 2;
				const int32_t dy = (static_cast<int32_t>(dstH) - outH) / 2;
				blit.dstOffsets[0] = { dx, dy, 0 };
				blit.dstOffsets[1] = { dx + outW, dy + outH, 1 };
				return;
			}

			if (fit == AuthBackgroundBlitFit::CoverHeight)
			{
				const float scale = dh / sh;
				const float scaledW = sw * scale;
				if (scaledW >= dw)
				{
					const float cropW = dw / scale;
					float sx0 = (sw - cropW) * 0.5f;
					sx0 = std::clamp(sx0, 0.f, sw - 1.f);
					float sx1 = sx0 + cropW;
					sx1 = std::min(sx1, sw);
					if (sx1 <= sx0)
					{
						sx1 = std::min(sx0 + 1.f, sw);
					}
					blit.srcOffsets[0] = { static_cast<int32_t>(std::floor(sx0)), 0, 0 };
					blit.srcOffsets[1] = { static_cast<int32_t>(std::ceil(sx1)), static_cast<int32_t>(srcH), 1 };
					return;
				}
				const int32_t outW = static_cast<int32_t>(std::lround(scaledW));
				const int32_t dx = (static_cast<int32_t>(dstW) - outW) / 2;
				blit.dstOffsets[0] = { dx, 0, 0 };
				blit.dstOffsets[1] = { dx + outW, static_cast<int32_t>(dstH), 1 };
				return;
			}

			// Cover : rogne la source au centre pour remplir le framebuffer sans étirement.
			const float scale = std::max(dw / sw, dh / sh);
			const float cropW = dw / scale;
			const float cropH = dh / scale;
			float sx0 = (sw - cropW) * 0.5f;
			float sy0 = (sh - cropH) * 0.5f;
			sx0 = std::clamp(sx0, 0.f, sw - 1.f);
			sy0 = std::clamp(sy0, 0.f, sh - 1.f);
			float sx1 = sx0 + cropW;
			float sy1 = sy0 + cropH;
			sx1 = std::min(sx1, sw);
			sy1 = std::min(sy1, sh);
			if (sx1 <= sx0)
			{
				sx1 = std::min(sx0 + 1.f, sw);
			}
			if (sy1 <= sy0)
			{
				sy1 = std::min(sy0 + 1.f, sh);
			}
			blit.srcOffsets[0] = { static_cast<int32_t>(std::floor(sx0)), static_cast<int32_t>(std::floor(sy0)), 0 };
			blit.srcOffsets[1] = { static_cast<int32_t>(std::ceil(sx1)), static_cast<int32_t>(std::ceil(sy1)), 1 };
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

		/// Return the first probe intensity parameter, or 1.0 when no probe exists.
		float GetGlobalProbeIntensity(const engine::world::ProbeSet& probeSet)
		{
			if (probeSet.probes.empty())
			{
				return 1.0f;
			}

			return probeSet.probes.front().params[0];
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

		float GetTestMeshDistanceMeters(const engine::RenderState& rs,
			const std::vector<engine::world::ChunkDrawDecision>& chunkDrawDecisions)
		{
			if (!chunkDrawDecisions.empty())
				return chunkDrawDecisions[0].distanceMeters;

			const float dx = rs.camera.position.x;
			const float dy = rs.camera.position.y;
			const float dz = rs.camera.position.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		engine::render::GpuDrawItem BuildGpuDrawItem(const engine::render::MeshAsset& mesh,
			uint32_t meshId,
			const float* modelMatrix,
			uint32_t lodLevel)
		{
			engine::render::GpuDrawItem item{};
			item.meshId = meshId;
			item.materialId = 0;
			item.lodLevel = lodLevel;
			std::memcpy(item.modelMatrix, modelMatrix, sizeof(item.modelMatrix));

			engine::math::Vec3 localMin(-0.5f, -0.5f, -0.5f);
			engine::math::Vec3 localMax(0.5f, 0.5f, 0.5f);
			if (mesh.hasLocalBounds)
			{
				localMin = mesh.localBoundsMin;
				localMax = mesh.localBoundsMax;
			}
			else
			{
				static bool s_loggedMissingBounds = false;
				if (!s_loggedMissingBounds)
				{
					LOG_WARN(Render, "[GpuDrivenCulling] Mesh local bounds missing, using unit bounds fallback");
					s_loggedMissingBounds = true;
				}
			}

			const engine::math::Vec3 localCenter = (localMin + localMax) * 0.5f;
			const engine::math::Vec3 localExtents = (localMax - localMin) * 0.5f;
			const float* m = modelMatrix;

			const float worldCenterX = m[0] * localCenter.x + m[4] * localCenter.y + m[8] * localCenter.z + m[12];
			const float worldCenterY = m[1] * localCenter.x + m[5] * localCenter.y + m[9] * localCenter.z + m[13];
			const float worldCenterZ = m[2] * localCenter.x + m[6] * localCenter.y + m[10] * localCenter.z + m[14];
			const float worldExtentX = std::fabs(m[0]) * localExtents.x + std::fabs(m[4]) * localExtents.y + std::fabs(m[8]) * localExtents.z;
			const float worldExtentY = std::fabs(m[1]) * localExtents.x + std::fabs(m[5]) * localExtents.y + std::fabs(m[9]) * localExtents.z;
			const float worldExtentZ = std::fabs(m[2]) * localExtents.x + std::fabs(m[6]) * localExtents.y + std::fabs(m[10]) * localExtents.z;

			item.boundsCenter[0] = worldCenterX;
			item.boundsCenter[1] = worldCenterY;
			item.boundsCenter[2] = worldCenterZ;
			item.boundsCenter[3] = 1.0f;
			item.boundsExtents[0] = worldExtentX;
			item.boundsExtents[1] = worldExtentY;
			item.boundsExtents[2] = worldExtentZ;
			item.boundsExtents[3] = 0.0f;
			item.indexCount = mesh.GetLodIndexCount(lodLevel);
			item.firstIndex = mesh.GetLodIndexOffset(lodLevel);
			item.vertexOffset = 0;
			item.firstInstance = 0;
			return item;
		}

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
		logSettings.subsystemFiles   = m_cfg.GetStringMapUnderPrefix("log.subsystem_files");
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

		// ------------------------------------------------------------------
		// Config + subsystems
		// ------------------------------------------------------------------
		ApplyUserSettingsOverrides(m_cfg);
		m_vsync   = m_cfg.GetBool("render.vsync", true);
		m_fixedDt = m_cfg.GetDouble("time.fixed_dt", 0.0);
		m_worldEditorExe = HasCliFlag(argc, argv, "--world-editor");
		m_editorEnabled = m_worldEditorExe || HasCliFlag(argc, argv, "--editor")
			|| m_cfg.GetBool("editor.enabled", false);

		// M100.1 — Branche éditeur monde "couche au-dessus" (distincte de
		// --world-editor qui active le shell M43.x). Activée par le flag CLI
		// --editor-world ou par editor.world.enabled = true dans config.json.
		// Les deux shells peuvent cohabiter pendant la transition.
		const bool worldEditorWorldFlag =
			HasCliFlag(argc, argv, "--editor-world") || m_cfg.GetBool("editor.world.enabled", false);

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
			if (m_worldEditorExe)
			{
				LOG_WARN(Core,
					"[Boot] --world-editor ET --editor-world activés — cas non-supporté ; les deux shells vont coexister");
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

		// CMANGOS.23 (Phase 5.23 step 3+4) — Cable le QuestUi presenter au
		// master via le helper generique d'AuthUi. Le presenter etait deja
		// init via Init(m_cfg) plus haut dans le boot ; ici on ne fait que
		// brancher le canal d'envoi reseau. La reception est dispatchee dans
		// le SetMasterPushHandler ci-dessous (opcodes 60/62/64/66/67).
		m_questUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
			return m_authUi.SendGenericRequestAsync(opcode, payload);
		});

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

		// CMANGOS.33 (Phase 5.33 step 3+4) — Init du presenter LFG + cable du
		// send callback pour les requetes 100/102/104/107. Reception dispatchee
		// dans le push handler ci-dessous (responses 101/103/105 + push 106).
		if (!m_lfgUi.Init())
		{
			LOG_WARN(Core, "[Boot] LfgUiPresenter init FAILED — panneau LFG desactive");
		}
		else
		{
			m_lfgUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_skillBookUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
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
			m_arenaUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_battleGroundUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_outdoorPvpUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_weatherUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_gameEventUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_guildUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_auctionHouseUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_lootRollUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
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
			m_tradeWindowUi.SetSendCallback([this](uint16_t opcode, const std::vector<uint8_t>& payload) -> bool {
				return m_authUi.SendGenericRequestAsync(opcode, payload);
			});
		}

		// Chat MVP — câblage bidirectionnel ChatUi <-> AuthUi (master TCP).
		// Send : ChatUi::SubmitInputLine appelle AuthUi::SendChatAsync sur la connexion master vivante.
		// Receive : AuthUi::PumpPostAuthEvents dispatche les paquets push (CHAT_RELAY notamment)
		// vers un handler qui parse et appelle ChatUi::PushNetworkLine.
		m_chatUi.SetSendCallback([this](uint8_t channel, std::string_view targetToken, std::string_view text) -> bool {
			// Wave 3 RBAC migration : helper local pour envoyer un audit
			// AdminCommand au master pour les UI panel toggles. Fire-and-forget
			// (requestId=0, on n'attend pas l'ACK : le toggle visuel est
			// applique immediatement, le master log juste le geste).
			//
			// Le master valide l'auth + role (player suffit pour ces commandes)
			// et emet [AdminCommand] result=OK dans son log audit.
			auto sendAdminAudit = [this](std::string_view cmd) {
				engine::network::admin::AdminCommandRequest req;
				req.command = std::string(cmd);
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
			};

			// CMANGOS.18 (Phase 3.18 step 4) — Intercept /mail avant l'envoi
			// chat. ParseSlashPrefixes ne connait pas /mail, donc le texte
			// arrive sur le canal Say avec text == "/mail" (ou "/mail ...").
			// On consomme le slash command localement et retourne true (le
			// chat presenter pense que c'est envoye et clear l'input).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/mail" || text.starts_with("/mail ") || text.starts_with("/mail\t")))
			{
				m_mailVisible = !m_mailVisible;
				if (m_mailVisible)
				{
					m_mailUi.RequestInbox();
				}
				LOG_INFO(Core, "[Engine] /mail toggle (visible={})", m_mailVisible);
				sendAdminAudit("/mail");
				return true;
			}
			// CMANGOS.23 (Phase 5.23 step 3+4) — Slash command /quest et /quests
			// pour ouvrir/fermer le panneau quete et synchroniser la liste depuis
			// le master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/quest" || text == "/quests"
				    || text.starts_with("/quest ") || text.starts_with("/quest\t")
				    || text.starts_with("/quests ") || text.starts_with("/quests\t")))
			{
				m_questVisible = !m_questVisible;
				if (m_questVisible)
				{
					m_questUi.RequestQuestList();
				}
				LOG_INFO(Core, "[Engine] /quest toggle (visible={})", m_questVisible);
				sendAdminAudit("/quest");
				return true;
			}
			// CMANGOS.27 (Phase 4.27 step 3+4) — Slash command /trade <accountId>
			// pour initier un echange avec le joueur cible. V1 : resolution par
			// account_id direct (la resolution par character_name viendra avec
			// PartySystem display ulterieurement). "/trade cancel" annule la
			// trade en cours.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/trade" || text.starts_with("/trade ") || text.starts_with("/trade\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				if (spaceIdx == std::string_view::npos)
				{
					LOG_INFO(Core, "[Engine] /trade : usage /trade <account_id> ou /trade cancel");
					return true;
				}
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				if (arg == "cancel")
				{
					m_tradeWindowUi.Cancel();
					LOG_INFO(Core, "[Engine] /trade cancel");
					return true;
				}
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /trade : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_tradeWindowUi.RequestBeginTrade(targetAccountId);
				LOG_INFO(Core, "[Engine] /trade {}", targetAccountId);
				return true;
			}
			// CMANGOS.24 (Phase 3.24 step 3+4) — Slash command /rep et /reputation
			// pour ouvrir/fermer le panneau Reputation et synchroniser la liste
			// depuis le master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/rep" || text == "/reputation"
				    || text.starts_with("/rep ") || text.starts_with("/rep\t")
				    || text.starts_with("/reputation ") || text.starts_with("/reputation\t")))
			{
				m_reputationVisible = !m_reputationVisible;
				if (m_reputationVisible)
				{
					m_reputationUi.RequestReputationList();
				}
				LOG_INFO(Core, "[Engine] /rep toggle (visible={})", m_reputationVisible);
				sendAdminAudit("/rep");
				return true;
			}
			// CMANGOS.39 (Phase 4.39 step 3+4) — Slash command /skills pour
			// ouvrir/fermer le panneau Skill Book et synchroniser la liste
			// depuis le master au moment de l'ouverture. La touche B fait
			// la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/skills" || text == "/skill"
				    || text.starts_with("/skills ") || text.starts_with("/skills\t")
				    || text.starts_with("/skill ") || text.starts_with("/skill\t")))
			{
				m_skillBookVisible = !m_skillBookVisible;
				if (m_skillBookVisible)
				{
					m_skillBookUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /skills toggle (visible={})", m_skillBookVisible);
				sendAdminAudit("/skills");
				return true;
			}
			// CMANGOS.33 (Phase 5.33 step 3+4) — Slash command /lfg pour
			// ouvrir/fermer la fenetre LFG et synchroniser le status depuis le
			// master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/lfg" || text.starts_with("/lfg ") || text.starts_with("/lfg\t")))
			{
				m_lfgVisible = !m_lfgVisible;
				if (m_lfgVisible)
				{
					m_lfgUi.RequestStatus();
				}
				LOG_INFO(Core, "[Engine] /lfg toggle (visible={})", m_lfgVisible);
				sendAdminAudit("/lfg");
				return true;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4) — Slash command /arena pour
			// ouvrir/fermer la fenetre Arena et synchroniser la liste des
			// teams depuis le master au moment de l'ouverture. La touche A
			// fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/arena" || text.starts_with("/arena ") || text.starts_with("/arena\t")))
			{
				m_arenaVisible = !m_arenaVisible;
				if (m_arenaVisible)
				{
					m_arenaUi.RequestTeams();
				}
				LOG_INFO(Core, "[Engine] /arena toggle (visible={})", m_arenaVisible);
				sendAdminAudit("/arena");
				return true;
			}
			// CMANGOS.10 (Phase 5 step 3+4) — Slash command /bg pour ouvrir/
			// fermer la fenetre BattleGround et synchroniser la liste depuis
			// le master au moment de l'ouverture. La touche G fait la meme
			// chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/bg" || text.starts_with("/bg ") || text.starts_with("/bg\t")))
			{
				m_battleGroundVisible = !m_battleGroundVisible;
				if (m_battleGroundVisible)
				{
					m_battleGroundUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /bg toggle (visible={})", m_battleGroundVisible);
				sendAdminAudit("/bg");
				return true;
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Slash command /pvp pour
			// ouvrir/fermer la fenetre OutdoorPvp et synchroniser la liste
			// des zones depuis le master au moment de l'ouverture. La touche
			// P fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/pvp" || text.starts_with("/pvp ") || text.starts_with("/pvp\t")))
			{
				m_outdoorPvpVisible = !m_outdoorPvpVisible;
				if (m_outdoorPvpVisible)
				{
					m_outdoorPvpUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /pvp toggle (visible={})", m_outdoorPvpVisible);
				sendAdminAudit("/pvp");
				return true;
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Slash command /weather pour
			// ouvrir/fermer le panneau Weather et synchroniser la liste des
			// zones meteo depuis le master au moment de l'ouverture. La
			// touche Y fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/weather" || text.starts_with("/weather ") || text.starts_with("/weather\t")))
			{
				m_weatherVisible = !m_weatherVisible;
				if (m_weatherVisible)
				{
					m_weatherUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /weather toggle (visible={})", m_weatherVisible);
				sendAdminAudit("/weather");
				return true;
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Slash command /events pour
			// ouvrir/fermer le panneau GameEvents et synchroniser la liste
			// depuis le master au moment de l'ouverture. La touche E fait
			// la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/events" || text.starts_with("/events ") || text.starts_with("/events\t")))
			{
				m_gameEventVisible = !m_gameEventVisible;
				if (m_gameEventVisible)
				{
					m_gameEventUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /events toggle (visible={})", m_gameEventVisible);
				sendAdminAudit("/events");
				return true;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Slash command /guild
			// pour ouvrir/fermer le panneau Guildes et synchroniser la liste
			// depuis le master au moment de l'ouverture. La touche U fait
			// la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/guild" || text == "/guilds"
				    || text.starts_with("/guild ") || text.starts_with("/guild\t")
				    || text.starts_with("/guilds ") || text.starts_with("/guilds\t")))
			{
				m_guildVisible = !m_guildVisible;
				if (m_guildVisible)
				{
					m_guildUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] /guild toggle (visible={})", m_guildVisible);
				sendAdminAudit("/guild");
				return true;
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Slash command /ah
			// pour ouvrir/fermer le panneau Hotel des Ventes et synchroniser
			// la liste des encheres depuis le master au moment de l'ouverture.
			// La touche H fait la meme chose (cf. boucle input dans BeginFrame).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ah" || text == "/auction"
				    || text.starts_with("/ah ") || text.starts_with("/ah\t")
				    || text.starts_with("/auction ") || text.starts_with("/auction\t")))
			{
				m_auctionHouseVisible = !m_auctionHouseVisible;
				if (m_auctionHouseVisible)
				{
					m_auctionHouseUi.RequestList(0u);
				}
				LOG_INFO(Core, "[Engine] /ah toggle (visible={})", m_auctionHouseVisible);
				sendAdminAudit("/ah");
				return true;
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Slash command /loot
			// pour ouvrir/fermer la fenetre Loot Roll. La touche L fait la
			// meme chose (cf. boucle input dans BeginFrame). Pas de fetch
			// immediat : la fenetre montre les pending rolls reçus via push
			// + le bouton Simulate Loot Roll (debug V1).
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/loot"
				    || text.starts_with("/loot ") || text.starts_with("/loot\t")))
			{
				m_lootRollVisible = !m_lootRollVisible;
				LOG_INFO(Core, "[Engine] /loot toggle (visible={})", m_lootRollVisible);
				return true;
			}
			// Phase 5 step 3+4 Lunar + M38.1 Sky : slash commands debug pour
			// inspecter et override le cycle jour/nuit + phase lunaire.
			//   /sky info        : log + chat-echo timeOfDay + sun dir + moon phase + illumination.
			//   /sky time <h>    : SetTime(h), ex. "/sky time 22.5".
			//   /sky moon <i>    : OnLunarPhaseChange(i, calc(i)), ex. "/sky moon 7".
			//
			// Chat echo : chaque commande pousse une ligne sur le canal Server
			// (sender="[Sky]") via m_chatUi.PushNetworkLine, en plus du LOG_INFO
			// dans le fichier engine.log. Le joueur voit donc le retour de la
			// commande directement dans la fenetre de chat in-game.
			auto pushSkyChatLine = [this](const char* fmt, auto... args) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), fmt, args...);
				engine::net::ChatMessage msg;
				msg.timestampUnixMs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
				msg.channel = engine::net::ChatChannel::Server;
				msg.sender  = "[Sky]";
				msg.text    = buf;
				m_chatUi.PushNetworkLine(msg);
			};

			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/sky info" || text == "/sky"))
			{
				const auto& s = m_dayNight.GetState();
				const char* moonName[16] = {
					"NewMoon", "WaxingCrescentEarly", "WaxingCrescentLate", "FirstQuarter",
					"WaxingGibbousEarly", "WaxingGibbousLate", "FullMoonRising", "FullMoon",
					"FullMoonSetting", "WaningGibbousEarly", "WaningGibbousLate", "LastQuarter",
					"WaningCrescentEarly", "WaningCrescentLate", "EarthshineEarly", "EarthshineLate"
				};
				LOG_INFO(Render, "[Sky] timeOfDay={:.2f}h isDaytime={}", s.timeOfDay, s.isDaytime);
				LOG_INFO(Render, "[Sky] sunDir=({:.2f},{:.2f},{:.2f})", s.lightDir[0], s.lightDir[1], s.lightDir[2]);
				LOG_INFO(Render, "[Sky] moonPhase={} ({}) illumination={:.0f}%",
					static_cast<unsigned>(s.moonPhase),
					moonName[s.moonPhase < 16 ? s.moonPhase : 0],
					s.moonIllumination * 100.0f);
				pushSkyChatLine("timeOfDay=%.2fh isDaytime=%s",
					static_cast<double>(s.timeOfDay), s.isDaytime ? "true" : "false");
				pushSkyChatLine("sunDir=(%.2f,%.2f,%.2f)",
					static_cast<double>(s.lightDir[0]),
					static_cast<double>(s.lightDir[1]),
					static_cast<double>(s.lightDir[2]));
				pushSkyChatLine("moonPhase=%u (%s) illumination=%.0f%%",
					static_cast<unsigned>(s.moonPhase),
					moonName[s.moonPhase < 16 ? s.moonPhase : 0],
					static_cast<double>(s.moonIllumination * 100.0f));
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/sky time "))
			{
				const auto rest = text.substr(10);
				float hours = 0.0f;
				try { hours = std::stof(std::string(rest)); } catch (...) { hours = 12.0f; }
				m_dayNight.SetTime(hours);
				LOG_INFO(Render, "[Sky] time set to {:.2f}h", hours);
				pushSkyChatLine("time set to %.2fh", static_cast<double>(hours));
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& text.starts_with("/sky moon "))
			{
				const auto rest = text.substr(10);
				int phase = 0;
				try { phase = std::stoi(std::string(rest)); } catch (...) { phase = 0; }
				if (phase < 0 || phase > 15)
				{
					LOG_WARN(Render, "[Sky] phase {} hors plage [0..15]", phase);
					pushSkyChatLine("phase %d hors plage [0..15]", phase);
					return true;
				}
				// AdminCommand RBAC pilot : /sky moon est admin-only. On envoie au
				// master qui valide le role + log audit + retourne Ok/Denied.
				// Le client applique l'override visuel SEULEMENT apres ACK Ok
				// (voir dispatch kOpcodeAdminCommandResponse plus bas).
				// Chat echo immediat pour feedback : la suite (Ok/Denied) sera
				// rendue par le case kOpcodeAdminCommandResponse.
				engine::network::admin::AdminCommandRequest req;
				req.command = "/sky moon";
				req.args.push_back(std::to_string(phase));
				std::vector<uint8_t> payload;
				engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
				(void)m_authUi.SendGenericRequestAsync(
					engine::network::kOpcodeAdminCommandRequest, payload);
				LOG_INFO(Render, "[Sky] /sky moon {} sent to master for RBAC validation", phase);
				pushSkyChatLine("/sky moon %d envoye au master (validation RBAC en cours...)", phase);
				return true;
			}
			// CMANGOS.32 (Phase 5.32 step 3+4) — Slash command /ticket et /gmticket
			// pour ouvrir/fermer le panneau Support GM et synchroniser la liste
			// depuis le master au moment de l'ouverture.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ticket" || text == "/gmticket"
				    || text.starts_with("/ticket ") || text.starts_with("/ticket\t")
				    || text.starts_with("/gmticket ") || text.starts_with("/gmticket\t")))
			{
				m_gmTicketsVisible = !m_gmTicketsVisible;
				if (m_gmTicketsVisible)
				{
					m_gmTicketUi.RequestMyTickets();
				}
				LOG_INFO(Core, "[Engine] /ticket toggle (visible={})", m_gmTicketsVisible);
				sendAdminAudit("/ticket");
				return true;
			}
			// CMANGOS.25 (Phase 3.25 step 3+4) — Slash commands /ignore et /unignore.
			// Format V1 : "/ignore <account_id>" et "/unignore <account_id>" — la
			// resolution par character_name viendra avec PartySystem display.
			// "/ignore list" sans argument refresh la cache depuis le master.
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text == "/ignore" || text.starts_with("/ignore ") || text.starts_with("/ignore\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				if (spaceIdx == std::string_view::npos)
				{
					// "/ignore" tout seul : refresh la liste.
					m_ignoreListUi.RequestIgnoreList();
					LOG_INFO(Core, "[Engine] /ignore (refresh list)");
					return true;
				}
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				if (arg == "list" || arg.empty())
				{
					m_ignoreListUi.RequestIgnoreList();
					LOG_INFO(Core, "[Engine] /ignore list");
					return true;
				}
				// Parse account_id en uint64 (base 10).
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /ignore : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_ignoreListUi.IgnoreAccount(targetAccountId);
				LOG_INFO(Core, "[Engine] /ignore {}", targetAccountId);
				return true;
			}
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
				&& (text.starts_with("/unignore ") || text.starts_with("/unignore\t")))
			{
				const auto spaceIdx = text.find_first_of(" \t");
				std::string_view arg = text.substr(spaceIdx + 1);
				while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
					arg.remove_prefix(1u);
				uint64_t targetAccountId = 0u;
				bool parsed = false;
				try
				{
					size_t pos = 0;
					const std::string argStr(arg);
					targetAccountId = std::stoull(argStr, &pos, 10);
					parsed = (pos > 0u && targetAccountId != 0u);
				}
				catch (...)
				{
					parsed = false;
				}
				if (!parsed)
				{
					LOG_WARN(Core, "[Engine] /unignore : argument '{}' n'est pas un account_id valide",
						std::string(arg));
					return true;
				}
				m_ignoreListUi.UnignoreAccount(targetAccountId);
				LOG_INFO(Core, "[Engine] /unignore {}", targetAccountId);
				return true;
			}
			return m_authUi.SendChatAsync(channel, targetToken, text);
		});
		m_authUi.SetMasterPushHandler([this](uint16_t opcode, const uint8_t* payload, size_t payloadSize) {
			using namespace engine::network;
			// CMANGOS.18 (Phase 3.18 step 4) — Dispatch des reponses Mail. Les
			// reponses (opcodes 50/52/54/56/58) ne sont pas des push purs mais
			// arrivent via le meme mecanisme PacketReceived puisque le client
			// n'utilise pas de RequestResponseDispatcher pour Mail (requestId=0
			// fire-and-forget cote envoi). On parse + dispatche ici.
			switch (opcode)
			{
			case kOpcodeMailListInboxResponse:
			{
				auto parsed = ParseMailListInboxResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_LIST_INBOX_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnInboxResponse(*parsed);
				return;
			}
			case kOpcodeMailReadResponse:
			{
				auto parsed = ParseMailReadResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_READ_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnReadResponse(*parsed);
				return;
			}
			case kOpcodeMailSendResponse:
			{
				auto parsed = ParseMailSendResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_SEND_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnSendResponse(*parsed);
				return;
			}
			case kOpcodeMailTakeAttachmentsResponse:
			{
				auto parsed = ParseMailTakeAttachmentsResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_TAKE_ATTACHMENTS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnTakeAttachmentsResponse(*parsed);
				return;
			}
			case kOpcodeMailDeleteResponse:
			{
				auto parsed = ParseMailDeleteResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] MAIL_DELETE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_mailUi.OnDeleteResponse(*parsed);
				return;
			}
			// CMANGOS.23 (Phase 5.23 step 3+4) — Dispatch des reponses Quest
			// (60/62/64/66) + push QuestStateUpdate (67).
			case kOpcodeQuestAcceptResponse:
			{
				auto parsed = ParseQuestAcceptResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] QUEST_ACCEPT_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_questUi.OnQuestAcceptResponse(*parsed);
				return;
			}
			case kOpcodeQuestCompleteResponse:
			{
				auto parsed = ParseQuestCompleteResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] QUEST_COMPLETE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_questUi.OnQuestCompleteResponse(*parsed);
				return;
			}
			case kOpcodeQuestRewardResponse:
			{
				auto parsed = ParseQuestRewardResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] QUEST_REWARD_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_questUi.OnQuestRewardResponse(*parsed);
				return;
			}
			case kOpcodeQuestListResponse:
			{
				auto parsed = ParseQuestListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] QUEST_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_questUi.OnQuestListResponse(*parsed);
				return;
			}
			case kOpcodeQuestStateUpdate:
			{
				auto parsed = ParseQuestStateUpdatePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] QUEST_STATE_UPDATE parse failed (size={})", payloadSize);
					return;
				}
				m_questUi.OnQuestStateUpdate(*parsed);
				return;
			}
			// CMANGOS.25 (Phase 3.25 step 3+4) — Dispatch des reponses IgnoreList
			// (69/71/73). La cache locale du presenter est mise a jour ici.
			case kOpcodeIgnoreAddResponse:
			{
				auto parsed = ParseIgnoreAddResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_ADD_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreAddResponse(*parsed);
				return;
			}
			case kOpcodeIgnoreRemoveResponse:
			{
				auto parsed = ParseIgnoreRemoveResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_REMOVE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreRemoveResponse(*parsed);
				return;
			}
			case kOpcodeIgnoreListResponse:
			{
				auto parsed = ParseIgnoreListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] IGNORE_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_ignoreListUi.OnIgnoreListResponse(*parsed);
				return;
			}
			// CMANGOS.32 (Phase 5.32 step 3+4) — Dispatch des reponses GmTickets
			// (77/79/81) + push GmTicketResolvedNotification (82).
			case kOpcodeGmTicketOpenResponse:
			{
				auto parsed = ParseGmTicketOpenResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_OPEN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnOpenResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketListMineResponse:
			{
				auto parsed = ParseGmTicketListMineResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_LIST_MINE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnListMineResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketCancelResponse:
			{
				auto parsed = ParseGmTicketCancelResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_CANCEL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnCancelResponse(*parsed);
				return;
			}
			case kOpcodeGmTicketResolvedNotification:
			{
				auto parsed = ParseGmTicketResolvedNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GMTICKET_RESOLVED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_gmTicketUi.OnResolvedNotification(*parsed);
				return;
			}
			// CMANGOS.24 (Phase 3.24 step 3+4) — Dispatch des reponses Reputation
			// (96) + push UpdateNotification (97).
			case kOpcodeReputationListResponse:
			{
				auto parsed = ParseReputationListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] REPUTATION_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_reputationUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeReputationUpdateNotification:
			{
				auto parsed = ParseReputationUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] REPUTATION_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_reputationUi.OnUpdateNotification(*parsed);
				return;
			}
			// CMANGOS.39 (Phase 4.39 step 3+4) — Dispatch des reponses Skills
			// (114/116/118) + push UpgradeNotification (119).
			case kOpcodeSkillsListResponse:
			{
				auto parsed = ParseSkillsListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILLS_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeSkillLearnResponse:
			{
				auto parsed = ParseSkillLearnResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_LEARN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnLearnResponse(*parsed);
				return;
			}
			case kOpcodeSkillUseResponse:
			{
				auto parsed = ParseSkillUseResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_USE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnUseResponse(*parsed);
				return;
			}
			case kOpcodeSkillUpgradeNotification:
			{
				auto parsed = ParseSkillUpgradeNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] SKILL_UPGRADE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_skillBookUi.OnUpgradeNotification(*parsed);
				return;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4) — Dispatch des reponses Arena
			// (121/123/125/128) + push notifications (126/129).
			case kOpcodeArenaTeamListResponse:
			{
				auto parsed = ParseArenaTeamListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_TEAM_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnTeamListResponse(*parsed);
				return;
			}
			case kOpcodeArenaQueueResponse:
			{
				auto parsed = ParseArenaQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeArenaLeaveQueueResponse:
			{
				auto parsed = ParseArenaLeaveQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnLeaveQueueResponse(*parsed);
				return;
			}
			case kOpcodeArenaMatchProposalNotification:
			{
				auto parsed = ParseArenaMatchProposalNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchProposalNotification(*parsed);
				return;
			}
			case kOpcodeArenaMatchAcceptResponse:
			{
				auto parsed = ParseArenaMatchAcceptResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_ACCEPT_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchAcceptResponse(*parsed);
				return;
			}
			case kOpcodeArenaMatchResultNotification:
			{
				auto parsed = ParseArenaMatchResultNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] ARENA_MATCH_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_arenaUi.OnMatchResultNotification(*parsed);
				return;
			}
			// CMANGOS.10 (Phase 5 step 3+4) — Dispatch des reponses BattleGround
			// (131/133/135) + push notifications (136/137/138).
			case kOpcodeBgListResponse:
			{
				auto parsed = ParseBgListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeBgQueueResponse:
			{
				auto parsed = ParseBgQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeBgLeaveQueueResponse:
			{
				auto parsed = ParseBgLeaveQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnLeaveQueueResponse(*parsed);
				return;
			}
			case kOpcodeBgMatchStartNotification:
			{
				auto parsed = ParseBgMatchStartNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_MATCH_START_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnMatchStartNotification(*parsed);
				return;
			}
			case kOpcodeBgScoreUpdateNotification:
			{
				auto parsed = ParseBgScoreUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_SCORE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnScoreUpdateNotification(*parsed);
				return;
			}
			case kOpcodeBgMatchEndNotification:
			{
				auto parsed = ParseBgMatchEndNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] BG_MATCH_END_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_battleGroundUi.OnMatchEndNotification(*parsed);
				return;
			}
			// CMANGOS.36 (Phase 5.36 step 3+4) — Dispatch des reponses OutdoorPvp
			// (141/143/145/147) + push notifications (148/149).
			case kOpcodeOutdoorPvpZoneListResponse:
			{
				auto parsed = ParseOutdoorPvpZoneListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_ZONE_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpSubscribeResponse:
			{
				auto parsed = ParseOutdoorPvpSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpUnsubscribeResponse:
			{
				auto parsed = ParseOutdoorPvpUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureStartResponse:
			{
				auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_START_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureStartResponse(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureProgressNotification:
			{
				auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureProgressNotification(*parsed);
				return;
			}
			case kOpcodeOutdoorPvpCaptureCompletedNotification:
			{
				auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_outdoorPvpUi.OnCaptureCompletedNotification(*parsed);
				return;
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Dispatch des reponses Weather
			// (151/153/155) + push notification (156).
			case kOpcodeWeatherListResponse:
			{
				auto parsed = ParseWeatherListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeWeatherSubscribeResponse:
			{
				auto parsed = ParseWeatherSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeWeatherUnsubscribeResponse:
			{
				auto parsed = ParseWeatherUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeWeatherUpdateNotification:
			{
				auto parsed = ParseWeatherUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] WEATHER_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_weatherUi.OnUpdateNotification(*parsed);
				return;
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Dispatch des reponses
			// GameEvents (158/160/162) + push notification (163).
			case kOpcodeGameEventListResponse:
			{
				auto parsed = ParseGameEventListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeGameEventSubscribeResponse:
			{
				auto parsed = ParseGameEventSubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnSubscribeResponse(*parsed);
				return;
			}
			case kOpcodeGameEventUnsubscribeResponse:
			{
				auto parsed = ParseGameEventUnsubscribeResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnUnsubscribeResponse(*parsed);
				return;
			}
			case kOpcodeGameEventStateChangeNotification:
			{
				auto parsed = ParseGameEventStateChangeNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GAME_EVENT_STATE_CHANGE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_gameEventUi.OnStateChangeNotification(*parsed);
				return;
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Dispatch des reponses
			// Guild (165/167/169/171) + push notification (172 MotdUpdate).
			case kOpcodeGuildListResponse:
			{
				auto parsed = ParseGuildListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeGuildMembersResponse:
			{
				auto parsed = ParseGuildMembersResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_MEMBERS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnMembersResponse(*parsed);
				return;
			}
			case kOpcodeGuildPermissionsResponse:
			{
				auto parsed = ParseGuildPermissionsResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_PERMISSIONS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnPermissionsResponse(*parsed);
				return;
			}
			case kOpcodeGuildBankResponse:
			{
				auto parsed = ParseGuildBankResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_BANK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnBankResponse(*parsed);
				return;
			}
			case kOpcodeGuildMotdUpdateNotification:
			{
				auto parsed = ParseGuildMotdUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] GUILD_MOTD_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_guildUi.OnMotdUpdateNotification(*parsed);
				return;
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Dispatch des
			// reponses Auction (174/176/178/180) + push notification 181
			// (AuctionExpired).
			case kOpcodeAuctionListResponse:
			{
				auto parsed = ParseAuctionListResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_LIST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnListResponse(*parsed);
				return;
			}
			case kOpcodeAuctionPostResponse:
			{
				auto parsed = ParseAuctionPostResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_POST_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnPostResponse(*parsed);
				return;
			}
			case kOpcodeAuctionBidResponse:
			{
				auto parsed = ParseAuctionBidResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_BID_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnBidResponse(*parsed);
				return;
			}
			case kOpcodeAuctionCancelResponse:
			{
				auto parsed = ParseAuctionCancelResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_CANCEL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnCancelResponse(*parsed);
				return;
			}
			case kOpcodeAuctionExpiredNotification:
			{
				auto parsed = ParseAuctionExpiredNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] AUCTION_EXPIRED_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_auctionHouseUi.OnExpiredNotification(*parsed);
				return;
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Dispatch des push Loot
			// (182 RollNotification + 185 RollResultNotification) + responses
			// (184 ChoiceResponse + 187 SimulateRollResponse).
			case kOpcodeLootRollNotification:
			{
				auto parsed = ParseLootRollNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnRollNotification(*parsed);
				return;
			}
			case kOpcodeLootRollChoiceResponse:
			{
				auto parsed = ParseLootRollChoiceResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_CHOICE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnChoiceResponse(*parsed);
				return;
			}
			case kOpcodeLootRollResultNotification:
			{
				auto parsed = ParseLootRollResultNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_ROLL_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnRollResultNotification(*parsed);
				return;
			}
			case kOpcodeLootSimulateRollResponse:
			{
				auto parsed = ParseLootSimulateRollResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LOOT_SIMULATE_ROLL_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lootRollUi.OnSimulateRollResponse(*parsed);
				return;
			}
			// Phase 5 step 3+4 Lunar — Dispatch des opcodes 193 (StateResponse)
			// et 194 (PhaseChangeNotification, push). Master autoritaire ; le
			// client recoit l'etat initial sur EnterWorld puis un push toutes
			// les ~21h sur changement de phase.
			case kOpcodeLunarStateResponse:
			{
				engine::network::lunar::LunarStateResponse parsed;
				if (!engine::network::lunar::ParseLunarStateResponsePayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] LUNAR_STATE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				if (parsed.status == engine::network::lunar::LunarStatus::Ok)
				{
					m_dayNight.OnLunarPhaseChange(parsed.phase, parsed.illumination);
					LOG_INFO(Render, "[Engine] LunarState received: phase={} illumination={:.3f}",
						static_cast<unsigned>(parsed.phase), parsed.illumination);
				}
				return;
			}
			case kOpcodeLunarPhaseChangeNotification:
			{
				engine::network::lunar::LunarPhaseChangeNotification parsed;
				if (!engine::network::lunar::ParseLunarPhaseChangeNotificationPayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] LUNAR_PHASE_CHANGE parse failed (size={})", payloadSize);
					return;
				}
				m_dayNight.OnLunarPhaseChange(parsed.newPhase, parsed.newIllumination);
				LOG_INFO(Render, "[Engine] LunarPhaseChange: phase={} illumination={:.3f}",
					static_cast<unsigned>(parsed.newPhase), parsed.newIllumination);
				return;
			}
			// AdminCommand RBAC — reponse master apres validation du role +
			// log audit. Si status==Ok, on dispatch sur le nom de la commande
			// pour appliquer l'effet local (ex: /sky moon -> override visuel).
			// Sinon on log le refus.
			case kOpcodeAdminCommandResponse:
			{
				engine::network::admin::AdminCommandResponse parsed;
				if (!engine::network::admin::ParseAdminCommandResponsePayload(payload, payloadSize, parsed))
				{
					LOG_WARN(Net, "[Engine] ADMIN_COMMAND_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				// Helper inline pour pousser une ligne SRV dans le chat in-game
				// (cf. pattern pushSkyChatLine du send callback).
				auto pushAdminChatLine = [this](const char* sender, const char* fmt, auto... args) {
					char buf[256];
					std::snprintf(buf, sizeof(buf), fmt, args...);
					engine::net::ChatMessage chatMsg;
					chatMsg.timestampUnixMs = static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::system_clock::now().time_since_epoch()).count());
					chatMsg.channel = engine::net::ChatChannel::Server;
					chatMsg.sender  = sender;
					chatMsg.text    = buf;
					m_chatUi.PushNetworkLine(chatMsg);
				};
				if (parsed.status == engine::network::admin::AdminCommandStatus::Ok)
				{
					if (parsed.command == "/sky moon")
					{
						// Parse echoed args : ["phase=7", "illumination=1.000"]
						uint8_t phase = 0;
						float illumination = 0.0f;
						for (const auto& kv : parsed.result)
						{
							if (kv.starts_with("phase="))
							{
								try { phase = static_cast<uint8_t>(std::stoi(kv.substr(6))); }
								catch (...) { phase = 0; }
							}
							else if (kv.starts_with("illumination="))
							{
								try { illumination = std::stof(kv.substr(13)); }
								catch (...) { illumination = 0.0f; }
							}
						}
						m_dayNight.OnLunarPhaseChange(phase, illumination);
						LOG_INFO(Render, "[Sky] /sky moon ACK applied : phase={} illumination={:.3f}",
							static_cast<unsigned>(phase), illumination);
						pushAdminChatLine("[Sky]",
							"OK : moon phase %u illumination=%.0f%% (master autorise)",
							static_cast<unsigned>(phase),
							static_cast<double>(illumination * 100.0f));
					}
					else
					{
						LOG_INFO(Net, "[AdminCommand] OK : command={} (no client effect V1)", parsed.command);
						pushAdminChatLine("[AdminCommand]", "OK : %s", parsed.command.c_str());
					}
				}
				else
				{
					LOG_WARN(Net, "[AdminCommand] REFUSED command={} status={} message={}",
						parsed.command,
						static_cast<unsigned>(parsed.status),
						parsed.message);
					const char* statusName = "ERROR";
					switch (parsed.status)
					{
						case engine::network::admin::AdminCommandStatus::Unauthorized:
							statusName = "UNAUTHORIZED"; break;
						case engine::network::admin::AdminCommandStatus::Denied:
							statusName = "DENIED (role insuffisant)"; break;
						case engine::network::admin::AdminCommandStatus::UnknownCommand:
							statusName = "UNKNOWN_COMMAND"; break;
						case engine::network::admin::AdminCommandStatus::InvalidArgs:
							statusName = "INVALID_ARGS"; break;
						default: break;
					}
					pushAdminChatLine("[AdminCommand]",
						"REFUSE %s : %s -- %s",
						statusName, parsed.command.c_str(), parsed.message.c_str());
				}
				return;
			}
			// CMANGOS.33 (Phase 5.33 step 3+4) — Dispatch des reponses LFG
			// (101/103/105) + push MatchProposalNotification (106).
			case kOpcodeLfgQueueResponse:
			{
				auto parsed = ParseLfgQueueResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnQueueResponse(*parsed);
				return;
			}
			case kOpcodeLfgLeaveResponse:
			{
				auto parsed = ParseLfgLeaveResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_LEAVE_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnLeaveResponse(*parsed);
				return;
			}
			case kOpcodeLfgStatusResponse:
			{
				auto parsed = ParseLfgStatusResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_STATUS_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnStatusResponse(*parsed);
				return;
			}
			case kOpcodeLfgMatchProposalNotification:
			{
				auto parsed = ParseLfgMatchProposalNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] LFG_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_lfgUi.OnMatchProposal(*parsed);
				return;
			}
			// CMANGOS.30 (Phase 5.30 step 3+4) — Dispatch des opcodes Cinematics
			// (push 108 + responses 110/112). 109 (Ack) et 111 (SkipRequest)
			// sont sortants : pas de dispatch ici.
			case kOpcodeCinematicPlayNotification:
			{
				auto parsed = ParseCinematicPlayNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_PLAY_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnPlayNotification(*parsed);
				return;
			}
			case kOpcodeCinematicAckResponse:
			{
				auto parsed = ParseCinematicAckResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_ACK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnAckResponse(*parsed);
				return;
			}
			case kOpcodeCinematicSkipResponse:
			{
				auto parsed = ParseCinematicSkipResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] CINEMATIC_SKIP_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_cinematicUi.OnSkipResponse(*parsed);
				return;
			}
			// CMANGOS.27 (Phase 4.27 step 3+4) — Dispatch des reponses Trade
			// (84/87/89/92) + push notifications (85/90/94).
			case kOpcodeTradeBeginResponse:
			{
				auto parsed = ParseTradeBeginResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_BEGIN_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeBeginResponse(*parsed);
				return;
			}
			case kOpcodeTradeBeginNotification:
			{
				auto parsed = ParseTradeBeginNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_BEGIN_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeBeginNotification(*parsed);
				return;
			}
			case kOpcodeTradeSetOfferResponse:
			{
				auto parsed = ParseTradeSetOfferResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_SET_OFFER_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeSetOfferResponse(*parsed);
				return;
			}
			case kOpcodeTradeLockResponse:
			{
				auto parsed = ParseTradeLockResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_LOCK_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeLockResponse(*parsed);
				return;
			}
			case kOpcodeTradeStateUpdateNotification:
			{
				auto parsed = ParseTradeStateUpdateNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_STATE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeStateUpdate(*parsed);
				return;
			}
			case kOpcodeTradeCommitResponse:
			{
				auto parsed = ParseTradeCommitResponsePayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_COMMIT_RESPONSE parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeCommitResponse(*parsed);
				return;
			}
			case kOpcodeTradeCancelNotification:
			{
				auto parsed = ParseTradeCancelNotificationPayload(payload, payloadSize);
				if (!parsed)
				{
					LOG_WARN(Net, "[Engine] TRADE_CANCEL_NOTIFICATION parse failed (size={})", payloadSize);
					return;
				}
				m_tradeWindowUi.OnTradeCancelNotification(*parsed);
				return;
			}
			default:
				break;
			}
			if (opcode != kOpcodeChatRelay)
				return;
			auto parsed = ParseChatRelayPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] CHAT_RELAY parse failed (size={})", payloadSize);
				return;
			}
			engine::net::ChatChannel ch = engine::net::ChatChannel::Say;
			(void)engine::net::TryDecodeChannelWire(parsed->channel, ch);
			engine::net::ChatMessage msg{};
			msg.timestampUnixMs = parsed->timestampUnixMs;
			msg.channel = ch;
			msg.sender = std::move(parsed->sender);
			msg.text = std::move(parsed->text);
			m_chatUi.PushNetworkLine(msg);
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
			LOG_INFO(Core, "[Boot] glfwInit OK");
			bool surfaceReady = false;
#if defined(_WIN32)
			surfaceReady = true;
#else
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			m_glfwWindowForVk = glfwCreateWindow(1, 1, "VkSurface", nullptr, nullptr);
			if (!m_glfwWindowForVk)
			{
				LOG_WARN(Platform, "[Boot] glfwCreateWindow returned null");
			}
			else
			{
				LOG_INFO(Core, "[Boot] glfwCreateWindow OK");
				surfaceReady = true;
			}
#endif

			if (surfaceReady && m_vkInstance.Create())
			{
				LOG_INFO(Core, "[Boot] VkInstance::Create OK");
#if defined(_WIN32)
				const bool surfaceOk = m_vkInstance.CreateSurface(m_window.GetNativeHandle());
#else
				const bool surfaceOk = m_vkInstance.CreateSurface(m_glfwWindowForVk);
#endif
				if (!surfaceOk)
				{
					LOG_WARN(Platform, "[Boot] VkInstance::CreateSurface failed");
				}
				else
				{
					LOG_INFO(Core, "[Boot] VkInstance::CreateSurface OK");
					if (!m_vkDeviceContext.Create(m_vkInstance.GetHandle(), m_vkInstance.GetSurface()))
					{
						LOG_WARN(Platform, "[Boot] VkDeviceContext::Create failed");
					}
					else
					{
						VkPhysicalDeviceProperties physProps{};
						vkGetPhysicalDeviceProperties(m_vkDeviceContext.GetPhysicalDevice(), &physProps);
						LOG_INFO(Core, "[Boot] VkDeviceContext::Create OK (GPU: {})", physProps.deviceName);

						VkPresentModeKHR requestedMode = VK_PRESENT_MODE_FIFO_KHR;
						if (!m_vsync)
							requestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
						else
						{
							const std::string pm = m_cfg.GetString("render.present_mode", "fifo");
							if (pm == "mailbox")
								requestedMode = VK_PRESENT_MODE_MAILBOX_KHR;
						}

						if (!m_vkSwapchain.Create(
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetDevice(),
							m_vkInstance.GetSurface(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
							m_vkDeviceContext.GetPresentQueueFamilyIndex(),
							static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
							requestedMode))
						{
							LOG_WARN(Platform, "[Boot] VkSwapchain::Create failed");
						}
						else
						{
							VkExtent2D swapExtent = m_vkSwapchain.GetExtent();
							LOG_INFO(Core, "[Boot] VkSwapchain::Create OK (extent={}x{}, images={})",
								swapExtent.width, swapExtent.height, m_vkSwapchain.GetImageCount());

							if (!engine::render::CreateFrameResources(
								m_vkDeviceContext.GetDevice(),
								m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
								m_frameResources))
							{
								LOG_WARN(Platform, "[Boot] FrameSync::Init failed");
							}
							else
							{
								LOG_INFO(Core, "[Boot] FrameSync::Init OK");

								if (m_vkSwapchain.IsValid())
								{
									// STAB.7 fix: VMA is disabled entirely. vmaCreateAllocator / vmaGetAllocatorInfo
									// corrupt the C++ heap on this MSVC build (ABI/CRT mismatch), which later
									// causes SEH 0xC0000005 in unrelated code (e.g. std::mutex::lock).
									// All GPU subsystems already use raw Vulkan allocations, so VMA is not needed.
									m_vmaAllocator = nullptr;
									LOG_INFO(Render, "[Boot] VMA allocator SKIPPED (STAB.7 — all subsystems use raw Vulkan)");

									{
										// M10.4: StagingAllocator with raw Vulkan (no VMA dependency).
										const size_t stagingBudget = m_gpuUploadQueue.GetBudgetBytes();
										if (!m_stagingAllocator.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), stagingBudget))
										{
											LOG_WARN(Render, "[Boot] StagingAllocator init FAILED (budget={} bytes) — streaming GPU uploads disabled", stagingBudget);
										}
										else
										{
											LOG_INFO(Render, "[Boot] StagingAllocator ready (budget={} bytes)", stagingBudget);
										}

										m_pipeline = std::make_unique<engine::render::DeferredPipeline>();

										m_assetRegistry.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), m_vmaAllocator, m_cfg);

										if (!m_profiler.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), 2u))
										{
											LOG_WARN(Core, "[Boot] Profiler init failed — GPU timestamp profiling disabled");
										}
										if (!m_profilerHud.Init())
										{
											LOG_WARN(Core, "[Boot] ProfilerHud init failed — in-game profiler overlay disabled");
										    m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
										}
										if (!m_audioEngine.Init(m_cfg))
										{
											LOG_WARN(Core, "[Boot] AudioEngine init failed — no sound");
										}
										else
										{
											m_audioEngine.SetMasterVolume(static_cast<float>(m_cfg.GetDouble("audio.master_volume", 1.0)));
											m_audioEngine.SetBusVolume("Music", static_cast<float>(m_cfg.GetDouble("audio.music_volume", 1.0)));
											m_audioEngine.SetBusVolume("SFX", static_cast<float>(m_cfg.GetDouble("audio.sfx_volume", 1.0)));
											m_audioEngine.SetBusVolume("UI", static_cast<float>(m_cfg.GetDouble("audio.ui_volume", 1.0)));
											std::string menuMusicRel = m_cfg.GetString("audio.menu_music_path", "");
											if (menuMusicRel.empty())
											{
												// Même piste que zone_audio.json (auth_music) si la clé n’est pas renseignée.
												menuMusicRel = "audio/Horns_of_the_Fallen_Bastion.mp3";
											}
											bool menuStarted = false;
											if (!m_worldEditorExe && !menuMusicRel.empty())
											{
												const std::filesystem::path menuResolved = engine::platform::FileSystem::ResolveContentPath(m_cfg, menuMusicRel);
												if (engine::platform::FileSystem::Exists(menuResolved))
												{
													menuStarted = m_audioEngine.StartMenuMusic(menuResolved);
													if (menuStarted)
													{
														LOG_INFO(Core, "[Boot] Menu music (miniaudio): {}", menuResolved.string());
														// Applique master × bus Music avant la première frame (Tick sinon ignoré si dt==0).
														(void)m_audioEngine.Tick(1.f / 60.f);
													}
												}
												else
												{
													LOG_WARN(Core, "[Boot] audio.menu_music_path missing on disk: {}", menuResolved.string());
												}
											}
											if (!menuStarted)
											{
												m_audioEngine.SetZone(0);
											}
											if (m_worldEditorExe)
											{
												m_audioEngine.StopMenuMusic();
												m_audioEngine.SetMasterVolume(0.0f);
												LOG_INFO(Core, "[Boot] World Editor: audio muet (pas de musique ni de lecture par défaut).");
											}
										}
										m_decalSystem.Init(m_cfg, m_assetRegistry);
										{
											engine::render::DecalComponent decal{};
											decal.center = { 0.0f, 0.05f, 0.0f };
											decal.halfExtents = { 2.0f, 1.0f, 2.0f };
											decal.albedoTexturePath = "textures/test.texr";
											decal.lifetimeSeconds = 30.0f;
											decal.fadeDurationSeconds = 5.0f;
											m_decalSystem.Spawn(decal);
										}

										{
											std::string lutPath = m_cfg.GetString("color_grading.lut_path", "");
											if (!lutPath.empty())
												m_colorGradingLutHandle = m_assetRegistry.LoadTexture(lutPath, true);
										}
										{
											const std::string authBgPath = m_cfg.GetString("render.auth_ui.background_path", "ui/login/background.png");
											if (!authBgPath.empty())
											{
												const std::filesystem::path authBgResolved = engine::platform::FileSystem::ResolveContentPath(m_cfg, authBgPath);
												LOG_INFO(Render, "[Boot] Auth UI background file: {}", authBgResolved.string());
												m_authUiBackgroundTexture = m_assetRegistry.LoadTextureForPresentBlit(authBgPath, m_vkSwapchain.GetImageFormat());
												if (!m_authUiBackgroundTexture.IsValid())
												{
													LOG_WARN(Render, "[Boot] Auth UI background not loaded (decode/path) — check file exists: {}", authBgPath);
												}
												else
												{
													if (!m_assetRegistry.FinalizePresentBlitTextureUpload(
															m_vkDeviceContext.GetGraphicsQueue(),
															m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
													{
														LOG_WARN(Render, "[Boot] Auth UI background GPU upload FAILED — fond absent jusqu'à correction");
													}
													else
													{
														m_authUiBackgroundLayoutReady = true;
														LOG_INFO(Render, "[Boot] Auth UI background prêt (GPU OK): {}", authBgPath);
														LOG_INFO(Render, "[Boot] Auth UI background_blit.fit={} (cover_height|cover|contain|stretch)",
															m_cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
													}
												}
											}
										}
										{
											const std::string authLogoPath = m_cfg.GetString("render.auth_ui.logo_path", "ui/login/logo_login.png");
											if (!authLogoPath.empty())
											{
												m_authLogoTexture = m_assetRegistry.LoadTexture(authLogoPath, true);
												if (!m_authLogoTexture.IsValid())
												{
													LOG_WARN(Render, "[Boot] Auth UI logo not loaded: {}", authLogoPath);
												}
											}
											m_authLogoSuccessTexture = m_assetRegistry.LoadTexture("ui/login/success.png", true);
											if (!m_authLogoSuccessTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI status OK logo not loaded: ui/login/success.png");
											}
											m_authLogoErrorTexture = m_assetRegistry.LoadTexture("ui/login/error.png", true);
											if (!m_authLogoErrorTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI status error logo not loaded: ui/login/error.png");
											}
											m_authUiInfoLoginTexture = m_assetRegistry.LoadTexture("ui/login/info.png", true);
											if (!m_authUiInfoLoginTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI field info icon not loaded: ui/login/info.png");
											}
											m_authUiInfoRegisterTexture = m_assetRegistry.LoadTexture("ui/register/info.png", true);
											if (!m_authUiInfoRegisterTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth UI field info icon not loaded: ui/register/info.png");
											}
											m_authFlagFrTexture = m_assetRegistry.LoadTexture("localization/fr/images/drapeau.png", true);
											if (!m_authFlagFrTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth flag FR not loaded: localization/fr/images/drapeau.png");
											}
											m_authFlagEnTexture = m_assetRegistry.LoadTexture("localization/en/images/drapeau.png", true);
											if (!m_authFlagEnTexture.IsValid())
											{
												LOG_WARN(Render, "[Boot] Auth flag EN not loaded: localization/en/images/drapeau.png");
											}
										}

										engine::render::ImageDesc sceneColorDesc{};
										sceneColorDesc.format    = m_vkSwapchain.GetImageFormat();
										sceneColorDesc.usage     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										sceneColorDesc.transient = true;
										m_fgSceneColorId = m_frameGraph.createImage("SceneColor", sceneColorDesc);

										m_fgBackbufferId = m_frameGraph.createExternalImage("Backbuffer");

										engine::render::ImageDesc gbufADesc{};
										gbufADesc.format = VK_FORMAT_R8G8B8A8_SRGB;
										gbufADesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferAId   = m_frameGraph.createImage("GBufferA", gbufADesc);

										engine::render::ImageDesc decalOverlayDesc{};
										decalOverlayDesc.format = VK_FORMAT_R8G8B8A8_SRGB;
										decalOverlayDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
										m_fgDecalOverlayId = m_frameGraph.createImage("DecalOverlay", decalOverlayDesc);

										engine::render::ImageDesc gbufBDesc{};
										gbufBDesc.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
										gbufBDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferBId   = m_frameGraph.createImage("GBufferB", gbufBDesc);

										engine::render::ImageDesc gbufCDesc{};
										gbufCDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
										gbufCDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferCId   = m_frameGraph.createImage("GBufferC", gbufCDesc);

										engine::render::ImageDesc gbufVelDesc{};
										gbufVelDesc.format = VK_FORMAT_R16G16_SFLOAT;
										gbufVelDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgGBufferVelocityId = m_frameGraph.createImage("GBufferVelocity", gbufVelDesc);

										engine::render::ImageDesc depthDesc{};
										depthDesc.format            = VK_FORMAT_D32_SFLOAT;
										depthDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										depthDesc.isDepthAttachment = true;
										m_fgDepthId = m_frameGraph.createImage("Depth", depthDesc);

										engine::render::ImageDesc sceneColorHDRDesc{};
										sceneColorHDRDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										sceneColorHDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                         | VK_IMAGE_USAGE_SAMPLED_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
										m_fgSceneColorHDRId = m_frameGraph.createImage("SceneColor_HDR", sceneColorHDRDesc);
										// M100.14 — Ping-pong target ecrit par WaterPass (ColorWrite) ou par
										// le fallback Water_Passthrough (vkCmdCopyImage TransferDst). Lu en
										// SampledRead par Bloom_Prefilter / Bloom_Combine.
										m_fgSceneColorHDRPostWaterId = m_frameGraph.createImage("SceneColor_HDR_PostWater", sceneColorHDRDesc);

										engine::render::ImageDesc sceneColorLDRDesc{};
										sceneColorLDRDesc.format = m_vkSwapchain.GetImageFormat();
										sceneColorLDRDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                         | VK_IMAGE_USAGE_SAMPLED_BIT
										                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										m_fgSceneColorLDRId = m_frameGraph.createImage("SceneColor_LDR", sceneColorLDRDesc);

										engine::render::ImageDesc ssaoRawDesc{};
										ssaoRawDesc.format = VK_FORMAT_R16_SFLOAT;
										ssaoRawDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSsaoRawId = m_frameGraph.createImage("SSAO_Raw", ssaoRawDesc);

										engine::render::ImageDesc ssaoBlurDesc{};
										ssaoBlurDesc.format = VK_FORMAT_R16_SFLOAT;
										ssaoBlurDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSsaoBlurTempId = m_frameGraph.createImage("SSAO_Blur_Temp", ssaoBlurDesc);
										m_fgSsaoBlurId     = m_frameGraph.createImage("SSAO_Blur", ssaoBlurDesc);

										engine::render::ImageDesc historyDesc{};
										historyDesc.format = m_vkSwapchain.GetImageFormat();
										historyDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
										                   | VK_IMAGE_USAGE_SAMPLED_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT
										                   | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
										historyDesc.transient = false;
										m_fgHistoryAId = m_frameGraph.createImage("HistoryA", historyDesc);
										m_fgHistoryBId = m_frameGraph.createImage("HistoryB", historyDesc);

										engine::render::ImageDesc bloomMipDesc{};
										bloomMipDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										bloomMipDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										for (uint32_t i = 0; i < engine::render::kBloomMipCount; ++i)
										{
											char name[32];
											bloomMipDesc.extentScalePower = i;
											std::snprintf(name, sizeof(name), "BloomDown_%u", i);
											m_fgBloomDownMipIds[i] = m_frameGraph.createImage(name, bloomMipDesc);
											std::snprintf(name, sizeof(name), "BloomUp_%u", i);
											m_fgBloomUpMipIds[i] = m_frameGraph.createImage(name, bloomMipDesc);
										}
										LOG_INFO(Render, "[Bloom] FrameGraph resources registered: {} down + {} up mips",
											m_fgBloomDownMipIds.size(),
											m_fgBloomUpMipIds.size());

										engine::render::ImageDesc sceneColorHDRWithBloomDesc{};
										sceneColorHDRWithBloomDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
										sceneColorHDRWithBloomDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										m_fgSceneColorHDRWithBloomId = m_frameGraph.createImage("SceneColor_HDR_WithBloom", sceneColorHDRWithBloomDesc);

										const uint32_t shadowRes = static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024));
										engine::render::ImageDesc shadowDesc{};
										shadowDesc.format            = VK_FORMAT_D32_SFLOAT;
										shadowDesc.width             = shadowRes;
										shadowDesc.height            = shadowRes;
										shadowDesc.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
										shadowDesc.isDepthAttachment = true;
										for (uint32_t i = 0; i < engine::render::kCascadeCount; ++i)
										{
											char name[32];
											std::snprintf(name, sizeof(name), "ShadowMap_%u", i);
											m_fgShadowMapIds[i] = m_frameGraph.createImage(name, shadowDesc);
										}

										{
											engine::render::ShaderCompiler sc;
											if (sc.LocateCompiler())
												LOG_INFO(Core, "[Boot] ShaderCompiler OK");
											else
												LOG_WARN(Render, "[Boot] ShaderCompiler glslangValidator not found");
										}

										auto loadSpirv = [&](const char* spvPath) -> std::vector<uint32_t>
										{
											// 1) Essaye de charger directement le .spv depuis game/data
											std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, spvPath);
											if (bytes.size() % 4 == 0 && !bytes.empty())
											{
												std::vector<uint32_t> out(bytes.size() / 4);
												std::memcpy(out.data(), bytes.data(), bytes.size());
												return out;
											}

											// 2) Pas de .spv valide → pas de fallback GLSL (pour éviter les crashes glslangValidator)
											// engine::render::ShaderCompiler compiler;
											// if (!compiler.LocateCompiler()) return {};
											// std::string base(spvPath);
											// if (base.size() > 4 && base.compare(base.size() - 4, 4, ".spv") == 0)
											// 	base.resize(base.size() - 4);
											// std::filesystem::path srcPath = engine::platform::FileSystem::ResolveContentPath(m_cfg, base);
											// engine::render::ShaderStage stage = engine::render::ShaderStage::Vertex;
											// if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".comp") == 0)
											// 	stage = engine::render::ShaderStage::Compute;
											// else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".vert") == 0)
											// 	stage = engine::render::ShaderStage::Vertex;
											// else if (base.size() >= 5 && base.compare(base.size() - 5, 5, ".frag") == 0)
											// 	stage = engine::render::ShaderStage::Fragment;
											// auto c = compiler.CompileGlslToSpirv(srcPath, stage);
											// if (c.has_value() && !c->empty()) return std::move(*c);

											LOG_WARN(Render, "Shader SPIR-V not found or invalid: {}", spvPath);
											return {};
										};

										bool pipelineOk = m_pipeline->Init(
										    m_vkDeviceContext.GetDevice(),
										    m_vkDeviceContext.GetPhysicalDevice(),
										    m_vmaAllocator,
										    m_cfg,
										    static_cast<uint32_t>(m_cfg.GetInt("shadows.resolution", 1024)),
										    m_vkSwapchain.GetImageFormat(),
										    m_vkDeviceContext.GetGraphicsQueue(),
										    m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
										    loadSpirv
										);
									LOG_INFO(Core, "[Boot] DeferredPipeline init OK");

									// M100 — Task 12 : Terrain Chunk Runtime — drawcall mesh-terrain par
									// chunk avec splat 8-layer. Co-existe avec le legacy TerrainRenderer
									// (skip strict si fichiers chunk absents). Cf.
									// docs/superpowers/specs/2026-05-07-terrain-chunk-runtime-design.md.
									if (pipelineOk)
									{
										std::string camErr;
										if (!CreateTerrainChunkCameraResources(camErr))
										{
											LOG_ERROR(Render, "[Engine] TerrainChunk camera resources init failed: {}", camErr);
										}
										else
										{
											m_terrainChunkRenderer = std::make_unique<engine::render::terrain_chunk::TerrainChunkRenderer>();
											std::string err;
											const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
											const std::string shaderRoot = contentRoot + "/shaders";
											// `staging` et `assetRegistry` sont passés mais non utilisés en M100
											// (uploads one-shot via VulkanBufferAllocator interne au renderer ;
											// PBR lookups directs via stb_image). Réservés pour évolutions futures.
											const bool ok = m_terrainChunkRenderer->Init(
												m_vkDeviceContext.GetDevice(),
												m_vkDeviceContext.GetPhysicalDevice(),
												m_pipeline->GetGeometryPass().GetRenderPassLoad(),
												m_terrainChunkCameraSetLayout,
												m_vkDeviceContext.GetGraphicsQueue(),
												m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
												&m_stagingAllocator,
												&m_assetRegistry,
												&m_streamCache,
												m_cfg,
												contentRoot,
												shaderRoot,
												err);
											if (!ok)
											{
												LOG_ERROR(Render, "[Engine] TerrainChunkRenderer init failed: {}", err);
												m_terrainChunkRenderer->Shutdown(m_vkDeviceContext.GetDevice());
												m_terrainChunkRenderer.reset();
												DestroyTerrainChunkCameraResources();
											}
											else
											{
												LOG_INFO(Core, "[Boot] TerrainChunkRenderer init OK");
											}
										}
									}

									// M100.14 — Water render pass FG-intégré.
									// Init WaterMeshGpu (buffer vide, prêt pour Rebuild). Sur les builds
									// STAB.7 (VMA disabled), m_vmaAllocator == nullptr → Init échoue par
									// design : la passe restera invalide et le fallback Water_Passthrough
									// (vkCmdCopyImage) prendra le relais en runtime.
									if (pipelineOk)
									{
										// Crée le command pool long-lived pour les uploads water (RESET entre Rebuilds).
										// Le pool est créé indépendamment du succès de WaterMeshGpu::Init : si VMA
										// est absent (STAB.7) Init échouera et le pool ne sera jamais utilisé,
										// mais il restera valide pour les builds qui réactivent VMA ultérieurement.
										{
											VkCommandPoolCreateInfo poolInfo{};
											poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
											poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
											                          | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
											poolInfo.queueFamilyIndex = m_vkDeviceContext.GetGraphicsQueueFamilyIndex();
											if (vkCreateCommandPool(m_vkDeviceContext.GetDevice(), &poolInfo, nullptr, &m_waterTransferPool) != VK_SUCCESS)
											{
												LOG_WARN(Render, "[Boot] WaterMeshGpu transfer pool creation failed — water rendering will be disabled");
												m_waterTransferPool = VK_NULL_HANDLE;
											}
										}

										if (!m_waterMeshGpu.Init(m_vkDeviceContext.GetDevice(), m_vmaAllocator))
										{
											LOG_WARN(Render,
												"[Boot] WaterMeshGpu::Init failed (vmaAllocator={}) — Water_Passthrough fallback",
												m_vmaAllocator ? "set" : "null (STAB.7)");
										}

										// Init WaterPass : prerequisites = shaders + normalMap + skybox cube.
										// Aucun de ces prerequisites n'est encore exposé par le moteur (pas de
										// skybox cube view, pas de sampler partagé linear-clamp, pas de tile
										// normal-map water dédiée). On passe VK_NULL_HANDLE → Init échoue
										// gracieusement et le fallback Water_Passthrough kicks in.
										std::vector<uint32_t> waterVert = loadSpirv("shaders/water.vert.spv");
										std::vector<uint32_t> waterFrag = loadSpirv("shaders/water.frag.spv");
										VkImageView normalView = VK_NULL_HANDLE;
										VkSampler   normalSamp = VK_NULL_HANDLE;
										VkImageView skyView    = VK_NULL_HANDLE;
										VkSampler   skySamp    = VK_NULL_HANDLE;

										if (!waterVert.empty() && !waterFrag.empty()
											&& normalView != VK_NULL_HANDLE && normalSamp != VK_NULL_HANDLE
											&& skyView != VK_NULL_HANDLE    && skySamp != VK_NULL_HANDLE
											&& m_waterMeshGpu.IsInitialized())
										{
											if (m_waterPass.Init(
													m_vkDeviceContext.GetDevice(),
													m_vkDeviceContext.GetPhysicalDevice(),
													VK_FORMAT_R16G16B16A16_SFLOAT,
													waterVert.data(), waterVert.size(),
													waterFrag.data(), waterFrag.size(),
													normalView, normalSamp,
													skyView,    skySamp,
													2u))
											{
												LOG_INFO(Render, "[Boot] WaterPass init OK");
											}
											else
											{
												LOG_WARN(Render, "[Boot] WaterPass::Init failed — Water_Passthrough fallback");
											}
										}
										else
										{
											LOG_WARN(Render,
												"[Boot] WaterPass : prerequisites missing (vert={} frag={} normalMap={} skybox={}) — Water_Passthrough fallback",
												!waterVert.empty(),
												!waterFrag.empty(),
												normalView != VK_NULL_HANDLE,
												skyView != VK_NULL_HANDLE);
										}
									}

									// Phase 5 Lunar + M38.1 Sky : init du SkyPass (charge sky.vert.spv +
									// sky.frag.spv depuis game/data/shaders, compile pipeline avec push
									// constants etendus pour la phase lunaire). Si l'init echoue
									// (shaders absents, push constants size mismatch, etc.), m_skyPassReady
									// reste false et le rendu fallback sur le clearColor existant.
									if (pipelineOk)
									{
										std::vector<uint32_t> skyVert = loadSpirv("shaders/sky.vert.spv");
										std::vector<uint32_t> skyFrag = loadSpirv("shaders/sky.frag.spv");
										if (!skyVert.empty() && !skyFrag.empty())
										{
											m_skyPassReady = m_skyPass.Init(
												m_vkDeviceContext.GetDevice(),
												m_pipeline->GetGeometryPass().GetRenderPassLoad(),
												/*subpass*/ 0u,
												skyVert.data(), skyVert.size(),
												skyFrag.data(), skyFrag.size());
											if (!m_skyPassReady)
												LOG_WARN(Render, "[Boot] SkyPass init failed -- fallback clearColor");
											else
												LOG_INFO(Render, "[Boot] SkyPass ready (Phase 5 Lunar + M38.1 Sky)");
										}
										else
										{
											LOG_WARN(Render, "[Boot] SkyPass : sky.vert.spv or sky.frag.spv missing -- fallback clearColor");
										}
									}

									// Client jeu : le terrain est toujours tenté (pas de drapeau « enabled ») ;
										// chemin par défaut conventionnel si la clé est absente ou vide.
										if (pipelineOk && !m_worldEditorExe)
										{
											static constexpr const char* kDefaultHeightmapRel = "terrain/heightmap.r16h";
											std::string hmGame = m_cfg.GetString("render.terrain.heightmap", kDefaultHeightmapRel);
											if (hmGame.empty())
												hmGame = kDefaultHeightmapRel;

											const std::string splat = m_cfg.GetString("render.terrain.splatmap", "");
											const std::string grass = m_cfg.GetString("render.terrain.grass_mask", "");
											const std::string hole = m_cfg.GetString("render.terrain.hole_mask", "");
											if (splat.empty())
											{
												LOG_INFO(Core,
													"[Boot] render.terrain.splatmap vide — splat CPU par défaut (herbe) ; "
													"voir docs/world_zone_demo_checklist.md §012.");
											}
											else
											{
												LOG_INFO(Core, "[Boot] render.terrain.splatmap = '{}'", splat);
											}
											if (grass.empty())
											{
												LOG_INFO(Core,
													"[Boot] render.terrain.grass_mask vide — masque herbe nul (ticket 010) ; "
													"aucun fichier GRMS requis.");
											}
											else
											{
												LOG_INFO(Core, "[Boot] render.terrain.grass_mask = '{}'", grass);
											}
											const std::vector<std::string> cliffPaths;
											auto loadFnTerrain = [this](const char* p) { return LoadTerrainSpirvWords(p); };
											if (m_terrain.Init(
													m_vkDeviceContext.GetDevice(),
													m_vkDeviceContext.GetPhysicalDevice(),
													m_cfg,
													hmGame,
													splat,
													grass,
													hole,
													cliffPaths,
													VK_FORMAT_R8G8B8A8_SRGB,
													VK_FORMAT_A2B10G10R10_UNORM_PACK32,
													VK_FORMAT_R8G8B8A8_UNORM,
													VK_FORMAT_R16G16_SFLOAT,
													VK_FORMAT_D32_SFLOAT,
													m_vkDeviceContext.GetGraphicsQueue(),
													m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
													loadFnTerrain))
											{
												LOG_INFO(Core, "[Boot] Terrain (jeu) initialisé: {}", hmGame);
											}
											else
											{
												LOG_WARN(Render, "[Boot] TerrainRenderer::Init (jeu) échec — vérifie le fichier sous paths.content : {}", hmGame);
											}
										}

										if (m_vkDeviceContext.SupportsDynamicRendering())
										{
											std::vector<uint32_t> authGlyphVert = loadSpirv("shaders/auth_glyph.vert.spv");
											std::vector<uint32_t> authGlyphFrag = loadSpirv("shaders/auth_glyph.frag.spv");
											std::vector<uint32_t> authTtfFrag = loadSpirv("shaders/auth_ttf.frag.spv");
											if (!authGlyphVert.empty() && !authGlyphFrag.empty())
											{
												const uint32_t* ttfFragPtr = authTtfFrag.empty() ? nullptr : authTtfFrag.data();
												const size_t ttfFragWords = authTtfFrag.size();
												if (m_authGlyphPass.Init(
														m_vkDeviceContext.GetDevice(),
														m_vkDeviceContext.GetPhysicalDevice(),
														m_vkSwapchain.GetImageFormat(),
														authGlyphVert.data(), authGlyphVert.size(),
														authGlyphFrag.data(), authGlyphFrag.size(),
														8192u,
														VK_NULL_HANDLE,
														ttfFragPtr,
														ttfFragWords))
												{
													LOG_INFO(Render, "[Boot] AuthGlyphPass OK");
													const std::string uiFontPath = m_cfg.GetString("render.auth_ui.font_path", "");
													if (!uiFontPath.empty() && ttfFragPtr != nullptr)
													{
														std::vector<uint8_t> fontBytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, uiFontPath);
														if (!fontBytes.empty())
														{
															const float fontPx = static_cast<float>(std::clamp<int64_t>(
																m_cfg.GetInt("render.auth_ui.font_pixel_height", 28), 12, 96));
															if (m_authGlyphPass.UploadUiFontFromTtf(
																	m_vkDeviceContext.GetDevice(),
																	m_vkDeviceContext.GetPhysicalDevice(),
																	m_vkDeviceContext.GetGraphicsQueue(),
																	m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
																	fontBytes.data(),
																	fontBytes.size(),
																	fontPx))
															{
																LOG_INFO(Render, "[Boot] Auth UI font loaded: {}", uiFontPath);
															}
															else
															{
																LOG_WARN(Render, "[Boot] Auth UI font upload failed: {}", uiFontPath);
															}
														}
														else
														{
															LOG_WARN(Render, "[Boot] Auth UI font file missing or empty: {}", uiFontPath);
														}
													}
													else if (!uiFontPath.empty() && ttfFragPtr == nullptr)
													{
														LOG_WARN(Render, "[Boot] auth_ttf.frag.spv missing — place compiled SPIR-V under game/data/shaders/");
													}
													// --- Second atlas: valeurs de champs (ex. Morpheus.ttf) ---
													const std::string valueFontPath = m_cfg.GetString("render.auth_ui.value_font_path", "");
													if (!valueFontPath.empty() && ttfFragPtr != nullptr)
													{
														std::vector<uint8_t> valueFontBytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, valueFontPath);
														if (!valueFontBytes.empty())
														{
															const float valueFontPx = static_cast<float>(std::clamp<int64_t>(
																m_cfg.GetInt("render.auth_ui.value_font_pixel_height", 24), 12, 96));
															if (m_authGlyphPass.UploadValueFontFromTtf(
																m_vkDeviceContext.GetDevice(),
																m_vkDeviceContext.GetPhysicalDevice(),
																m_vkDeviceContext.GetGraphicsQueue(),
																m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
																valueFontBytes.data(),
																valueFontBytes.size(),
																valueFontPx))
															{
																LOG_INFO(Render, "[Boot] Auth UI value font loaded: {}", valueFontPath);
															}
															else
															{
																LOG_WARN(Render, "[Boot] Auth UI value font upload failed: {}", valueFontPath);
															}
														}
														else
														{
															LOG_WARN(Render, "[Boot] Auth UI value font file missing or empty: {}", valueFontPath);
														}
													}
												}
												else
												{
													LOG_WARN(Render, "[Boot] AuthGlyphPass init failed");
												}
											}
											else
											{
												LOG_WARN(Render, "[Boot] AuthGlyphPass shaders missing");
											}
											std::vector<uint32_t> authLogoVert = loadSpirv("shaders/auth_logo.vert.spv");
											std::vector<uint32_t> authLogoFrag = loadSpirv("shaders/auth_logo.frag.spv");
											if (!authLogoVert.empty() && !authLogoFrag.empty())
											{
												if (m_authLogoPass.Init(
														m_vkDeviceContext.GetDevice(),
														m_vkSwapchain.GetImageFormat(),
														authLogoVert.data(),
														authLogoVert.size(),
														authLogoFrag.data(),
														authLogoFrag.size()))
												{
													LOG_INFO(Render, "[Boot] AuthLogoPass OK");
												}
												else
												{
													LOG_WARN(Render, "[Boot] AuthLogoPass init failed");
												}
											}
											else
											{
												LOG_WARN(Render, "[Boot] AuthLogoPass shaders missing");
											}
										}

										m_frameGraph.addPass("Clear",
											[this](engine::render::PassBuilder& b) {
												b.write(m_fgSceneColorId, engine::render::ImageUsage::TransferDst);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												VkImage img = reg.getImage(m_fgSceneColorId);
												if (img == VK_NULL_HANDLE) return;
												// C3 simplifie : clear color = couleur horizon ciel calculee par
												// DayNightCycle (au lieu d'un gris constant 0.15/0.15/0.18). Donne
												// un ciel dynamique qui change de couleur selon l'heure (bleu jour,
												// orange dawn/dusk, sombre nuit) sans avoir besoin d'une SkyboxPass
												// Vulkan complete. A remplacer par un vrai sky shader plus tard.
												// Log periodique pour verifier que la valeur change quand l'utilisateur
												// modifie le slider Heure dans le panneau Atmosphere.
												const engine::render::DayNightCycle::State& dn = m_dayNight.GetState();
												static float s_lastLoggedTime = -999.f;
												if (std::fabs(dn.timeOfDay - s_lastLoggedTime) > 0.05f)
												{
													s_lastLoggedTime = dn.timeOfDay;
													LOG_INFO(Render, "[Atmosphere] clear color update: time={:.2f}h skyHorizon=({:.2f},{:.2f},{:.2f}) lightColor=({:.2f},{:.2f},{:.2f}) isDay={}",
														dn.timeOfDay,
														dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2],
														dn.lightColor[0], dn.lightColor[1], dn.lightColor[2],
														dn.isDaytime ? 1 : 0);
												}
												VkClearColorValue clearColor = {
													{ dn.skyHorizon[0], dn.skyHorizon[1], dn.skyHorizon[2], 1.0f }
												};
												VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
												vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
											});

										// Etape 2 vue 3eme personne : on charge directement le mesh placeholder
										// avatar (cube 0.5x1.8x0.5 m) au boot. Avant, on chargeait
										// 'meshes/test.mesh' (un simple triangle), utilise comme proxy de scene.
										// L'avatar est positionne a la cible orbitale via out.objectModelMatrix
										// (cf. branche !m_editorMode du Update).
										m_geometryMeshHandle = m_assetRegistry.LoadMesh("meshes/avatar_placeholder.mesh");

										// Texture peau avatar : 1x1 sRGB violet clair (190,140,230). Le materiel
										// dedie m_avatarMaterialId remplace le default fallback (texture blanche
										// invisible sur sol blanc) pour que l'humanoide ressorte clairement.
										m_avatarSkinTextureHandle = m_assetRegistry.LoadTexture("textures/avatar_skin.texr", true);
										if (m_avatarSkinTextureHandle.IsValid() && m_pipeline)
										{
											auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
											if (materialCache.IsValid())
											{
												engine::render::Material avatarMat{};
												avatarMat.baseColor = m_avatarSkinTextureHandle;
												m_avatarMaterialId = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), avatarMat);
												LOG_INFO(Render, "[Avatar] Materiel violet clair enregistre, materialId={}", m_avatarMaterialId);
											}
										}


										m_frameGraph.addPass("GPU_Cull",
											[](engine::render::PassBuilder&) {},
											[this](VkCommandBuffer cmd, engine::render::Registry&) {
												auto& cullingPass = m_pipeline->GetGpuDrivenCullingPass();
												if (!cullingPass.IsValid())
													return;

												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
												uint32_t drawItemCount = 0;
												engine::render::GpuDrawItem drawItem{};

												if (mesh && mesh->vertexBuffer != VK_NULL_HANDLE && mesh->indexBuffer != VK_NULL_HANDLE)
												{
													const float distCam = GetTestMeshDistanceMeters(rs, m_chunkDrawDecisions);
													const uint32_t lodLevel = static_cast<uint32_t>(m_lodConfig.GetLodLevel(distCam));
													drawItem = BuildGpuDrawItem(*mesh, m_geometryMeshHandle.Id(), rs.objectModelMatrix, lodLevel);
													if (drawItem.indexCount > 0)
														drawItemCount = 1;
												}

												if (!cullingPass.UploadDrawItems(m_vkDeviceContext.GetDevice(), m_currentFrame,
														drawItemCount > 0 ? &drawItem : nullptr, drawItemCount))
												{
													LOG_WARN(Render, "[GpuDrivenCulling] Draw-item upload failed");
													return;
												}

												const auto& hiZPass = m_pipeline->GetHiZPyramidPass();
												cullingPass.Record(
													m_vkDeviceContext.GetDevice(), cmd, rs.viewProjMatrix.m, m_currentFrame,
													hiZPass.GetImageView(m_currentFrame),
													hiZPass.GetExtent(),
													hiZPass.GetMipCount());
											});

										// Terrain prepass must not be a separate FrameGraph pass: it targets the same
										// G-buffer attachments as Geometry, and the MVP graph forbids multi-writer.
										// Record terrain inside the Geometry pass before GeometryPass::Record* (LOAD path).

										m_frameGraph.addPass("Geometry",
											[this](engine::render::PassBuilder& b) {
												b.write(m_fgGBufferAId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferBId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferCId,        engine::render::ImageUsage::ColorWrite);
												b.write(m_fgGBufferVelocityId, engine::render::ImageUsage::ColorWrite);
												b.write(m_fgDepthId,           engine::render::ImageUsage::DepthWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												const bool terrainBeforeGeometry = m_terrain.IsValid()
													&& m_pipeline->GetGeometryPass().HasLoadPass();
												if (terrainBeforeGeometry)
												{
													m_terrain.Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
														m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
														rs.camera.position, rs.frustum);
												}
												engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
												const engine::world::GlobalChunkCoord chunk = engine::world::WorldToGlobalChunkCoord(rs.camera.position.x, rs.camera.position.z);
												const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
												const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
												m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
												// Provisoire pour le mesh de test unique a l'origine monde; un ticket dedie
												// remplacera ce calcul par une distance par instance/objet.
												const float distCam = GetTestMeshDistanceMeters(rs, m_chunkDrawDecisions);
												const int lodLevel = m_lodConfig.GetLodLevel(distCam);
												static int s_lastLoggedLod = -1;
												if (lodLevel != s_lastLoggedLod)
												{
													LOG_DEBUG(Render, "[LOD] Geometry test mesh lod={} dist_m={:.2f}", lodLevel, distCam);
													s_lastLoggedLod = lodLevel;
												}
												auto& cullingPass = m_pipeline->GetGpuDrivenCullingPass();
												auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
												if (cullingPass.IsValid())
												{
													m_pipeline->GetGeometryPass().RecordIndirect(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
														cullingPass.GetIndirectBuffer(m_currentFrame),
														cullingPass.GetDrawItemCount(m_currentFrame),
														materialCache.GetDescriptorSet(),
														rs.objectModelMatrix,
														(m_avatarMaterialId != 0u ? m_avatarMaterialId : materialCache.GetDefaultMaterialIndex()),
														terrainBeforeGeometry);
												}
												else
												{
													m_pipeline->GetGeometryPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
														rs.prevViewProjMatrix.m, rs.viewProjMatrix.m, mesh,
														static_cast<uint32_t>(lodLevel),
														materialCache.GetDescriptorSet(),
														rs.objectModelMatrix,
														(m_avatarMaterialId != 0u ? m_avatarMaterialId : materialCache.GetDefaultMaterialIndex()),
														terrainBeforeGeometry);
												}

												// M100 — Task 12 : drawcall terrain chunk (post-Phase-3a).
												// Dessine les chunks visibles qui ont terrain.bin + splat.bin
												// dans une passe de chargement (loadOp=LOAD) après la geometry
												// principale. Skip strict si fichiers absents (legacy
												// TerrainRenderer continue à les dessiner). PAS de branche
												// m_editorEnabled — parité jeu/éditeur garantie par le format
												// binaire identique (cf. critère M100.5/.9).
												if (m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid())
												{
													UpdateTerrainChunkCameraUbo(rs.viewProjMatrix.m);
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														m_world.GetActiveAndVisibleChunks();
													if (!visibleChunks.empty())
													{
														m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
															m_vkDeviceContext.GetDevice(), cmd, reg,
															m_vkSwapchain.GetExtent(),
															m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
															m_fgGBufferVelocityId, m_fgDepthId,
															[this, &visibleChunks](VkCommandBuffer innerCmd) {
																m_terrainChunkRenderer->RenderVisibleChunks(
																	innerCmd,
																	m_terrainChunkCameraSet,
																	m_world,
																	visibleChunks);
															});
													}
												}

												// Phase 5 Lunar + M38.1 Sky : enregistre le draw fullscreen-quad
												// du SkyPass (ciel + disque lunaire procedural) dans le render
												// pass loadOp=LOAD du GeometryPass. SkyPass a ete Init contre
												// `GetRenderPassLoad()` au boot, donc on doit etre dans ce
												// render pass actif pour que vkCmdDraw soit valide. On reuse
												// `RecordTerrainChunkBatch` (qui ouvre exactement ce render
												// pass + framebuffer GBuffer + viewport/scissor) comme
												// wrapper. La pass est emise en fin de lambda Geometry pour
												// que le clear initial du GeometryPass ne l'ecrase pas. Avec
												// le pipeline SkyPass (depthTest=FALSE, depthWrite=FALSE), le
												// fullscreen-quad couvre tout le champ de vision ; les pixels
												// reels de geometrie ont deja ete ecrits dans GBuffer
												// avant. La consommation finale par LightingPass se fait via
												// les autres attachments (depth, normal, ORM) — Sky ne
												// renseigne que GBuffer A (albedo).
												if (m_skyPassReady)
												{
													engine::render::SkyPass::PushConstants skyPc{};

													// Calcul invViewProj inline (meme pattern que Decals/Lighting plus bas
													// dans le fichier — pas de helper Mat4::Inverse global pour l'instant).
													const float* vp = rs.viewProjMatrix.m;
													const float a00=vp[0], a10=vp[1], a20=vp[2],  a30=vp[3];
													const float a01=vp[4], a11=vp[5], a21=vp[6],  a31=vp[7];
													const float a02=vp[8], a12=vp[9], a22=vp[10], a32=vp[11];
													const float a03=vp[12],a13=vp[13],a23=vp[14], a33=vp[15];
													const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
													const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
													const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
													const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
													const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
													if (det > 1e-7f || det < -1e-7f)
													{
														const float invDet = 1.0f / det;
														skyPc.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*invDet;
														skyPc.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*invDet;
														skyPc.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*invDet;
														skyPc.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*invDet;
														skyPc.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*invDet;
														skyPc.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*invDet;
														skyPc.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*invDet;
														skyPc.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*invDet;
														skyPc.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*invDet;
														skyPc.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*invDet;
														skyPc.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*invDet;
														skyPc.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*invDet;
														skyPc.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*invDet;
														skyPc.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*invDet;
														skyPc.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*invDet;
														skyPc.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*invDet;
													}
													else
													{
														// Identity fallback — l'invViewProj sera approximatif mais le
														// shader continuera a tourner sans NaN.
														skyPc.invViewProj[0] = skyPc.invViewProj[5] =
															skyPc.invViewProj[10] = skyPc.invViewProj[15] = 1.0f;
													}

													const auto& dn = m_dayNight.GetState();
													skyPc.lightDir[0]      = dn.lightDir[0];
													skyPc.lightDir[1]      = dn.lightDir[1];
													skyPc.lightDir[2]      = dn.lightDir[2];
													skyPc.zenithColor[0]   = dn.skyZenith[0];
													skyPc.zenithColor[1]   = dn.skyZenith[1];
													skyPc.zenithColor[2]   = dn.skyZenith[2];
													skyPc.horizonColor[0]  = dn.skyHorizon[0];
													skyPc.horizonColor[1]  = dn.skyHorizon[1];
													skyPc.horizonColor[2]  = dn.skyHorizon[2];
													// Lune = direction opposee au soleil (convention LCDLLN).
													skyPc.moonDir[0]       = -dn.lightDir[0];
													skyPc.moonDir[1]       = -dn.lightDir[1];
													skyPc.moonDir[2]       = -dn.lightDir[2];
													skyPc.moonIntensity    = dn.isDaytime ? 0.0f : 1.0f;
													skyPc.moonPhase        = static_cast<float>(dn.moonPhase);
													skyPc.moonIllumination = dn.moonIllumination;

													m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_vkSwapchain.GetExtent(),
														m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
														m_fgGBufferVelocityId, m_fgDepthId,
														[this, &skyPc](VkCommandBuffer innerCmd) {
															m_skyPass.Record(innerCmd, skyPc);
														});
												}
											});

										m_frameGraph.addPass("HiZ_Build",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgDepthId, engine::render::ImageUsage::SampledRead);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												auto& hiZPass = m_pipeline->GetHiZPyramidPass();
												if (!hiZPass.IsValid())
													return;

												hiZPass.Record(
													m_vkDeviceContext.GetDevice(), cmd,
													reg.getImage(m_fgDepthId), reg.getImageView(m_fgDepthId),
													m_vkSwapchain.GetExtent(), m_currentFrame);
											});

										for (uint32_t cascade = 0; cascade < engine::render::kCascadeCount; ++cascade)
										{
											const std::string passName = std::string("ShadowMap_") + std::to_string(cascade);
											m_frameGraph.addPass(passName,
												[this, cascade](engine::render::PassBuilder& b) {
													b.write(m_fgShadowMapIds[cascade], engine::render::ImageUsage::DepthWrite);
												},
												[this, cascade](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetShadowMapPass().IsValid()) return;
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													engine::render::MeshAsset* mesh = rs.objectVisible ? m_geometryMeshHandle.Get() : nullptr;
													const engine::world::GlobalChunkCoord chunk = engine::world::WorldToGlobalChunkCoord(rs.camera.position.x, rs.camera.position.z);
													const engine::world::ChunkRing ring = m_world.GetRingForChunk(chunk);
													const uint32_t triCount = (mesh && mesh->indexCount > 0) ? (mesh->indexCount / 3) : 0;
													m_chunkStats.RecordDraw(chunk, ring, 1, triCount);
													const float depthBiasConstant = static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_constant", 1.25));
													const float depthBiasSlope    = static_cast<float>(m_cfg.GetDouble("shadows.depth_bias_slope", 1.75));
													const bool  cullFrontFaces    = m_cfg.GetBool("shadows.cull_front_faces", false);
													m_pipeline->GetShadowMapPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg,
														m_fgShadowMapIds[cascade],
														rs.cascades.lightViewProj[cascade].m,
														mesh, depthBiasConstant, depthBiasSlope, cullFrontFaces);
												});
										}

										m_frameGraph.addPass("SSAO_Generate",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgDepthId,    engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoRawId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoPass().IsValid()) return;
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::SsaoPass::SsaoParams sp{};
												const float* proj = rs.projMatrix.m;
												const float a00=proj[0], a10=proj[1], a20=proj[2],  a30=proj[3];
												const float a01=proj[4], a11=proj[5], a21=proj[6],  a31=proj[7];
												const float a02=proj[8], a12=proj[9], a22=proj[10], a32=proj[11];
												const float a03=proj[12],a13=proj[13],a23=proj[14], a33=proj[15];
												const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
												const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
												const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
												const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
												const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
												if (det > 1e-7f || det < -1e-7f)
												{
													const float inv = 1.0f / det;
													sp.invProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
													sp.invProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
													sp.invProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
													sp.invProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
													sp.invProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
													sp.invProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
													sp.invProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
													sp.invProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
													sp.invProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
													sp.invProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
													sp.invProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
													sp.invProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
													sp.invProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
													sp.invProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
													sp.invProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
													sp.invProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
												}
												std::memcpy(sp.view, rs.viewMatrix.m, sizeof(sp.view));
												std::memcpy(sp.proj, rs.projMatrix.m, sizeof(sp.proj));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgDepthId, m_fgGBufferBId, m_fgSsaoRawId,
													m_pipeline->GetSsaoKernelNoise().GetKernelBuffer(),
													m_pipeline->GetSsaoKernelNoise().GetNoiseImageView(),
													m_pipeline->GetSsaoKernelNoise().GetNoiseSampler(),
													sp, frameIdx);
											});

										m_frameGraph.addPass("SSAO_BlurH",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSsaoRawId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,         engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoBlurTempId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoBlurPass().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoBlurPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSsaoRawId, m_fgDepthId, m_fgSsaoBlurTempId, true, frameIdx);
											});

										m_frameGraph.addPass("SSAO_BlurV",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSsaoBlurTempId, engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,        engine::render::ImageUsage::SampledRead);
												b.write(m_fgSsaoBlurId,    engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetSsaoBlurPass().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetSsaoBlurPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSsaoBlurTempId, m_fgDepthId, m_fgSsaoBlurId, false, frameIdx);
											});

										if (m_pipeline->GetDecalPass().IsValid())
										{
											m_frameGraph.addPass("Decals",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgDepthId,           engine::render::ImageUsage::SampledRead);
													b.write(m_fgDecalOverlayId,   engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];
													const float* vp = rs.viewProjMatrix.m;
													float invViewProj[16]{};
													const float a00=vp[0], a10=vp[1], a20=vp[2],  a30=vp[3];
													const float a01=vp[4], a11=vp[5], a21=vp[6],  a31=vp[7];
													const float a02=vp[8], a12=vp[9], a22=vp[10], a32=vp[11];
													const float a03=vp[12],a13=vp[13],a23=vp[14], a33=vp[15];
													const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
													const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
													const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
													const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
													const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
													if (det > 1e-7f || det < -1e-7f)
													{
														const float inv = 1.0f / det;
														invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
														invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
														invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
														invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
														invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
														invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
														invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
														invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
														invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
														invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
														invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
														invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
														invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
														invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
														invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
														invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
													}
													m_decalSystem.BuildVisibleList(rs.camera, m_visibleDecals);
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetDecalPass().Record(
														m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
														m_fgDepthId, m_fgDecalOverlayId, invViewProj, m_visibleDecals, frameIdx);
												});
											LOG_INFO(Render, "[Engine] Decal frame-graph pass registered");
										}
										else
										{
											m_frameGraph.addPass("DecalOverlay_Clear",
												[this](engine::render::PassBuilder& b) {
													b.write(m_fgDecalOverlayId, engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage img = reg.getImage(m_fgDecalOverlayId);
													if (img == VK_NULL_HANDLE) return;
													VkClearColorValue clearColor = { { 0.0f, 0.0f, 0.0f, 0.0f } };
													VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
													vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
												});
											LOG_WARN(Render, "[Engine] Decal pass disabled, overlay clear fallback registered");
										}

										m_frameGraph.addPass("Lighting",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgGBufferAId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferBId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferCId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,          engine::render::ImageUsage::SampledRead);
												b.read(m_fgSsaoBlurId,       engine::render::ImageUsage::SampledRead);
												b.read(m_fgDecalOverlayId,   engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetLightingPass().IsValid()) return;
												const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
												const engine::RenderState& rs = m_renderStates[readIdx];
												engine::render::LightingPass::LightParams lp{};
												const float* vp = rs.viewProjMatrix.m;
												const float a00=vp[0], a10=vp[1], a20=vp[2],  a30=vp[3];
												const float a01=vp[4], a11=vp[5], a21=vp[6],  a31=vp[7];
												const float a02=vp[8], a12=vp[9], a22=vp[10], a32=vp[11];
												const float a03=vp[12],a13=vp[13],a23=vp[14], a33=vp[15];
												const float b00=a00*a11-a10*a01, b01=a00*a21-a20*a01, b02=a00*a31-a30*a01;
												const float b03=a10*a21-a20*a11, b04=a10*a31-a30*a11, b05=a20*a31-a30*a21;
												const float b06=a02*a13-a12*a03, b07=a02*a23-a22*a03, b08=a02*a33-a32*a03;
												const float b09=a12*a23-a22*a13, b10=a12*a33-a32*a13, b11=a22*a33-a32*a23;
												const float det = b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
												if (det > 1e-7f || det < -1e-7f)
												{
													const float inv = 1.0f / det;
													lp.invViewProj[0]  = ( a11*b11-a21*b10+a31*b09)*inv;
													lp.invViewProj[1]  = (-a10*b11+a20*b10-a30*b09)*inv;
													lp.invViewProj[2]  = ( a13*b05-a23*b04+a33*b03)*inv;
													lp.invViewProj[3]  = (-a12*b05+a22*b04-a32*b03)*inv;
													lp.invViewProj[4]  = (-a01*b11+a21*b08-a31*b07)*inv;
													lp.invViewProj[5]  = ( a00*b11-a20*b08+a30*b07)*inv;
													lp.invViewProj[6]  = (-a03*b05+a23*b02-a33*b01)*inv;
													lp.invViewProj[7]  = ( a02*b05-a22*b02+a32*b01)*inv;
													lp.invViewProj[8]  = ( a01*b10-a11*b08+a31*b06)*inv;
													lp.invViewProj[9]  = (-a00*b10+a10*b08-a30*b06)*inv;
													lp.invViewProj[10] = ( a03*b04-a13*b02+a33*b00)*inv;
													lp.invViewProj[11] = (-a02*b04+a12*b02-a32*b00)*inv;
													lp.invViewProj[12] = (-a01*b09+a11*b07-a21*b06)*inv;
													lp.invViewProj[13] = ( a00*b09-a10*b07+a20*b06)*inv;
													lp.invViewProj[14] = (-a03*b03+a13*b01-a23*b00)*inv;
													lp.invViewProj[15] = ( a02*b03-a12*b01+a22*b00)*inv;
												}
												lp.cameraPos[0] = rs.camera.position.x;
												lp.cameraPos[1] = rs.camera.position.y;
												lp.cameraPos[2] = rs.camera.position.z;
												lp.cameraPos[3] = 0.0f;
												lp.lightDir[0] = m_zoneAtmosphere.sunDirection[0]; lp.lightDir[1] = m_zoneAtmosphere.sunDirection[1]; lp.lightDir[2] = m_zoneAtmosphere.sunDirection[2]; lp.lightDir[3] = 0.0f;
												lp.lightColor[0] = m_zoneAtmosphere.sunColor[0]; lp.lightColor[1] = m_zoneAtmosphere.sunColor[1]; lp.lightColor[2] = m_zoneAtmosphere.sunColor[2]; lp.lightColor[3] = 0.0f;
												const float probeIntensity = GetGlobalProbeIntensity(m_zoneProbes);
												lp.ambientColor[0] = m_zoneAtmosphere.ambientColor[0] * probeIntensity;
												lp.ambientColor[1] = m_zoneAtmosphere.ambientColor[1] * probeIntensity;
												lp.ambientColor[2] = m_zoneAtmosphere.ambientColor[2] * probeIntensity;
												lp.ambientColor[3] = 0.0f;
												// Couleur du ciel utilisée par lighting.frag pour les pixels
												// sans géométrie (depth==1.0). Pilotée par DayNightCycle pour
												// rendre le cycle jour/nuit visible sans skybox dédié.
												// Phase 5 Lunar (PR #561 fix Concern 3) : skyColor.w sert de
												// flag "SkyPass ready". Quand m_skyPassReady=true, le SkyPass
												// a ecrit la sky procedurale (gradient + sun glow + moon disk
												// avec phase) dans GBufferA pour les pixels depth==1.0 ;
												// lighting.frag lit alors GBufferA. Sinon (SkyPass.Init failed
												// ou shaders absents) on tombe sur la flat skyColor pour
												// preserver le rendu jour/nuit.
												{
													const engine::render::DayNightCycle::State& dnLight = m_dayNight.GetState();
													lp.skyColor[0] = dnLight.skyHorizon[0];
													lp.skyColor[1] = dnLight.skyHorizon[1];
													lp.skyColor[2] = dnLight.skyHorizon[2];
													lp.skyColor[3] = m_skyPassReady ? 1.0f : 0.0f;
												}
												VkImageView irrView       = VK_NULL_HANDLE;
												VkSampler   irrSamp       = VK_NULL_HANDLE;
												VkImageView prefilterView = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetImageView() : VK_NULL_HANDLE;
												VkSampler   prefilterSamp = m_pipeline->GetSpecularPrefilterPass().IsValid() ? m_pipeline->GetSpecularPrefilterPass().GetSampler()    : VK_NULL_HANDLE;
												VkImageView brdfView      = m_pipeline->GetBrdfLutPass().GetImageView();
												VkSampler   brdfSamp      = m_pipeline->GetBrdfLutPass().GetSampler();
												lp.useIBL = (irrView != VK_NULL_HANDLE && prefilterView != VK_NULL_HANDLE && brdfView != VK_NULL_HANDLE) ? 1.0f : 0.0f;
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetLightingPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgDepthId,
													m_fgSceneColorHDRId, m_fgSsaoBlurId, m_fgDecalOverlayId,
													irrView, irrSamp, prefilterView, prefilterSamp, brdfView, brdfSamp,
													lp, frameIdx);
											});

										// M100.14 — Water render pass FG-intégré (ping-pong SceneColor_HDR → SceneColor_HDR_PostWater).
										// Si WaterPass::Init a échoué (shaders, normal map ou skybox absents),
										// on enregistre un passthrough vkCmdCopyImage à la place pour garantir
										// que le ping-pong PostWater est toujours renseigné — sinon Bloom_Prefilter
										// (qui lit PostWater) lirait du contenu indéfini.
										if (m_waterPass.IsValid())
										{
											m_frameGraph.addPass("Water",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::SampledRead);
													b.read(m_fgDepthId,                   engine::render::ImageUsage::SampledRead);
													b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::ColorWrite);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													const engine::world::water::WaterScene* scene = nullptr;
													if (m_worldEditorExe && m_worldEditorShell)
														scene = &m_worldEditorShell->GetWaterDocument().Get();
													else if (m_clientWaterScene)
														scene = m_clientWaterScene.get();
													if (!scene || !m_waterMeshGpu.IsValid()) return;

													const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
													const engine::RenderState& rs = m_renderStates[readIdx];

													engine::render::WaterPassPushConstants base{};
													std::memcpy(base.viewProj, rs.viewProjMatrix.m, sizeof(float) * 16);
													base.cameraPos[0] = rs.camera.position.x;
													base.cameraPos[1] = rs.camera.position.y;
													base.cameraPos[2] = rs.camera.position.z;
													// TODO(M100.x): vraie source de temps wall-clock. Note : perte de précision
													// float après ~78h (uint32_t > 2^24 = 16.7 M frames @ 60Hz).
													base.timeSeconds   = static_cast<float>(m_currentFrame) / 60.0f;
													base.screenSize[0] = static_cast<float>(m_vkSwapchain.GetExtent().width);
													base.screenSize[1] = static_cast<float>(m_vkSwapchain.GetExtent().height);

													const uint32_t frameIdx = m_currentFrame % 2;
													m_waterPass.Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
														m_fgSceneColorHDRId, m_fgDepthId, m_fgSceneColorHDRPostWaterId,
														m_waterMeshGpu, base, *scene, frameIdx);
												});
										}
										else
										{
											// Fallback : copie SceneColor_HDR → SceneColor_HDR_PostWater pour
											// que les passes aval (Bloom_*) lisent une image valide même quand
											// la passe water est désactivée (shaders/textures absents, VMA off, etc.).
											m_frameGraph.addPass("Water_Passthrough",
												[this](engine::render::PassBuilder& b) {
													b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::TransferSrc);
													b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::TransferDst);
												},
												[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
													VkImage src = reg.getImage(m_fgSceneColorHDRId);
													VkImage dst = reg.getImage(m_fgSceneColorHDRPostWaterId);
													if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
													VkImageCopy copy{};
													copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.srcSubresource.layerCount = 1;
													copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													copy.dstSubresource.layerCount = 1;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													copy.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
														dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
												});
										}

										m_frameGraph.addPass("Bloom_Prefilter",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgBloomDownMipIds[0], engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomPrefilterPass().IsValid()) return;
												engine::render::BloomPrefilterPass::PrefilterParams pp{};
												pp.threshold = static_cast<float>(m_cfg.GetDouble("bloom.threshold", 1.0));
												pp.knee      = static_cast<float>(m_cfg.GetDouble("bloom.knee", 0.5));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomPrefilterPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_vkSwapchain.GetExtent(),
													m_fgSceneColorHDRPostWaterId, m_fgBloomDownMipIds[0], pp, frameIdx);
											});

										for (uint32_t i = 0; i < engine::render::kBloomMipCount - 1; ++i)
										{
											const engine::render::ResourceId idSrc = m_fgBloomDownMipIds[i];
											const engine::render::ResourceId idDst = m_fgBloomDownMipIds[i + 1];
											char passName[32];
											std::snprintf(passName, sizeof(passName), "Bloom_Downsample_%u", i);
											m_frameGraph.addPass(passName,
												[this, idSrc, idDst](engine::render::PassBuilder& b) {
													b.read(idSrc, engine::render::ImageUsage::SampledRead);
													b.write(idDst, engine::render::ImageUsage::ColorWrite);
												},
												[this, i, idSrc, idDst](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetBloomDownsamplePass().IsValid()) return;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													VkExtent2D extentDst{ std::max(1u, ext.width >> (i+1)), std::max(1u, ext.height >> (i+1)) };
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetBloomDownsamplePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, extentDst, idSrc, idDst, frameIdx);
												});
										}

										for (uint32_t ii = engine::render::kBloomMipCount - 1; ii-- > 0; )
										{
											const uint32_t i = ii;
											// src = niveau du dessous dans le pyramid upsample (ou down si premier)
											const engine::render::ResourceId idSrc = (i == engine::render::kBloomMipCount - 2)
												? m_fgBloomDownMipIds[i + 1]
												: m_fgBloomUpMipIds[i + 1];
											const engine::render::ResourceId idDst = m_fgBloomUpMipIds[i];
											char passName[32];
											std::snprintf(passName, sizeof(passName), "Bloom_Upsample_%u", i);
											m_frameGraph.addPass(passName,
												[this, idSrc, idDst](engine::render::PassBuilder& b) {
													b.read(idSrc, engine::render::ImageUsage::SampledRead);
													b.write(idDst, engine::render::ImageUsage::ColorWrite);
												},
												[this, i, idSrc, idDst](VkCommandBuffer cmd, engine::render::Registry& reg) {
													if (!m_pipeline->GetBloomUpsamplePass().IsValid()) return;
													VkExtent2D ext = m_vkSwapchain.GetExtent();
													VkExtent2D extentDst{ std::max(1u, ext.width >> i), std::max(1u, ext.height >> i) };
													const uint32_t frameIdx = m_currentFrame % 2;
													m_pipeline->GetBloomUpsamplePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, extentDst, idSrc, idDst, frameIdx);
												});
										}

										m_frameGraph.addPass("Bloom_Combine",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRPostWaterId,  engine::render::ImageUsage::SampledRead);
												b.read(m_fgBloomUpMipIds[0],         engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetBloomCombinePass().IsValid()) return;
												engine::render::BloomCombinePass::CombineParams cp{};
												cp.intensity = static_cast<float>(m_cfg.GetDouble("bloom.intensity", 1.0));
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRPostWaterId, m_fgBloomUpMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
											});

										m_frameGraph.addPass("AutoExposure_Luminance",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetAutoExposure().IsValid()) return;
												const uint32_t frameIdx = m_currentFrame % 2u;
												m_pipeline->GetAutoExposure().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg,
													m_fgSceneColorHDRWithBloomId, m_vkSwapchain.GetExtent(), frameIdx);
											});

										m_frameGraph.addPass("Tonemap",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorHDRWithBloomId, engine::render::ImageUsage::SampledRead);
												b.write(m_fgSceneColorLDRId,         engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												if (!m_pipeline->GetTonemapPass().IsValid()) return;
												engine::render::TonemapPass::TonemapParams tp{};
												tp.exposure = m_pipeline->GetAutoExposure().IsValid()
													? m_pipeline->GetAutoExposure().GetExposure()
													: static_cast<float>(m_cfg.GetDouble("tonemap.exposure", 1.0));
												bool lutEnabled = m_cfg.GetBool("color_grading.enable", false) && m_colorGradingLutHandle.IsValid();
												tp.strength = lutEnabled ? static_cast<float>(m_cfg.GetDouble("color_grading.strength", 1.0)) : 0.0f;
												VkImageView lutView = VK_NULL_HANDLE;
												if (lutEnabled) { engine::render::TextureAsset* lutTex = m_colorGradingLutHandle.Get(); if (lutTex && lutTex->view != VK_NULL_HANDLE) lutView = lutTex->view; }
												const uint32_t frameIdx = m_currentFrame % 2;
												m_pipeline->GetTonemapPass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRWithBloomId, m_fgSceneColorLDRId, tp, lutView, frameIdx);
											});

										m_frameGraph.addPass("TAA",
											[this](engine::render::PassBuilder& b) {
												// Lecture pour TAA
												b.read(m_fgSceneColorLDRId,   engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryAId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgHistoryBId,        engine::render::ImageUsage::SampledRead);
												b.read(m_fgGBufferVelocityId, engine::render::ImageUsage::SampledRead);
												b.read(m_fgDepthId,           engine::render::ImageUsage::SampledRead);
												// Lecture supplémentaire pour init d'historique (copies image)
												b.read(m_fgSceneColorLDRId,   engine::render::ImageUsage::TransferSrc);
												// Historiques écrits uniquement par cette passe
												b.write(m_fgHistoryAId,       engine::render::ImageUsage::ColorWrite);
												b.write(m_fgHistoryBId,       engine::render::ImageUsage::ColorWrite);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												// Bootstrap the history on first use, even if Update() already cleared
												// m_taaHistoryInvalid for this frame.
												if (!m_taaHistoryEverFilled || m_taaHistoryInvalid)
												{
													VkImage srcImg = reg.getImage(m_fgSceneColorLDRId);
													if (srcImg != VK_NULL_HANDLE)
													{
														VkExtent2D ext = m_vkSwapchain.GetExtent();
														VkImageCopy region{};
														region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
														region.extent        = { ext.width, ext.height, 1 };

														// Helper: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_DST_OPTIMAL
													auto toTransferDst = [&](VkImage img) {
														VkImageMemoryBarrier bar{};
														bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
														bar.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
														bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.image = img;
														bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															VK_PIPELINE_STAGE_TRANSFER_BIT,
															0, 0, nullptr, 0, nullptr, 1, &bar);
													};
													// Helper: TRANSFER_DST_OPTIMAL -> COLOR_ATTACHMENT_OPTIMAL
													auto toColorAttachment = [&](VkImage img) {
														VkImageMemoryBarrier bar{};
														bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
														bar.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
														bar.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bar.image = img;
														bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_TRANSFER_BIT,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															0, 0, nullptr, 0, nullptr, 1, &bar);
													};

													if (!m_taaHistoryEverFilled)
													{
														VkImage dstA = reg.getImage(m_fgHistoryAId);
														VkImage dstB = reg.getImage(m_fgHistoryBId);
														if (dstA != VK_NULL_HANDLE && dstB != VK_NULL_HANDLE)
														{
															toTransferDst(dstA);
															toTransferDst(dstB);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstA,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstB,   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															toColorAttachment(dstA);
															toColorAttachment(dstB);
															m_taaHistoryEverFilled = true;
															m_taaHistoryInvalid = false;
															return;
														}
													}
													else
													{
														engine::render::ResourceId nextId = GetTaaHistoryNextId();
														VkImage dstNext = reg.getImage(nextId);
														if (dstNext != VK_NULL_HANDLE)
														{
															toTransferDst(dstNext);
															vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
															               dstNext, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															               1, &region);
															toColorAttachment(dstNext);
															m_taaHistoryInvalid = false;
															return;
														}
													}
													}
												}

												if (!m_pipeline->GetTaaPass().IsValid()) return;
												engine::render::TaaPass::TaaParams tp{};
												tp.alpha = 0.9f; tp._pad[0] = tp._pad[1] = tp._pad[2] = 0.0f;
												const uint32_t frameIdx = m_currentFrame % 2u;
												m_pipeline->GetTaaPass().Record(
													m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
													m_fgSceneColorLDRId,
													GetTaaHistoryPrevId(),
													m_fgGBufferVelocityId,
													m_fgDepthId,
													GetTaaHistoryNextId(),
													tp,
													frameIdx);
											});

										m_frameGraph.addPass("CopyPresent",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgHistoryAId,      engine::render::ImageUsage::TransferSrc);
												b.read(m_fgHistoryBId,      engine::render::ImageUsage::TransferSrc);
												b.read(m_fgSceneColorLDRId, engine::render::ImageUsage::TransferSrc);
												b.write(m_fgBackbufferId,   engine::render::ImageUsage::TransferDst);
											},
											[this](VkCommandBuffer cmd, engine::render::Registry& reg) {
												engine::render::ResourceId srcId = m_pipeline->GetTaaPass().IsValid() ? GetTaaHistoryNextId() : m_fgSceneColorLDRId;
												VkImage srcImg = reg.getImage(srcId);
												VkImage dstImg = reg.getImage(m_fgBackbufferId);
												if (srcImg == VK_NULL_HANDLE || dstImg == VK_NULL_HANDLE) return;
												VkExtent2D ext = m_vkSwapchain.GetExtent();
												bool authPhotoBackdrop = false;
												const bool presentSolidColorDebug = m_cfg.GetBool("render.debug_present_solid_color.enabled", false);
												if (presentSolidColorDebug)
												{
													LOG_DEBUG(Render, "[CopyPresent] debug solid-color present enabled; skipping scene copy");
													const VkClearColorValue debugColor = { { 0.9f, 0.0f, 0.9f, 1.0f } };
													VkImageSubresourceRange clearRange{};
													clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
													clearRange.baseMipLevel = 0;
													clearRange.levelCount = 1;
													clearRange.baseArrayLayer = 0;
													clearRange.layerCount = 1;
													vkCmdClearColorImage(cmd, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &debugColor, 1, &clearRange);
													LOG_DEBUG(Render, "[CopyPresent] debug clear color applied");
												}
												else
												{
													LOG_DEBUG(Render, "[CopyPresent] vkCmdCopyImage begin");
													// Use a direct copy for presentation. Some Intel/swapchain combinations are fragile
													// with vkCmdBlitImage here even when source and destination extents match.
													VkImageCopy region{};
													region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.srcOffset = { 0, 0, 0 };
													region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
													region.dstOffset = { 0, 0, 0 };
													region.extent = { ext.width, ext.height, 1 };
													vkCmdCopyImage(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
													LOG_DEBUG(Render, "[CopyPresent] vkCmdCopyImage done");
												}
												const engine::client::AuthUiPresenter::VisualState authVisualState = m_authUi.GetVisualState();
												const bool authBgBlitWanted = authVisualState.active && m_authUiBackgroundTexture.IsValid()
													&& m_cfg.GetBool("render.auth_ui.background_blit.enabled", true) && !presentSolidColorDebug;
												if (authBgBlitWanted)
												{
													engine::render::TextureAsset* bgTex = m_authUiBackgroundTexture.Get();
													if (bgTex && bgTex->image != VK_NULL_HANDLE && bgTex->width > 0u && bgTex->height > 0u)
													{
														const AuthBackgroundBlitFit authBgFit = ParseAuthBackgroundBlitFit(
															m_cfg.GetString("render.auth_ui.background_blit.fit", "cover_height"));
														static bool s_authBgBlitLogOnce = false;
														if (!s_authBgBlitLogOnce)
														{
															s_authBgBlitLogOnce = true;
															const char* fitName = "cover_height";
															switch (authBgFit)
															{
															case AuthBackgroundBlitFit::Stretch:
																fitName = "stretch";
																break;
															case AuthBackgroundBlitFit::Contain:
																fitName = "contain";
																break;
															case AuthBackgroundBlitFit::Cover:
																fitName = "cover";
																break;
															case AuthBackgroundBlitFit::CoverHeight:
															default:
																fitName = "cover_height";
																break;
															}
															LOG_INFO(Render,
																"[CopyPresent] auth fond: blit {}x{} → {}x{} fit={} (log unique; blit chaque frame)",
																bgTex->width,
																bgTex->height,
																ext.width,
																ext.height,
																fitName);
														}
														VkImageMemoryBarrier bgSrc{};
														bgSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														bgSrc.srcAccessMask = m_authUiBackgroundLayoutReady ? VK_ACCESS_TRANSFER_READ_BIT : 0;
														bgSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
														bgSrc.oldLayout = m_authUiBackgroundLayoutReady ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PREINITIALIZED;
														bgSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
														bgSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bgSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														bgSrc.image = bgTex->image;
														bgSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														const VkPipelineStageFlags bgSrcStages = m_authUiBackgroundLayoutReady ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
														vkCmdPipelineBarrier(cmd, bgSrcStages, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bgSrc);
														VkImageBlit blit{};
														BuildAuthBackgroundBlit(authBgFit, bgTex->width, bgTex->height, ext.width, ext.height, blit);
														vkCmdBlitImage(cmd, bgTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
														m_authUiBackgroundLayoutReady = true;
														authPhotoBackdrop = true;
													}
												}
												const bool authUiDynamicRenderingEnabled = m_cfg.GetBool("render.auth_ui_dynamic_rendering.enabled", true);
												const VkImageView backbufferView = reg.getImageView(m_fgBackbufferId);

												bool renderedAuthUi = false;
												const bool authImguiSkipsVkOverlay = m_authImGui != nullptr
													&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false);
												if (authVisualState.active
													&& authUiDynamicRenderingEnabled
													&& !authImguiSkipsVkOverlay
													&& backbufferView != VK_NULL_HANDLE
													&& m_vkDeviceContext.SupportsDynamicRendering())
												{
													LOG_DEBUG(Render, "[CopyPresent] auth overlay enabled; building model");
													const engine::client::AuthUiPresenter::RenderModel authRenderModel = m_authUi.BuildRenderModel();
													LOG_DEBUG(Render, "[CopyPresent] auth render model built; loading theme");
													const engine::render::AuthUiTheme authTheme = engine::render::LoadAuthUiTheme(m_cfg);
													LOG_DEBUG(Render, "[CopyPresent] auth theme loaded; issuing barriers");
													VkImageMemoryBarrier toColor{};
													toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
													toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
													toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
													toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
													toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
													toColor.image = dstImg;
													toColor.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
													vkCmdPipelineBarrier(cmd,
														VK_PIPELINE_STAGE_TRANSFER_BIT,
														VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
														0, 0, nullptr, 0, nullptr, 1, &toColor);

													LOG_DEBUG(Render, "[CopyPresent] begin rendering attachment");
													// IMPORTANT: If we end up calling the KHR entrypoints, we must pass the KHR
													// structs/sType values (some drivers crash if you mix core structs with KHR fns).
													VkRenderingAttachmentInfo colorAttachment{};
													colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
													colorAttachment.imageView = backbufferView;
													colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													colorAttachment.loadOp = authPhotoBackdrop ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
													colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
													colorAttachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

													VkRenderingAttachmentInfoKHR colorAttachmentKHR{};
													colorAttachmentKHR.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
													colorAttachmentKHR.imageView = backbufferView;
													colorAttachmentKHR.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
													colorAttachmentKHR.loadOp = authPhotoBackdrop ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
													colorAttachmentKHR.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
													colorAttachmentKHR.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
													LOG_DEBUG(Render, "[CopyPresent] attachment info ready (view={})", (void*)backbufferView);

													VkRenderingInfo renderingInfo{};
													renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
													renderingInfo.renderArea.offset = { 0, 0 };
													renderingInfo.renderArea.extent = ext;
													renderingInfo.layerCount = 1;
													renderingInfo.colorAttachmentCount = 1;
													renderingInfo.pColorAttachments = &colorAttachment;

													VkRenderingInfoKHR renderingInfoKHR{};
													renderingInfoKHR.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
													renderingInfoKHR.renderArea.offset = { 0, 0 };
													renderingInfoKHR.renderArea.extent = ext;
													renderingInfoKHR.layerCount = 1;
													renderingInfoKHR.colorAttachmentCount = 1;
													renderingInfoKHR.pColorAttachments = &colorAttachmentKHR;
													LOG_INFO(Render,
														"[CopyPresent] renderingInfo renderArea.extent width={} height={}",
														ext.width,
														ext.height);
													LOG_DEBUG(Render, "[CopyPresent] vkCmdBeginRendering call (proc lookup)");

													// Pointeurs résolus à la création du device (VkDeviceContext) — évite les nullptr
													// du loader et les incohérences avec une instance Vulkan < 1.3.
													const PFN_vkCmdBeginRendering pfnBeginCore = m_vkDeviceContext.GetCmdBeginRenderingCore();
													const PFN_vkCmdEndRendering pfnEndCoreStored = m_vkDeviceContext.GetCmdEndRenderingCore();
													const PFN_vkCmdBeginRenderingKHR pfnBeginKHR = m_vkDeviceContext.GetCmdBeginRenderingKHR();
													const PFN_vkCmdEndRenderingKHR pfnEndKHRStored = m_vkDeviceContext.GetCmdEndRenderingKHR();
													LOG_INFO(Render,
														"[CopyPresent] proc addresses (device ctx): beginCore={} endCore={} beginKHR={} endKHR={}",
														(void*)pfnBeginCore, (void*)pfnEndCoreStored, (void*)pfnBeginKHR, (void*)pfnEndKHRStored);

													bool didBeginRendering = false;
													bool beganWithKHR = false;
													PFN_vkCmdEndRendering pfnEndCore = nullptr;
													PFN_vkCmdEndRenderingKHR pfnEndKHR = nullptr;
													// Préférer KHR si les deux paires sont présentes (certains loaders / ICD).
													if (pfnBeginKHR && pfnEndKHRStored)
													{
														pfnBeginKHR(cmd, &renderingInfoKHR);
														didBeginRendering = true;
														beganWithKHR = true;
														pfnEndKHR = pfnEndKHRStored;
													}
													else if (pfnBeginCore && pfnEndCoreStored)
													{
														pfnBeginCore(cmd, &renderingInfo);
														didBeginRendering = true;
														beganWithKHR = false;
														pfnEndCore = pfnEndCoreStored;
													}
													else
													{
														LOG_ERROR(Render, "[CopyPresent] dynamic rendering entrypoints not found; skipping auth UI overlay");
													}

													if (!didBeginRendering)
													{
														// No active rendering: do not clear attachments / do not end rendering.
													}
													else
													{
														LOG_DEBUG(Render, "[CopyPresent] vkCmdBeginRendering returned");

														LOG_DEBUG(Render, "[CopyPresent] building UI layers");
														const bool authCalibOverlay = m_cfg.GetBool("render.auth_ui_calibration_overlay.enabled", false);
														const std::vector<engine::render::AuthUiLayer> layers =
															engine::render::BuildAuthUiLayers(ext, authVisualState, authRenderModel, authTheme, authCalibOverlay, authPhotoBackdrop);
														LOG_DEBUG(Render, "[CopyPresent] UI layers built; clearing attachments");
														for (const engine::render::AuthUiLayer& layer : layers)
														{
															VkClearAttachment clearAttachment{};
															clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
															clearAttachment.colorAttachment = 0;
															clearAttachment.clearValue.color = layer.color;
															vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &layer.rect);
														}
														LOG_DEBUG(Render, "[CopyPresent] UI layers cleared; recording glyphs (if valid)");
														// Dessiner le logo AVANT le texte pour éviter qu’un PNG opaque ne recouvre les glyphes.
														// Logo statut : uniquement pendant la requête HTTP (pas quand le cache est à jour).
														const bool showAuthStatusLogo = authVisualState.login
															&& authVisualState.authLogoSpin;
														if (m_authLogoPass.IsValid() && showAuthStatusLogo)
														{
															engine::render::TextureAsset* logoTex = nullptr;
															if (authVisualState.authLogoSpin && m_authLogoTexture.IsValid())
															{
																logoTex = m_authLogoTexture.Get();
															}
															else if (authVisualState.authStatusOk && m_authLogoSuccessTexture.IsValid())
															{
																logoTex = m_authLogoSuccessTexture.Get();
															}
															else if (!authVisualState.authStatusOk && m_authLogoErrorTexture.IsValid())
															{
																logoTex = m_authLogoErrorTexture.Get();
															}
															else if (m_authLogoTexture.IsValid())
															{
																logoTex = m_authLogoTexture.Get();
															}
															if (logoTex && logoTex->image != VK_NULL_HANDLE && logoTex->view != VK_NULL_HANDLE)
															{
																const float half = static_cast<float>(m_authUi.GetAuthLogoSizePx()) * 0.5f;
																const float margin =
																	static_cast<float>(engine::render::kAuthUiStatusLogoCornerMarginPx);
																const float cx = margin + half;
																// Ajustement repère vertical : le shader du logo attend un centre "haut-gauche"
																// alors que le rendu actuel le place en bas-gauche.
																const float cy = static_cast<float>(ext.height) - (margin + half);
																// L’orientation 180° est appliquée dans AuthLogoPass ; ici uniquement l’angle de spin chargement.
																const float spin = authVisualState.authLogoSpin ? m_authUi.GetAuthLogoRotationRadians() : 0.f;
																m_authLogoPass.Record(
																	m_vkDeviceContext.GetDevice(),
																	cmd,
																	ext,
																	logoTex->image,
																	logoTex->view,
																	m_authLogoImageLayoutReady,
																	cx,
																	cy,
																	half,
																	spin);
															}
														}
														{
															const std::vector<engine::render::AuthFieldInfoIconLayout> infoLayouts =
																engine::render::BuildAuthFieldInfoIconLayouts(ext, authVisualState, authRenderModel);
															for (const engine::render::AuthFieldInfoIconLayout& iconLay : infoLayouts)
															{
																if (!iconLay.valid)
																{
																	continue;
																}
																engine::render::TextureHandle& infoTex = authVisualState.registerMode
																	? m_authUiInfoRegisterTexture
																	: m_authUiInfoLoginTexture;
																bool& infoLayoutReady = authVisualState.registerMode
																	? m_authUiInfoRegisterLayoutReady
																	: m_authUiInfoLoginLayoutReady;
																engine::render::TextureAsset* it = infoTex.Get();
																if (!it || it->image == VK_NULL_HANDLE || it->view == VK_NULL_HANDLE)
																{
																	continue;
																}
																m_authLogoPass.Record(
																	m_vkDeviceContext.GetDevice(),
																	cmd,
																	ext,
																	it->image,
																	it->view,
																	infoLayoutReady,
																	iconLay.centerXPx,
																	iconLay.centerYPx,
																	iconLay.halfExtentPx,
																	0.f,
																	0.f);
															}
														}
														{
															if (authRenderModel.languageFirstRunLayout && m_authLogoPass.IsValid())
															{
																const engine::render::AuthUiLayoutMetrics layLang =
																	engine::render::BuildAuthUiLayoutMetrics(ext, authVisualState, authRenderModel);
																for (int32_t ci = 0; ci < layLang.languageCardCount; ++ci)
																{
																	if (static_cast<size_t>(ci) >= authRenderModel.languageFirstRunCards.size())
																	{
																		break;
																	}
																	const std::string& tag =
																		authRenderModel.languageFirstRunCards[static_cast<size_t>(ci)].localeTag;
																	engine::render::TextureAsset* flagTex = nullptr;
																	bool* flagLayoutReady = nullptr;
																	if (tag == "fr")
																	{
																		flagTex = m_authFlagFrTexture.Get();
																		flagLayoutReady = &m_authFlagFrLayoutReady;
																	}
																	else if (tag == "en")
																	{
																		flagTex = m_authFlagEnTexture.Get();
																		flagLayoutReady = &m_authFlagEnLayoutReady;
																	}
																	if (flagTex == nullptr || flagTex->image == VK_NULL_HANDLE || flagTex->view == VK_NULL_HANDLE
																		|| flagLayoutReady == nullptr)
																	{
																		continue;
																	}
																	m_authLogoPass.Record(
																		m_vkDeviceContext.GetDevice(),
																		cmd,
																		ext,
																		flagTex->image,
																		flagTex->view,
																		*flagLayoutReady,
																		static_cast<float>(layLang.languageFlagCenterX[ci]),
																		static_cast<float>(layLang.languageFlagCenterY[ci]),
																		static_cast<float>(layLang.languageFlagHalfExtentPx[ci]),
																		0.f,
																		0.f);
																}
															}
														}
														if (m_authGlyphPass.IsValid())
														{
															m_authGlyphPass.RecordModel(
																m_vkDeviceContext.GetDevice(),
																cmd,
																ext,
																authVisualState,
																authRenderModel,
																authTheme);
														}

														if (beganWithKHR && pfnEndKHR)
															pfnEndKHR(cmd);
														else if (!beganWithKHR && pfnEndCore)
															pfnEndCore(cmd);
														LOG_DEBUG(Render, "[CopyPresent] vkCmdEndRendering done; barrier to present");

														VkImageMemoryBarrier toPresent{};
														toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
														toPresent.dstAccessMask = 0;
														toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
														toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
														toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														toPresent.image = dstImg;
														toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
															VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
															0, 0, nullptr, 0, nullptr, 1, &toPresent);
														renderedAuthUi = true;
													}
												}

												if (!renderedAuthUi)
												{
													if (authVisualState.active && !authUiDynamicRenderingEnabled)
														LOG_DEBUG(Render, "[CopyPresent] auth dynamic rendering disabled by config; using present-only path");
													if (authVisualState.active && backbufferView == VK_NULL_HANDLE)
														LOG_DEBUG(Render, "[CopyPresent] backbuffer imageView is null; skipping auth UI overlay");

													bool worldEditorUiToPresent = false;
#if defined(_WIN32)
													// Phase 3.11.1 — RecordToBackbuffer fire aussi quand le panneau chat est actif post-auth
													// (le draw list ImGui contient alors la fenêtre chat émise par ChatImGuiRenderer).
													// Responsabilite : chat HUD VISIBLE uniquement si auth INITIALISEE *et* COMPLETE.
													// Sans `IsInitialized()`, un echec d'init de localisation fait passer authGateActive
													// a false (m_initialized=false) et le chat apparaissait alors par-dessus un ecran noir.
													const bool chatImguiActive = m_chatImGui && m_chatUi.IsInitialized()
														&& m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
														&& !m_worldEditorExe
														&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible);
													// M43.4 — RecordToBackbuffer également quand --editor (sans world-editor).
													const bool editorHubActive = m_editorHubImGui && m_editorEnabled && !m_worldEditorExe;
													if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
														&& (m_worldEditorExe
															|| (m_authImGui && authVisualState.active
																&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false))
															|| chatImguiActive
															|| editorHubActive)
														&& m_vkDeviceContext.SupportsDynamicRendering() && backbufferView != VK_NULL_HANDLE
														&& !presentSolidColorDebug)
													{
														worldEditorUiToPresent = m_worldEditorImGui->RecordToBackbuffer(
															cmd, dstImg, backbufferView, ext, m_vkDeviceContext);
													}
#endif
													if (!worldEditorUiToPresent)
													{
														VkImageMemoryBarrier barrier{};
														barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
														barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
														barrier.dstAccessMask = 0;
														barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
														barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
														barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
														barrier.image = dstImg;
														barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
														vkCmdPipelineBarrier(cmd,
															VK_PIPELINE_STAGE_TRANSFER_BIT,
															VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
															0, 0, nullptr, 0, nullptr, 1, &barrier);
													}
												}
											});

										m_frameGraph.addPass("PostRead",
											[this](engine::render::PassBuilder& b) {
												b.read(m_fgSceneColorId, engine::render::ImageUsage::SampledRead);
											},
											[](VkCommandBuffer, engine::render::Registry&) {});

										engine::render::MeshHandle    h2 = m_assetRegistry.LoadMesh("meshes/test.mesh");
										engine::render::TextureHandle t1 = m_assetRegistry.LoadTexture("textures/test.texr", false);
										engine::render::TextureHandle t2 = m_assetRegistry.LoadTexture("textures/test.texr", false);
										if (m_geometryMeshHandle.IsValid() && h2.IsValid() && m_geometryMeshHandle.Id() == h2.Id()) { /* cache OK */ }
										if (t1.IsValid() && t2.IsValid() && t1.Id() == t2.Id()) { /* cache OK */ }
									}
								}
							}
						}
					}
				}
			}
			else
			{
				if (m_glfwWindowForVk)
					LOG_WARN(Platform, "[Boot] VkInstance::Create failed");
				else
					LOG_WARN(Platform, "[Boot] Vulkan instance or GLFW window for surface failed");
			}
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

		while (!m_quitRequested && !m_window.ShouldClose())
		{
			BeginFrame();
			Update();
			SwapRenderState();
			//			
			Render();

			EndFrame();

			const auto now = std::chrono::steady_clock::now();
			if (now - lastFpsLog >= std::chrono::seconds(1))
			{
				LOG_INFO(Core, "fps={:.1f} dt_ms={:.3f} frame={}", m_time.FPS(), m_time.DeltaSeconds() * 1000.0, m_time.FrameIndex());
				lastFpsLog = now;
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
#if defined(_WIN32)
			if (m_texturePreviewCache) m_texturePreviewCache->Shutdown();
			m_texturePreviewCache.reset();
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
			// Phase 5 Lunar + M38.1 Sky : detruit le SkyPass (pipeline + layout)
			// avant le DeferredPipeline (m_skyPass depend du renderPass de
			// GeometryPass, donc on libere le pipeline d'abord).
			if (m_skyPassReady)
			{
				m_skyPass.Shutdown(m_vkDeviceContext.GetDevice());
				m_skyPassReady = false;
			}
			m_waterMeshGpu.Destroy();
			if (m_waterTransferPool != VK_NULL_HANDLE)
			{
				vkDestroyCommandPool(m_vkDeviceContext.GetDevice(), m_waterTransferPool, nullptr);
				m_waterTransferPool = VK_NULL_HANDLE;
			}
			if (m_pipeline)
			{
				m_pipeline->Destroy(m_vkDeviceContext.GetDevice());
				m_pipeline.reset();
			}
			m_profilerHud.Shutdown();
			m_profiler.Shutdown(m_vkDeviceContext.GetDevice());
			m_audioEngine.Shutdown();
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
			const bool sent = m_authUi.SavePositionAsync(m_currentCharacterId,
				playerPos.x, playerPos.y, playerPos.z,
				yawDeg, pitchDeg);
			LOG_INFO(Core, "[SavePosition] shutdown save (character_id={}, pos=({:.1f},{:.1f},{:.1f}), sent={})",
				m_currentCharacterId, playerPos.x, playerPos.y, playerPos.z, sent);
			m_shutdownPositionSaved = true;
		}

		ShutdownGameplayNet();
		m_authUi.Shutdown();
		m_chatUi.Shutdown();
		m_mailUi.Shutdown();
		m_gmTicketUi.Shutdown();
		m_reputationUi.Shutdown();
		m_lfgUi.Shutdown();
		m_cinematicUi.Shutdown();
		m_skillBookUi.Shutdown();
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

		// CMANGOS.39 (Phase 4.39 step 3+4) — Touche B post-auth toggle le
		// panneau Skill Book (equivalent a la slash command /skills). Bloquee
		// si le chat a le focus (l'utilisateur tape) ou si le menu pause /
		// editor est ouvert pour eviter les toggles accidentels.
		{
			const bool chatBlocks = m_chatUi.IsInitialized() && m_chatUi.IsChatFocusActive();
			const bool inGameNoMenu = !m_inGamePauseMenuVisible
				&& !m_inGameOptionsPanelVisible
				&& !m_editorEnabled
				&& m_authUi.IsInitialized()
				&& m_authUi.IsFlowComplete();
			if (inGameNoMenu && !chatBlocks
				&& m_input.WasPressed(engine::platform::Key::B))
			{
				m_skillBookVisible = !m_skillBookVisible;
				if (m_skillBookVisible)
				{
					m_skillBookUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] B toggle skillbook (visible={})", m_skillBookVisible);
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
				&& m_input.WasPressed(engine::platform::Key::A))
			{
				m_arenaVisible = !m_arenaVisible;
				if (m_arenaVisible)
				{
					m_arenaUi.RequestTeams();
				}
				LOG_INFO(Core, "[Engine] A toggle arena (visible={})", m_arenaVisible);
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
				&& m_input.WasPressed(engine::platform::Key::G))
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
				&& m_input.WasPressed(engine::platform::Key::P))
			{
				m_outdoorPvpVisible = !m_outdoorPvpVisible;
				if (m_outdoorPvpVisible)
				{
					m_outdoorPvpUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] P toggle outdoorpvp (visible={})", m_outdoorPvpVisible);
			}
			// CMANGOS.42 (Phase 4.42 step 3+4) — Touche Y : toggle panneau
			// Weather + RequestList si on l'ouvre. Memes guards que A/G/P.
			// Note : la touche Y est aussi utilisee comme Ctrl+Y pour le
			// redo dans WorldEditorShell, mais le guard inGameNoMenu inclut
			// !m_editorEnabled donc pas de conflit. WorldEditorShell traite
			// Ctrl+Y / Ctrl+Shift+Y dans son propre bloc en aval.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(engine::platform::Key::Y))
			{
				m_weatherVisible = !m_weatherVisible;
				if (m_weatherVisible)
				{
					m_weatherUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] Y toggle weather (visible={})", m_weatherVisible);
			}
			// CMANGOS.31 (Phase 5.31 step 3+4) — Touche E : toggle panneau
			// GameEvents + RequestList si on l'ouvre. Memes guards que Y.
			// Pas de conflit Ctrl+E (non utilise par WorldEditorShell).
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(engine::platform::Key::E))
			{
				m_gameEventVisible = !m_gameEventVisible;
				if (m_gameEventVisible)
				{
					m_gameEventUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] E toggle gameevents (visible={})", m_gameEventVisible);
			}
			// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Touche U : toggle
			// panneau Guildes + RequestList si on l'ouvre. Memes guards que
			// E (chat focus, pause, editor). Pas de conflit Ctrl+U.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(engine::platform::Key::U))
			{
				m_guildVisible = !m_guildVisible;
				if (m_guildVisible)
				{
					m_guildUi.RequestList();
				}
				LOG_INFO(Core, "[Engine] U toggle guilds (visible={})", m_guildVisible);
			}
			// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Touche H : toggle
			// panneau Hotel des Ventes + RequestList si on l'ouvre. Memes
			// guards que U. Pas de conflit Ctrl+H.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(engine::platform::Key::H))
			{
				m_auctionHouseVisible = !m_auctionHouseVisible;
				if (m_auctionHouseVisible)
				{
					m_auctionHouseUi.RequestList(0u);
				}
				LOG_INFO(Core, "[Engine] H toggle auction house (visible={})", m_auctionHouseVisible);
			}
			// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Touche L : toggle
			// fenetre Loot Roll. Memes guards que U. Pas de conflit Ctrl+L.
			// Pas de fetch a l'ouverture : les pending rolls arrivent via
			// push, le bouton Simulate sert pour la demo V1.
			if (inGameNoMenu && !chatBlocks
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& m_input.WasPressed(engine::platform::Key::L))
			{
				m_lootRollVisible = !m_lootRollVisible;
				LOG_INFO(Core, "[Engine] L toggle loot roll (visible={})", m_lootRollVisible);
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
			if (m_input.WasPressed(engine::platform::Key::Z))
				m_worldEditorShell->HandleShortcut('Z', ctrl, shift);
			if (m_input.WasPressed(engine::platform::Key::Y))
				m_worldEditorShell->HandleShortcut('Y', ctrl, shift);
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
				// M100.14 — Le cache framebuffer de la passe water est indexé par
				// VkImageView source : un resize détruit toutes les vues FG → cache
				// stale. Invalidation au même endroit que terrain/pipeline.
				m_waterPass.InvalidateFramebufferCache(m_vkDeviceContext.GetDevice());

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
			m_worldEditorImGui = std::make_unique<engine::editor::WorldEditorImGui>();
			if (m_worldEditorImGui->Init(
				m_vkInstance.GetHandle(),
				m_vkDeviceContext,
				m_vkSwapchain.GetImageFormat(),
				m_vkSwapchain.GetImageCount(),
				VK_API_VERSION_1_1,
				m_window.GetNativeHandle(),
				&m_cfg,
				m_worldEditorExe))
			{
				if (m_worldEditorExe)
				{
					m_worldEditorImGui->SetEditorContext(m_worldEditorSession.get(), &m_cfg);

					// Cache de vignettes pour les textures de splatting.
					m_texturePreviewCache = std::make_unique<engine::editor::TexturePreviewCache>();
					const std::string contentDir = m_cfg.GetString("paths.content", "game/data");
					const std::filesystem::path absContent = std::filesystem::absolute(contentDir);
					if (!m_texturePreviewCache->Init(m_vkDeviceContext.GetDevice(),
					                                 m_vkDeviceContext.GetPhysicalDevice(),
					                                 m_vkDeviceContext.GetGraphicsQueue(),
					                                 m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					                                 absContent.string()))
					{
						LOG_WARN(Render, "[Engine] TexturePreviewCache init failed -- vignettes editeur indisponibles");
						m_texturePreviewCache.reset();
					}
					if (m_worldEditorImGui && m_texturePreviewCache)
					{
						m_worldEditorImGui->SetTexturePreviewCache(m_texturePreviewCache.get());
					}
				}
				// Branche le DayNightCycle au panneau "Atmosphere" pour que l'utilisateur
				// puisse regler time-of-day et timeScale en live depuis l'editeur monde.
				m_worldEditorImGui->SetDayNightCycle(&m_dayNight);
				m_worldEditorImGui->AttachPlatformWindow(m_window.GetNativeHandle(), m_window);
				m_authImGui = std::make_unique<engine::render::AuthImGuiRenderer>();
				m_authImGui->BindAuthUiBridge(&m_authUi, &m_cfg, &m_window);
				// Phase 3.11.1 — partage du même contexte ImGui (NewFrame/Render gérés par m_worldEditorImGui).
				m_chatImGui = std::make_unique<engine::render::ChatImGuiRenderer>();
				m_chatImGui->BindChatUi(&m_chatUi, &m_cfg);
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
				m_editorHubImGui = std::make_unique<engine::render::EditorHubImGuiRenderer>();
				if (m_editorMode)
					m_editorHubImGui->BindEditorMode(m_editorMode.get());
			}
			else
			{
				m_worldEditorImGui.reset();
			}
		}
		if (m_worldEditorExe && m_worldEditorSession && m_worldEditorSession->ConsumeTerrainGpuReloadRequest())
		{
			RebuildWorldEditorTerrainGpu();
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

		const bool authGateActive = m_authUi.IsInitialized() && !m_authUi.IsFlowComplete();
		if (authGateActive)
		{
			LOG_DEBUG(Render, "[DIAG] authUi.Update begin frame={}", m_currentFrame);
			m_authUi.Update(m_input, static_cast<float>(dt), m_window, m_cfg);
			LOG_DEBUG(Render, "[DIAG] authUi.Update done frame={}", m_currentFrame);
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
				if (gameplayWasEnabled != gameCmd.gameplayUdpEnabled)
				{
					if (gameCmd.gameplayUdpEnabled)
						InitGameplayNet();
					else
						ShutdownGameplayNet();
				}
				LOG_INFO(Core, "[Options] Game applied (gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
					gameCmd.gameplayUdpEnabled, gameCmd.allowInsecureDev, gameCmd.authTimeoutMs);
			}
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

				// Coupe la musique des ecrans d'auth/menu (Horns_of_the_Fallen_Bastion.mp3)
				// au moment d'entrer dans le monde : le joueur entend desormais l'ambiance
				// du shard (zone audio) et eventuellement de futures musiques in-game.
				m_audioEngine.StopMenuMusic();

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
				if (m_currentCharacterId != 0u && !enterCmd.characterName.empty())
				{
					(void)m_authUi.SendEnterWorldAsync(m_currentCharacterId, enterCmd.characterName);
					// Phase 5 reconnect — mémorise l'identité pour pouvoir ré-envoyer
					// CHARACTER_ENTER_WORLD à la reconnexion sans repasser par tout le flow.
					m_authUi.RememberPostEnterWorldCharacter(m_currentCharacterId, enterCmd.characterName);
				}

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

			// Phase 3.6.6 — Tick périodique de sauvegarde de position. Démarré à la consommation
			// de EnterWorldCommand (m_currentCharacterId != 0). Intervalle borné à >= 5 s côté
			// AuthUiPresenter::SavePositionAsync via la config `client.save_position.interval_sec`.
			//
			// Etape 6 vue 3eme personne : ajout d'une heuristique mouvement -> save plus
			// frequente. Quand le perso a bouge de plus de 0.5 m depuis la derniere
			// synchro, on declenche immediatement (rate-limite a 1.0 s entre 2 saves).
			// Si statique, on revient a l'intervalle long (m_savePositionIntervalSec).
			if (m_currentCharacterId != 0u)
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
					if (m_authUi.SavePositionAsync(m_currentCharacterId,
						playerPos.x, playerPos.y, playerPos.z,
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

		// M100.1 — Rendu de la coquille du nouvel éditeur monde. Doit être
		// appelée après ImGui::NewFrame (fait par WorldEditorImGui::NewFrame
		// ci-dessus, qui partage le même contexte ImGui) et avant
		// ImGui::Render. RenderFrame est no-op si Init() n'a pas été appelé
		// avec succès. Pas thread : main thread uniquement.
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized()
			&& m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			m_worldEditorShell->RenderFrame();
		}
#endif

		if (!m_editorEnabled)
		{
			if (!authGateActive && !m_chatUi.IsChatFocusActive())
			{
				// Etape 1 vue 3eme personne : controleur orbital (camera derriere une
				// position cible, qui sera celle de l'avatar dans un PR ulterieur).
				// Comportement : souris libre par defaut ; clic droit maintenu rotate
				// la camera autour de la cible (yaw/pitch) ; molette zoom in/out ;
				// WASD deplace la cible dans le plan XZ et la camera suit.
				//
				// Chantier 2 : passe la hauteur sol terrain au controleur pour la
				// collision camera-decor (plus seulement Y=0). 0 si pas de terrain.
				//
				// Convention MMORPG : la souris est LIBRE par defaut (curseur visible,
				// utilisable pour cliquer sur l'UI HUD). Maintenir le CLIC DROIT pour
				// faire pivoter la camera autour du personnage. Sans clic droit, la
				// camera reste fixe par rapport au monde et le perso continue d'etre
				// dans la vue (la camera est positionnee derriere lui via le yaw
				// courant). L'avatar suit le yaw camera donc on voit toujours son dos.
				const bool rmbLook = m_input.IsMouseDown(engine::platform::MouseButton::Right);
				const auto& camTarget = m_orbitalCameraController.GetTargetPosition();
				const float groundY = m_terrain.IsValid()
					? m_terrain.SampleHeightAtWorldXZ(camTarget.x, camTarget.z)
					: 0.0f;

				// Modificateur de vitesse combine RACE x TERRAIN.
				// RACE : table id->multiplicateur (placeholder, a tuner gameplay).
				// L'identifiant race est stocke en string sur AuthUi (m_characterRaceId).
				// TODO : eventuellement migrer vers la table races (DB) pour que les
				// game-designers tunent sans recompiler.
				auto raceMultiplier = [](const std::string& raceId) -> float {
					if (raceId == "elfes")              return 1.10f; // legers, agiles.
					if (raceId == "nains")              return 0.85f; // courts sur pattes.
					if (raceId == "orcs")               return 0.95f;
					if (raceId == "morts_vivants")      return 0.90f; // demarche raide.
					if (raceId == "demons")             return 1.05f; // au sol ; vol = autre meca.
					if (raceId == "chevaliers_dragons") return 1.00f; // monture = autre meca.
					if (raceId == "humains" || raceId.empty() || raceId == "default") return 1.00f;
					return 1.00f;
				};
				const float raceMul = raceMultiplier(m_authUi.GetSelectedCharacterRaceId());

				// TERRAIN : pour l'instant 1.0, hook pour future query splatmap.
				// TODO : ajouter TerrainRenderer::SampleSpeedMultiplierAtWorldXZ(x,z)
				// qui lit le splat CPU (R=grass G=dirt B=rock A=snow) et retourne
				// une moyenne ponderee (grass=1.0, dirt=0.95, rock=0.90, snow=0.65).
				constexpr float terrainMul = 1.0f;

				const float speedMul = raceMul * terrainMul;
				m_orbitalCameraController.Update(m_input, dt, mouseSensitivity, invertY, movementLayout,
					rmbLook, true, out.camera, groundY, speedMul);
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
		else if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
		{
			// Ne pas lier souris et clavier : un InputText actif mettait WantCaptureKeyboard à true et bloquait
			// aussi la souris (orientation), ce qui figeait la caméra dans l’éditeur.
			const bool capMouse = m_worldEditorImGui->WantsCaptureMouse();
			const bool capKb = m_worldEditorImGui->WantsCaptureKeyboard();
			// Convention UX standard (Unity/Unreal/Blender) : la rotation de la
			// caméra free-fly de l'éditeur n'est active QUE pendant que le clic
			// droit est maintenu. Sinon le moindre déplacement de la souris fait
			// dériver la caméra et l'utilisateur perd le terrain. Avant ce fix,
			// `applyLook = !capMouse || rmbLook` faisait tourner la caméra dès
			// que la souris survolait la zone 3D — UX cassée.
			const bool rmbLook = m_input.IsMouseDown(engine::platform::MouseButton::Right);
			const bool applyLook = rmbLook;
			const bool applyKb = !capKb;
			float terrainWorldM = 0.f;
			if (m_terrain.IsValid())
			{
				terrainWorldM = m_terrain.GetTerrainWorldSize();
			}
			const float editorSpeedMult = static_cast<float>(
				m_cfg.GetDouble("controls.editor_camera_speed_multiplier", 1.0));
			m_fpsCameraController.Update(m_input, dt, mouseSensitivity, invertY, movementLayout, true, applyLook, applyKb,
				terrainWorldM, out.camera, editorSpeedMult);
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
		m_audioEngine.Tick(static_cast<float>(dt));
		m_decalSystem.Tick(static_cast<float>(dt));

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

		out.projMatrix = out.camera.ComputeProjectionMatrix();

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
				// Etape 5 : bob vertical placeholder quand le perso marche / court.
				// Idle : pas d'oscillation. L'amplitude est de 4 cm en walk, 7 cm
				// en run -- visible sans etre desagreable. A remplacer par de
				// vraies anims squelettiques quand un format anim sera cable.
				float bobY = 0.0f;
				const auto loco = m_orbitalCameraController.GetLocomotionState();
				if (loco != engine::render::OrbitalCameraController::LocomotionState::Idle)
				{
					const float bobAmpM = (loco == engine::render::OrbitalCameraController::LocomotionState::Run) ? 0.07f : 0.04f;
					bobY = std::sin(m_orbitalCameraController.GetWalkBobPhaseRad()) * bobAmpM;
				}
				// Le orbital target est a kTargetEyeHeight (1.7 m) au-dessus du sol :
				// c'est la position des yeux du joueur. L'avatar (mesh feet at mesh-Y=0)
				// doit etre translate de (target.y - kTargetEyeHeight) pour que ses
				// pieds soient sur le sol et non flottent dans les airs. groundY ici
				// est 0 (sol plat) ou la hauteur terrain echantillonnee plus haut.
				const float feetY = target.y - engine::render::OrbitalCameraController::kTargetEyeHeight + bobY;
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
			float dw = static_cast<float>(std::max(1, m_width));
			float dh = static_cast<float>(std::max(1, m_height));
			int vw = std::max(1, m_width);
			int vh = std::max(1, m_height);
			if (m_vkSwapchain.IsValid())
			{
				const VkExtent2D extUi = m_vkSwapchain.GetExtent();
				if (extUi.width > 0 && extUi.height > 0)
				{
					dw = static_cast<float>(extUi.width);
					dh = static_cast<float>(extUi.height);
					vw = static_cast<int>(extUi.width);
					vh = static_cast<int>(extUi.height);
				}
			}

			engine::editor::WorldEditorViewportOverlayDesc overlay{};
			overlay.viewProjColMajor = out.viewProjMatrix.m;
			overlay.cameraWorldX = out.camera.position.x;
			overlay.cameraWorldY = out.camera.position.y;
			overlay.cameraWorldZ = out.camera.position.z;
			overlay.cameraYawDeg = out.camera.yaw * (180.0f / 3.14159265f);
			overlay.cameraPitchDeg = out.camera.pitch * (180.0f / 3.14159265f);
			overlay.viewportWidth = vw;
			overlay.viewportHeight = vh;

			bool terrainPick = false;
			float pickX = 0.f;
			float pickZ = 0.f;
			if (m_terrain.IsValid() && m_worldEditorSession)
			{
				const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
				if (hm.width > 0u && hm.height > 0u)
				{
					overlay.heightmap = &hm;
					overlay.terrainOriginX = m_terrain.GetTerrainOriginX();
					overlay.terrainOriginZ = m_terrain.GetTerrainOriginZ();
					overlay.terrainWorldSize = m_terrain.GetTerrainWorldSize();
					overlay.heightScale = m_terrain.GetHeightScale();
					overlay.showGrid = m_worldEditorSession->ShowGrid();
					overlay.gridCellMeters = m_worldEditorSession->GridCellMeters();
					overlay.brushRadiusMeters = m_worldEditorSession->BrushRadius();
					const bool capBeforeUi = m_worldEditorImGui->WantsCaptureMouse();
					terrainPick = RaycastTerrainFromCamera(out.camera, vw, vh, m_input.MouseX(), m_input.MouseY(), hm,
						overlay.terrainOriginX, overlay.terrainOriginZ, overlay.terrainWorldSize, overlay.heightScale,
						pickX, pickZ);
					overlay.showBrushPreview =
						terrainPick && !capBeforeUi && m_worldEditorSession->TerrainEditMode() != 3
						&& m_worldEditorSession->TerrainEditMode() != 4;
					if (terrainPick)
					{
						overlay.brushWorldX = pickX;
						overlay.brushWorldZ = pickZ;
					}
				}
				overlay.layoutInstancesOverlay = &m_worldEditorSession->MutableDoc().layoutInstances;
				overlay.selectedLayoutInstanceOverlay = m_worldEditorSession->SelectedLayoutInstanceIndex();
			}

			m_worldEditorImGui->BuildUi(&overlay);

			if (m_worldEditorExe && m_worldEditorSession && m_worldEditorTerrainTools.IsValid() && m_vkDeviceContext.IsValid()
				&& m_worldEditorSession->ConsumeRouteApplyDraftRequest())
			{
				engine::editor::WorldEditorSession& ws = *m_worldEditorSession;
				if (ws.RouteDraftPoints().size() < 2u)
				{
					ws.SetStatus("Routes: au moins 2 points dans le brouillon (clics gauche sur le sol).");
				}
				else
				{
					engine::editor::WorldMapRoutePolyline rp{};
					rp.pointsXz = ws.RouteDraftPoints();
					rp.widthM = static_cast<double>(ws.RouteStripWidthM());
					rp.splatLayer = static_cast<uint32_t>(std::clamp(ws.RouteSplatLayer(), 0, 3));
					engine::render::terrain::BrushParams bp{};
					bp.radius = ws.BrushRadius();
					bp.strength = ws.BrushStrength();
					bp.falloff = 1.f;
					bp.flattenTarget = 0.5f;
					if (m_worldEditorTerrainTools.PaintSplatAlongPolyline(rp.pointsXz, rp.widthM, rp.splatLayer, bp))
					{
						(void)m_worldEditorTerrainTools.FlushSplatMap(m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
						ws.MutableDoc().routes.push_back(std::move(rp));
						ws.ClearRouteDraft();
						ws.SetStatus("Routes: bande splat appliquée — sauvegardez l’édition pour persister SLAP + JSON.");
					}
					else
					{
						ws.SetStatus("Routes: peinture splat impossible (vérifier le terrain / la splat).");
					}
				}
			}

			if (m_terrain.IsValid() && m_worldEditorSession && terrainPick && m_vkDeviceContext.IsValid())
			{
				const bool cap = m_worldEditorImGui->WantsCaptureMouse();
				engine::editor::WorldEditorSession& ws = *m_worldEditorSession;
				if (!cap && ws.TerrainEditMode() == 4 && m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const float ox = m_terrain.GetTerrainOriginX();
					const float oz = m_terrain.GetTerrainOriginZ();
					const float wsiz = m_terrain.GetTerrainWorldSize();
					const float eps = wsiz * 1e-5f;
					const float px = std::clamp(pickX, ox + eps, ox + wsiz - eps);
					const float pz = std::clamp(pickZ, oz + eps, oz + wsiz - eps);
					ws.AddRouteDraftPoint(static_cast<double>(px), static_cast<double>(pz));
				}
				else if (!cap && ws.TerrainEditMode() == 3 && m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const engine::render::terrain::HeightmapData& hm = m_terrain.GetMutableHeightmapData();
					float wy = 0.f;
					if (TryTerrainWorldY(hm, overlay.terrainOriginX, overlay.terrainOriginZ, overlay.terrainWorldSize, overlay.heightScale,
							pickX, pickZ, wy))
					{
						ws.PlaceOrMoveLayoutInstanceAtTerrainHit(m_cfg, static_cast<double>(pickX), static_cast<double>(wy),
							static_cast<double>(pickZ));
					}
				}
				else if (m_worldEditorTerrainTools.IsValid() && !cap && m_input.IsMouseDown(engine::platform::MouseButton::Left)
					&& ws.TerrainEditMode() != 3 && ws.TerrainEditMode() != 4)
				{
					engine::render::terrain::BrushParams bp;
					bp.radius = ws.BrushRadius();
					bp.strength = ws.BrushStrength();
					bp.falloff = 1.f;
					bp.flattenTarget = 0.5f;
					if (ws.TerrainEditMode() == 1)
					{
						const uint32_t layer = static_cast<uint32_t>(std::clamp(ws.SplatLayer(), 0, 3));
						m_worldEditorTerrainTools.PaintSplat(pickX, pickZ, layer, bp);
						(void)m_worldEditorTerrainTools.FlushSplatMap(m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
					}
					else if (ws.TerrainEditMode() == 2)
					{
						m_worldEditorTerrainTools.PaintGrassMask(pickX, pickZ, bp, ws.GrassMaskEraseBrush());
						(void)m_worldEditorTerrainTools.FlushGrassMask(m_terrain,
							m_vkDeviceContext.GetDevice(),
							m_vkDeviceContext.GetPhysicalDevice(),
							m_vkDeviceContext.GetGraphicsQueue(),
							m_vkDeviceContext.GetGraphicsQueueFamilyIndex());
					}
					else
					{
						engine::render::terrain::BrushOp op = engine::render::terrain::BrushOp::Raise;
						switch (ws.BrushOp())
						{
						case 1: op = engine::render::terrain::BrushOp::Lower; break;
						case 2: op = engine::render::terrain::BrushOp::Smooth; break;
						case 3: op = engine::render::terrain::BrushOp::Flatten; break;
						default: break;
						}
						m_worldEditorTerrainTools.ApplyBrush(pickX, pickZ, op, bp);
					}
				}
			}
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_authImGui
			&& m_cfg.GetBool("render.auth_ui.imgui.enabled", false) && m_authUi.GetVisualState().active)
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
			const engine::client::AuthUiPresenter::VisualState authVsImgui = m_authUi.GetVisualState();
			const engine::client::AuthUiPresenter::RenderModel authRmImgui = m_authUi.BuildRenderModel();
			m_authImGui->Render(authVsImgui, authRmImgui, dw, dh);
			// Chat HUD desactive sur les ecrans pre-EnterWorld (auth/ShardPick/
			// CharacterSelect/CharacterCreate) suite a retour utilisateur "on laisse
			// tomber pour le moment, l'affichage du CHAT juste apres l'authentification".
			// Le chat reapparaitra une fois la branche !authGateActive prise (in-game).
			ImGui::Render();
		}
		else if (m_worldEditorImGui && m_worldEditorImGui->IsReady() && m_chatImGui
			&& m_chatUi.IsInitialized()
			&& m_authUi.IsInitialized() && m_authUi.IsMasterAuthenticated()
			&& !m_worldEditorExe
			&& (m_cfg.GetBool("render.chat_imgui.enabled", true) || m_inGamePauseMenuVisible || m_inGameOptionsPanelVisible))
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
			if (m_skillBookVisible && m_skillBookImGui && m_skillBookUi.IsInitialized())
			{
				m_skillBookImGui->SetEnabled(true);
				m_skillBookImGui->SetViewportSize(static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
				m_skillBookImGui->Render();
			}
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
			if (m_inGamePauseMenuVisible)
			{
				const float menuW = 320.f;
				const float menuH = 220.f;
				ImGui::SetNextWindowPos(ImVec2((dw - menuW) * 0.5f, (dh - menuH) * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.92f);
				ImGui::Begin("##ln_pause_menu", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
				ImGui::SetWindowFontScale(1.2f);
				const char* title = "PAUSE";
				const float titleW = ImGui::CalcTextSize(title).x;
				ImGui::SetCursorPosX((menuW - titleW) * 0.5f);
				ImGui::TextUnformatted(title);
				ImGui::SetWindowFontScale(1.f);
				ImGui::Separator();
				ImGui::Spacing();
				const float btnW = menuW - 40.f;
				if (ImGui::Button("Reprendre", ImVec2(btnW, 32.f)))
				{
					m_inGamePauseMenuVisible = false;
				}
				ImGui::Spacing();
				if (ImGui::Button("Options", ImVec2(btnW, 32.f)))
				{
					m_inGamePauseMenuVisible = false;
					m_inGameOptionsPanelVisible = true;
				}
				ImGui::Spacing();
				if (ImGui::Button("Se deconnecter", ImVec2(btnW, 32.f)))
				{
					RequestLogoutToLoginScreen();
				}
				ImGui::Spacing();
				if (ImGui::Button("Quitter le jeu", ImVec2(btnW, 32.f)))
				{
					OnQuit();
				}
				ImGui::End();
			}
			// Mini-panel Options in-game (ouvert via le bouton Options du menu pause).
			// Contient les controles essentiels qu'on veut pouvoir ajuster sans
			// quitter le jeu : volume master, plein ecran, vsync, sensibilite souris.
			// Le full panel auth Options reste accessible via Se deconnecter -> Login -> Options.
			if (m_inGameOptionsPanelVisible)
			{
				const float optW = 420.f;
				const float optH = 320.f;
				ImGui::SetNextWindowPos(ImVec2((dw - optW) * 0.5f, (dh - optH) * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(optW, optH), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.95f);
				ImGui::Begin("##ln_ingame_options", nullptr,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
					| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
					| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
				ImGui::SetWindowFontScale(1.15f);
				const char* optTitle = "OPTIONS";
				const float optTitleW = ImGui::CalcTextSize(optTitle).x;
				ImGui::SetCursorPosX((optW - optTitleW) * 0.5f);
				ImGui::TextUnformatted(optTitle);
				ImGui::SetWindowFontScale(1.f);
				ImGui::Separator();
				ImGui::Spacing();

				// Lecture des valeurs actuelles depuis la config et ecriture in-place.
				float vol = static_cast<float>(m_cfg.GetDouble("audio.master_volume", 1.0));
				bool fullscreen = m_cfg.GetBool("video.fullscreen", false);
				bool vsync = m_cfg.GetBool("render.vsync", true);
				float sens = static_cast<float>(m_cfg.GetDouble("controls.mouse_sensitivity", 0.002));

				if (ImGui::SliderFloat("Volume general", &vol, 0.0f, 1.0f, "%.2f"))
				{
					m_cfg.SetValue("audio.master_volume", static_cast<double>(vol));
					(void)m_audioEngine.SetMasterVolume(vol);
				}
				if (ImGui::Checkbox("Plein ecran", &fullscreen))
				{
					m_cfg.SetValue("video.fullscreen", fullscreen);
					// Nota : changement effectif au prochain restart (toggle live = autre PR).
				}
				if (ImGui::Checkbox("VSync", &vsync))
				{
					m_cfg.SetValue("render.vsync", vsync);
				}
				if (ImGui::SliderFloat("Sensibilite souris", &sens, 0.0005f, 0.01f, "%.4f rad/px"))
				{
					m_cfg.SetValue("controls.mouse_sensitivity", static_cast<double>(sens));
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				const float optBtnW = optW - 40.f;
				if (ImGui::Button("Fermer", ImVec2(optBtnW, 32.f)))
				{
					m_inGameOptionsPanelVisible = false;
				}
				ImGui::End();
			}
			ImGui::Render();
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
	        m_pipeline->GetAutoExposure().Update(device, dt, key, speed, frameIndex);
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

	void Engine::RequestLogoutToLoginScreen()
	{
		LOG_INFO(Core, "[InGamePauseMenu] logout requested -> Login screen");
		// 1) Coupe la connexion gameplay UDP + presenters in-game.
		if (m_gameplayNetInitialized)
		{
			ShutdownGameplayNet();
		}
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

		m_uiObserverHandle = m_uiModelBinding.AddObserver(
			[this](const engine::client::UIModel& model, uint32_t changeMask)
			{
				(void)m_shopUi.ApplyModel(model, changeMask);
				(void)m_auctionUi.ApplyModel(model, changeMask);
				(void)m_invUi.ApplyModel(model, changeMask);
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
		(void)m_gameplayUdp.SendHello(reqTick, reqSnap, charKey);

		m_gameplayNetInitialized = true;
		LOG_INFO(Core,
			"[GameplayNet] Init OK (host={}, port={}, vendor_target='{}', auction_target='{}')",
			host,
			port,
			m_gameplayVendorTalkTarget,
			m_gameplayAuctionTalkTarget);
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

	void Engine::PumpGameplayPackets()
	{
		if (!m_gameplayNetInitialized || !m_gameplayUdp.IsActive())
		{
			return;
		}

		std::vector<std::vector<std::byte>> packets = m_gameplayUdp.PollIncoming();
		for (std::vector<std::byte>& packet : packets)
		{
			engine::server::MessageKind kind{};
			if (engine::server::PeekMessageKind(packet, kind) && kind == engine::server::MessageKind::Welcome)
			{
				continue;
			}
			(void)m_uiModelBinding.ApplyPacket(packet);
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

#if defined(_WIN32)
	void Engine::RebuildWorldEditorTerrainGpu()
	{
		if (!m_worldEditorExe || !m_vkDeviceContext.IsValid() || !m_worldEditorSession)
		{
			return;
		}
		VkDevice device = m_vkDeviceContext.GetDevice();
		VkPhysicalDevice physDev = m_vkDeviceContext.GetPhysicalDevice();
		vkDeviceWaitIdle(device);
		m_worldEditorTerrainTools.Shutdown();
		if (m_terrain.IsValid())
		{
			m_terrain.Destroy(device);
		}

		const std::string& hmRel = m_worldEditorSession->Doc().heightmapContentRelativePath;
		if (hmRel.empty())
		{
			return;
		}

		auto loadFn = [this](const char* p) { return LoadTerrainSpirvWords(p); };
		std::optional<float> worldSizeOverride;
		const engine::editor::WorldMapEditDocument& wed = m_worldEditorSession->Doc();
		if (wed.hasTerrainWorldSizeM && wed.terrainWorldSizeM > 0.0)
		{
			worldSizeOverride = static_cast<float>(wed.terrainWorldSizeM);
		}
		const std::string& splatRel = wed.splatmapContentRelativePath;
		const std::string& grassRel = wed.grassMaskContentRelativePath;
		const bool ok = m_terrain.Init(
			device,
			physDev,
			m_cfg,
			hmRel,
			splatRel,
			grassRel,
			"",
			{},
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_FORMAT_A2B10G10R10_UNORM_PACK32,
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_FORMAT_R16G16_SFLOAT,
			VK_FORMAT_D32_SFLOAT,
			m_vkDeviceContext.GetGraphicsQueue(),
			m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
			loadFn,
			worldSizeOverride);
		if (!ok)
		{
			LOG_WARN(Render,
				"[WorldEditor] TerrainRenderer::Init failed for \"{}\" — fichier introuvable sous paths.content ? "
				"Lancer l’éditeur avec le cwd à la racine du dépôt (config.json + game/data), ou rebuild avec la détection cwd (world_editor_main).",
				hmRel);
			return;
		}
		if (!m_worldEditorTerrainTools.Init(
				&m_terrain.GetMutableHeightmapData(),
				&m_terrain.GetSplatting(),
				&m_terrain.GetMutableGrassMaskData(),
				m_terrain.GetTerrainOriginX(),
				m_terrain.GetTerrainOriginZ(),
				m_terrain.GetTerrainWorldSize(),
				m_terrain.GetHeightScale()))
		{
			LOG_WARN(Render, "[WorldEditor] TerrainEditingTools::Init failed");
		}
		m_terrain.InvalidateFramebufferCache(device);

		// Detection "aucune texture utilisateur assignee" -> pousse le flag fallback
		// orange au TerrainRenderer (via push-constant). Des qu'au moins une couche
		// splat recoit un mapping texture (refs[i] non vide), le flag retombe a
		// false et le rendu normal reprend.
		// Garde stricte sur m_worldEditorExe : le client jeu ne doit jamais lever ce
		// flag (regression visuelle).
		bool noUserTextures = false;
		if (m_worldEditorExe && m_worldEditorSession)
		{
			noUserTextures = true;
			const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
			for (const std::string& r : refs)
			{
				if (!r.empty()) { noUserTextures = false; break; }
			}
		}
		m_terrain.SetNoUserTexturesFallback(noUserTextures);

		// World Editor : desactive le frustum cull. Diagnostic (cf. PR #427) :
		// avec heightmap 256x256 + world_size override 10000m, le ratio
		// vertStepWorld/patchSize ne correspond pas a la matrice viewProj
		// utilisee par Frustum::ExtractFromMatrix, qui rejette TOUS les patches
		// meme quand la camera est pile au centre. Bug pre-existant (Gribb-
		// Hartmann avec convention Vulkan Z[0,1] + Y inverse). En attendant la
		// correction de l'extraction frustum, on desactive le cull cote World
		// Editor (225 patches max -> aucun impact perf). Le client jeu garde
		// le cull actif (defaut).
		m_terrain.SetFrustumCullEnabled(false);

		// Repositionne la camera au centre du terrain qu'on vient de charger pour
		// que l'utilisateur voie immediatement le sol apres "Creer une nouvelle
		// carte" ou "Charger la carte selectionnee". Sans ce reset, la camera peut
		// se retrouver hors champ ou sous le sol selon la heightmap chargee.
		// On utilise m_worldEditorExe (et non m_editorMode qui peut etre null si
		// EditorMode::Init a echoue) comme garde du reset.
		if (m_worldEditorExe)
		{
			const float ox = m_terrain.GetTerrainOriginX();
			const float oz = m_terrain.GetTerrainOriginZ();
			const float hs = m_terrain.GetHeightScale();

			// CRITIQUE : la zone REELLEMENT maillee (`actualExtX/Z`) peut etre
			// nettement plus petite que `GetTerrainWorldSize()` (cf.
			// `GetActualRenderedExtentX/Z`). Pour une heightmap 256x256 avec
			// world_size=10000, on couvre 2344 m au lieu de 10000 m. Si on
			// place la camera au centre du `world_size`, elle se retrouve hors
			// des patches et le frustum cull rejette TOUT (terrain invisible).
			// On vise donc le centre des patches reels.
			const float actualExtX = m_terrain.GetActualRenderedExtentX();
			const float actualExtZ = m_terrain.GetActualRenderedExtentZ();
			const float maxExt     = std::max(actualExtX, actualExtZ);

			const float centerX     = ox + actualExtX * 0.5f;
			const float centerZ     = oz + actualExtZ * 0.5f;
			const float midGroundY  = hs * 0.5f;            // hauteur moyenne attendue du sol
			const float camAltitude = midGroundY + 80.0f;   // marge confortable pour sculpter

			engine::render::Camera reset;
			reset.position.x = centerX;
			reset.position.y = camAltitude;
			reset.position.z = centerZ;
			reset.yaw        = 0.0f;
			reset.pitch      = 0.35f;                       // ~20deg vers le bas
			reset.fovYDeg    = 70.0f;
			reset.aspect     = static_cast<float>(std::max(1, m_width)) / static_cast<float>(std::max(1, m_height));
			reset.nearZ      = 0.1f;
			reset.farZ       = std::max(5000.0f, maxExt * 1.5f); // garantit la visibilite des bords sur grand terrain
			m_renderStates[0].camera = reset;
			m_renderStates[1].camera = reset;
			LOG_INFO(Render,
				"[WorldEditor] Camera reset: pos=({:.1f},{:.1f},{:.1f}) farZ={:.0f} actualExt=({:.0f}x{:.0f}) origin=({:.0f},{:.0f})",
				reset.position.x, reset.position.y, reset.position.z, reset.farZ,
				actualExtX, actualExtZ, ox, oz);
		}
	}

	void Engine::ProcessSplatRefsDirty()
	{
		if (!m_worldEditorExe || !m_worldEditorSession) return;
		if (!m_texturePreviewCache || !m_texturePreviewCache->IsReady()) return;
		if (!m_worldEditorSession->ConsumeSplatRefsDirty()) return;
		if (!m_terrain.IsValid()) return;

		engine::render::terrain::TerrainSplatting& splatting = m_terrain.GetSplatting();

		// Pour chaque layer : pousser le buffer CPU adequat dans TerrainSplatting.
		const auto& refs = m_worldEditorSession->Doc().splatLayerTextureRefs;
		for (uint32_t layer = 0; layer < engine::render::terrain::kSplatLayerCount; ++layer)
		{
			const std::vector<uint8_t>* rgba = nullptr;
			if (!refs[layer].empty())
			{
				// Force la decode/upload de la vignette si pas deja fait
				// (cree l'entree dans le cache si absente).
				m_texturePreviewCache->GetTexrThumb(refs[layer]);
				rgba = m_texturePreviewCache->GetCpuRgba256(refs[layer]);
			}
			if (rgba == nullptr)
			{
				// Fallback procedurale : assure que l'entree procedurale existe.
				m_texturePreviewCache->GetProceduralThumb(layer);
				rgba = m_texturePreviewCache->GetCpuRgba256(
					"procedural:" + std::to_string(layer));
			}
			if (rgba != nullptr)
			{
				splatting.SetLayerCpuRgba256(layer, *rgba);
			}
		}

		if (!splatting.RebuildAlbedoArrayFromCpuLayers(
				m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(),
				m_vkDeviceContext.GetGraphicsQueue(),
				m_vkDeviceContext.GetGraphicsQueueFamilyIndex()))
		{
			LOG_ERROR(Render, "[Engine] ProcessSplatRefsDirty: splat array rebuild failed");
		}
	}
#endif

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

