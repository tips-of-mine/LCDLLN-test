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
#include "src/world_editor/ui/WorldEditorSession.h"
#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/core/CompositeCommand.h"            // Roadmap-6 : undo groupé du gizmo
#include "src/world_editor/inspector/SetEntityTransformCommand.h" // Roadmap-6 : undo du drag de gizmo

#include <vulkan/vulkan.h>

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
} // namespace engine
