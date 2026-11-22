// Atelier décors — tests des enseignes de service (fonctions pures).

#include "src/client/world/ServiceSigns.h"

#include <cmath>
#include <iostream>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	using namespace engine::client;
	Check(ServiceKeyFromMeshPath("meshes/props/Sign_Service_forgeron.gltf") == "forgeron", "cle depuis le chemin");
	Check(ServiceKeyFromMeshPath("meshes\\props\\Sign_Service_ecole_magie.gltf") == "ecole_magie", "separateur Windows, souligne");
	Check(ServiceKeyFromMeshPath("meshes/props/Sign_Service_auberge_LOD1.gltf") == "auberge", "niveau de detail ignore");
	Check(ServiceKeyFromMeshPath("meshes/props/Sign_Wood_1.gltf").empty(), "autre panneau : pas une enseigne");
	Check(ServiceKeyFromMeshPath("meshes/props/Barrel.gltf").empty(), "autre mesh");
	Check(ServiceKeyFromMeshPath("meshes/props/Sign_Service_.gltf").empty(), "cle vide");
	Check(ServiceNameKey("marche") == "service.marche", "cle de catalogue");

	const float fov = 1.0472f; // 60°
	const float nearR = ServiceSignPickRadiusPx(2.0f, 1080.0f, fov);
	const float farR = ServiceSignPickRadiusPx(30.0f, 1080.0f, fov);
	Check(nearR <= 120.0f && nearR > farR, "rayon plus grand de pres, borne haute");
	Check(std::fabs(ServiceSignPickRadiusPx(500.0f, 1080.0f, fov) - 14.0f) < 0.01f, "rayon minimal de loin");

	std::vector<ServiceSignScreen> signs = { { 100.0f, 100.0f, 20.0f, 12.0f }, { 105.0f, 100.0f, 20.0f, 6.0f }, { 400.0f, 300.0f, 20.0f, 3.0f } };
	Check(PickServiceSign(signs, 102.0f, 98.0f) == 1, "recouvrement : le plus proche de la camera");
	Check(PickServiceSign(signs, 400.0f, 315.0f) == 2, "dans le rayon");
	Check(PickServiceSign(signs, 250.0f, 250.0f) == -1, "rien sous le curseur");
	Check(PickServiceSign({}, 0.0f, 0.0f) == -1, "liste vide");

	if (s_failCount == 0) std::cout << "[service_signs_tests] OK" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
