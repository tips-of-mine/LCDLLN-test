// Engine — code propre à l'éditeur monde (`lcdlln_world_editor.exe`, flag --world-editor).
//
// Regroupe les fonctions membres d'Engine qui n'ont de sens qu'en mode éditeur :
//   - aperçu et gizmo de placement des bâtiments (SyncEditorBuildingPreview,
//     DrawEditorBuildingGizmo, UpdateEditorBuildingGizmoDrag, GizmoHandleSizes) ;
//   - gizmo de transformation de la scène (DrawEditorSceneGizmo,
//     UpdateEditorSceneGizmoDrag, SceneGizmoHandleSizes) ;
//   - synchronisation terrain document -> GPU (RebuildWorldEditorTerrainGpu,
//     SyncWorldEditorHeightmapFromDocument, ProcessSplatRefsDirty).
// Code déplacé tel quel depuis Engine.cpp (aucun changement de comportement) :
// première étape de la séparation éditeur / jeu. La documentation de chaque
// fonction est dans Engine.h.

#include "src/client/app/Engine.h"

#include "src/world_editor/panels/BuildingEditorPanel.h"
#include "src/world_editor/ui/WorldEditorImGui.h"
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/ui/WorldMapIo.h" // lot B3 : SanitizeZoneId (namespacing zone)
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/core/CompositeCommand.h"            // Roadmap-6 : undo groupé du gizmo
#include "src/world_editor/inspector/SetEntityTransformCommand.h" // Roadmap-6 : undo du drag de gizmo
#include "src/world_editor/HazardCommand.h"                    // Roadmap-8 : pose de danger undoable
#include "src/world_editor/scene/LayoutPlacementCommands.h"    // Roadmap-6 : placement undoable
#include "src/world_editor/panels/ScenePanel.h"

#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "imgui.h"
#endif

namespace engine
{
	// Picking terrain du viewport éditeur (déplacé d'Engine.cpp avec les sous-étapes
	// d'Update() qui l'utilisent ; seul l'éditeur s'en sert).
	namespace
	{
		constexpr float kWorldEditorPickPi = 3.14159265f;

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
	}

	void Engine::SyncEditorBuildingPreview()
	{
		if (!m_worldEditorShell || !m_pipeline) return;
		engine::editor::world::panels::BuildingEditorPanel* panel =
			m_worldEditorShell->GetBuildingEditorPanel();
		if (!panel) return;

		// Demande de re-centrage (ex: « Charger dans l'éditeur ») : on invalide
		// l'origine stable pour que la variante fraîchement chargée apparaisse
		// devant la caméra, pas à l'ancienne position (hors-champ).
		if (panel->ConsumeRecenterRequest())
			m_editorPreviewValid = false;

		// Edge-triggered : ne reconstruit qu'après un changement (évite la
		// création de ressources GPU à chaque frame). Pendant un drag du gizmo,
		// les mutations sont silencieuses (pas de dirty) ; le mesh n'est donc
		// reconstruit qu'au relâchement (MarkPreviewDirty), pas à chaque frame —
		// indispensable car BuildPropFromMeshMatrix ne libère pas les meshes GPU.
		if (!panel->ConsumePreviewDirty()) return;

		// On NE touche QU'AU brouillon : on retire les props d'aperçu ajoutés
		// précédemment (tout ce qui dépasse le « monde » chargé au boot : décor
		// + bâtiments posés), sans effacer ce monde.
		if (m_props.size() > m_editorBaselinePropCount)
			m_props.resize(m_editorBaselinePropCount);

		constexpr float kDeg2Rad = 3.14159265f / 180.f;
		auto scaleMat = [](float s) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			m.m[0] = s; m.m[5] = s; m.m[10] = s; return m;
		};
		auto rotZ = [](float r) {
			engine::math::Mat4 m = engine::math::Mat4::Identity();
			const float c = std::cos(r), s = std::sin(r);
			m.m[0] = c; m.m[1] = s; m.m[4] = -s; m.m[5] = c; return m;
		};
		auto emitParts = [&](const std::vector<engine::world::instances::BuildingPart>& parts,
			float ox, float oz, float oyaw, float oscale)
		{
			const float groundY = m_terrainCollider.GroundHeightAt(ox, oz);
			const engine::math::Mat4 groupM =
				engine::math::Mat4::Translate(engine::math::Vec3{ ox, groundY, oz }) *
				engine::math::Mat4::RotateY(oyaw * kDeg2Rad) * scaleMat(oscale);
			for (const auto& pt : parts)
			{
				if (pt.gltfRelativePath.empty()) continue;
				const engine::math::Mat4 localM =
					engine::math::Mat4::Translate(pt.localPosition) *
					engine::math::Mat4::RotateY(pt.localEulerDeg.y * kDeg2Rad) *
					engine::math::Mat4::RotateX(pt.localEulerDeg.x * kDeg2Rad) *
					rotZ(pt.localEulerDeg.z * kDeg2Rad) * scaleMat(pt.localScale);
				BuildPropFromMeshMatrix(pt.gltfRelativePath, groupM * localM,
					/*interactableIndex*/ -1, pt.solid, pt.collisionRadius);
			}
		};

		// Origine du brouillon. PREMIÈRE apparition (m_editorPreviewValid==false,
		// ex. première pièce ajoutée ou variante chargée) : on la place au point
		// que REGARDE la caméra (projection du rayon de visée sur le sol) pour
		// qu'elle soit visible — sinon, à une coord fixe, elle tombe hors du
		// rayon de culling des props (~80 m). Une fois posée, l'origine reste
		// STABLE entre les rebuilds (sélection/édition de pièce), pour que le
		// bâtiment ne « téléporte » pas sous la caméra à chaque clic et que le
		// gizmo/picking reste cohérent. (Vider la variante remet valid=false →
		// re-centrage de la prochaine composition sur la vue.)
		float previewX;
		float previewZ;
		if (m_editorPreviewValid)
		{
			previewX = m_editorPreviewOriginX;
			previewZ = m_editorPreviewOriginZ;
		}
		else
		{
			const engine::render::Camera& cam =
				m_renderStates[m_renderReadIndex.load(std::memory_order_acquire)].camera;
			previewX = cam.position.x;
			previewZ = cam.position.z;
			const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
			const float fwdY = -sp; // forward.y (convention OrbitalCameraController)
			const float gy0 = m_terrainCollider.GroundHeightAt(cam.position.x, cam.position.z);
			if (fwdY < -1e-3f) // caméra regarde vers le bas : intersection avec le sol
			{
				float t = (gy0 - cam.position.y) / fwdY;
				if (t < 0.0f) t = 0.0f;
				if (t > 30.0f) t = 30.0f; // borne ~30 m : assez près pour être visible (rasant pouvait donner ~280 m, hors-champ)
				previewX = cam.position.x + t * (-std::sin(cam.yaw) * cp);
				previewZ = cam.position.z + t * (-std::cos(cam.yaw) * cp);
			}
			else // visée horizontale/vers le haut : 20 m devant, sur XZ
			{
				previewX = cam.position.x + (-std::sin(cam.yaw)) * 30.0f;
				previewZ = cam.position.z + (-std::cos(cam.yaw)) * 30.0f;
			}
		}
		// Brouillon + pièce EN COURS de configuration (aperçu live). Les
		// bâtiments DÉJÀ posés (dont l'auberge) sont déjà rendus par le boot
		// (LoadBuildings) et conservés dans le « monde » baseline ci-dessus.
		const std::vector<engine::world::instances::BuildingPart> previewParts =
			panel->PartsForPreview();
		emitParts(previewParts, previewX, previewZ,
			panel->PreviewYaw(), panel->PreviewScale());

		// Mémorise la transform du groupe pour le picking viewport (clic =
		// sélection), afin que la position monde calculée pour chaque pièce
		// corresponde exactement à ce qui vient d'être rendu.
		m_editorPreviewOriginX = previewX;
		m_editorPreviewOriginZ = previewZ;
		m_editorPreviewYaw     = panel->PreviewYaw();
		m_editorPreviewScale   = panel->PreviewScale();
		m_editorPreviewValid   = !previewParts.empty();

		// Cible du gizmo : position monde de la pièce active (sélectionnée ou en
		// cours), pour l'overlay DrawEditorBuildingGizmo.
		float activeLocal[3];
		if (panel->ActivePartLocalPos(activeLocal))
		{
			const float gY = m_terrainCollider.GroundHeightAt(previewX, previewZ);
			const engine::math::Mat4 grpM =
				engine::math::Mat4::Translate(engine::math::Vec3{ previewX, gY, previewZ }) *
				engine::math::Mat4::RotateY(panel->PreviewYaw() * kDeg2Rad) *
				scaleMat(panel->PreviewScale());
			const float* M = grpM.m;
			m_editorGizmoPos = engine::math::Vec3{
				M[0]*activeLocal[0] + M[4]*activeLocal[1] + M[8]*activeLocal[2]  + M[12],
				M[1]*activeLocal[0] + M[5]*activeLocal[1] + M[9]*activeLocal[2]  + M[13],
				M[2]*activeLocal[0] + M[6]*activeLocal[1] + M[10]*activeLocal[2] + M[14] };
			m_editorGizmoValid = true;
		}
		else
		{
			m_editorGizmoValid = false;
		}

		LOG_INFO(Render, "[Buildings][editeur] apercu : monde={} + brouillon/encours={} piece(s) @ ({:.1f},{:.1f}) -> total {}",
			static_cast<int>(m_editorBaselinePropCount),
			static_cast<int>(previewParts.size()),
			previewX, previewZ, static_cast<int>(m_props.size()));
	}

	void Engine::GizmoHandleSizes(float& axisLen, float& ringR) const
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::render::Camera& cam = m_renderStates[readIdx].camera;
		const float dx = cam.position.x - m_editorGizmoPos.x;
		const float dy = cam.position.y - m_editorGizmoPos.y;
		const float dz = cam.position.z - m_editorGizmoPos.z;
		const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		// ~constant à l'écran : longueur d'axe proportionnelle à la distance.
		axisLen = std::clamp(dist * 0.09f, 0.5f, 10.0f);
		ringR   = axisLen * 0.8f;
	}

	void Engine::DrawEditorBuildingGizmo()
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_editorGizmoValid) return;
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return;

		// Projection monde -> écran (pixels) via la viewProj courante.
		auto project = [&](const engine::math::Vec3& w, ImVec2& out) -> bool {
			const float* M = vp.m; // column-major
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false; // derrière la caméra
			out.x = (cx / cw * 0.5f + 0.5f) * W;
			out.y = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		ImVec2 so;
		if (!project(m_editorGizmoPos, so)) return;

		// Background draw list : par-dessus la 3D, sous les fenêtres ImGui.
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		float axisLen, ringR; GizmoHandleSizes(axisLen, ringR); // taille ~constante à l'écran
		const ImU32 colX = IM_COL32(235, 70, 70, 255);  // X rouge
		const ImU32 colY = IM_COL32(70, 210, 70, 255);  // Y vert
		const ImU32 colZ = IM_COL32(80, 130, 255, 255); // Z bleu
		const ImU32 hot  = IM_COL32(255, 240, 130, 255);// survol/actif : jaune vif
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const engine::math::Vec3 perps[3] = { {0,1,0}, {0,0,1}, {1,0,0} };
		const ImU32 cols[3] = { colX, colY, colZ };
		const engine::math::Vec3 o = m_editorGizmoPos;

		// Géométrie écran des handles (axes + anneaux).
		ImVec2 se[3]; bool seOk[3]; float ringRad[3]; bool ringOk[3];
		for (int a = 0; a < 3; ++a)
		{
			const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
			seOk[a] = project(tip, se[a]);
			ImVec2 sp;
			const engine::math::Vec3 rp{ o.x + perps[a].x*ringR, o.y + perps[a].y*ringR, o.z + perps[a].z*ringR };
			ringOk[a] = project(rp, sp);
			ringRad[a] = ringOk[a] ? std::sqrt((sp.x-so.x)*(sp.x-so.x) + (sp.y-so.y)*(sp.y-so.y)) : 0.0f;
		}

		// Handle SURVOLÉ (ou actif pendant un drag) → mis en évidence. Même test
		// que le picking, pour que ce qui s'allume soit ce qu'on saisira.
		int hovAxis = -1, hovMode = 0;
		{
			const ImVec2 mp = io.MousePos;
			auto distToSeg = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float {
				const float vx=b.x-a.x, vy=b.y-a.y, wx=p.x-a.x, wy=p.y-a.y;
				const float len2=vx*vx+vy*vy; float t=(len2>1e-6f)?(wx*vx+wy*vy)/len2:0.0f;
				t=std::clamp(t,0.0f,1.0f); const float cx=a.x+t*vx, cy=a.y+t*vy;
				return std::sqrt((p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy));
			};
			float best = 9.0f;
			for (int a = 0; a < 3; ++a)
			{
				if (seOk[a]) { const float sl=std::sqrt((se[a].x-so.x)*(se[a].x-so.x)+(se[a].y-so.y)*(se[a].y-so.y));
					if (sl>12.0f) { const float d=distToSeg(mp,so,se[a]); if (d<best){best=d;hovAxis=a;hovMode=0;} } }
				if (ringOk[a] && ringRad[a]>12.0f) { const float dm=std::sqrt((mp.x-so.x)*(mp.x-so.x)+(mp.y-so.y)*(mp.y-so.y));
					const float dr=std::abs(dm-ringRad[a]); if (dr<best){best=dr;hovAxis=a;hovMode=1;} }
			}
			if (m_gizmoDragAxis >= 0) { hovAxis = m_gizmoDragAxis; hovMode = m_gizmoDragMode; }
		}

		for (int a = 0; a < 3; ++a)
		{
			const bool axHot   = (hovAxis == a && hovMode == 0);
			const bool ringHot = (hovAxis == a && hovMode == 1);
			if (seOk[a])
			{
				dl->AddLine(so, se[a], axHot ? hot : cols[a], axHot ? 5.0f : 3.0f);
				dl->AddCircleFilled(se[a], axHot ? 7.0f : 5.0f, axHot ? hot : cols[a]);
			}
			if (ringOk[a] && ringRad[a] > 3.0f && ringRad[a] < 4000.0f)
				dl->AddCircle(so, ringRad[a], ringHot ? hot : cols[a], 48, ringHot ? 4.0f : 2.0f);
		}
		dl->AddCircleFilled(so, 4.0f, IM_COL32(255, 255, 255, 255)); // centre

		// Lecteur de valeurs : affiche la transform locale de la pièce active à
		// côté du gizmo (position + rotation), pour combler le « manque
		// d'information sur la valeur » pendant le drag (les anneaux qui tournent
		// ne montrent pas l'angle). L'axe en cours de manipulation est surligné.
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell ? m_worldEditorShell->GetBuildingEditorPanel() : nullptr)
		{
			float pos[3], rot[3], sc = 1.0f;
			if (bp->ActivePartTransform(pos, rot, sc))
			{
				const int dragAxis = m_gizmoDragAxis;          // -1 si pas de drag
				const bool dragRot  = (m_gizmoDragMode == 1);
				char l1[96], l2[96];
				std::snprintf(l1, sizeof(l1), "Pos  X %.2f  Y %.2f  Z %.2f  (m)", pos[0], pos[1], pos[2]);
				std::snprintf(l2, sizeof(l2), "Rot  X %.0f  Y %.0f  Z %.0f  (deg)", rot[0], rot[1], rot[2]);
				const ImVec2 p1{ so.x + 14.0f, so.y + 10.0f };
				const ImVec2 p2{ so.x + 14.0f, so.y + 26.0f };
				// Fond semi-opaque pour lisibilité par-dessus la 3D.
				dl->AddRectFilled(ImVec2(p1.x - 4.0f, p1.y - 2.0f), ImVec2(p1.x + 230.0f, p2.y + 16.0f),
					IM_COL32(0, 0, 0, 150), 3.0f);
				// Surligne la ligne active (jaune) selon le mode de drag.
				const ImU32 hotTxt = IM_COL32(255, 230, 90, 255);
				const ImU32 white = IM_COL32(235, 235, 235, 255);
				dl->AddText(p1, (dragAxis >= 0 && !dragRot) ? hotTxt : white, l1);
				dl->AddText(p2, (dragAxis >= 0 &&  dragRot) ? hotTxt : white, l2);
			}
		}
#endif
	}

	bool Engine::UpdateEditorBuildingGizmoDrag(int mouseX, int mouseY)
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_editorGizmoValid) return false;
		if (!m_worldEditorShell) return false;
		engine::editor::world::panels::BuildingEditorPanel* panel =
			m_worldEditorShell->GetBuildingEditorPanel();
		// Le drag n'agit que sur une pièce SÉLECTIONNÉE du brouillon (la pièce en
		// cours d'ajout reste pilotée par les champs du panneau).
		if (!panel || panel->SelectedDraft() < 0) { m_gizmoDragAxis = -1; return false; }

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return false;

		auto project = [&](const engine::math::Vec3& w, float& sx, float& sy) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false;
			sx = (cx / cw * 0.5f + 0.5f) * W;
			sy = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		float axisLen, ringR; GizmoHandleSizes(axisLen, ringR); // identique au dessin
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const engine::math::Vec3 perps[3] = { {0,1,0}, {0,0,1}, {1,0,0} };
		const engine::math::Vec3 o = m_editorGizmoPos;

		float ox, oy;
		if (!project(o, ox, oy)) { m_gizmoDragAxis = -1; return false; }

		// Géométrie écran courante des handles (recalculée chaque frame).
		float tipX[3], tipY[3]; bool tipOk[3];
		float ringRad[3]; bool ringOk[3];
		for (int a = 0; a < 3; ++a)
		{
			const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
			tipOk[a] = project(tip, tipX[a], tipY[a]);
			float spx, spy;
			const engine::math::Vec3 rp{ o.x + perps[a].x*ringR, o.y + perps[a].y*ringR, o.z + perps[a].z*ringR };
			ringOk[a] = project(rp, spx, spy);
			ringRad[a] = ringOk[a] ? std::sqrt((spx-ox)*(spx-ox) + (spy-oy)*(spy-oy)) : 0.0f;
		}

		const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);

		// Distance point → segment [a,b] en écran.
		auto distToSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
			const float vx = bx-ax, vy = by-ay;
			const float wx = px-ax, wy = py-ay;
			const float len2 = vx*vx + vy*vy;
			float t = (len2 > 1e-6f) ? (wx*vx + wy*vy) / len2 : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			const float cx = ax + t*vx, cy = ay + t*vy;
			const float dx = px-cx, dy = py-cy;
			return std::sqrt(dx*dx + dy*dy);
		};

		const bool pressed  = m_input.WasMousePressed(engine::platform::MouseButton::Left);
		const bool downNow  = m_input.IsMouseDown(engine::platform::MouseButton::Left);
		const bool released = m_input.WasMouseReleased(engine::platform::MouseButton::Left);

		// --- Saisie d'un handle au clic -----------------------------------
		if (m_gizmoDragAxis < 0 && pressed)
		{
			constexpr float kPick = 9.0f; // tolérance écran (pixels)
			int bestAxis = -1, bestMode = 0;
			float bestDist = kPick;
			for (int a = 0; a < 3; ++a)
			{
				if (tipOk[a])
				{
					const float dseg = distToSeg(mx, my, ox, oy, tipX[a], tipY[a]);
					// Évite de saisir un axe quasi vu de bout (segment trop court).
					const float segLen = std::sqrt((tipX[a]-ox)*(tipX[a]-ox) + (tipY[a]-oy)*(tipY[a]-oy));
					if (segLen > 12.0f && dseg < bestDist) { bestDist = dseg; bestAxis = a; bestMode = 0; }
				}
				if (ringOk[a] && ringRad[a] > 12.0f)
				{
					const float dmouse = std::sqrt((mx-ox)*(mx-ox) + (my-oy)*(my-oy));
					const float dring = std::abs(dmouse - ringRad[a]);
					if (dring < bestDist) { bestDist = dring; bestAxis = a; bestMode = 1; }
				}
			}
			if (bestAxis >= 0)
			{
				// Un drag = une étape d'annulation : on capture l'état AVANT de
				// commencer à déplacer/tourner (les mutations seront silencieuses).
				panel->PushUndoSnapshot();
				m_gizmoDragAxis = bestAxis;
				m_gizmoDragMode = bestMode;
				m_gizmoDragLastX = mx; m_gizmoDragLastY = my;
				m_gizmoDragLastAngle = std::atan2(my - oy, mx - ox);
				return true; // capture : pas de sélection de pièce sur ce clic
			}
		}

		// --- Application pendant le drag -----------------------------------
		if (m_gizmoDragAxis >= 0 && downNow)
		{
			const int a = m_gizmoDragAxis;
			if (m_gizmoDragMode == 0) // translation
			{
				// Direction écran de l'axe + mètres-monde par pixel le long de
				// l'axe (la poignée à axisLen mètres se projette à |tip-o|).
				float axSx = tipX[a]-ox, axSy = tipY[a]-oy;
				const float segLen = std::sqrt(axSx*axSx + axSy*axSy);
				if (segLen > 1e-3f)
				{
					axSx /= segLen; axSy /= segLen;
					const float worldPerPix = axisLen / segLen;
					const float dScreen = (mx - m_gizmoDragLastX)*axSx + (my - m_gizmoDragLastY)*axSy;
					const float d = dScreen * worldPerPix; // mètres monde le long de l'axe a
					// Monde → local : inverse(RotateY(groupYaw)) / échelle groupe.
					constexpr float kDeg2Rad = 3.14159265f / 180.f;
					const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
					const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
					const float invScale = (m_editorPreviewScale > 1e-4f) ? 1.0f/m_editorPreviewScale : 1.0f;
					float lx = 0, ly = 0, lz = 0;
					if (a == 0)      { lx = c*d*invScale;  lz = s*d*invScale; }
					else if (a == 1) { ly = d*invScale; }
					else             { lx = -s*d*invScale; lz = c*d*invScale; }
					// Silencieux : on ne reconstruit PAS le mesh à chaque frame (fuite
					// GPU) ; le gizmo suit via la donnée, le mesh est rebâti au release.
					panel->TranslateSelectedSilent(lx, ly, lz);
				}
			}
			else // rotation
			{
				const float angNow = std::atan2(my - oy, mx - ox);
				float dAng = angNow - m_gizmoDragLastAngle;
				// Normalise dans [-pi, pi] (passage ±180°).
				while (dAng >  3.14159265f) dAng -= 6.28318530f;
				while (dAng < -3.14159265f) dAng += 6.28318530f;
				m_gizmoDragLastAngle = angNow;
				panel->AddRotationSelectedSilent(a, dAng * (180.0f / 3.14159265f));
			}
			m_gizmoDragLastX = mx; m_gizmoDragLastY = my;

			// Rafraîchit le MESH périodiquement pendant le drag (~1 frame sur 6,
			// soit ~10 Hz) pour un retour visuel : les données sont déjà à jour
			// (mutateurs silencieux), on demande juste un rebuild ponctuel. Throttlé
			// car BuildPropFromMeshMatrix ne libère pas les meshes GPU (un rebuild
			// par frame saturerait). Le rebuild final a lieu au relâchement.
			if (++m_gizmoDragRefreshTick >= 6)
			{
				m_gizmoDragRefreshTick = 0;
				panel->MarkPreviewDirty();
			}

			// Recalcule la cible du gizmo CHAQUE frame depuis la pièce mise à jour,
			// pour que les cercles suivent la souris en temps réel même si le mesh
			// n'est reconstruit qu'à intervalles (throttle ci-dessus).
			float local[3];
			if (panel->ActivePartLocalPos(local))
			{
				constexpr float kDeg2Rad = 3.14159265f / 180.f;
				const float gY = m_terrainCollider.GroundHeightAt(m_editorPreviewOriginX, m_editorPreviewOriginZ);
				const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
				const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
				const float sx = m_editorPreviewScale * local[0];
				const float sy = m_editorPreviewScale * local[1];
				const float sz = m_editorPreviewScale * local[2];
				m_editorGizmoPos = engine::math::Vec3{
					m_editorPreviewOriginX + (c*sx + s*sz),
					gY + sy,
					m_editorPreviewOriginZ + (-s*sx + c*sz) };
			}
			return true;
		}

		// Fin du drag : reconstruire le mesh UNE fois à la position finale (les
		// mutations pendant le drag étaient silencieuses pour éviter la fuite GPU).
		if (released)
		{
			const bool wasDragging = (m_gizmoDragAxis >= 0);
			m_gizmoDragAxis = -1;
			if (wasDragging) { panel->MarkPreviewDirty(); return true; }
		}
		return m_gizmoDragAxis >= 0;
#else
		(void)mouseX; (void)mouseY; return false;
#endif
	}

	void Engine::SceneGizmoHandleSizes(const engine::math::Vec3& pos, float& axisLen, float& ringR) const
	{
		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::render::Camera& cam = m_renderStates[readIdx].camera;
		const float dx = cam.position.x - pos.x;
		const float dy = cam.position.y - pos.y;
		const float dz = cam.position.z - pos.z;
		const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
		// ~constant à l'écran : longueur d'axe proportionnelle à la distance
		// (mêmes coefficients que le gizmo bâtiment, cf. GizmoHandleSizes).
		axisLen = std::clamp(dist * 0.09f, 0.5f, 10.0f);
		ringR   = axisLen * 0.8f;
	}

	void Engine::DrawEditorSceneGizmo()
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_worldEditorShell || !m_worldEditorShell->IsInitialized()) return;
		// Le gizmo bâtiment garde la main en mode édition bâtiment (deux gizmos
		// superposés seraient illisibles et se disputeraient les clics).
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell->GetBuildingEditorPanel(); bp && bp->EditModeActive()) return;
		const engine::editor::scene::EditorSelection& sel = m_worldEditorShell->GetSelection();
		if (!sel.HasSelection()) return;
		const engine::editor::scene::EditorSceneModel& model = m_worldEditorShell->GetSceneModel();
		const engine::editor::scene::SceneEntity* primary = model.Find(sel.Current());
		if (primary == nullptr || !primary->hasTransform) return;

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return;

		// Projection monde -> écran (pixels) via la viewProj courante (même
		// convention que le gizmo bâtiment : viewProj col-major, pas de Y-flip).
		auto project = [&](const engine::math::Vec3& w, ImVec2& out) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false; // derrière la caméra
			out.x = (cx / cw * 0.5f + 0.5f) * W;
			out.y = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		ImDrawList* dl = ImGui::GetBackgroundDrawList();

		// Marqueurs de multi-sélection : un anneau magenta par entité (double
		// épaisseur sur la primaire) pour VOIR ce qui sera affecté par le geste.
		const ImU32 colSel = IM_COL32(230, 80, 230, 220);
		for (const engine::editor::scene::EntityId& id : sel.Items())
		{
			const engine::editor::scene::SceneEntity* e = model.Find(id);
			if (e == nullptr || !e->hasTransform) continue;
			ImVec2 p;
			if (project(e->transform.position, p))
				dl->AddCircle(p, (id == sel.Current()) ? 11.0f : 7.0f, colSel, 24,
					(id == sel.Current()) ? 3.0f : 1.5f);
		}

		const engine::math::Vec3 o = primary->transform.position;
		ImVec2 so;
		if (!project(o, so)) return;

		float axisLen, ringR; SceneGizmoHandleSizes(o, axisLen, ringR);
		const ImU32 colX = IM_COL32(235, 70, 70, 255);  // X rouge
		const ImU32 colY = IM_COL32(70, 210, 70, 255);  // Y vert
		const ImU32 colZ = IM_COL32(80, 130, 255, 255); // Z bleu
		const ImU32 hot  = IM_COL32(255, 240, 130, 255);// survol/actif : jaune vif
		const ImU32 cols[3] = { colX, colY, colZ };
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const int mode = static_cast<int>(m_worldEditorShell->GetSceneGizmoMode());
		const bool dragging = (m_sceneGizmoDragAxis >= 0);

		if (mode == 0) // Translation : 3 axes monde
		{
			auto distToSeg = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float {
				const float vx=b.x-a.x, vy=b.y-a.y, wx=p.x-a.x, wy=p.y-a.y;
				const float len2=vx*vx+vy*vy; float t=(len2>1e-6f)?(wx*vx+wy*vy)/len2:0.0f;
				t=std::clamp(t,0.0f,1.0f); const float cx=a.x+t*vx, cy=a.y+t*vy;
				return std::sqrt((p.x-cx)*(p.x-cx)+(p.y-cy)*(p.y-cy));
			};
			int hovAxis = dragging ? m_sceneGizmoDragAxis : -1;
			for (int a = 0; a < 3; ++a)
			{
				const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
				ImVec2 se;
				if (!project(tip, se)) continue;
				if (!dragging)
				{
					const float sl = std::sqrt((se.x-so.x)*(se.x-so.x) + (se.y-so.y)*(se.y-so.y));
					if (sl > 12.0f && distToSeg(io.MousePos, so, se) < 9.0f) hovAxis = a;
				}
				const bool axHot = (hovAxis == a);
				dl->AddLine(so, se, axHot ? hot : cols[a], axHot ? 5.0f : 3.0f);
				dl->AddCircleFilled(se, axHot ? 7.0f : 5.0f, axHot ? hot : cols[a]);
			}
		}
		else if (mode == 1) // Rotation yaw : un anneau autour de Y
		{
			ImVec2 rp;
			const engine::math::Vec3 rw{ o.x + ringR, o.y, o.z };
			if (project(rw, rp))
			{
				const float rad = std::sqrt((rp.x-so.x)*(rp.x-so.x) + (rp.y-so.y)*(rp.y-so.y));
				if (rad > 3.0f && rad < 4000.0f)
				{
					bool ringHot = dragging;
					if (!dragging)
					{
						const float dm = std::sqrt((io.MousePos.x-so.x)*(io.MousePos.x-so.x)
							+ (io.MousePos.y-so.y)*(io.MousePos.y-so.y));
						ringHot = std::abs(dm - rad) < 9.0f;
					}
					dl->AddCircle(so, rad, ringHot ? hot : colY, 48, ringHot ? 4.0f : 2.5f);
				}
			}
		}
		else // Échelle uniforme : poignée carrée au centre (drag vertical)
		{
			bool sqHot = dragging;
			if (!dragging)
			{
				sqHot = std::abs(io.MousePos.x - so.x) < 14.0f && std::abs(io.MousePos.y - so.y) < 14.0f;
			}
			dl->AddRectFilled(ImVec2(so.x - 9.0f, so.y - 9.0f), ImVec2(so.x + 9.0f, so.y + 9.0f),
				sqHot ? hot : IM_COL32(210, 210, 210, 255), 2.0f);
		}
		dl->AddCircleFilled(so, 4.0f, IM_COL32(255, 255, 255, 255)); // centre

		// Lecteur de valeurs : transform de l'entité primaire + taille de la
		// sélection (le drag n'affiche pas ses valeurs autrement).
		{
			char l1[112], l2[112];
			std::snprintf(l1, sizeof(l1), "Pos  X %.2f  Y %.2f  Z %.2f  (m)",
				primary->transform.position.x, primary->transform.position.y, primary->transform.position.z);
			std::snprintf(l2, sizeof(l2), "Yaw  %.0f deg   Echelle  %.2f   (%d selectionnee(s))",
				primary->transform.eulerDeg.y, primary->transform.uniformScale,
				static_cast<int>(sel.Count()));
			const ImVec2 p1{ so.x + 14.0f, so.y + 10.0f };
			const ImVec2 p2{ so.x + 14.0f, so.y + 26.0f };
			dl->AddRectFilled(ImVec2(p1.x - 4.0f, p1.y - 2.0f), ImVec2(p1.x + 290.0f, p2.y + 16.0f),
				IM_COL32(0, 0, 0, 150), 3.0f);
			const ImU32 white = IM_COL32(235, 235, 235, 255);
			const ImU32 hotTxt = IM_COL32(255, 230, 90, 255);
			dl->AddText(p1, (dragging && m_sceneGizmoDragMode == 0) ? hotTxt : white, l1);
			dl->AddText(p2, (dragging && m_sceneGizmoDragMode != 0) ? hotTxt : white, l2);
		}
#endif
	}

	bool Engine::UpdateEditorSceneGizmoDrag(int mouseX, int mouseY)
	{
#if defined(_WIN32)
		if (!m_editorEnabled || !m_worldEditorShell || !m_worldEditorShell->IsInitialized()) return false;
		if (engine::editor::world::panels::BuildingEditorPanel* bp =
			m_worldEditorShell->GetBuildingEditorPanel(); bp && bp->EditModeActive())
		{
			m_sceneGizmoDragAxis = -1;
			return false;
		}
		const engine::editor::scene::EditorSelection& sel = m_worldEditorShell->GetSelection();
		const engine::editor::scene::EditorSceneModel& model = m_worldEditorShell->GetSceneModel();
		const engine::editor::scene::SceneEntity* primary =
			sel.HasSelection() ? model.Find(sel.Current()) : nullptr;
		if (primary == nullptr || !primary->hasTransform)
		{
			m_sceneGizmoDragAxis = -1;
			return false;
		}
		const engine::editor::world::WorldEditorShell::TransformWriter& writer =
			m_worldEditorShell->GetTransformWriter();
		if (!writer) return false;

		const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
		const engine::math::Mat4& vp = m_renderStates[readIdx].viewProjMatrix;
		const ImGuiIO& io = ImGui::GetIO();
		const float W = io.DisplaySize.x, H = io.DisplaySize.y;
		if (W <= 0.0f || H <= 0.0f) return false;

		auto project = [&](const engine::math::Vec3& w, float& sx, float& sy) -> bool {
			const float* M = vp.m;
			const float cx = M[0]*w.x + M[4]*w.y + M[8]*w.z  + M[12];
			const float cy = M[1]*w.x + M[5]*w.y + M[9]*w.z  + M[13];
			const float cw = M[3]*w.x + M[7]*w.y + M[11]*w.z + M[15];
			if (cw <= 1e-4f) return false;
			sx = (cx / cw * 0.5f + 0.5f) * W;
			sy = (cy / cw * 0.5f + 0.5f) * H;
			return true;
		};

		const engine::math::Vec3 o = primary->transform.position;
		float ox, oy;
		if (!project(o, ox, oy)) { m_sceneGizmoDragAxis = -1; return false; }
		float axisLen, ringR; SceneGizmoHandleSizes(o, axisLen, ringR);
		const engine::math::Vec3 dirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		const int mode = static_cast<int>(m_worldEditorShell->GetSceneGizmoMode());
		const float mx = static_cast<float>(mouseX), my = static_cast<float>(mouseY);

		const bool pressed  = m_input.WasMousePressed(engine::platform::MouseButton::Left);
		const bool downNow  = m_input.IsMouseDown(engine::platform::MouseButton::Left);
		const bool released = m_input.WasMouseReleased(engine::platform::MouseButton::Left);

		// --- Saisie d'un handle au clic (selon le mode courant) --------------
		if (m_sceneGizmoDragAxis < 0 && pressed)
		{
			int grabbedAxis = -1;
			if (mode == 0) // translation : un des 3 axes
			{
				auto distToSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
					const float vx = bx-ax, vy = by-ay, wx = px-ax, wy = py-ay;
					const float len2 = vx*vx + vy*vy;
					float t = (len2 > 1e-6f) ? (wx*vx + wy*vy) / len2 : 0.0f;
					t = std::clamp(t, 0.0f, 1.0f);
					const float cx = ax + t*vx, cy = ay + t*vy;
					return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
				};
				float bestDist = 9.0f; // tolérance écran (pixels)
				for (int a = 0; a < 3; ++a)
				{
					const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
					float tx, ty;
					if (!project(tip, tx, ty)) continue;
					const float segLen = std::sqrt((tx-ox)*(tx-ox) + (ty-oy)*(ty-oy));
					if (segLen <= 12.0f) continue; // axe quasi vu de bout
					const float d = distToSeg(mx, my, ox, oy, tx, ty);
					if (d < bestDist) { bestDist = d; grabbedAxis = a; }
				}
			}
			else if (mode == 1) // rotation yaw : l'anneau
			{
				float rx, ry;
				const engine::math::Vec3 rw{ o.x + ringR, o.y, o.z };
				if (project(rw, rx, ry))
				{
					const float rad = std::sqrt((rx-ox)*(rx-ox) + (ry-oy)*(ry-oy));
					const float dm = std::sqrt((mx-ox)*(mx-ox) + (my-oy)*(my-oy));
					if (rad > 12.0f && std::abs(dm - rad) < 9.0f) grabbedAxis = 0;
				}
			}
			else // échelle : la poignée centrale
			{
				if (std::abs(mx - ox) < 14.0f && std::abs(my - oy) < 14.0f) grabbedAxis = 0;
			}

			if (grabbedAxis >= 0)
			{
				// Capture des transforms de départ de TOUTES les entités
				// éditables de la sélection (ancien état des commandes d'undo).
				m_sceneGizmoDragStart.clear();
				for (const engine::editor::scene::EntityId& id : sel.Items())
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e != nullptr && e->hasTransform)
						m_sceneGizmoDragStart.emplace_back(id, e->transform);
				}
				if (m_sceneGizmoDragStart.empty()) return false;
				m_sceneGizmoDragAxis = grabbedAxis;
				m_sceneGizmoDragMode = mode;
				m_sceneGizmoDragLastX = mx;
				m_sceneGizmoDragLastY = my;
				m_sceneGizmoDragLastAngle = std::atan2(my - oy, mx - ox);
				return true; // capture : pas de picking/outil sur ce clic
			}
		}

		// --- Application pendant le drag (écriture directe, undo au release) --
		if (m_sceneGizmoDragAxis >= 0 && downNow)
		{
			if (m_sceneGizmoDragMode == 0) // translation le long de l'axe monde
			{
				const int a = m_sceneGizmoDragAxis;
				const engine::math::Vec3 tip{ o.x + dirs[a].x*axisLen, o.y + dirs[a].y*axisLen, o.z + dirs[a].z*axisLen };
				float tx, ty;
				if (project(tip, tx, ty))
				{
					float axSx = tx - ox, axSy = ty - oy;
					const float segLen = std::sqrt(axSx*axSx + axSy*axSy);
					if (segLen > 1e-3f)
					{
						axSx /= segLen; axSy /= segLen;
						const float worldPerPix = axisLen / segLen;
						const float dScreen = (mx - m_sceneGizmoDragLastX)*axSx + (my - m_sceneGizmoDragLastY)*axSy;
						const float d = dScreen * worldPerPix; // mètres monde le long de l'axe a
						for (const auto& [id, startT] : m_sceneGizmoDragStart)
						{
							const engine::editor::scene::SceneEntity* e = model.Find(id);
							if (e == nullptr) continue;
							engine::editor::scene::EntityTransform t = e->transform;
							t.position.x += dirs[a].x * d;
							t.position.y += dirs[a].y * d;
							t.position.z += dirs[a].z * d;
							writer(id, t);
						}
					}
				}
			}
			else if (m_sceneGizmoDragMode == 1) // rotation yaw autour du pivot de chaque entité
			{
				const float angNow = std::atan2(my - oy, mx - ox);
				float dAng = angNow - m_sceneGizmoDragLastAngle;
				while (dAng >  3.14159265f) dAng -= 6.28318530f;
				while (dAng < -3.14159265f) dAng += 6.28318530f;
				m_sceneGizmoDragLastAngle = angNow;
				const float dDeg = dAng * (180.0f / 3.14159265f);
				for (const auto& [id, startT] : m_sceneGizmoDragStart)
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e == nullptr) continue;
					engine::editor::scene::EntityTransform t = e->transform;
					t.eulerDeg.y += dDeg;
					writer(id, t);
				}
			}
			else // échelle uniforme (drag vertical : haut = agrandir)
			{
				const float factor = 1.0f + (m_sceneGizmoDragLastY - my) * 0.01f;
				for (const auto& [id, startT] : m_sceneGizmoDragStart)
				{
					const engine::editor::scene::SceneEntity* e = model.Find(id);
					if (e == nullptr) continue;
					engine::editor::scene::EntityTransform t = e->transform;
					t.uniformScale = std::clamp(t.uniformScale * factor, 0.01f, 1000.0f);
					writer(id, t);
				}
			}
			m_sceneGizmoDragLastX = mx;
			m_sceneGizmoDragLastY = my;
			return true;
		}

		// --- Fin du drag : pousser UNE étape d'annulation pour tout le geste --
		if (released && m_sceneGizmoDragAxis >= 0)
		{
			auto sameTransform = [](const engine::editor::scene::EntityTransform& a,
				const engine::editor::scene::EntityTransform& b) -> bool
			{
				return a.position.x == b.position.x && a.position.y == b.position.y
					&& a.position.z == b.position.z
					&& a.eulerDeg.x == b.eulerDeg.x && a.eulerDeg.y == b.eulerDeg.y
					&& a.eulerDeg.z == b.eulerDeg.z
					&& a.uniformScale == b.uniformScale;
			};
			const char* label = (m_sceneGizmoDragMode == 0) ? "Gizmo : déplacer"
				: (m_sceneGizmoDragMode == 1) ? "Gizmo : tourner" : "Gizmo : échelle";
			// Toujours envelopper dans une CompositeCommand (mergeKey 0) : deux
			// drags successifs de la même entité ne doivent PAS fusionner en une
			// seule étape d'annulation (contrairement au slider de l'Inspector).
			auto composite = std::make_unique<engine::editor::world::CompositeCommand>(label);
			for (const auto& [id, startT] : m_sceneGizmoDragStart)
			{
				const engine::editor::scene::SceneEntity* e = model.Find(id);
				if (e == nullptr || sameTransform(startT, e->transform)) continue;
				composite->AddChild(std::make_unique<engine::editor::world::SetEntityTransformCommand>(
					id, startT, e->transform, writer));
			}
			if (!composite->Empty())
			{
				m_worldEditorShell->MutableCommandStack().Push(std::move(composite));
			}
			m_sceneGizmoDragAxis = -1;
			m_sceneGizmoDragStart.clear();
			return true;
		}
		return m_sceneGizmoDragAxis >= 0;
#else
		(void)mouseX; (void)mouseY; return false;
#endif
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

		// M100.46+ — Pont TerrainDocument → HeightmapData GPU. Le callback
		// est appelé synchrone par OnCommit (thread main) — il ne fait que
		// set un flag atomique pour différer la sync au prochain tick (où
		// l'on a accès à VkDeviceContext sans race). Re-set à chaque
		// Rebuild pour ne pas perdre l'observer si Shell ou Tools sont
		// reset.
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			m_worldEditorShell->MutableTerrainDocument().SetOnChunkChanged(
				[this](engine::world::GlobalChunkCoord) {
					m_worldEditorTerrainNeedsSync.store(true, std::memory_order_release);
				});
		}

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

			// Cadrage ADAPTATIF : on place la camera en recul (+Z) et en hauteur
			// PROPORTIONNELS a l'etendue REELLE du terrain, orientee pour viser
			// exactement son centre. L'ancienne version (altitude fixe +80 m,
			// pitch fixe ~20deg) cadrait les grands terrains mais laissait une
			// PETITE carte neuve (ex. 60 m) entierement SOUS le bas de l'ecran :
			// le terrain etait bien rendu (Record diag kept=225) mais hors champ,
			// d'ou le ressenti "rien ne s'affiche apres Creer une nouvelle carte".
			// On calcule la distance qui fait tenir l'etendue verticale du
			// terrain dans le FOV (+ marge), avec un garde-fou bas pour les
			// micro-terrains. La camera vise le centre : avec la base camera
			// (forward = (0, -sin(pitch), -cos(pitch)) pour yaw=0), poser la
			// camera a `centre + (0, dist*sin(pitch), dist*cos(pitch))` garantit
			// que l'axe optique passe pile par le centre du sol.
			constexpr float kPi      = 3.14159265358979323846f;
			constexpr float fovYDeg  = 70.0f;
			constexpr float pitchDown = 0.70f;              // ~40deg : vue d'ensemble plongeante
			const float halfFovY     = (fovYDeg * 0.5f) * (kPi / 180.0f);
			const float fitDist      = (std::max(maxExt, 1.0f) * 0.5f * 1.3f) / std::tan(halfFovY);
			const float dist         = std::max(fitDist, 8.0f); // garde-fou micro-terrain

			engine::render::Camera reset;
			reset.position.x = centerX;
			reset.position.y = midGroundY + dist * std::sin(pitchDown);
			reset.position.z = centerZ + dist * std::cos(pitchDown);
			reset.yaw        = 0.0f;
			reset.pitch      = pitchDown;
			reset.fovYDeg    = fovYDeg;
			reset.aspect     = static_cast<float>(std::max(1, m_width)) / static_cast<float>(std::max(1, m_height));
			reset.nearZ      = 0.1f;
			reset.farZ       = std::max(5000.0f, (dist + maxExt) * 2.0f); // visibilite des bords
			m_renderStates[0].camera = reset;
			m_renderStates[1].camera = reset;
			LOG_INFO(Render,
				"[WorldEditor] Camera reset: pos=({:.1f},{:.1f},{:.1f}) farZ={:.0f} actualExt=({:.0f}x{:.0f}) origin=({:.0f},{:.0f})",
				reset.position.x, reset.position.y, reset.position.z, reset.farZ,
				actualExtX, actualExtZ, ox, oz);
		}
	}

	void Engine::SyncWorldEditorHeightmapFromDocument()
	{
#if defined(_WIN32)
		// Garde stricte : seul l'éditeur monde a un TerrainDocument actif
		// (le Shell). Le client jeu ne passe jamais ici.
		if (!m_worldEditorExe || !m_worldEditorShell || !m_worldEditorShell->IsInitialized())
			return;
		if (!m_worldEditorTerrainTools.IsValid()) return;
		if (!m_terrain.IsValid()) return;

		auto& terrainDoc = m_worldEditorShell->MutableTerrainDocument();
		auto& hm         = m_terrain.GetMutableHeightmapData();
		if (hm.width == 0 || hm.height == 0 || hm.heights.empty()) return;

		const float heightScale = m_terrain.GetHeightScale();
		if (heightScale <= 0.0f) return;

		// Map terrain world coords → heightmap pixel coords.
		// La heightmap couvre `terrainWorldSize` mètres avec `width-1` mailles
		// horizontales. Origine en (terrainOriginX, terrainOriginZ).
		const float originX     = m_terrain.GetTerrainOriginX();
		const float originZ     = m_terrain.GetTerrainOriginZ();
		const float worldSize   = m_terrain.GetTerrainWorldSize();
		if (worldSize <= 0.0f) return;
		const uint32_t hmW      = hm.width;
		const uint32_t hmH      = hm.height;
		const float pixelsPerMX = static_cast<float>(hmW - 1u) / worldSize;
		const float pixelsPerMZ = static_cast<float>(hmH - 1u) / worldSize;

		constexpr uint32_t kChunkRes =
			static_cast<uint32_t>(engine::world::terrain::kTerrainResolution);
		constexpr float kCell =
			engine::world::terrain::kTerrainCellSizeMeters;

		// Itère **tous les chunks chargés** du document (pas seulement 2×2),
		// car les zone presets s'appliquent sur 10 km (toute la zone) et
		// peuvent charger n'importe quelle paire (cx, cz). Pour chaque
		// cellule (ix, iz), on calcule sa position monde → pixel heightmap.
		// Conversion float (m) → uint16 normalise par heightScale.
		uint32_t touchedChunks = 0u;
		uint64_t modifiedCells = 0u;
		terrainDoc.ForEachLoadedChunk(
			[&](engine::world::GlobalChunkCoord coord,
			    const std::shared_ptr<engine::world::terrain::TerrainChunk>& chunk)
			{
				if (!chunk) return;
				++touchedChunks;
				const float chunkOriginX = static_cast<float>(coord.x) * (kChunkRes - 1u) * kCell;
				const float chunkOriginZ = static_cast<float>(coord.z) * (kChunkRes - 1u) * kCell;
				for (uint32_t iz = 0; iz < kChunkRes; ++iz)
				{
					for (uint32_t ix = 0; ix < kChunkRes; ++ix)
					{
						const float worldX = originX + chunkOriginX + static_cast<float>(ix) * kCell;
						const float worldZ = originZ + chunkOriginZ + static_cast<float>(iz) * kCell;
						const float fx = (worldX - originX) * pixelsPerMX;
						const float fz = (worldZ - originZ) * pixelsPerMZ;
						if (fx < 0.0f || fz < 0.0f) continue;
						const uint32_t px = static_cast<uint32_t>(fx + 0.5f);
						const uint32_t pz = static_cast<uint32_t>(fz + 0.5f);
						if (px >= hmW || pz >= hmH) continue;
						const float heightMeters = chunk->heights[iz * kChunkRes + ix];
						const float norm = std::clamp(heightMeters / heightScale, 0.0f, 1.0f);
						const uint16_t u16 = static_cast<uint16_t>(norm * 65535.0f + 0.5f);
						hm.heights[pz * hmW + px] = u16;
						++modifiedCells;
					}
				}
			});
		LOG_INFO(EditorWorld,
			"[Engine] SyncHeightmapFromDocument: {} chunks visited, {} cells written, hm={}x{}, heightScale={:.1f}m",
			touchedChunks, modifiedCells, hmW, hmH, heightScale);
		if (touchedChunks == 0u) return;

		// Pousse au GPU. FlushHeightmap re-upload la heightmap CPU complète
		// vers l'image GPU R16 (single staging buffer + one-time command
		// buffer). Coût ~10-30 ms sur 1025² uint16 (~2 MB).
		const auto& hmGpu = m_terrain.GetHeightmapGpu();
		(void)m_worldEditorTerrainTools.FlushHeightmap(
			m_vkDeviceContext.GetDevice(),
			m_vkDeviceContext.GetPhysicalDevice(),
			m_vkDeviceContext.GetGraphicsQueue(),
			m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
			hmGpu.image);
#endif
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
#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::UpdateWorldEditorSession()
	{
		// Sous-projet 1 (boucle d'edition d'une zone) — Zone NEUVE : initialise
		// les chunks plats (source de verite) AVANT le rebuild GPU. L'empreinte
		// N (chunks par axe) se deduit de la resolution r16h : apres l'alignement
		// A2, 1 texel = 1 m, et un chunk fait 256 m, donc N = max(1, res/256).
		if (m_worldEditorSession->ConsumeNewZoneChunkInitRequest()
			&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			const uint32_t hmRes = m_worldEditorSession->Doc().heightmapResolution;
			const int chunksPerAxis = static_cast<int>(std::max<uint32_t>(1u, hmRes / 256u));
			// Accord de hauteur chunk <-> heightmap (corrige le "terrain qui
			// chute hors champ a la 1ere edition"). ActionNewMap ecrit la
			// heightmap r16h a la valeur 32768 (milieu). Le chunk plat
			// (source de verite) doit demarrer a la MEME hauteur physique,
			// sinon la 1ere SyncWorldEditorHeightmapFromDocument reecrit
			// toute la heightmap a la hauteur du chunk et fait chuter le sol.
			// height_scale est lue avec la meme cle/le meme defaut que
			// TerrainRenderer::Init (terrain.height_scale, defaut 200).
			const float heightScale =
				static_cast<float>(m_cfg.GetDouble("terrain.height_scale", 200.0));
			const float flatHeightMeters = (32768.0f / 65535.0f) * heightScale;
			// Lot B3 (audit 2026-06-10 §4.2, correctifs 1-2-4) — Nouvelle carte
			// = changement de monde : vide la pile undo, reinitialise les 4
			// documents (terrain/eau/mesh/portails) et propage le zoneId
			// (namespacing disque chunks/zone_<id>/...) AVANT l'init des
			// chunks plats. Pas de LoadZoneDocuments ici : une zone neuve
			// demarre avec des documents vides.
			m_worldEditorShell->ResetForZoneChange(
				engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
			m_worldEditorShell->InitNewZoneTerrain(chunksPerAxis, flatHeightMeters);
			// Réorganisation UI 2026-07-17 — une zone fraîchement créée
			// (fichiers déjà écrits par ActionNewMap) repart d'un état
			// « tout enregistré » : sans ce NoteSaved, le Clear de la
			// pile undo (ResetForZoneChange) marquerait la carte neuve
			// « non sauvegardée » dans la barre de statut.
			m_worldEditorShell->NoteSaved();
			// Nouvelle carte = la caméra est replacée sur la nouvelle zone :
			// l'origine STABLE du brouillon (mémorisée pour le gizmo/picking)
			// pointe encore sur l'ANCIENNE carte. On l'invalide pour que le
			// brouillon (auberge en cours) se recentre sur la vue de la
			// nouvelle carte au prochain rebuild — sinon il reste hors-champ
			// et « ne se charge plus ». On force aussi le rebuild.
			m_editorPreviewValid = false;
			if (auto* bp = m_worldEditorShell->GetBuildingEditorPanel())
				bp->MarkPreviewDirty();
		}
		// Lot B3 (correctifs 1+3) — Chargement d'une carte EXISTANTE :
		// reinitialise les documents (les chunks de la carte precedente
		// restaient en RAM et reecrivaient la heightmap de la nouvelle via
		// SyncWorldEditorHeightmapFromDocument), vide l'undo, propage le
		// zoneId, puis recharge eau / mesh inserts / portails depuis les
		// chemins namespaces (fallback legacy plat en lecture). Place AVANT
		// la consommation du reload GPU pour que le rebuild parte d'un
		// etat document coherent.
		if (m_worldEditorSession->ConsumeZoneDocumentsReloadRequest()
			&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			m_worldEditorShell->ResetForZoneChange(
				engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
			m_worldEditorShell->LoadZoneDocuments(m_cfg);
			// Réorganisation UI 2026-07-17 — une carte fraîchement
			// chargée repart d'un état « tout enregistré » (cf. barre de
			// statut / modale Quitter) : le Clear de la pile undo fait
			// par ResetForZoneChange incrémente le sériel du CommandStack.
			m_worldEditorShell->NoteSaved();
			// Force la reconstruction de l'aperçu 3D des bâtiments de la
			// nouvelle zone (placements chargés depuis buildings.bin).
			if (auto* bp = m_worldEditorShell->GetBuildingEditorPanel())
				bp->MarkPreviewDirty();
			// Changement de zone : l'origine STABLE du brouillon (mémorisée pour
			// le picking) n'est plus pertinente — la prochaine apparition se
			// recentre sur la caméra de la nouvelle carte.
			m_editorPreviewValid = false;
		}
		if (m_worldEditorSession->ConsumeTerrainGpuReloadRequest())
		{
			RebuildWorldEditorTerrainGpu();
		}
		// Sous-projet 1 — Sauvegarde des chunks (source de verite) demandee par
		// ActionSaveCurrentMap / ActionSaveEditJson. Le r16h est ecrit par le
		// terrainSaveHook ; ici on persiste en plus les chunks terrain/splat.
		if (m_worldEditorSession->ConsumeTerrainChunksSaveRequest()
			&& m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			// Lot B3 (correctif 4) — re-propage le zoneId courant avant la
			// persistance : couvre le « save sous un autre nom de zone »
			// (ActionSaveCurrentMap re-sanitize m_doc.zoneId depuis le
			// buffer UI juste avant d'armer la requete de save).
			m_worldEditorShell->PropagateZoneIdToDocuments(
				engine::editor::SanitizeZoneId(m_worldEditorSession->Doc().zoneId));
			const size_t nWritten = m_worldEditorShell->SaveTerrainChunks(m_cfg);
			// Lot B3 (correctif 3) — persiste aussi les documents annexes
			// (eau, mesh inserts, portails de donjon) : avant ce fix, aucun
			// SaveToDisk n'etait appele hors tests -> lacs/rivieres/grottes/
			// arches/portails places etaient perdus a la fermeture.
			const size_t nDocs = m_worldEditorShell->SaveZoneDocuments(m_cfg);
			LOG_INFO(EditorWorld,
				"[Engine] Persisted {} terrain chunk file(s) + {} zone document(s)",
				nWritten, nDocs);
		}
	}
#endif

#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::ResizeWorldEditorViewportTarget()
	{
		auto* scenePanel = dynamic_cast<engine::editor::world::panels::ScenePanel*>(
			m_worldEditorShell->MutablePanels()[0].get());
		if (scenePanel != nullptr)
		{
			const int wRaw = scenePanel->GetViewportWidth();
			const int hRaw = scenePanel->GetViewportHeight();
			const uint32_t w = (wRaw > 0) ? static_cast<uint32_t>(wRaw) : 0u;
			const uint32_t h = (hRaw > 0) ? static_cast<uint32_t>(hRaw) : 0u;
			if (w > 0u && h > 0u
				&& (w != m_editorViewportTarget.GetWidth()
				 || h != m_editorViewportTarget.GetHeight()))
			{
				vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
				if (!m_editorViewportTarget.Resize(
					m_vkDeviceContext.GetDevice(),
					m_vkDeviceContext.GetPhysicalDevice(),
					m_vkDeviceContext.GetGraphicsQueue(),
					m_vkDeviceContext.GetGraphicsQueueFamilyIndex(),
					w, h))
				{
					LOG_WARN(Render,
						"[Engine] EditorViewportRenderTarget::Resize({}x{}) failed",
						w, h);
				}
			}
		}
	}
#endif

#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::RenderWorldEditorShell(engine::RenderState& out)
	{
		// M100.34 incrément 1 — pousse le texture ID de l'image offscreen
		// du viewport au ScenePanel (premier panel du Shell, ordre stable
		// garanti par WorldEditorShell::Init). PR 1 : image noire, pas
		// encore branchée au FrameGraph. PR 2 : copie SceneColor_LDR
		// dedans et la cible devient visible.
		if (m_editorViewportTarget.IsValid()
			&& !m_worldEditorShell->Panels().empty()
			&& m_worldEditorShell->Panels()[0])
		{
			auto* scenePanel = dynamic_cast<engine::editor::world::panels::ScenePanel*>(
				m_worldEditorShell->MutablePanels()[0].get());
			if (scenePanel != nullptr)
			{
				scenePanel->SetEditorViewportTextureId(
					m_editorViewportTarget.GetImguiTextureId());
			}
		}
		// Sous-projet 1, bloc B/C — alimente l'Outliner / l'Inspector : lie le
		// modèle de scène aux documents sources (layout = session, mesh/donjon
		// = shell) puis le reconstruit. Coût négligeable (quelques clears +
		// push de vecteurs) ; fait chaque frame pour refléter les placements/
		// suppressions immédiatement.
		if (m_worldEditorSession)
		{
			m_worldEditorShell->MutableSceneModel().Bind(
				&m_worldEditorSession->Doc(),
				&m_worldEditorShell->GetMeshInsertDocument(),
				&m_worldEditorShell->GetDungeonPortalDocument());
			m_worldEditorShell->MutableSceneModel().Rebuild();

			// Sous-projet 1, bloc D — foncteur d'écriture de transform consommé
			// par l'Inspector (SetEntityTransformCommand). Écrit dans le
			// document concret par EntityId.index (= position dans la liste
			// source au moment du Rebuild de cette frame). Capture [this] :
			// l'Engine survit aux commandes (undo/redo différé OK).
			m_worldEditorShell->SetTransformWriter(
				[this](engine::editor::scene::EntityId id,
					const engine::editor::scene::EntityTransform& t)
				{
					using K = engine::editor::scene::EntityKind;
					if (id.kind == K::LayoutInstance && m_worldEditorSession)
					{
						auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
						if (id.index < insts.size())
						{
							auto& inst = insts[id.index];
							inst.worldX = static_cast<double>(t.position.x);
							inst.worldY = static_cast<double>(t.position.y);
							inst.worldZ = static_cast<double>(t.position.z);
							inst.yawDegrees = static_cast<double>(t.eulerDeg.y);
							inst.uniformScale = static_cast<double>(t.uniformScale);
						}
					}
					else if (id.kind == K::MeshInsert && m_worldEditorShell)
					{
						auto& doc = m_worldEditorShell->MutableMeshInsertDocument();
						const auto& all = doc.All();
						if (id.index < all.size())
						{
							auto updated = all[id.index];
							updated.worldPosition = t.position;
							updated.eulerRotationDeg = t.eulerDeg;
							updated.uniformScale = t.uniformScale;
							doc.Update(updated.guid, updated);
						}
					}
					else if (id.kind == K::DungeonPortal && m_worldEditorShell)
					{
						auto& doc = m_worldEditorShell->MutableDungeonPortalDocument();
						const auto& all = doc.All();
						if (id.index < all.size())
						{
							auto updated = all[id.index];
							updated.worldPosition = t.position;
							updated.eulerRotationDeg = t.eulerDeg;
							doc.Update(updated.guid, updated);
						}
					}
				});

			// Lot 5 (2026-07-18) — Foncteurs d'édition STRUCTURELLE des
			// entités (Dupliquer/Supprimer undoables). Même pattern que le
			// TransformWriter ci-dessus : l'Engine capture [this] pour
			// atteindre le doc layout (session) que le shell ne possède
			// pas. Réinstallés chaque frame (comme le writer) — les
			// commandes gardent leur PROPRE copie des foncteurs au Push.
			{
				using engine::editor::scene::EntityId;
				using engine::editor::scene::EntityKind;
				using engine::editor::scene::EntitySnapshot;
				engine::editor::scene::EntityEditOps ops;

				// Copie l'entité (kind+index scène) en snapshot à guid
				// stable, sans la retirer. false si index hors bornes ou
				// kind non éditable.
				ops.capture = [this](EntityId id, EntitySnapshot& out) -> bool
				{
					if (id.kind == EntityKind::LayoutInstance && m_worldEditorSession)
					{
						const auto& insts = m_worldEditorSession->Doc().layoutInstances;
						if (id.index >= insts.size()) return false;
						out.kind = id.kind;
						out.layout = insts[id.index];
						out.layoutIndex = id.index;
						return true;
					}
					if (id.kind == EntityKind::MeshInsert && m_worldEditorShell)
					{
						const auto& all = m_worldEditorShell->GetMeshInsertDocument().All();
						if (id.index >= all.size()) return false;
						out.kind = id.kind;
						out.meshInsert = all[id.index];
						return true;
					}
					if (id.kind == EntityKind::DungeonPortal && m_worldEditorShell)
					{
						const auto& all = m_worldEditorShell->GetDungeonPortalDocument().All();
						if (id.index >= all.size()) return false;
						out.kind = id.kind;
						out.portal = all[id.index];
						return true;
					}
					return false;
				};

				// Retire l'entité du snapshot — par guid (stable), jamais
				// par index (instable après édition structurelle).
				ops.remove = [this](const EntitySnapshot& s) -> bool
				{
					if (s.kind == EntityKind::LayoutInstance && m_worldEditorSession)
					{
						auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
						for (size_t i = 0; i < insts.size(); ++i)
						{
							if (insts[i].guid == s.layout.guid)
							{
								insts.erase(insts.begin() + static_cast<ptrdiff_t>(i));
								return true;
							}
						}
						return false;
					}
					if (s.kind == EntityKind::MeshInsert && m_worldEditorShell)
						return m_worldEditorShell->MutableMeshInsertDocument().Remove(s.meshInsert.guid);
					if (s.kind == EntityKind::DungeonPortal && m_worldEditorShell)
						return m_worldEditorShell->MutableDungeonPortalDocument().Remove(s.portal.guid);
					return false;
				};

				// Réinsère le snapshot : au rang d'origine (borné) pour le
				// layout, par Add (guid conservé, compteur resynchronisé)
				// pour les volumes.
				ops.restore = [this](const EntitySnapshot& s) -> bool
				{
					if (s.kind == EntityKind::LayoutInstance && m_worldEditorSession)
					{
						auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
						const size_t at = std::min<size_t>(s.layoutIndex, insts.size());
						insts.insert(insts.begin() + static_cast<ptrdiff_t>(at), s.layout);
						return true;
					}
					if (s.kind == EntityKind::MeshInsert && m_worldEditorShell)
					{
						(void)m_worldEditorShell->MutableMeshInsertDocument().Add(s.meshInsert);
						return true;
					}
					if (s.kind == EntityKind::DungeonPortal && m_worldEditorShell)
					{
						(void)m_worldEditorShell->MutableDungeonPortalDocument().Add(s.portal);
						return true;
					}
					return false;
				};

				// Ajoute une COPIE de src (nouveau guid + décalage +1,5 m
				// en X pour que la copie soit visible à côté de
				// l'original) et renvoie son snapshot dans outCopy.
				ops.duplicate = [this](const EntitySnapshot& src, EntitySnapshot& outCopy) -> bool
				{
					constexpr double kDupOffsetM = 1.5;
					outCopy = src;
					if (src.kind == EntityKind::LayoutInstance && m_worldEditorSession)
					{
						auto& insts = m_worldEditorSession->MutableDoc().layoutInstances;
						// Préfixe "we_dup_" distinct du "we_inst_" de la
						// session : aucun risque de collision de guid.
						static uint64_t s_dupSeq = 1u;
						char buf[48];
						std::snprintf(buf, sizeof(buf), "we_dup_%llu",
							static_cast<unsigned long long>(s_dupSeq++));
						outCopy.layout.guid = buf;
						outCopy.layout.worldX += kDupOffsetM;
						outCopy.layoutIndex = static_cast<uint32_t>(insts.size());
						insts.push_back(outCopy.layout);
						return true;
					}
					if (src.kind == EntityKind::MeshInsert && m_worldEditorShell)
					{
						auto& doc = m_worldEditorShell->MutableMeshInsertDocument();
						outCopy.meshInsert.guid = 0u; // Add() assigne un nouveau guid
						outCopy.meshInsert.worldPosition.x += static_cast<float>(kDupOffsetM);
						outCopy.meshInsert.guid = doc.Add(outCopy.meshInsert);
						return true;
					}
					if (src.kind == EntityKind::DungeonPortal && m_worldEditorShell)
					{
						auto& doc = m_worldEditorShell->MutableDungeonPortalDocument();
						outCopy.portal.guid = 0u; // Add() assigne un nouveau guid
						outCopy.portal.worldPosition.x += static_cast<float>(kDupOffsetM);
						outCopy.portal.guid = doc.Add(outCopy.portal);
						return true;
					}
					return false;
				};

				m_worldEditorShell->SetEntityEditOps(std::move(ops));
			}
		}
		// Aperçu 3D live des bâtiments (brouillon + placements) dans la vue
		// éditeur. Reconstruit m_props seulement si l'aperçu est « sale ».
		SyncEditorBuildingPreview();
		m_worldEditorShell->RenderFrame();
		// Gizmo (axes/anneaux X=rouge/Y=vert/Z=bleu) sur la pièce active,
		// en overlay sur la 3D (sous les panneaux). Visuel seul pour l'instant.
		DrawEditorBuildingGizmo();
		// Roadmap-6 — gizmo des entités de scène sélectionnées (E/T/C) +
		// marqueurs de multi-sélection. No-op en mode édition bâtiment.
		DrawEditorSceneGizmo();
	}
#endif

	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::UpdateWorldEditorCamera(engine::RenderState& out, double dt, float mouseSensitivity, bool invertY, engine::render::MovementLayout movementLayout)
	{
		// Ne pas lier souris et clavier : un InputText actif mettait WantCaptureKeyboard à true et bloquait
		// aussi la souris (orientation), ce qui figeait la caméra dans l’éditeur.
		[[maybe_unused]] const bool capMouse = m_imguiOverlay->WantsCaptureMouse();
		const bool capKb = m_imguiOverlay->WantsCaptureKeyboard();
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

#if defined(_WIN32)
	// Sous-étape d'Engine::Update() — voir la documentation dans Engine.h.
	void Engine::UpdateWorldEditorViewportOverlay(engine::RenderState& out)
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
				const bool capBeforeUi = m_imguiOverlay->WantsCaptureMouse();
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

		// Sous-projet 1, bloc G — pipeline d'edition unifie : quand un outil
		// MODERNE du Shell est actif (Sculpt ou Stamp), on le pilote depuis
		// l'input viewport (il edite les CHUNKS -> sync 1:1 vers le GPU via
		// A2/A3 + undo/redo via CommandStack) et on NEUTRALISE le pinceau
		// legacy (garde `!modernEditActive` sur le bloc legacy plus bas). Sans
		// outil moderne d'edition actif, le legacy reste le chemin par defaut.
		// Ctrl+clic gauche reste reserve au picking B3 (donc exclu ici).
		// Mode « édition bâtiment » : quand actif (case à cocher du Building
		// Editor), le clic gauche dans la vue NE modifie PAS le terrain (ni
		// outils modernes ni brush legacy) — il est réservé à la
		// sélection/édition des pièces de bâtiment. Évite de sculpter le
		// terrain par réflexe en voulant choisir une pièce.
		const bool buildingEditMode =
			(m_worldEditorShell && m_worldEditorShell->GetBuildingEditorPanel())
				? m_worldEditorShell->GetBuildingEditorPanel()->EditModeActive()
				: false;

		bool modernEditActive = false;
		if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
		{
			const engine::editor::world::ActiveTool tool = m_worldEditorShell->GetActiveTool();
			const bool freeClick = !m_imguiOverlay->WantsCaptureMouse()
				&& !m_input.IsDown(engine::platform::Key::Control)
				&& !buildingEditMode;
			if (tool == engine::editor::world::ActiveTool::TerrainSculpt)
			{
				modernEditActive = true;
				if (freeClick)
				{
					engine::editor::world::TerrainSculptTool& sculpt =
						m_worldEditorShell->MutableSculptTool();
					const int mx = m_input.MouseX();
					const int my = m_input.MouseY();
					if (m_input.WasMousePressed(engine::platform::MouseButton::Left))
						sculpt.OnMouseDown(out.camera, mx, my, vw, vh, m_cfg);
					else if (m_input.IsMouseDown(engine::platform::MouseButton::Left))
						sculpt.OnMouseMove(out.camera, mx, my, vw, vh, m_cfg);
					if (m_input.WasMouseReleased(engine::platform::MouseButton::Left))
						sculpt.OnMouseUp();
				}
			}
			else if (tool == engine::editor::world::ActiveTool::TerrainStamp)
			{
				modernEditActive = true;
				// Stamp one-shot : clic gauche au point de sol cliqué (pickX,pickZ)
				// -> calcule la preview puis l'applique (push command -> sync +
				// persistance + undo). terrainPick garantit un point valide.
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					engine::editor::world::TerrainStampTool& stamp =
						m_worldEditorShell->MutableStampTool();
					stamp.OnClickAt(m_cfg, pickX, pickZ);
					stamp.Apply();
				}
			}
			else if (tool == engine::editor::world::ActiveTool::MountainRange)
			{
				modernEditActive = true;
				// Macro polyline : chaque clic gauche ajoute un sommet ; la
				// generation (rasterisation -> command -> sync validee) se fait
				// via le bouton "Apply" du ToolPropertiesPanel.
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					m_worldEditorShell->MutableMountainRangeTool().AddVertex(pickX, pickZ);
				}
			}
			else if (tool == engine::editor::world::ActiveTool::ValleyChain)
			{
				modernEditActive = true;
				// Jumeau soustractif de MountainRange (vallees). Meme entree :
				// clic = ajout de sommet, "Apply" (panneau) genere.
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					m_worldEditorShell->MutableValleyChainTool().AddVertex(pickX, pickZ);
				}
			}
			else if (tool == engine::editor::world::ActiveTool::Spline)
			{
				modernEditActive = true;
				// Roadmap-8 — spline (routes/chemins) : chaque clic ajoute un
				// noeud au trace en cours (largeur = defaut du panneau) ; le
				// bouton « Ajouter la spline » du ToolPropertiesPanel pousse
				// l'AddSplineCommand undoable.
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					engine::editor::world::SplineTool& spline =
						m_worldEditorShell->MutableSplineTool();
					const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
					spline.AddNode(engine::math::Vec3{ pickX, wy, pickZ },
						spline.DefaultWidthMeters());
				}
			}
			else if (tool == engine::editor::world::ActiveTool::GameplayZone)
			{
				modernEditActive = true;
				// Roadmap-8 — zone de gameplay : chaque clic ajoute un sommet
				// au polygone en cours ; « Ajouter la zone » (panneau) pousse
				// l'AddGameplayZoneCommand undoable.
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
					m_worldEditorShell->MutableZoneTool().AddPoint(
						engine::math::Vec3{ pickX, wy, pickZ });
				}
			}
			else if (tool == engine::editor::world::ActiveTool::Hazard)
			{
				modernEditActive = true;
				// Roadmap-8 — danger : pose one-shot au point clique, undoable
				// immediatement (AddHazardCommand aux parametres courants du
				// panneau — type, forme, profondeur...).
				if (freeClick && terrainPick
					&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
				{
					const float wy = m_terrainCollider.GroundHeightAt(pickX, pickZ);
					m_worldEditorShell->MutableCommandStack().Push(
						std::make_unique<engine::editor::world::AddHazardCommand>(
							m_worldEditorShell->MutableHazardDocument(),
							m_worldEditorShell->GetHazardTool().CreateAt(
								engine::math::Vec3{ pickX, wy, pickZ })));
				}
			}
		}

		// Gizmo étape 3.2 — cliquer-glisser : en mode « édition bâtiment », on
		// tente d'abord de saisir/glisser un handle du gizmo (axe = translation,
		// anneau = rotation) sur la pièce sélectionnée. S'il capture la souris,
		// on N'enchaîne PAS sur la sélection de pièce ci-dessous.
		bool gizmoGrabbed = false;
		if (buildingEditMode && !m_imguiOverlay->WantsCaptureMouse()
			&& !m_input.IsDown(engine::platform::Key::Control))
		{
			gizmoGrabbed = UpdateEditorBuildingGizmoDrag(m_input.MouseX(), m_input.MouseY());
		}

		// Gizmo étape 3.1 — picking de pièce de bâtiment : en mode « édition
		// bâtiment », un clic gauche dans la vue (hors ImGui, sans Ctrl)
		// sélectionne la pièce du brouillon dont la position monde (XZ) est la
		// plus proche du point de sol cliqué. Réutilise la transform du groupe
		// mémorisée au dernier rendu (m_editorPreview*), pour que le picking
		// corresponde à ce qui est affiché. Remplace la saisie de valeurs « à
		// l'aveugle » : on clique la pièce, le gizmo s'y replace.
		if (!gizmoGrabbed && buildingEditMode && m_editorPreviewValid && terrainPick
			&& m_worldEditorShell && m_worldEditorShell->GetBuildingEditorPanel()
			&& !m_imguiOverlay->WantsCaptureMouse()
			&& !m_input.IsDown(engine::platform::Key::Control)
			&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
		{
			engine::editor::world::panels::BuildingEditorPanel* bpanel =
				m_worldEditorShell->GetBuildingEditorPanel();
			const auto& parts = bpanel->DraftParts();
			constexpr float kDeg2Rad = 3.14159265f / 180.f;
			const float c = std::cos(m_editorPreviewYaw * kDeg2Rad);
			const float s = std::sin(m_editorPreviewYaw * kDeg2Rad);
			int best = -1;
			float bestDist2 = 36.0f; // (6 m)^2 : seuil de tolérance autour de la pièce
			for (size_t i = 0; i < parts.size(); ++i)
			{
				const float sx = m_editorPreviewScale * parts[i].localPosition.x;
				const float sz = m_editorPreviewScale * parts[i].localPosition.z;
				const float wx = m_editorPreviewOriginX + (c * sx + s * sz);
				const float wz = m_editorPreviewOriginZ + (-s * sx + c * sz);
				const float dx = wx - pickX;
				const float dz = wz - pickZ;
				const float d2 = dx * dx + dz * dz;
				if (d2 < bestDist2) { bestDist2 = d2; best = static_cast<int>(i); }
			}
			if (best >= 0)
			{
				bpanel->SetSelectedDraft(best);
				LOG_INFO(EditorWorld, "[Buildings][editeur] piece selectionnee par clic: #{} (dist={:.1f} m)",
					best, std::sqrt(bestDist2));
			}
		}

		// Roadmap-6 (2026-07-19) — gizmo de scène : cliquer-glisser sur les
		// handles (axes / anneau yaw / poignée d'échelle) de la sélection.
		// Hors mode bâtiment (le gizmo bâtiment garde la main) et sans Ctrl
		// (Ctrl = picking d'entité ci-dessous). S'il capture la souris, le
		// clic ne doit alimenter NI le picking NI les outils terrain.
		bool sceneGizmoGrabbed = false;
		if (!buildingEditMode && m_worldEditorShell && m_worldEditorShell->IsInitialized()
			&& !m_imguiOverlay->WantsCaptureMouse()
			&& !m_input.IsDown(engine::platform::Key::Control))
		{
			sceneGizmoGrabbed = UpdateEditorSceneGizmoDrag(m_input.MouseX(), m_input.MouseY());
		}

		// Sous-projet 1, bloc B3 — picking d'entite : Ctrl+clic gauche dans la
		// vue 3D (hors ImGui) selectionne l'entite dont la position (XZ) est la
		// plus proche du point de sol cliqué (seuil ~3 m), via EditorSceneModel
		// + EditorSelection. Geste Ctrl+clic distinct du clic d'edition
		// (sculpt/placement) pour ne pas interferer avec les deux systemes.
		// Roadmap-6 : Ctrl+Maj+clic BASCULE l'entité dans la multi-sélection ;
		// Ctrl+clic dans le vide vide la sélection.
		if (terrainPick && m_worldEditorShell && m_worldEditorShell->IsInitialized()
			&& !m_imguiOverlay->WantsCaptureMouse()
			&& m_input.IsDown(engine::platform::Key::Control)
			&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
		{
			const std::vector<engine::editor::scene::SceneEntity>& entities =
				m_worldEditorShell->GetSceneModel().Entities();
			const engine::editor::scene::SceneEntity* best = nullptr;
			float bestDist2 = 9.0f; // (3 m)^2
			for (const engine::editor::scene::SceneEntity& e : entities)
			{
				if (!e.hasTransform) continue;
				const float dx = e.transform.position.x - pickX;
				const float dz = e.transform.position.z - pickZ;
				const float d2 = dx * dx + dz * dz;
				if (d2 < bestDist2) { bestDist2 = d2; best = &e; }
			}
			const bool shiftHeld = m_input.IsDown(engine::platform::Key::Shift);
			if (best != nullptr)
			{
				if (shiftHeld)
					m_worldEditorShell->MutableSelection().Toggle(best->id);
				else
					m_worldEditorShell->MutableSelection().Select(best->id);
				LOG_INFO(EditorWorld, "[WorldEditor] Entite selectionnee (Ctrl+clic{}): {} ({} au total)",
					shiftHeld ? "+Maj" : "", best->label,
					m_worldEditorShell->GetSelection().Count());
			}
			else if (!shiftHeld)
			{
				m_worldEditorShell->MutableSelection().Clear();
			}
		}

		if (!modernEditActive && !buildingEditMode && !sceneGizmoGrabbed && m_terrain.IsValid() && m_worldEditorSession && terrainPick && m_vkDeviceContext.IsValid())
		{
			const bool cap = m_imguiOverlay->WantsCaptureMouse();
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
					// Roadmap-6 (2026-07-19) — placement/déplacement UNDOABLE :
					// le clic passe par des commandes sur la pile du shell
					// (adressage par guid stable). Fallback direct historique
					// si le shell n'est pas initialisé (pas de pile undo).
					if (m_worldEditorShell && m_worldEditorShell->IsInitialized())
					{
						// Foncteurs capturant [this] : l'Engine survit aux
						// commandes dans la pile undo (même pattern Lot 5).
						engine::editor::scene::LayoutPlacementOps ops;
						ops.add = [this](const engine::editor::WorldMapEditLayoutInstance& inst) -> bool
						{
							return m_worldEditorSession ? m_worldEditorSession->AddLayoutInstance(inst) : false;
						};
						ops.removeByGuid = [this](const std::string& guid) -> bool
						{
							return m_worldEditorSession ? m_worldEditorSession->RemoveLayoutInstanceByGuid(guid) : false;
						};
						ops.setPositionByGuid = [this](const std::string& guid, double x, double y, double z) -> bool
						{
							return m_worldEditorSession
								? m_worldEditorSession->SetLayoutInstancePositionByGuid(guid, x, y, z) : false;
						};
						const int selIdx = ws.SelectedLayoutInstanceIndex();
						if (selIdx >= 0 && selIdx < static_cast<int>(ws.Doc().layoutInstances.size()))
						{
							const engine::editor::WorldMapEditLayoutInstance& inst =
								ws.Doc().layoutInstances[static_cast<size_t>(selIdx)];
							m_worldEditorShell->MutableCommandStack().Push(
								std::make_unique<engine::editor::scene::MoveLayoutInstanceCommand>(
									inst.guid, inst.worldX, inst.worldY, inst.worldZ,
									static_cast<double>(pickX), static_cast<double>(wy), static_cast<double>(pickZ),
									ops));
							ws.Status() = "Instance deplacee.";
						}
						else
						{
							engine::editor::WorldMapEditLayoutInstance inst{};
							if (ws.BuildLayoutInstanceForPlacement(m_cfg, static_cast<double>(pickX),
									static_cast<double>(wy), static_cast<double>(pickZ), inst))
							{
								m_worldEditorShell->MutableCommandStack().Push(
									std::make_unique<engine::editor::scene::PlaceLayoutInstanceCommand>(
										std::move(inst), ops));
							}
						}
					}
					else
					{
						ws.PlaceOrMoveLayoutInstanceAtTerrainHit(m_cfg, static_cast<double>(pickX), static_cast<double>(wy),
							static_cast<double>(pickZ));
					}
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
#endif
} // namespace engine
