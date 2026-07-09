#include "src/client/render/skinned/ModularAvatar.h"

#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/shared/core/Log.h"

namespace engine::render::skinned
{
	void ModularAvatar::SetBody(const SkinnedMesh* body)
	{
		m_body = body;
	}

	bool ModularAvatar::SkeletonCompatible(const SkinnedMesh* mesh) const
	{
		if (m_body == nullptr || mesh == nullptr)
			return false;
		const std::vector<Bone>& a = m_body->skeleton.bones;
		const std::vector<Bone>& b = mesh->skeleton.bones;
		if (a.size() != b.size())
			return false;
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].name != b[i].name)
				return false;
		}
		return true;
	}

	bool ModularAvatar::SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh)
	{
		const std::uint32_t idx = static_cast<std::uint32_t>(slot);
		if (slot == EquipVisualSlot::Body || idx >= static_cast<std::uint32_t>(EquipVisualSlot::Count))
			return false;
		if (mesh != nullptr && !SkeletonCompatible(mesh))
		{
			LOG_WARN(Render,
				"[ModularAvatar] partie rejetee : squelette incompatible (slot={})", idx);
			return false;
		}
		m_parts[idx] = mesh;
		return true;
	}

	void ModularAvatar::ClearPart(EquipVisualSlot slot)
	{
		const std::uint32_t idx = static_cast<std::uint32_t>(slot);
		if (slot != EquipVisualSlot::Body && idx < static_cast<std::uint32_t>(EquipVisualSlot::Count))
			m_parts[idx] = nullptr;
	}

	std::vector<const SkinnedMesh*> ModularAvatar::ActiveParts() const
	{
		std::vector<const SkinnedMesh*> out;
		if (m_body != nullptr)
			out.push_back(m_body);
		for (std::size_t i = 0; i < static_cast<std::size_t>(EquipVisualSlot::Count); ++i)
		{
			if (m_parts[i] != nullptr)
				out.push_back(m_parts[i]);
		}
		return out;
	}

	const Skeleton* ModularAvatar::SharedSkeleton() const
	{
		return m_body != nullptr ? &m_body->skeleton : nullptr;
	}
}
