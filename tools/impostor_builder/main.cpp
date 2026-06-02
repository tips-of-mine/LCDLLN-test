/// M45.4 — impostor_builder : outil offline CLI de pré-rendu d'atlas d'impostors
/// octaédriques. 100 % autonome (pas d'engine_core, pas de Vulkan).
///
/// Usage (mode build) :
///   impostor_builder --input <mesh.gltf> [--output impostor_atlas.bin]
///                    [--views 8] [--tile 64] [--ss 2]
///
/// Usage (mode vérification round-trip) :
///   impostor_builder --verify <atlas.bin>
///
/// Mode build (FORMAT v2) : charge le mesh glTF, calcule un hash de contenu
/// FNV-1a 64 bits des octets bruts du .gltf, puis pour chaque vue (i,j) calcule
/// la direction octaédrique, construit une view-projection orthographique
/// cadrant l'AABB, et rasterise les TROIS atlas (albedo / normal / orm) à une
/// résolution supersamplée (SS×) tile par tile. Après toutes les vues, downsample
/// box SS→final, écrit le fichier (albedo, normal, orm), puis auto-vérifie par
/// relecture (round-trip + comparaison contentHash).

#include "GltfMeshLoader.h"
#include "ImpostorFormat.h"
#include "OctahedralMap.h"
#include "Rasterizer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace tools::impostor_builder;

	/// Matrice 4x4 column-major minimale (autonome — pas de dépendance moteur).
	struct Mat4
	{
		float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	};

	/// Produit matriciel a*b (column-major).
	Mat4 Mul(const Mat4& a, const Mat4& b)
	{
		Mat4 r;
		for (int col = 0; col < 4; ++col)
			for (int row = 0; row < 4; ++row)
			{
				float v = 0.0f;
				for (int i = 0; i < 4; ++i)
					v += a.m[i * 4 + row] * b.m[col * 4 + i];
				r.m[col * 4 + row] = v;
			}
		return r;
	}

	/// Construit une matrice de vue « look-at » droitière.
	/// \param eye    Position caméra.
	/// \param center Cible regardée.
	/// \param up     Vecteur up de référence.
	Mat4 LookAt(const std::array<float, 3>& eye,
	            const std::array<float, 3>& center,
	            const std::array<float, 3>& up)
	{
		auto sub = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
			return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
		};
		auto norm = [](std::array<float, 3> v) {
			const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if (l > 1e-8f) { v[0] /= l; v[1] /= l; v[2] /= l; }
			return v;
		};
		auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
			return std::array<float, 3>{a[1] * b[2] - a[2] * b[1],
			                            a[2] * b[0] - a[0] * b[2],
			                            a[0] * b[1] - a[1] * b[0]};
		};
		auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		};

		// f = direction de vue (eye -> center) ; convention OpenGL : caméra regarde -z.
		const std::array<float, 3> f = norm(sub(center, eye));
		std::array<float, 3> s = norm(cross(f, up));
		// Garde-fou si up colinéaire à f.
		if (s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f)
			s = norm(cross(f, std::array<float, 3>{1.0f, 0.0f, 0.0f}));
		const std::array<float, 3> u = cross(s, f);

		Mat4 r;
		r.m[0] = s[0]; r.m[4] = s[1]; r.m[8]  = s[2];  r.m[12] = -dot(s, eye);
		r.m[1] = u[0]; r.m[5] = u[1]; r.m[9]  = u[2];  r.m[13] = -dot(u, eye);
		r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2]; r.m[14] = dot(f, eye);
		r.m[3] = 0.0f; r.m[7] = 0.0f; r.m[11] = 0.0f;  r.m[15] = 1.0f;
		return r;
	}

	/// Projection orthographique (NDC Vulkan : Y vers le bas, Z dans [0,1]).
	/// Le facteur m[5] est négatif (inversion Y type Vulkan), cohérent avec le
	/// rasterizer qui écrit la ligne 0 en haut.
	///
	/// Convention de profondeur : la matrice de vue est droitière (caméra
	/// regarde -z), donc l'espace vue a z négatif devant la caméra. On mappe
	/// -z_vue vers [0,1] : m[10] = -1/(zf-zn). Ainsi un fragment PLUS PROCHE de
	/// la caméra (|z_vue| petit) obtient une profondeur PLUS PETITE, ce qui est
	/// cohérent avec le z-test « garde le plus petit » du rasterizer.
	Mat4 OrthoVulkan(float l, float r, float b, float t, float zn, float zf)
	{
		Mat4 o;
		o.m[0]  = 2.0f / (r - l);
		o.m[5]  = -2.0f / (t - b);
		o.m[10] = -1.0f / (zf - zn);
		o.m[12] = -(r + l) / (r - l);
		o.m[13] = (t + b) / (t - b);
		o.m[14] = -zn / (zf - zn);
		o.m[15] = 1.0f;
		return o;
	}

	/// Calcule le hash FNV-1a 64 bits des octets bruts d'un fichier.
	/// NOTE : c'est bien FNV-1a (offset basis 1469598103934665603, prime
	/// 1099511628211), PAS xxHash. Utilisé comme contentHash du .gltf source
	/// pour détecter les changements de mesh entre deux builds.
	/// \param path  Chemin du fichier source (lu en binaire).
	/// \param outOk Mis à false si le fichier ne peut pas être lu.
	/// \return Hash FNV-1a 64 (0 et outOk=false si lecture impossible).
	uint64_t Fnv1a64File(const std::string& path, bool& outOk)
	{
		std::ifstream is(path, std::ios::binary);
		if (!is)
		{
			outOk = false;
			return 0u;
		}
		outOk = true;
		uint64_t hash = 1469598103934665603ull; // offset basis FNV-1a 64
		constexpr uint64_t prime = 1099511628211ull;
		char buf[65536];
		while (is)
		{
			is.read(buf, sizeof(buf));
			const std::streamsize n = is.gcount();
			for (std::streamsize k = 0; k < n; ++k)
			{
				hash ^= static_cast<uint8_t>(buf[k]);
				hash *= prime;
			}
		}
		return hash;
	}

	/// Box-downsample un atlas supersamplé (ssDim×ssDim) vers la résolution
	/// finale (dstDim×dstDim) en moyennant, par canal RGBA, le bloc ss×ss de
	/// texels source correspondant à chaque texel final.
	///
	/// \param src    Atlas source RGBA8 supersamplé (ssDim*ssDim*4 octets).
	/// \param ssDim  Côté de l'atlas source en texels (= dstDim*ss).
	/// \param dstDim Côté de l'atlas final en texels (= views*tile).
	/// \param ss     Facteur de supersampling (>=1).
	/// \param dst    Atlas final RGBA8 (dstDim*dstDim*4 octets, redimensionné).
	void DownsampleBox(const std::vector<uint8_t>& src, uint32_t ssDim,
	                   uint32_t dstDim, uint32_t ss, std::vector<uint8_t>& dst)
	{
		dst.assign(static_cast<size_t>(dstDim) * dstDim * 4, 0);
		const uint32_t blockArea = ss * ss; // nombre d'échantillons moyennés
		for (uint32_t dy = 0; dy < dstDim; ++dy)
		{
			for (uint32_t dx = 0; dx < dstDim; ++dx)
			{
				uint32_t acc[4] = {0, 0, 0, 0};
				for (uint32_t sy = 0; sy < ss; ++sy)
				{
					const uint32_t srcY = dy * ss + sy;
					for (uint32_t sx = 0; sx < ss; ++sx)
					{
						const uint32_t srcX = dx * ss + sx;
						const size_t si = (static_cast<size_t>(srcY) * ssDim + srcX) * 4;
						acc[0] += src[si + 0];
						acc[1] += src[si + 1];
						acc[2] += src[si + 2];
						acc[3] += src[si + 3];
					}
				}
				const size_t di = (static_cast<size_t>(dy) * dstDim + dx) * 4;
				dst[di + 0] = static_cast<uint8_t>(acc[0] / blockArea);
				dst[di + 1] = static_cast<uint8_t>(acc[1] / blockArea);
				dst[di + 2] = static_cast<uint8_t>(acc[2] / blockArea);
				dst[di + 3] = static_cast<uint8_t>(acc[3] / blockArea);
			}
		}
	}

	void PrintUsage()
	{
		std::cerr <<
			"Usage:\n"
			"  impostor_builder --input <mesh.gltf> [--output impostor_atlas.bin]\n"
			"                   [--views 8] [--tile 64] [--ss 2]\n"
			"  impostor_builder --verify <atlas.bin>\n"
			"\n"
			"Options:\n"
			"  --input <file>   Mesh glTF source (mode build)\n"
			"  --output <file>  Fichier atlas de sortie (défaut: impostor_atlas.bin)\n"
			"  --views <N>      Vues par axe -> grille N×N (défaut: 8)\n"
			"  --tile <px>      Côté d'une tile en pixels (défaut: 64)\n"
			"  --ss <N>         Facteur de supersampling AA (défaut: 2)\n"
			"  --verify <file>  Relit un atlas et vérifie le round-trip\n";
	}

	/// Mode vérification : relit un atlas v2 et affiche ses métadonnées.
	int RunVerify(const std::string& path)
	{
		ImpostorAtlasInfo info;
		uint64_t contentHash = 0;
		std::vector<uint8_t> albedo, normal, orm;
		std::string err;
		if (!ReadImpostorFile(path, info, contentHash, albedo, normal, orm, err))
		{
			std::cerr << "[impostor_builder] verify FAILED: " << err << "\n";
			return 1;
		}
		std::cout << "[impostor_builder] verify OK: " << path << "\n"
		          << "  magic/version : MIPO / " << kImpostorVersion << "\n"
		          << "  viewsPerAxis  : " << info.viewsPerAxis << "\n"
		          << "  tileSize      : " << info.tileSize << "\n"
		          << "  channels      : " << info.channels << "\n"
		          << "  albedo bytes  : " << albedo.size() << "\n"
		          << "  normal bytes  : " << normal.size() << "\n"
		          << "  orm bytes     : " << orm.size() << "\n"
		          << "  contentHash   : 0x" << std::hex << contentHash << std::dec << "\n";
		return 0;
	}
}

int main(int argc, char** argv)
{
	std::string inputPath;
	std::string outputPath = "impostor_atlas.bin";
	std::string verifyPath;
	uint32_t views = 8;
	uint32_t tile  = 64;
	uint32_t ss    = 2; // facteur supersampling AA (défaut 2)

	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--input" && i + 1 < argc)
			inputPath = argv[++i];
		else if (arg == "--output" && i + 1 < argc)
			outputPath = argv[++i];
		else if (arg == "--views" && i + 1 < argc)
			views = static_cast<uint32_t>(std::stoul(argv[++i]));
		else if (arg == "--tile" && i + 1 < argc)
			tile = static_cast<uint32_t>(std::stoul(argv[++i]));
		else if (arg == "--ss" && i + 1 < argc)
			ss = static_cast<uint32_t>(std::stoul(argv[++i]));
		else if (arg == "--verify" && i + 1 < argc)
			verifyPath = argv[++i];
		else if (arg == "--help" || arg == "-h")
		{
			PrintUsage();
			return 0;
		}
	}

	// --- Mode vérification pure -----------------------------------------
	if (!verifyPath.empty())
		return RunVerify(verifyPath);

	// --- Mode build -----------------------------------------------------
	if (inputPath.empty())
	{
		std::cerr << "[impostor_builder] --input <mesh.gltf> requis (ou --verify <file>)\n\n";
		PrintUsage();
		return 1;
	}
	if (views == 0 || tile == 0 || ss == 0)
	{
		std::cerr << "[impostor_builder] --views, --tile et --ss doivent être > 0\n";
		return 1;
	}

	// --- Hash de contenu FNV-1a 64 du .gltf source ----------------------
	bool hashOk = false;
	const uint64_t contentHash = Fnv1a64File(inputPath, hashOk);
	if (!hashOk)
	{
		std::cerr << "[impostor_builder] impossible de lire le fichier d'entrée pour le hash: "
		          << inputPath << "\n";
		return 1;
	}

	LoadedMesh mesh;
	std::string err;
	if (!LoadGltf(inputPath, mesh, err))
	{
		std::cerr << "[impostor_builder] " << err << "\n";
		return 1;
	}
	std::cout << "[impostor_builder] mesh chargé: " << mesh.vertices.size()
	          << " sommets, " << (mesh.indices.size() / 3) << " triangles\n";

	// Centre et rayon de l'AABB pour cadrer chaque vue.
	const float cx = 0.5f * (mesh.boundsMin[0] + mesh.boundsMax[0]);
	const float cy = 0.5f * (mesh.boundsMin[1] + mesh.boundsMax[1]);
	const float cz = 0.5f * (mesh.boundsMin[2] + mesh.boundsMax[2]);
	const float ex = 0.5f * (mesh.boundsMax[0] - mesh.boundsMin[0]);
	const float ey = 0.5f * (mesh.boundsMax[1] - mesh.boundsMin[1]);
	const float ez = 0.5f * (mesh.boundsMax[2] - mesh.boundsMin[2]);
	// Rayon de la sphère englobante (couvre toute orientation de vue).
	float radius = std::sqrt(ex * ex + ey * ey + ez * ez);
	if (radius < 1e-4f) radius = 1.0f;

	// --- Allocation des atlas -------------------------------------------
	ImpostorAtlasInfo info;
	info.viewsPerAxis = views;
	info.tileSize     = tile;
	info.channels     = 4;
	for (int k = 0; k < 3; ++k)
	{
		info.boundsMin[k] = mesh.boundsMin[k];
		info.boundsMax[k] = mesh.boundsMax[k];
	}

	// Résolution finale et résolution supersamplée (SS×).
	const uint32_t atlasDim = views * tile;
	const uint32_t ssDim    = views * tile * ss; // côté atlas supersamplé
	const uint32_t ssTile   = tile * ss;         // côté tile supersamplée
	const size_t   ssBytes  = static_cast<size_t>(ssDim) * ssDim * 4;

	// Atlas rendus à la résolution supersamplée (downsamplés ensuite).
	std::vector<uint8_t> albedoSS(ssBytes, 0);
	std::vector<uint8_t> normalSS(ssBytes, 0);
	std::vector<uint8_t> ormSS(ssBytes, 0);
	// Z-buffer d'UNE tile supersamplée (partagé entre sous-meshes d'une vue).
	std::vector<float>   zbuf(static_cast<size_t>(ssTile) * ssTile, 0.0f);

	// --- Rendu de chaque vue (résolution SS×) ---------------------------
	for (uint32_t j = 0; j < views; ++j)
	{
		for (uint32_t i = 0; i < views; ++i)
		{
			const OctDir dir = ViewDir(i, j, views);

			// Caméra placée à distance le long de la direction, regardant le centre.
			const float dist = radius * 2.0f;
			const std::array<float, 3> center{cx, cy, cz};
			const std::array<float, 3> eye{cx + dir.x * dist,
			                               cy + dir.y * dist,
			                               cz + dir.z * dist};
			// Up = +Y, sauf si la vue est quasi-verticale (bascule sur +Z).
			std::array<float, 3> up{0.0f, 1.0f, 0.0f};
			if (std::fabs(dir.y) > 0.99f)
				up = std::array<float, 3>{0.0f, 0.0f, 1.0f};

			const Mat4 view = LookAt(eye, center, up);
			// Volume ortho carré de côté 2*radius, profondeur [0, 2*dist].
			const Mat4 proj = OrthoVulkan(-radius, radius, -radius, radius, 0.0f, dist * 2.0f);
			const Mat4 vp = Mul(proj, view);

			// Cible = tile (i,j) dans les atlas SUPERSAMPLÉS.
			RasterTarget target;
			target.albedo     = albedoSS.data();
			target.normal     = normalSS.data();
			target.orm        = ormSS.data();
			target.atlasWidth = ssDim;
			target.tileSize   = ssTile;
			target.tileX      = i;
			target.tileY      = j;

			// Efface la tile + le z-buffer UNE fois avant les sous-meshes.
			ClearTile(target, zbuf);

			// Accumule chaque sous-mesh (matériau propre) dans la même tile.
			for (const LoadedSubMesh& sm : mesh.subMeshes)
			{
				RasterMaterial mat; // défaut : pas de texture, blanc, rough=1
				if (sm.materialIndex >= 0 &&
				    static_cast<size_t>(sm.materialIndex) < mesh.materials.size())
				{
					const LoadedMaterial& lm = mesh.materials[sm.materialIndex];
					mat.baseColorRGBA = lm.baseColorRGBA.empty() ? nullptr
					                                             : lm.baseColorRGBA.data();
					mat.bcW = lm.bcW;
					mat.bcH = lm.bcH;
					for (int k = 0; k < 4; ++k) mat.baseColorFactor[k] = lm.baseColorFactor[k];
					mat.metallic    = lm.metallic;
					mat.roughness   = lm.roughness;
					mat.alphaCutout = lm.alphaBlendOrMask;
				}

				// Slice d'indices de ce sous-mesh.
				if (sm.indexCount == 0) continue;
				const size_t first = sm.firstIndex;
				const size_t last  = first + sm.indexCount;
				if (last > mesh.indices.size()) continue; // garde-fou
				std::vector<uint32_t> sub(mesh.indices.begin() + first,
				                          mesh.indices.begin() + last);

				RasterizeSubMesh(mesh.vertices, sub, vp.m, target, zbuf, mat, 0.0f, 1.0f);
			}
		}
	}

	// --- Downsample box SS -> résolution finale -------------------------
	std::vector<uint8_t> albedo, normal, orm;
	DownsampleBox(albedoSS, ssDim, atlasDim, ss, albedo);
	DownsampleBox(normalSS, ssDim, atlasDim, ss, normal);
	DownsampleBox(ormSS, ssDim, atlasDim, ss, orm);
	const size_t atlasBytes = static_cast<size_t>(atlasDim) * atlasDim * 4;

	// --- Création du dossier de sortie ----------------------------------
	{
		std::error_code ec;
		const auto parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty())
			std::filesystem::create_directories(parent, ec);
	}

	// --- Écriture (FORMAT v2 : albedo, normal, orm) ---------------------
	if (!WriteImpostorFile(outputPath, info, contentHash, albedo, normal, orm, err))
	{
		std::cerr << "[impostor_builder] échec écriture: " << err << "\n";
		return 1;
	}
	std::cout << "[impostor_builder] atlas écrit: " << outputPath
	          << " (" << views << "x" << views << " vues, tile " << tile
	          << "px, ss " << ss << "x, " << atlasBytes << " octets/canal, "
	          << "contentHash 0x" << std::hex << contentHash << std::dec << ")\n";

	// --- Auto-vérification round-trip -----------------------------------
	{
		ImpostorAtlasInfo rInfo;
		uint64_t rHash = 0;
		std::vector<uint8_t> rAlbedo, rNormal, rOrm;
		std::string rErr;
		if (!ReadImpostorFile(outputPath, rInfo, rHash, rAlbedo, rNormal, rOrm, rErr))
		{
			std::cerr << "[impostor_builder] round-trip FAILED (relecture): " << rErr << "\n";
			return 1;
		}
		if (rInfo.viewsPerAxis != info.viewsPerAxis ||
		    rInfo.tileSize != info.tileSize ||
		    rInfo.channels != info.channels ||
		    rHash != contentHash ||
		    rAlbedo.size() != albedo.size() ||
		    rNormal.size() != normal.size() ||
		    rOrm.size() != orm.size())
		{
			std::cerr << "[impostor_builder] round-trip FAILED: métadonnées/tailles/hash incohérents\n";
			return 1;
		}
		std::cout << "[impostor_builder] round-trip OK\n";
	}

	return 0;
}
