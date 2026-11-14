// dynamic_light_system_tests — v1.0.118 : lumières des cellules du monde (torches du
// gouffre, zone.json `world.point_lights`) dans DynamicLightSystem, et choix des 64
// lumières les plus proches pour l'UBO de la passe Lighting.
#include "src/client/render/DynamicLightSystem.h"

#include "src/shared/core/Config.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	/// Torche de test en (x, 0, z), allumée en permanence ou seulement la nuit.
	engine::render::PointLightDefinition Torche(const char* id, float x, float z, bool nuitSeulement)
	{
		engine::render::PointLightDefinition d{};
		d.id = id;
		d.type = engine::render::DynamicLightType::Torch;
		d.position[0] = x; d.position[1] = 0.0f; d.position[2] = z;
		d.radius = 9.0f;
		d.baseIntensity = 3.0f;
		d.autoNightLight = nuitSeulement;
		return d;
	}
}

int main()
{
	using engine::render::ActivePointLight;
	using engine::render::DynamicLightSystem;

	engine::core::Config cfg;
	cfg.SetValue("paths.content", std::string("dossier_absent_pour_le_test"));
	DynamicLightSystem sys;
	Check(sys.Init(cfg) && sys.IsInitialized() && sys.GetLightCount() == 0, "init sans fichier global : 0 lumiere");

	sys.AddZoneLights(0, { Torche("ignoree", 0, 0, false) });
	Check(sys.GetLightCount() == 0, "zoneId 0 refuse (reserve au fichier global)");

	sys.AddZoneLights(7, { Torche("t1", 10, 20, false), Torche("t2", 30, 40, true) });
	Check(sys.GetLightCount() == 2 && sys.GetZoneLightCount() == 2, "deux lumieres de cellule ajoutees");

	// Midi : la torche permanente est déjà allumée à pleine intensité, celle de nuit reste éteinte.
	sys.Tick(12.0f, 0.016f);
	Check(sys.GetActiveLights().size() == 1 && std::fabs(sys.GetActiveLights()[0].intensity - 3.0f) < 1e-4f,
		"torche permanente allumee des l'arrivee, torche de nuit eteinte a midi");

	sys.TranslateZoneLights(5.0f, -5.0f);
	sys.Tick(12.0f, 0.016f);
	Check(std::fabs(sys.GetActiveLights()[0].position[0] - 15.0f) < 1e-4f
		&& std::fabs(sys.GetActiveLights()[0].position[2] - 15.0f) < 1e-4f, "rebase : lumieres de cellule decalees");

	sys.ClearZoneLights();
	Check(sys.GetLightCount() == 0 && sys.GetZoneLightCount() == 0, "changement de carte : lumieres de cellule retirees");

	// 70 lumières en ligne : on garde les 64 plus proches de l'origine, dans l'ordre d'entrée.
	std::vector<ActivePointLight> v(70);
	for (size_t i = 0; i < v.size(); ++i)
	{
		v[i].position[0] = static_cast<float>(69 - i) * 10.0f; // la dernière est la plus proche
		v[i].radius = 1.0f;
	}
	const std::vector<ActivePointLight> sel = DynamicLightSystem::SelectNearest(v, 0, 0, 0, 64);
	bool ordre = sel.size() == 64;
	for (size_t i = 0; ordre && i < sel.size(); ++i)
		ordre = std::fabs(sel[i].position[0] - static_cast<float>(63 - i) * 10.0f) < 1e-4f;
	Check(ordre, "64 lumieres les plus proches gardees, ordre d'entree conserve");
	Check(DynamicLightSystem::SelectNearest(std::vector<ActivePointLight>(3), 0, 0, 0, 64).size() == 3,
		"moins de 64 lumieres : liste inchangee");

	sys.Shutdown();
	if (g_fail == 0) std::printf("dynamic_light_system_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
