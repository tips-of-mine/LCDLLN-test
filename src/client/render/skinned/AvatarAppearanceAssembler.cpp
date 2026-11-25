#include "src/client/render/skinned/AvatarAppearanceAssembler.h"

#include "src/client/render/AssetRegistry.h"
#include "src/client/render/Material.h"
#include "src/client/render/skinned/SkinnedMeshLoader.h"
#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::render::skinned
{
	namespace
	{
		/// Vrai si \p part partage le squelette de \p body (même nombre d'os, mêmes
		/// noms dans le même ordre) — condition pour réutiliser ses matrices d'os.
		bool SameSkeleton(const SkinnedMesh& part, const SkinnedMesh& body)
		{
			const auto& a = part.skeleton.bones;
			const auto& b = body.skeleton.bones;
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
			{
				if (a[i].name != b[i].name)
					return false;
			}
			return true;
		}

		/// Vrai si le nom de maillage contient l'un des motifs à masquer.
		bool IsHidden(const std::string& meshName, const std::vector<std::string>& hidden)
		{
			for (const auto& h : hidden)
			{
				if (!h.empty() && meshName.find(h) != std::string::npos)
					return true;
			}
			return false;
		}
	}

	bool AvatarAppearanceAssembler::Init(VkDevice device, VkPhysicalDevice physicalDevice,
	                                     engine::render::AssetRegistry* assets,
	                                     engine::render::MaterialDescriptorCache* materials,
	                                     const engine::client::AppearanceCatalog* catalog,
	                                     const std::string& contentRoot,
	                                     uint32_t outfitMaterialId,
	                                     const std::string& skinNormalMale,
	                                     const std::string& skinNormalFemale)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || assets == nullptr
			|| materials == nullptr || catalog == nullptr || !materials->IsValid())
		{
			LOG_WARN(Render, "[AvatarAppearance] Init refusé (paramètres invalides) — rendu historique conservé");
			return false;
		}
		m_device = device;
		m_physicalDevice = physicalDevice;
		m_assets = assets;
		m_materials = materials;
		m_catalog = catalog;
		m_contentRoot = contentRoot;
		m_outfitMaterialId = outfitMaterialId;
		m_skinNormal[0] = skinNormalMale;
		m_skinNormal[1] = skinNormalFemale;
		WarmUpMaterials();
		LOG_INFO(Render, "[AvatarAppearance] Init OK (races={}, tenue={}, matériaux={})",
			catalog->Races().size(), outfitMaterialId, m_materialIds.size());
		return true;
	}

	void AvatarAppearanceAssembler::Shutdown(VkDevice device)
	{
		for (auto& kv : m_parts)
		{
			if (kv.second)
				kv.second->Destroy(device);
		}
		m_parts.clear();
		m_materialIds.clear();
		m_device = VK_NULL_HANDLE;
		m_catalog = nullptr;
	}

	const SkinnedMesh* AvatarAppearanceAssembler::GetPart(const std::string& relPath, const SkinnedMesh& body)
	{
		auto it = m_parts.find(relPath);
		if (it == m_parts.end())
		{
			std::unique_ptr<SkinnedMesh> mesh;
			auto loaded = SkinnedMeshLoader::Load(m_device, m_physicalDevice, m_contentRoot + "/" + relPath);
			if (loaded)
				mesh = std::make_unique<SkinnedMesh>(std::move(*loaded));
			else
				LOG_WARN(Render, "[AvatarAppearance] Pièce introuvable ou illisible : {}", relPath);
			it = m_parts.emplace(relPath, std::move(mesh)).first;
		}
		const SkinnedMesh* part = it->second.get();
		if (part == nullptr)
			return nullptr;
		if (!SameSkeleton(*part, body))
		{
			if (m_loggedIncompatible.insert(relPath).second)
				LOG_WARN(Render, "[AvatarAppearance] Pièce {} : squelette différent du corps ({} os vs {}) — ignorée",
					relPath, part->skeleton.bones.size(), body.skeleton.bones.size());
			return nullptr;
		}
		return part;
	}

	uint32_t AvatarAppearanceAssembler::HairFlags()
	{
		// Teinte de couleur + découpe alpha : l'atlas des coiffures sculptées porte le
		// contour fin de la chevelure dans son alpha (les textures sans alpha valent 1).
		return static_cast<uint32_t>(engine::render::MaterialFlags::Tint)
			| static_cast<uint32_t>(engine::render::MaterialFlags::AlphaCutout);
	}

	uint32_t AvatarAppearanceAssembler::SkinFlags(uint32_t tint)
	{
		return tint == 0xFFFFFFFFu ? 0u : static_cast<uint32_t>(engine::render::MaterialFlags::Tint);
	}

	void AvatarAppearanceAssembler::WarmUpMaterials()
	{
		using engine::network::AppearanceSlot;
		m_allowMaterialCreation = true;
		const uint32_t tintMasked = static_cast<uint32_t>(engine::render::MaterialFlags::TintMasked);
		const uint32_t hairFlags = HairFlags();
		for (const std::string& race : m_catalog->Races())
		{
			for (const char* gender : { "male", "female" })
			{
				const engine::client::AppearanceRaceGender* rg = m_catalog->Find(race, gender);
				if (rg == nullptr)
					continue;
				const bool female = std::string(gender) == "female";
				for (const auto& o : rg->Options(AppearanceSlot::SkinTone))
					(void)GetMaterial(o.texture, m_skinNormal[female ? 1 : 0], o.tintRgba8, SkinFlags(o.tintRgba8));
				for (const auto& o : rg->Options(AppearanceSlot::HairColor))
					(void)GetMaterial(o.texture, std::string(), o.tintRgba8, hairFlags);
				for (const auto& o : rg->Options(AppearanceSlot::EyeColor))
					(void)GetMaterial(o.texture, std::string(), o.tintRgba8, tintMasked);
				// Visages à atlas propre : peau du visage par teinte de peau, yeux par couleur.
				for (const auto& f : rg->Options(AppearanceSlot::Face))
				{
					if (f.texture.empty())
						continue;
					for (const auto& o : rg->Options(AppearanceSlot::SkinTone))
						(void)GetMaterial(f.texture, f.normal, o.tintRgba8, SkinFlags(o.tintRgba8));
					for (const auto& o : rg->Options(AppearanceSlot::EyeColor))
						(void)GetMaterial(f.texture, std::string(), o.tintRgba8, tintMasked);
				}
			}
		}
		(void)GetMaterial(m_catalog->HornTexture(), std::string(), 0xFFFFFFFFu, 0u);
		(void)GetMaterial(m_catalog->TuskTexture(), std::string(), 0xFFFFFFFFu, 0u);
		m_allowMaterialCreation = false;
	}

	uint32_t AvatarAppearanceAssembler::GetMaterial(const std::string& baseColor, const std::string& normal,
	                                                uint32_t tint, uint32_t flags)
	{
		if (baseColor.empty())
			return m_outfitMaterialId;
		const std::string key = baseColor + "|" + normal + "|" + std::to_string(tint) + "|" + std::to_string(flags);
		const auto it = m_materialIds.find(key);
		if (it != m_materialIds.end())
			return it->second;
		if (!m_allowMaterialCreation)
			return m_outfitMaterialId; // jamais pendant l'enregistrement d'une image
		engine::render::Material mat{};
		mat.baseColor = m_assets->LoadTexture(baseColor, /*useSrgb*/ true);
		if (!normal.empty())
		{
			const engine::render::TextureHandle n = m_assets->LoadTexture(normal, /*useSrgb*/ false);
			if (n.IsValid())
				mat.normal = n;
		}
		mat.flags = static_cast<engine::render::MaterialFlags>(flags);
		mat.tintRgba8 = tint;
		uint32_t id = 0;
		if (mat.baseColor.IsValid())
			id = m_materials->CreateMaterial(m_device, mat);
		if (id == 0u)
		{
			LOG_WARN(Render, "[AvatarAppearance] Matériau non créé ({}), repli sur la tenue", baseColor);
			id = m_outfitMaterialId;
		}
		m_materialIds.emplace(key, id);
		return id;
	}

	uint32_t AvatarAppearanceAssembler::RouteMaterial(const std::string& materialName, const RoutedMaterials& m) const
	{
		const uint32_t skin = m.skin, hair = m.hair, eye = m.eye, horn = m.horn, tusk = m.tusk;
		if (materialName == m_catalog->FaceMaterialName())
			return m.face;
		if (materialName == m_catalog->FaceEyeMaterialName())
			return m.faceEye;
		const auto& skins = m_catalog->SkinMaterialNames();
		if (std::find(skins.begin(), skins.end(), materialName) != skins.end())
			return skin;
		if (materialName == m_catalog->HairMaterialName())
			return hair;
		if (materialName == m_catalog->EyeMaterialName())
			return eye;
		if (materialName == m_catalog->HornMaterialName())
			return horn;
		if (materialName == m_catalog->TuskMaterialName())
			return tusk;
		return m_outfitMaterialId;
	}

	bool AvatarAppearanceAssembler::Assemble(const SkinnedMesh& body, const std::string& raceId,
	                                         const std::string& gender,
	                                         const engine::network::CharacterAppearance& a, int lod,
	                                         AssembledAvatar& out)
	{
		out = AssembledAvatar{};
		if (!IsReady())
			return false;
		const engine::client::ResolvedAppearance r = m_catalog->Resolve(raceId, gender, a, lod);
		if (r.body.empty())
			return false;
		const bool female = (gender == "female");
		const uint32_t tintMasked = static_cast<uint32_t>(engine::render::MaterialFlags::TintMasked);
		const uint32_t skin = GetMaterial(r.skinTexture, m_skinNormal[female ? 1 : 0], r.skinTint, SkinFlags(r.skinTint));
		const uint32_t hair = GetMaterial(r.hairTexture, std::string(), r.hairTint, HairFlags());
		const uint32_t eye = GetMaterial(r.eyeTexture, std::string(), r.eyeTint, tintMasked);
		const uint32_t horn = GetMaterial(m_catalog->HornTexture(), std::string(), 0xFFFFFFFFu, 0u);
		const uint32_t tusk = GetMaterial(m_catalog->TuskTexture(), std::string(), 0xFFFFFFFFu, 0u);
		RoutedMaterials routed{ skin, hair, eye, horn, tusk, skin, eye };
		if (!r.faceTexture.empty())
		{
			routed.face = GetMaterial(r.faceTexture, r.faceNormal, r.skinTint, SkinFlags(r.skinTint));
			routed.faceEye = GetMaterial(r.faceTexture, std::string(), r.eyeTint, tintMasked);
		}

		// Corps : peau / tenue par sous-maillage ; la capuche est masquée (la tête
		// personnalisée la remplace).
		SkinnedDrawPart bodyPart;
		bodyPart.mesh = &body;
		bodyPart.materialIndex = m_outfitMaterialId;
		bodyPart.submeshMaterialIndices.reserve(body.submeshes.size());
		for (const auto& sub : body.submeshes)
		{
			if (IsHidden(sub.meshName, r.hiddenBodyMeshes))
				bodyPart.submeshMaterialIndices.push_back(SkinnedRenderer::kSkipSubmesh);
			else
				bodyPart.submeshMaterialIndices.push_back(RouteMaterial(sub.materialName, routed));
		}
		out.parts.push_back(std::move(bodyPart));

		for (const std::string& path : r.parts)
		{
			const SkinnedMesh* mesh = GetPart(path, body);
			if (mesh == nullptr)
				continue;
			SkinnedDrawPart p;
			p.mesh = mesh;
			p.materialIndex = skin;
			p.submeshMaterialIndices.reserve(mesh->submeshes.size());
			for (const auto& sub : mesh->submeshes)
				p.submeshMaterialIndices.push_back(RouteMaterial(sub.materialName, routed));
			out.parts.push_back(std::move(p));
		}
		out.skinMaterialId = skin;
		out.heightScale = r.heightScale;
		out.widthScale = r.widthScale;
		return true;
	}
}
