/// M45.4 — impostor_builder : outil offline CLI de pré-rendu d'atlas d'impostors
/// octaédriques. 100 % autonome (pas d'engine_core, pas de Vulkan).
///
/// Usage (mode build) :
///   impostor_builder --input <mesh.gltf> [--output impostor_atlas.bin]
///                    [--views 8] [--tile 64]
///
/// Usage (mode vérification round-trip) :
///   impostor_builder --verify <atlas.bin>
///
/// Mode build : charge le mesh glTF, pour chaque vue (i,j) calcule la direction
/// octaédrique, construit une view-projection orthographique cadrant l'AABB,
/// rasterise albedo + normale dans la tile (i,j), écrit le fichier, puis
/// auto-vérifie par relecture (round-trip).

#include "GltfMeshLoader.h"
#include "ImpostorFormat.h"
#include "OctahedralMap.h"
#include "Rasterizer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

	void PrintUsage()
	{
		std::cerr <<
			"Usage:\n"
			"  impostor_builder --input <mesh.gltf> [--output impostor_atlas.bin]\n"
			"                   [--views 8] [--tile 64]\n"
			"  impostor_builder --verify <atlas.bin>\n"
			"\n"
			"Options:\n"
			"  --input <file>   Mesh glTF source (mode build)\n"
			"  --output <file>  Fichier atlas de sortie (défaut: impostor_atlas.bin)\n"
			"  --views <N>      Vues par axe -> grille N×N (défaut: 8)\n"
			"  --tile <px>      Côté d'une tile en pixels (défaut: 64)\n"
			"  --verify <file>  Relit un atlas et vérifie le round-trip\n";
	}

	/// Mode vérification : relit un atlas et affiche ses métadonnées.
	int RunVerify(const std::string& path)
	{
		ImpostorAtlasInfo info;
		std::vector<uint8_t> albedo, normal;
		std::string err;
		if (!ReadImpostorFile(path, info, albedo, normal, err))
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
		          << "  normal bytes  : " << normal.size() << "\n";
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
	if (views == 0 || tile == 0)
	{
		std::cerr << "[impostor_builder] --views et --tile doivent être > 0\n";
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

	const uint32_t atlasDim = views * tile;
	const size_t   atlasBytes = static_cast<size_t>(atlasDim) * atlasDim * 4;
	std::vector<uint8_t> albedo(atlasBytes, 0);
	std::vector<uint8_t> normal(atlasBytes, 0);
	std::vector<float>   zbuf;

	// --- Rendu de chaque vue --------------------------------------------
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

			RasterTarget target;
			target.albedo     = albedo.data();
			target.normal     = normal.data();
			target.atlasWidth = atlasDim;
			target.tileSize   = tile;
			target.tileX      = i;
			target.tileY      = j;

			RasterizeTile(mesh.vertices, mesh.indices, vp.m, target, zbuf);
		}
	}

	// --- Création du dossier de sortie ----------------------------------
	{
		std::error_code ec;
		const auto parent = std::filesystem::path(outputPath).parent_path();
		if (!parent.empty())
			std::filesystem::create_directories(parent, ec);
	}

	// --- Écriture -------------------------------------------------------
	if (!WriteImpostorFile(outputPath, info, albedo, normal, err))
	{
		std::cerr << "[impostor_builder] échec écriture: " << err << "\n";
		return 1;
	}
	std::cout << "[impostor_builder] atlas écrit: " << outputPath
	          << " (" << views << "x" << views << " vues, tile " << tile
	          << "px, " << atlasBytes << " octets/canal)\n";

	// --- Auto-vérification round-trip -----------------------------------
	{
		ImpostorAtlasInfo rInfo;
		std::vector<uint8_t> rAlbedo, rNormal;
		std::string rErr;
		if (!ReadImpostorFile(outputPath, rInfo, rAlbedo, rNormal, rErr))
		{
			std::cerr << "[impostor_builder] round-trip FAILED (relecture): " << rErr << "\n";
			return 1;
		}
		if (rInfo.viewsPerAxis != info.viewsPerAxis ||
		    rInfo.tileSize != info.tileSize ||
		    rInfo.channels != info.channels ||
		    rAlbedo.size() != albedo.size() ||
		    rNormal.size() != normal.size())
		{
			std::cerr << "[impostor_builder] round-trip FAILED: métadonnées/tailles incohérentes\n";
			return 1;
		}
		std::cout << "[impostor_builder] round-trip OK\n";
	}

	return 0;
}
