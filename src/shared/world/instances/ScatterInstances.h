#pragma once

// Monde jouable (chantier « Royaumes ») — instances de décor dispersées en masse
// (arbres, sous-bois, rochers, mobilier urbain, remparts…) :
// struct + sérialisation `instances/zone_<id>/scatter.bin` (magic « LCSC » v1/v2).
//
// Sérialisation HEADER-ONLY (inline), sur le patron de PropInstances.h /
// Buildings.h : partagée par le client (lecture, rendu instancié) et par
// l'outil offline `tools/world_gen` (écriture), sans dupliquer de symboles ni
// tirer Vulkan. Le format est versionné et ADDITIF (règle d'architecture) :
// toute évolution incrémente kScatterVersion et garde la lecture des versions
// précédentes.
//
// Contrairement à props.bin (assetId opaque), chaque instance référence un
// chemin de mesh via une TABLE DE CHAÎNES en tête de fichier : le client résout
// directement `paths.content + "/" + meshPath` sans registre d'assets.
//
// Disposition binaire (little-endian) :
//   [magic u32 'LCSC'][version u32][meshCount u32][instanceCount u32]
//   meshCount  × [len u16][bytes]                      (chemins relatifs au content)
//   instanceCount × 32 octets :
//     [meshIndex u16][flags u8][cullClass u8]
//     [x f32][yOffset f32][z f32][yawDeg f32][scale f32][collisionRadius f32]
//     [tiltDeg f32]
//   v2 SEULEMENT — table de chunks (streaming par carreaux, cellules de 10 km) :
//     [originX f32][originZ f32][chunkSizeM f32][cols u16][rows u16]
//     cols × rows × [firstInstance u32][instanceCount u32]   (ligne par ligne, Z puis X)
//   Les instances d'un chunk sont CONTIGUËS (fichier trié par chunk) : le client
//   ne charge que les chunks proches du joueur. Un v1 se lit comme un chunk unique.
//
// Conventions :
//   - x/z en mètres monde ; Y = sol (terrain) + yOffset, résolu AU CHARGEMENT
//     par le client (le terrain est la source de vérité de l'altitude).
//   - yawDeg autour de +Y, tiltDeg = inclinaison légère autour de +X local
//     (variété naturelle des arbres/rochers ; 0 pour le mobilier).
//   - flags : bit0 = solide (collision), bit1 = « pièce de bâtiment » (la
//     collision passe par le catalogue de pièces, cf. building_pieces.json),
//     bit2 = occulteur caméra (fondu d'occlusion autorisé).
//   - cullClass : distance de culling (0 = grand : arbres/bâti, 1 = moyen :
//     buissons/rochers, 2 = petit : fleurs/herbes/champignons).

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace engine::world::instances
{
	constexpr uint32_t kScatterMagic   = 0x4353434Cu; // "LCSC" little-endian
	constexpr uint32_t kScatterVersion = 2u;
	/// Taille (octets) d'un enregistrement d'instance sérialisé (v1).
	constexpr uint32_t kScatterRecordBytes = 32u;

	/// Bits de ScatterInstance::flags.
	enum ScatterFlags : uint8_t
	{
		kScatterFlagSolid         = 1u << 0, ///< bloque le déplacement (collision)
		kScatterFlagBuildingPiece = 1u << 1, ///< collision via le catalogue de pièces
		kScatterFlagOccluder      = 1u << 2, ///< participe au fondu d'occlusion caméra
	};

	/// Classe de distance de culling (lue par le client, distances en config).
	enum class ScatterCullClass : uint8_t
	{
		Large  = 0, ///< arbres, bâtiments, remparts
		Medium = 1, ///< buissons, rochers, mobilier
		Small  = 2, ///< fleurs, herbes, champignons, petits accessoires
	};

	/// Une instance de décor. `meshIndex` indexe `ScatterFile::meshPaths`.
	struct ScatterInstance
	{
		uint16_t meshIndex = 0;
		uint8_t  flags = 0;
		uint8_t  cullClass = 0;
		float    x = 0.0f;
		float    yOffset = 0.0f;
		float    z = 0.0f;
		float    yawDeg = 0.0f;
		float    scale = 1.0f;
		float    collisionRadius = 0.0f; ///< 0 = empreinte auto (bounding box du mesh)
		float    tiltDeg = 0.0f;
	};

	/// Plage d'instances d'un chunk (v2).
	struct ScatterChunkRange
	{
		uint32_t first = 0;
		uint32_t count = 0;
	};

	/// Découpage en chunks (v2). `cols == 0` : pas de table (fichier v1).
	struct ScatterChunkGrid
	{
		float    originX = 0.0f;    ///< coin min X du chunk (0,0), mètres monde
		float    originZ = 0.0f;    ///< coin min Z du chunk (0,0)
		float    chunkSizeM = 500.0f;
		uint16_t cols = 0;
		uint16_t rows = 0;
		std::vector<ScatterChunkRange> ranges; ///< cols × rows, index = row * cols + col
	};

	/// Contenu complet d'un scatter.bin.
	struct ScatterFile
	{
		std::vector<std::string>     meshPaths;
		std::vector<ScatterInstance> instances;
		ScatterChunkGrid             chunks;   ///< v2 ; vide en v1
	};

	namespace scatter_detail
	{
		inline void PutU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
		inline void PutU16(std::vector<uint8_t>& b, uint16_t v)
		{
			b.push_back(uint8_t(v & 0xFFu));
			b.push_back(uint8_t((v >> 8) & 0xFFu));
		}
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
			b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutF32(std::vector<uint8_t>& b, float f)
		{
			uint32_t u = 0; std::memcpy(&u, &f, 4); PutU32(b, u);
		}
		inline bool GetU8(std::span<const uint8_t> b, size_t& p, uint8_t& out)
		{
			if (p + 1 > b.size()) return false;
			out = b[p]; p += 1; return true;
		}
		inline bool GetU16(std::span<const uint8_t> b, size_t& p, uint16_t& out)
		{
			if (p + 2 > b.size()) return false;
			out = uint16_t(uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8));
			p += 2; return true;
		}
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& out)
		{
			if (p + 4 > b.size()) return false;
			out = uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
			      (uint32_t(b[p + 3]) << 24);
			p += 4; return true;
		}
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& out)
		{
			uint32_t u = 0;
			if (!GetU32(b, p, u)) return false;
			std::memcpy(&out, &u, 4); return true;
		}
	}

	/// Sérialise au format scatter.bin : v2 si `file.chunks.cols > 0` (table de
	/// chunks), sinon v1 (rétro-compatible octet pour octet). Les chemins de plus
	/// de 65535 octets sont tronqués (jamais le cas : chemins relatifs courts).
	inline std::vector<uint8_t> SaveScatterBin(const ScatterFile& file)
	{
		using namespace scatter_detail;
		const bool v2 = file.chunks.cols > 0 && file.chunks.rows > 0
			&& file.chunks.ranges.size() == static_cast<size_t>(file.chunks.cols) * file.chunks.rows;
		std::vector<uint8_t> b;
		b.reserve(16 + file.meshPaths.size() * 40 + file.instances.size() * kScatterRecordBytes
			+ (v2 ? 16 + file.chunks.ranges.size() * 8 : 0));
		PutU32(b, kScatterMagic);
		PutU32(b, v2 ? 2u : 1u);
		PutU32(b, static_cast<uint32_t>(file.meshPaths.size()));
		PutU32(b, static_cast<uint32_t>(file.instances.size()));
		for (const std::string& s : file.meshPaths)
		{
			const uint16_t len = static_cast<uint16_t>(s.size() > 0xFFFFu ? 0xFFFFu : s.size());
			PutU16(b, len);
			b.insert(b.end(), s.begin(), s.begin() + len);
		}
		for (const ScatterInstance& in : file.instances)
		{
			PutU16(b, in.meshIndex);
			PutU8(b, in.flags);
			PutU8(b, in.cullClass);
			PutF32(b, in.x);
			PutF32(b, in.yOffset);
			PutF32(b, in.z);
			PutF32(b, in.yawDeg);
			PutF32(b, in.scale);
			PutF32(b, in.collisionRadius);
			PutF32(b, in.tiltDeg);
		}
		if (v2)
		{
			PutF32(b, file.chunks.originX);
			PutF32(b, file.chunks.originZ);
			PutF32(b, file.chunks.chunkSizeM);
			PutU16(b, file.chunks.cols);
			PutU16(b, file.chunks.rows);
			for (const ScatterChunkRange& r : file.chunks.ranges)
			{
				PutU32(b, r.first);
				PutU32(b, r.count);
			}
		}
		return b;
	}

	/// Désérialise un scatter.bin. Valide magic, version, bornes et indices de
	/// mesh (un meshIndex hors table = fichier corrompu → échec). Reset `out`.
	inline bool LoadScatterBin(std::span<const uint8_t> bytes, ScatterFile& out, std::string& err)
	{
		using namespace scatter_detail;
		out.meshPaths.clear();
		out.instances.clear();
		out.chunks = ScatterChunkGrid{};
		size_t p = 0;
		uint32_t magic = 0, version = 0, meshCount = 0, instanceCount = 0;
		if (!GetU32(bytes, p, magic) || magic != kScatterMagic)
		{
			err = "scatter.bin: magic invalide (LCSC attendu)"; return false;
		}
		if (!GetU32(bytes, p, version) || version == 0u || version > kScatterVersion)
		{
			err = "scatter.bin: version non supportee"; return false;
		}
		if (!GetU32(bytes, p, meshCount) || !GetU32(bytes, p, instanceCount))
		{
			err = "scatter.bin: header tronque"; return false;
		}
		if (meshCount > 0xFFFFu)
		{
			err = "scatter.bin: table de meshes trop grande"; return false;
		}
		out.meshPaths.reserve(meshCount);
		for (uint32_t i = 0; i < meshCount; ++i)
		{
			uint16_t len = 0;
			if (!GetU16(bytes, p, len) || p + len > bytes.size())
			{
				err = "scatter.bin: table de meshes tronquee"; return false;
			}
			out.meshPaths.emplace_back(reinterpret_cast<const char*>(bytes.data() + p), len);
			p += len;
		}
		// Garde anti-allocation démesurée : le compteur doit tenir dans le fichier.
		if (static_cast<uint64_t>(instanceCount) * kScatterRecordBytes > bytes.size() - p)
		{
			err = "scatter.bin: instances tronquees"; return false;
		}
		out.instances.resize(instanceCount);
		for (uint32_t i = 0; i < instanceCount; ++i)
		{
			ScatterInstance& in = out.instances[i];
			const bool ok = GetU16(bytes, p, in.meshIndex) && GetU8(bytes, p, in.flags)
				&& GetU8(bytes, p, in.cullClass) && GetF32(bytes, p, in.x)
				&& GetF32(bytes, p, in.yOffset) && GetF32(bytes, p, in.z)
				&& GetF32(bytes, p, in.yawDeg) && GetF32(bytes, p, in.scale)
				&& GetF32(bytes, p, in.collisionRadius) && GetF32(bytes, p, in.tiltDeg);
			if (!ok)
			{
				err = "scatter.bin: instance tronquee"; out.instances.clear(); return false;
			}
			if (in.meshIndex >= meshCount)
			{
				err = "scatter.bin: meshIndex hors table"; out.instances.clear(); return false;
			}
			if (in.cullClass > static_cast<uint8_t>(ScatterCullClass::Small))
				in.cullClass = static_cast<uint8_t>(ScatterCullClass::Small);
		}
		if (version >= 2u)
		{
			ScatterChunkGrid& g = out.chunks;
			if (!GetF32(bytes, p, g.originX) || !GetF32(bytes, p, g.originZ) || !GetF32(bytes, p, g.chunkSizeM)
				|| !GetU16(bytes, p, g.cols) || !GetU16(bytes, p, g.rows))
			{
				err = "scatter.bin: table de chunks tronquee"; out.instances.clear(); return false;
			}
			const uint64_t n = static_cast<uint64_t>(g.cols) * g.rows;
			if (!(g.chunkSizeM > 0.0f) || n * 8u > bytes.size() - p)
			{
				err = "scatter.bin: table de chunks invalide"; out.instances.clear(); return false;
			}
			g.ranges.resize(static_cast<size_t>(n));
			for (ScatterChunkRange& r : g.ranges)
			{
				(void)GetU32(bytes, p, r.first);
				(void)GetU32(bytes, p, r.count);
				if (static_cast<uint64_t>(r.first) + r.count > instanceCount)
				{
					err = "scatter.bin: plage de chunk hors instances"; out.instances.clear(); out.chunks = ScatterChunkGrid{}; return false;
				}
			}
		}
		return true;
	}
}
