// Engine — monde ouvert (chantier « Royaumes ») : décor et bâtiments INSTANCIÉS.
//
// Le rendu historique des props (BuildPropFromMesh / BuildPropFromMeshMatrix)
// cuit chaque instance dans ses propres buffers GPU et ouvre une passe par prop :
// viable pour quelques centaines d'objets, pas pour un continent (≈130 000
// éléments de décor + des dizaines de milliers de pièces de bâtiments). Ici :
//   - UN mesh GPU par chemin de mesh (partagé par toutes ses instances) ;
//   - des matrices d'instance écrites chaque frame dans un anneau de buffers
//     HOST_VISIBLE (un slot par frame en vol) ;
//   - UN GeometryPass::RecordInstanced par frame (une passe, un draw par lot
//     mesh × matériau × niveau de fondu).
// Les instances sont rangées dans engine::world::scatter::ScatterField (grille
// XZ) : seules les cases proches sont visitées, puis filtrage distance + frustum.

#include "src/client/app/Engine.h"

#include "src/client/render/DeferredPipeline.h"
#include "src/client/render/InstanceBatch.h"
#include "src/client/render/static_mesh/StaticMeshLoader.h"
#include "src/client/world/instances/ScatterInstances.h"
#include "src/client/world/scatter/PropMaterialRules.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine
{
	bool Engine::EnsureScatterMesh(uint32_t meshId)
	{
		if (meshId >= m_scatterMeshes.size())
			m_scatterMeshes.resize(static_cast<size_t>(meshId) + 1u);
		ScatterMeshGpu& entry = m_scatterMeshes[meshId];
		if (entry.attempted) return entry.valid;
		entry.attempted = true;
		if (!m_pipeline) return false;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();
		if (!materialCache.IsValid()) return false;

		const std::string& meshPath = m_scatterField.MeshPaths()[meshId];
		const std::string contentRoot = m_cfg.GetString("paths.content", "game/data");
		// Monde « Royaumes » : un mesh nouveau peut arriver en cours de partie ; ses
		// matériaux réécrivent le set bindless partagé, que la frame précédente ne doit
		// plus lire. Rare (une fois par modèle) : attente du GPU.
		if (m_cellWorld && m_vkDeviceContext.IsValid())
			vkDeviceWaitIdle(m_vkDeviceContext.GetDevice());
		auto cpu = engine::render::staticmesh::StaticMeshLoader::LoadCpuOnlyForTests(contentRoot + "/" + meshPath);
		if (!cpu || cpu->vertices.empty() || cpu->submeshes.empty())
		{
			LOG_WARN(Render, "[Scatter] mesh introuvable ou vide '{}' (instances ignorees)", meshPath);
			return false;
		}
		// AABB locale (bornes de culling + empreinte de collision).
		entry.localMin = { 1e30f, 1e30f, 1e30f };
		entry.localMax = { -1e30f, -1e30f, -1e30f };
		for (const auto& v : cpu->vertices)
		{
			entry.localMin = { std::min(entry.localMin.x, v.pos[0]), std::min(entry.localMin.y, v.pos[1]), std::min(entry.localMin.z, v.pos[2]) };
			entry.localMax = { std::max(entry.localMax.x, v.pos[0]), std::max(entry.localMax.y, v.pos[1]), std::max(entry.localMax.z, v.pos[2]) };
		}
		std::string meshDir;
		{
			const auto slash = meshPath.find_last_of('/');
			if (slash != std::string::npos) meshDir = meshPath.substr(0, slash + 1);
		}
		// Regroupe les indices par matériau (une partie GPU par matériau), ordre
		// d'apparition des sous-maillages (déterministe).
		std::vector<std::string> matOrder;
		std::unordered_map<std::string, std::vector<uint32_t>> idxByMat;
		std::unordered_map<std::string, const engine::render::staticmesh::StaticSubMesh*> repByMat;
		for (const auto& sub : cpu->submeshes)
		{
			auto [it, inserted] = idxByMat.try_emplace(sub.materialName);
			if (inserted) { matOrder.push_back(sub.materialName); repByMat[sub.materialName] = &sub; }
			it->second.insert(it->second.end(), cpu->indices.begin() + sub.firstIndex,
				cpu->indices.begin() + sub.firstIndex + sub.indexCount);
		}

		// Politique de matériaux (cf. PropMaterialRules.h) : textures glTF si
		// présentes ; sinon textures du kit pour les matériaux « MI_* » ; sinon
		// couleur de sommet (décor nature). Cache par (matériau, texture).
		auto resolveMaterial = [&](const std::string& matName,
			const engine::render::staticmesh::StaticSubMesh* rep) -> uint32_t
		{
			const std::string key = matName + "|" + (rep ? rep->baseColorUri : std::string()) + "|" + meshDir;
			if (auto c = m_scatterMatCache.find(key); c != m_scatterMatCache.end()) return c->second;
			uint32_t idx = materialCache.GetDefaultMaterialIndex();
			engine::render::Material mat{};
			if (rep && !rep->baseColorUri.empty())
			{
				const engine::render::TextureHandle bc = m_assetRegistry.LoadTexture(meshDir + rep->baseColorUri, /*useSrgb*/ true);
				if (bc.IsValid())
				{
					mat.baseColor = bc;
					if (!rep->normalUri.empty())
						if (auto n = m_assetRegistry.LoadTexture(meshDir + rep->normalUri, false); n.IsValid()) mat.normal = n;
					if (!rep->ormUri.empty())
						if (auto o = m_assetRegistry.LoadTexture(meshDir + rep->ormUri, false); o.IsValid()) mat.orm = o;
					if (rep->alphaCutout) mat.flags = engine::render::MaterialFlags::AlphaCutout;
					idx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
				}
			}
			else if (engine::world::scatter::UsesKitTextures(matName))
			{
				engine::world::scatter::TrimTextureSet trim;
				engine::render::TextureHandle bc;
				if (engine::world::scatter::ResolveTrimTextures(matName, meshPath, trim))
					bc = m_assetRegistry.LoadTexture(meshDir + trim.baseColor, /*useSrgb*/ true);
				if (bc.IsValid())
				{
					mat.baseColor = bc;
					if (trim.normal) if (auto n = m_assetRegistry.LoadTexture(meshDir + trim.normal, false); n.IsValid()) mat.normal = n;
					if (trim.orm) if (auto o = m_assetRegistry.LoadTexture(meshDir + trim.orm, false); o.IsValid()) mat.orm = o;
				}
				else
				{
					uint8_t r = 200, g = 195, b = 185;
					engine::world::scatter::FallbackFlatColor(matName, meshPath, r, g, b);
					mat.baseColor = SolidColorTexture(r, g, b);
				}
				idx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
			}
			else
			{
				mat.flags = engine::render::MaterialFlags::VertexColorAlbedo;
				idx = materialCache.CreateMaterial(m_vkDeviceContext.GetDevice(), mat);
			}
			m_scatterMatCache.emplace(key, idx);
			return idx;
		};

		for (const std::string& matName : matOrder)
		{
			const std::vector<uint32_t>& idxs = idxByMat[matName];
			if (idxs.empty()) continue;
			engine::render::MeshHandle mh = m_assetRegistry.CreateMeshFromData(
				cpu->vertices.data(), static_cast<uint32_t>(cpu->vertices.size()),
				idxs.data(), static_cast<uint32_t>(idxs.size()));
			if (!mh.IsValid()) continue;
			PropPart part;
			part.mesh = mh;
			part.materialIndex = resolveMaterial(matName, repByMat[matName]);
			part.highlightMaterialIndex = part.materialIndex;
			entry.parts.push_back(part);
		}
		entry.valid = !entry.parts.empty();
		return entry.valid;
	}

	void Engine::AddScatterInstance(uint32_t meshId, const engine::math::Mat4& model, uint8_t cullClass,
		uint8_t flags, bool solid, bool buildingPiece, float collisionRadius, const InstanceTarget& target)
	{
		if (meshId >= m_scatterMeshes.size() || !m_scatterMeshes[meshId].valid) return;
		const ScatterMeshGpu& mesh = m_scatterMeshes[meshId];
		engine::world::scatter::ScatterItem item;
		item.model = model;
		item.meshId = meshId;
		item.cullClass = cullClass;
		item.flags = flags;
		engine::math::Vec3 wMin{}, wMax{};
		engine::world::scatter::TransformBounds(model, mesh.localMin, mesh.localMax, item.center, item.radius, wMin, wMax);
		const uint32_t index = target.field->Add(item);
		(void)index;
		if (!solid) return;
		const float cx = model.m[12], cz = model.m[14];
		if (buildingPiece)
		{
			// Rayon auto : demi-diagonale XZ de l'AABB monde (borne supérieure de
			// l'empreinte ; le catalogue de pièces prend le relais pour les murs).
			const float ex = 0.5f * (wMax.x - wMin.x), ez = 0.5f * (wMax.z - wMin.z);
			RegisterBuildingPieceCollision(m_scatterField.MeshPaths()[meshId], model,
				0.5f * (wMin.x + wMax.x), 0.5f * (wMin.z + wMax.z), std::sqrt(ex * ex + ez * ez), wMin.y, wMax.y, collisionRadius,
				*target.collider);
			return;
		}
		// Décor (tronc, rocher, mobilier) : cylindre autour du pivot, dessus
		// marchable (comportement historique de LoadScenery / BuildPropFromMesh).
		float radius = collisionRadius;
		if (radius <= 0.0f)
		{
			const float ex = std::max(std::fabs(wMax.x - cx), std::fabs(wMin.x - cx));
			const float ez = std::max(std::fabs(wMax.z - cz), std::fabs(wMin.z - cz));
			radius = std::max(0.3f, std::min(ex, ez));
		}
		std::string lower = m_scatterField.MeshPaths()[meshId];
		for (char& ch : lower) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
		engine::gameplay::PropCylinder cyl{ cx, cz, radius, wMin.y, wMax.y };
		cyl.passable = lower.find("door") != std::string::npos;
		cyl.stair = lower.find("stair") != std::string::npos || lower.find("escalier") != std::string::npos;
		target.collider->AddCylinder(cyl);
	}

	Engine::InstanceTarget Engine::LegacyInstanceTarget()
	{
		InstanceTarget t;
		t.field = &m_scatterField;
		t.collider = &m_worldCollider;
		t.ground = [this](float x, float z) { return m_terrainCollider.GroundHeightAt(x, z); };
		return t;
	}

	void Engine::LoadScatterWorld()
	{
		if (!m_pipeline) return;
		const std::string zone = m_cfg.GetString("world.active_zone", "");
		std::string rel = m_cfg.GetString("world.scatter_path", "");
		if (rel.empty() && !zone.empty()) rel = "instances/zone_" + zone + "/scatter.bin";
		if (rel.empty()) return;
		const size_t placed = LoadScatterInto(rel, LegacyInstanceTarget());
		LOG_INFO(Render, "[Scatter] '{}' : {} instance(s) posee(s), {} mesh(es) partage(s) ; collisions {} cylindre(s) / {} boite(s)",
			rel, placed, m_scatterField.MeshPaths().size(),
			m_worldCollider.CylinderCount(), m_worldCollider.BoxCount());
	}

	size_t Engine::LoadScatterInto(const std::string& rel, const InstanceTarget& target)
	{
		if (!m_pipeline || target.field == nullptr || target.collider == nullptr || !target.ground) return 0;
		const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(m_cfg, rel);
		if (bytes.empty())
		{
			LOG_DEBUG(Render, "[Scatter] aucun scatter.bin ('{}')", rel);
			return 0;
		}
		engine::world::instances::ScatterFile file;
		std::string err;
		if (!engine::world::instances::LoadScatterBin(bytes, file, err))
		{
			LOG_WARN(Render, "[Scatter] '{}' invalide : {}", rel, err);
			return 0;
		}
		return PlaceScatterFile(file, target, rel);
	}

	size_t Engine::PlaceScatterFile(const engine::world::instances::ScatterFile& file,
		const InstanceTarget& target, const std::string& label, size_t first, size_t count)
	{
		if (!m_pipeline || target.field == nullptr || target.collider == nullptr || !target.ground) return 0;
		const std::string& rel = label;
		// Les identifiants de mesh sont ceux de la table PARTAGÉE (m_scatterField) :
		// les champs des cellules du monde « Royaumes » s'y réfèrent aussi.
		std::vector<uint32_t> remap(file.meshPaths.size(), 0u);
		for (size_t i = 0; i < file.meshPaths.size(); ++i)
			remap[i] = m_scatterField.InternMesh(file.meshPaths[i]);

		using engine::world::instances::kScatterFlagBuildingPiece;
		using engine::world::instances::kScatterFlagSolid;
		size_t placed = 0, skipped = 0;
		const size_t end = (first >= file.instances.size()) ? first
			: first + std::min(count, file.instances.size() - first);
		for (size_t k = first; k < end; ++k)
		{
			const auto& in = file.instances[k];
			const uint32_t meshId = remap[in.meshIndex];
			if (!EnsureScatterMesh(meshId)) { ++skipped; continue; }
			const ScatterMeshGpu& mesh = m_scatterMeshes[meshId];
			// Position dans le repère de la cible (globale − origine de la cible).
			const float px = static_cast<float>(static_cast<double>(in.x) - target.originX);
			const float pz = static_cast<float>(static_cast<double>(in.z) - target.originZ);
			const float ground = target.ground(px, pz);
			const bool piece = (in.flags & kScatterFlagBuildingPiece) != 0u;
			float y = ground + in.yOffset;
			if (!piece)
			{
				// Ancrage au sol (comme BuildPropFromMesh) : le point le PLUS BAS du
				// mesh orienté repose sur le terrain, puis le décalage de la donnée
				// (enfoncement léger des troncs/rochers) s'applique.
				const engine::math::Mat4 probe = engine::world::scatter::ComposeInstanceMatrix(
					{ px, 0.0f, pz }, in.yawDeg, in.tiltDeg, in.scale);
				engine::math::Vec3 c{}, lo{}, hi{};
				float r = 0.0f;
				engine::world::scatter::TransformBounds(probe, mesh.localMin, mesh.localMax, c, r, lo, hi);
				y = ground - lo.y + in.yOffset;
			}
			const engine::math::Mat4 model = engine::world::scatter::ComposeInstanceMatrix(
				{ px, y, pz }, in.yawDeg, in.tiltDeg, in.scale);
			AddScatterInstance(meshId, model, in.cullClass, in.flags,
				(in.flags & kScatterFlagSolid) != 0u, piece, in.collisionRadius, target);
			++placed;
		}
		if (skipped > 0)
			LOG_INFO(Render, "[Scatter] '{}' : {} instance(s) ignoree(s) (mesh absent)", rel, skipped);
		return placed;
	}

	bool Engine::EnsureScatterInstanceBuffer(uint32_t slot, uint32_t capacity)
	{
		if (slot >= kScatterFrameSlots) return false;
		if (m_scatterInstBuf[slot] != VK_NULL_HANDLE && m_scatterInstCapacity[slot] >= capacity) return true;
		VkDevice device = m_vkDeviceContext.GetDevice();
		// Le slot n'a plus été utilisé depuis kScatterFrameSlots frames : sa
		// libération immédiate est sûre (au plus 2 frames en vol).
		if (m_scatterInstBuf[slot] != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, m_scatterInstBuf[slot], nullptr);
			vkFreeMemory(device, m_scatterInstMem[slot], nullptr);
			m_scatterInstBuf[slot] = VK_NULL_HANDLE;
			m_scatterInstMem[slot] = VK_NULL_HANDLE;
			m_scatterInstMapped[slot] = nullptr;
			m_scatterInstCapacity[slot] = 0;
		}
		uint32_t cap = 16384u;
		while (cap < capacity) cap *= 2u;
		VkBufferCreateInfo bi{};
		bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bi.size = static_cast<VkDeviceSize>(cap) * engine::render::kInstanceMatrixSizeBytes;
		bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bi, nullptr, &m_scatterInstBuf[slot]) != VK_SUCCESS)
		{
			m_scatterInstBuf[slot] = VK_NULL_HANDLE;
			LOG_ERROR(Render, "[Scatter] vkCreateBuffer (instances, {} mat4) ECHEC", cap);
			return false;
		}
		VkMemoryRequirements req{};
		vkGetBufferMemoryRequirements(device, m_scatterInstBuf[slot], &req);
		VkPhysicalDeviceMemoryProperties props{};
		vkGetPhysicalDeviceMemoryProperties(m_vkDeviceContext.GetPhysicalDevice(), &props);
		uint32_t type = UINT32_MAX;
		for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
		{
			const VkMemoryPropertyFlags f = props.memoryTypes[i].propertyFlags;
			if ((req.memoryTypeBits & (1u << i)) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			{
				type = i;
				break;
			}
		}
		VkMemoryAllocateInfo ai{};
		ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = type;
		if (type == UINT32_MAX || vkAllocateMemory(device, &ai, nullptr, &m_scatterInstMem[slot]) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, m_scatterInstBuf[slot], nullptr);
			m_scatterInstBuf[slot] = VK_NULL_HANDLE;
			m_scatterInstMem[slot] = VK_NULL_HANDLE;
			LOG_ERROR(Render, "[Scatter] allocation memoire HOST_VISIBLE des instances ECHEC");
			return false;
		}
		vkBindBufferMemory(device, m_scatterInstBuf[slot], m_scatterInstMem[slot], 0);
		if (vkMapMemory(device, m_scatterInstMem[slot], 0, VK_WHOLE_SIZE, 0, &m_scatterInstMapped[slot]) != VK_SUCCESS)
		{
			vkDestroyBuffer(device, m_scatterInstBuf[slot], nullptr);
			vkFreeMemory(device, m_scatterInstMem[slot], nullptr);
			m_scatterInstBuf[slot] = VK_NULL_HANDLE;
			m_scatterInstMem[slot] = VK_NULL_HANDLE;
			m_scatterInstMapped[slot] = nullptr;
			return false;
		}
		m_scatterInstCapacity[slot] = cap;
		return true;
	}

	void Engine::DestroyScatterResources()
	{
		if (!m_vkDeviceContext.IsValid()) return;
		VkDevice device = m_vkDeviceContext.GetDevice();
		for (uint32_t s = 0; s < kScatterFrameSlots; ++s)
		{
			if (m_scatterInstMapped[s] && m_scatterInstMem[s] != VK_NULL_HANDLE) vkUnmapMemory(device, m_scatterInstMem[s]);
			if (m_scatterInstBuf[s] != VK_NULL_HANDLE) vkDestroyBuffer(device, m_scatterInstBuf[s], nullptr);
			if (m_scatterInstMem[s] != VK_NULL_HANDLE) vkFreeMemory(device, m_scatterInstMem[s], nullptr);
			m_scatterInstBuf[s] = VK_NULL_HANDLE;
			m_scatterInstMem[s] = VK_NULL_HANDLE;
			m_scatterInstMapped[s] = nullptr;
			m_scatterInstCapacity[s] = 0;
		}
		// Les meshes GPU appartiennent à l'AssetRegistry (détruit ensuite).
		m_scatterMeshes.clear();
		m_scatterField.Clear();
		m_scatterMatCache.clear();
	}

	void Engine::GatherScatterLayers(std::vector<ScatterLayer>& out) const
	{
		out.clear();
		if (m_scatterField.Count() > 0)
			out.push_back({ &m_scatterField, 0.0f, 0.0f, 0u });
		// Monde « Royaumes » : un champ par cellule résidente, en coordonnées locales
		// à la cellule, décalé par son centre exprimé dans le repère de rendu.
		for (const CellRenderTile& tile : m_cellTiles)
		{
			if (!tile.scatter || tile.scatter->Count() == 0)
				continue;
			const engine::world::cells::CellPoint c = m_renderFrame.CellCenterRender(tile.cell);
			const std::uint32_t id = 1u + ((static_cast<std::uint32_t>(tile.cell.n) & 7u) << 3
				| (static_cast<std::uint32_t>(tile.cell.e) & 7u));
			out.push_back({ tile.scatter.get(), static_cast<float>(c.x), static_cast<float>(c.z), id });
		}
	}

	void Engine::CollectScatterOccluders(const engine::math::Vec3& camera, const engine::math::Vec3& focus,
		std::vector<engine::render::OccluderSphere>& out) const
	{
		// (pas de « near » : macro vide sous Windows via windef.h)
		static thread_local std::vector<uint32_t> nearIdx;
		static thread_local std::vector<ScatterLayer> layers;
		GatherScatterLayers(layers);
		for (uint32_t li = 0; li < static_cast<uint32_t>(layers.size()); ++li)
		{
			const ScatterLayer& layer = layers[li];
			const engine::math::Vec3 camL{ camera.x - layer.offsetX, camera.y, camera.z - layer.offsetZ };
			const engine::math::Vec3 focusL{ focus.x - layer.offsetX, focus.y, focus.z - layer.offsetZ };
			layer.field->GatherNearSegment(camL, focusL, 0.6f, nearIdx);
			for (uint32_t idx : nearIdx)
			{
				const auto& it = layer.field->Item(idx);
				const engine::math::Vec3 center{ it.center.x + layer.offsetX, it.center.y, it.center.z + layer.offsetZ };
				out.push_back({ ScatterOccluderId(layer.id, idx), center, it.radius });
			}
		}
	}

	void Engine::RecordScatterGeometry(VkCommandBuffer cmd, engine::render::Registry& reg,
		const engine::RenderState& rs)
	{
		if (!m_pipeline) return;
		static thread_local std::vector<ScatterLayer> layers;
		GatherScatterLayers(layers);
		if (layers.empty()) return;
		auto& geom = m_pipeline->GetGeometryPass();
		if (!geom.HasLoadPass()) return;
		auto& materialCache = m_pipeline->GetMaterialDescriptorCache();

		// Distances relues chaque frame (réglage à chaud dans zone.json / config.json).
		engine::world::scatter::CullDistances dist;
		dist.largeM = static_cast<float>(m_cfg.GetDouble("world.scatter.cull_large_m", 150.0));
		dist.mediumM = static_cast<float>(m_cfg.GetDouble("world.scatter.cull_medium_m", 80.0));
		dist.smallM = static_cast<float>(m_cfg.GetDouble("world.scatter.cull_small_m", 42.0));

		// Une entrée par (instance × partie) : clé de lot = (mesh GPU, matériau, fondu).
		struct DrawRef { const engine::render::MeshAsset* mesh; uint32_t material; uint32_t fadeQ; uint32_t layer; uint32_t item; };
		static thread_local std::vector<DrawRef> refs;
		refs.clear();
		size_t visibleTotal = 0, itemTotal = 0;
		for (uint32_t li = 0; li < static_cast<uint32_t>(layers.size()); ++li)
		{
			const ScatterLayer& layer = layers[li];
			itemTotal += layer.field->Count();
			// Visibilité dans le repère du champ : caméra décalée, frustum de
			// viewProj × Translate(décalage) (plans exprimés en coordonnées locales).
			const engine::math::Vec3 eyeL{ rs.camera.position.x - layer.offsetX, rs.camera.position.y,
				rs.camera.position.z - layer.offsetZ };
			const engine::math::Mat4 vpLocal = rs.viewProjMatrix
				* engine::math::Mat4::Translate(engine::math::Vec3{ layer.offsetX, 0.0f, layer.offsetZ });
			const engine::world::scatter::Frustum frustum =
				engine::world::scatter::Frustum::FromViewProj(vpLocal.m);
			layer.field->Gather(eyeL, frustum, dist, m_scatterVisible);
			visibleTotal += m_scatterVisible.size();
			for (uint32_t idx : m_scatterVisible)
			{
				const auto& it = layer.field->Item(idx);
				if (it.meshId >= m_scatterMeshes.size()) continue;
				uint32_t fadeQ = 16u; // 16 = opaque
				if ((it.flags & engine::world::instances::kScatterFlagOccluder) != 0u)
				{
					const float f = m_cameraOcclusionFade.FadeFor(ScatterOccluderId(layer.id, idx));
					if (f < 0.999f) fadeQ = static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 15.0f));
				}
				for (const PropPart& part : m_scatterMeshes[it.meshId].parts)
				{
					engine::render::MeshAsset* mesh = part.mesh.Get();
					if (mesh == nullptr) continue;
					refs.push_back({ mesh, part.materialIndex, fadeQ, li, idx });
				}
			}
		}
		if (refs.empty()) return;
		std::sort(refs.begin(), refs.end(), [](const DrawRef& a, const DrawRef& b) {
			if (a.mesh != b.mesh) return a.mesh < b.mesh;
			if (a.material != b.material) return a.material < b.material;
			if (a.fadeQ != b.fadeQ) return a.fadeQ < b.fadeQ;
			if (a.layer != b.layer) return a.layer < b.layer;
			return a.item < b.item;
		});

		const uint32_t slot = m_currentFrame % kScatterFrameSlots;
		const uint32_t needed = static_cast<uint32_t>(refs.size());
		if (!EnsureScatterInstanceBuffer(slot, needed) || m_scatterInstMapped[slot] == nullptr) return;
		auto* dst = static_cast<uint8_t*>(m_scatterInstMapped[slot]);

		static thread_local std::vector<engine::render::InstanceBatch> batches;
		batches.clear();
		for (uint32_t i = 0; i < needed;)
		{
			uint32_t j = i;
			while (j < needed && refs[j].mesh == refs[i].mesh && refs[j].material == refs[i].material && refs[j].fadeQ == refs[i].fadeQ)
			{
				// Matrice du champ + décalage de la cellule (translation en m12/m14).
				const ScatterLayer& layer = layers[refs[j].layer];
				float model[16];
				std::memcpy(model, layer.field->Item(refs[j].item).model.m, sizeof(model));
				model[12] += layer.offsetX;
				model[14] += layer.offsetZ;
				std::memcpy(dst + static_cast<size_t>(j) * engine::render::kInstanceMatrixSizeBytes,
					model, engine::render::kInstanceMatrixSizeBytes);
				++j;
			}
			engine::render::InstanceBatch b;
			b.mesh = refs[i].mesh;
			b.materialIndex = refs[i].material;
			b.materialDescriptorSet = materialCache.GetDescriptorSet();
			b.instanceCount = j - i;
			b.instanceBufferOffset = i * engine::render::kInstanceMatrixSizeBytes;
			b.fade = refs[i].fadeQ >= 16u ? 1.0f : static_cast<float>(refs[i].fadeQ) / 15.0f;
			batches.push_back(b);
			i = j;
		}
		geom.RecordInstanced(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
			m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId, m_fgGBufferVelocityId, m_fgDepthId,
			rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
			batches.data(), static_cast<uint32_t>(batches.size()), m_scatterInstBuf[slot],
			/*loadExistingGbuffer*/ true);

		if ((m_currentFrame % 120) == 0)
			LOG_DEBUG(Render, "[Scatter] visibles={} draws={} lots={} (total={}, champs={})",
				visibleTotal, needed, batches.size(), itemTotal, layers.size());
	}
}

// ============================================================================
// Monde jouable — lieux du monde : carte (touche M) et bannière d'entrée.
// ============================================================================
namespace engine
{
	void Engine::EnsureWorldPlaces()
	{
		if (m_worldPlacesAttempted)
			return;
		m_worldPlacesAttempted = true;
		// Le monde porte le nom de la zone active (« royaumes ») ; une zone
		// historique (feyhin…) n'a pas de places.json → ni carte ni bannière.
		const std::string world = m_cfg.GetString("world.active_zone", "");
		if (world.empty())
			return;
		std::string err;
		if (m_worldPlaces.LoadFromContent(m_cfg, world, err))
			LOG_INFO(Core, "[WorldPlaces] '{}' : {} villes, {} lieux, {} regions (carte : touche M)",
				world, m_worldPlaces.Cities().size(), m_worldPlaces.Pois().size(), m_worldPlaces.Regions().size());
		else
			LOG_INFO(Core, "[WorldPlaces] pas de lieux pour '{}' ({}) : carte du monde indisponible", world, err);
	}

	void Engine::DrawWorldPlacesHud(ImDrawList* fg, float dw, float dh, float dtSec)
	{
		EnsureWorldPlaces();
		if (!m_worldPlaces.IsLoaded())
			return;
		// Lieux du monde en coordonnées GLOBALES (places.json) : position du joueur convertie.
		engine::math::Vec3 pos = m_characterController.GetPosition();
		if (m_cellWorld)
		{
			pos.x = static_cast<float>(m_renderFrame.ToGlobalX(pos.x));
			pos.z = static_cast<float>(m_renderFrame.ToGlobalZ(pos.z));
		}
		m_placeBanner.Update(m_worldPlaces, pos.x, pos.z, dtSec);
		engine::render::DrawPlaceBanner(fg, dw, dh, m_placeBanner);
		if (!m_worldMapVisible)
			return;
		engine::render::WorldMapHudInput in;
		in.places = &m_worldPlaces;
		in.mapTexture = m_worldPlaces.MapImage().empty() ? 0u : m_skillIconCache.GetOrLoad(m_worldPlaces.MapImage());
		in.playerX = pos.x;
		in.playerZ = pos.z;
		in.playerYaw = m_avatarYaw;
		in.playerRace = m_localRaceId.c_str();
		in.viewportW = dw;
		in.viewportH = dh;
		engine::render::DrawWorldMapWindow(&m_worldMapVisible, in, m_worldMapView);
	}
}
