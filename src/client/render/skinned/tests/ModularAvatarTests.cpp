#include "src/client/render/skinned/ModularAvatar.h"
#include "src/client/render/skinned/SkinnedMesh.h"

#include <cstdio>
#include <string>
#include <vector>

using engine::render::skinned::EquipVisualSlot;
using engine::render::skinned::ModularAvatar;
using engine::render::skinned::SkinnedMesh;

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	/// Construit un SkinnedMesh CPU minimal : uniquement le squelette (noms d'os).
	/// Aucune ressource GPU (les handles restent VK_NULL_HANDLE, pas d'Upload).
	SkinnedMesh MakeMesh(const std::vector<std::string>& boneNames)
	{
		SkinnedMesh m;
		m.skeleton.bones.resize(boneNames.size());
		for (std::size_t i = 0; i < boneNames.size(); ++i)
		{
			m.skeleton.bones[i].name = boneNames[i];
			m.skeleton.bones[i].parentIndex = (i == 0) ? -1 : static_cast<int>(i - 1);
		}
		return m;
	}

	void Test_NoBody_EmptyAndNullSkeleton()
	{
		ModularAvatar a;
		REQUIRE(!a.HasBody());
		REQUIRE(a.ActiveParts().empty());
		REQUIRE(a.SharedSkeleton() == nullptr);
		// SetPart sans corps -> rejet (squelette incompatible car pas de ref).
		SkinnedMesh part = MakeMesh({"root", "head"});
		REQUIRE(!a.SetPart(EquipVisualSlot::Head, &part));
	}

	void Test_SetBody_ExposesBodyAndSkeleton()
	{
		SkinnedMesh body = MakeMesh({"root", "head", "hand"});
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(a.HasBody());
		REQUIRE(a.SharedSkeleton() == &body.skeleton);
		const auto parts = a.ActiveParts();
		REQUIRE(parts.size() == 1);
		REQUIRE(parts[0] == &body);
	}

	void Test_SetPart_CompatibleSkeleton_Accepted()
	{
		SkinnedMesh body = MakeMesh({"root", "head", "hand"});
		SkinnedMesh helmet = MakeMesh({"root", "head", "hand"}); // mêmes noms/ordre
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(a.SetPart(EquipVisualSlot::Head, &helmet));
		const auto parts = a.ActiveParts();
		REQUIRE(parts.size() == 2);
		REQUIRE(parts[0] == &body);   // corps d'abord
		REQUIRE(parts[1] == &helmet); // puis la partie
	}

	void Test_SetPart_DifferentBoneCount_Rejected()
	{
		SkinnedMesh body = MakeMesh({"root", "head", "hand"});
		SkinnedMesh bad = MakeMesh({"root", "head"}); // 2 os != 3
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(!a.SetPart(EquipVisualSlot::Head, &bad));
		REQUIRE(a.ActiveParts().size() == 1); // pas ajoutée
	}

	void Test_SetPart_DifferentBoneNames_Rejected()
	{
		SkinnedMesh body = MakeMesh({"root", "head", "hand"});
		SkinnedMesh bad = MakeMesh({"root", "HEAD", "hand"}); // nom différent
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(!a.SetPart(EquipVisualSlot::Head, &bad));
		REQUIRE(a.ActiveParts().size() == 1);
	}

	void Test_SetPart_BodySlot_Rejected()
	{
		SkinnedMesh body = MakeMesh({"root", "head"});
		SkinnedMesh other = MakeMesh({"root", "head"});
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(!a.SetPart(EquipVisualSlot::Body, &other));
	}

	void Test_ClearPart_RemovesPart()
	{
		SkinnedMesh body = MakeMesh({"root", "head"});
		SkinnedMesh helmet = MakeMesh({"root", "head"});
		ModularAvatar a;
		a.SetBody(&body);
		REQUIRE(a.SetPart(EquipVisualSlot::Head, &helmet));
		REQUIRE(a.ActiveParts().size() == 2);
		a.ClearPart(EquipVisualSlot::Head);
		REQUIRE(a.ActiveParts().size() == 1);
		REQUIRE(a.ActiveParts()[0] == &body);
	}

	void Test_ActiveParts_OrderedByEnum()
	{
		SkinnedMesh body = MakeMesh({"root", "head"});
		SkinnedMesh chest = MakeMesh({"root", "head"});
		SkinnedMesh head = MakeMesh({"root", "head"});
		ModularAvatar a;
		a.SetBody(&body);
		// Pose Chest avant Head : l'ordre de sortie doit suivre l'enum (Head < Chest).
		REQUIRE(a.SetPart(EquipVisualSlot::Chest, &chest));
		REQUIRE(a.SetPart(EquipVisualSlot::Head, &head));
		const auto parts = a.ActiveParts();
		REQUIRE(parts.size() == 3);
		REQUIRE(parts[0] == &body);
		REQUIRE(parts[1] == &head);  // Head (enum 1) avant
		REQUIRE(parts[2] == &chest); // Chest (enum 2)
	}
}

int main()
{
	Test_NoBody_EmptyAndNullSkeleton();
	Test_SetBody_ExposesBodyAndSkeleton();
	Test_SetPart_CompatibleSkeleton_Accepted();
	Test_SetPart_DifferentBoneCount_Rejected();
	Test_SetPart_DifferentBoneNames_Rejected();
	Test_SetPart_BodySlot_Rejected();
	Test_ClearPart_RemovesPart();
	Test_ActiveParts_OrderedByEnum();
	return g_failed == 0 ? 0 : 1;
}
