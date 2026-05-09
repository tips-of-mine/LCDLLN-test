/// Tests unitaires CPU pour `EditorCameraController` (M100.4).
///
/// Couvre les cinq cas listés dans la spec M100.4 §"Tests" :
///   - `Test_SetMode_PreservesFocusPoint` : SetMode ne réinitialise pas
///     le `m_focusPoint` (préservation du POI lors d'un Numpad 1/3/7).
///   - `Test_BuildCamera_FPS_Position` : en mode FPS la caméra démarre
///     à (0, 5, 10) — la composante Y est l'invariant le plus stable
///     (les autres dépendent de yaw/pitch initiaux).
///   - `Test_BuildCamera_Orbital_OrientedTowardFocus` : en mode Orbital
///     `BuildCamera` remplit `lookAt` avec le focusPoint courant.
///   - `Test_BuildCamera_TopDown_OrthoExtent` : en mode TopDownOrtho
///     `cam.ortho == true` et `cam.orthoExtent == m_topDownExtent`.
///   - `Test_FocusOn_RecentersCamera` : `FocusOn` met à jour
///     `GetFocusPoint`.
///
/// Pas de dépendance ImGui ni Vulkan : le contrôleur est pure CPU. Le
/// gating WIN32 dans le CMake reste cohérent avec les autres tests éditeur
/// monde (M100.1, M100.2) — pas une nécessité technique mais un choix
/// de cohérence.

#include "src/world_editor/EditorCameraController.h"

#include <cmath>
#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	/// Tolérance pour les comparaisons float dans les tests caméra.
	/// Les calculs internes sont en `float` (yaw/pitch/distance), donc
	/// 1e-4 est largement suffisant pour distinguer les égalités exactes
	/// (initialisations, copies) des dérives accumulées (cumulatif).
	constexpr float kEps = 1e-4f;

	/// Compare deux floats avec tolérance kEps.
	bool ApproxEq(float a, float b, float eps = kEps)
	{
		return std::fabs(a - b) <= eps;
	}

	using engine::editor::world::EditorCameraController;
	using engine::editor::world::EditorCameraMode;

	/// Test_SetMode_PreservesFocusPoint : `SetMode(Orbital)` après
	/// `FocusOn({1,2,3})` ne touche pas à `m_focusPoint`.
	void Test_SetMode_PreservesFocusPoint()
	{
		EditorCameraController c;
		c.FocusOn(engine::math::Vec3{ 1.0f, 2.0f, 3.0f });
		const auto fp1 = c.GetFocusPoint();
		c.SetMode(EditorCameraMode::Orbital);
		REQUIRE(ApproxEq(c.GetFocusPoint().x, fp1.x));
		REQUIRE(ApproxEq(c.GetFocusPoint().y, fp1.y));
		REQUIRE(ApproxEq(c.GetFocusPoint().z, fp1.z));
		REQUIRE(ApproxEq(c.GetFocusPoint().x, 1.0f));
		REQUIRE(ApproxEq(c.GetFocusPoint().y, 2.0f));
		REQUIRE(ApproxEq(c.GetFocusPoint().z, 3.0f));
	}

	/// Test_BuildCamera_FPS_Position : en mode FPS la position initiale
	/// est (0, 5, 10) — on vérifie au minimum la composante Y (les autres
	/// dépendent du repère retenu par BuildCamera et sont moins stables).
	void Test_BuildCamera_FPS_Position()
	{
		EditorCameraController c;
		c.SetMode(EditorCameraMode::FPS);
		const auto cam = c.BuildCamera(1920, 1080);
		REQUIRE(ApproxEq(cam.position.y, 5.0f));
		// Aspect ratio 16/9 attendu pour 1920x1080.
		REQUIRE(ApproxEq(cam.aspect, 1920.0f / 1080.0f));
		REQUIRE(cam.ortho == false);
	}

	/// Test_BuildCamera_Orbital_OrientedTowardFocus : en mode Orbital,
	/// après `FocusOn({0,0,0})`, `BuildCamera` produit un `lookAt`
	/// strictement égal à l'origine.
	void Test_BuildCamera_Orbital_OrientedTowardFocus()
	{
		EditorCameraController c;
		c.SetMode(EditorCameraMode::Orbital);
		c.FocusOn(engine::math::Vec3{ 0.0f, 0.0f, 0.0f });
		const auto cam = c.BuildCamera(1920, 1080);
		REQUIRE(ApproxEq(cam.lookAt.x, 0.0f));
		REQUIRE(ApproxEq(cam.lookAt.y, 0.0f));
		REQUIRE(ApproxEq(cam.lookAt.z, 0.0f));
		REQUIRE(cam.ortho == false);
	}

	/// Test_BuildCamera_TopDown_OrthoExtent : en mode TopDownOrtho,
	/// `cam.ortho == true` et `cam.orthoExtent == m_topDownExtent`
	/// (50.0 par défaut, cf. EditorCameraController.h).
	void Test_BuildCamera_TopDown_OrthoExtent()
	{
		EditorCameraController c;
		c.SetMode(EditorCameraMode::TopDownOrtho);
		const auto cam = c.BuildCamera(1920, 1080);
		REQUIRE(cam.ortho == true);
		REQUIRE(ApproxEq(cam.orthoExtent, 50.0f));
	}

	/// Test_FocusOn_RecentersCamera : `FocusOn({10, 5, -3})` met à jour
	/// `GetFocusPoint` strictement.
	void Test_FocusOn_RecentersCamera()
	{
		EditorCameraController c;
		c.FocusOn(engine::math::Vec3{ 10.0f, 5.0f, -3.0f });
		const auto fp = c.GetFocusPoint();
		REQUIRE(ApproxEq(fp.x, 10.0f));
		REQUIRE(ApproxEq(fp.y, 5.0f));
		REQUIRE(ApproxEq(fp.z, -3.0f));
	}
}

int main()
{
	Test_SetMode_PreservesFocusPoint();
	Test_BuildCamera_FPS_Position();
	Test_BuildCamera_Orbital_OrientedTowardFocus();
	Test_BuildCamera_TopDown_OrthoExtent();
	Test_FocusOn_RecentersCamera();

	if (g_failed == 0)
	{
		std::printf("[PASS] EditorCameraControllerTests (5/5)\n");
		return 0;
	}
	std::printf("[FAIL] EditorCameraControllerTests: %d failure(s)\n", g_failed);
	return 1;
}
