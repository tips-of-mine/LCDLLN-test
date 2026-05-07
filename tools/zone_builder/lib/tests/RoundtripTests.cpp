/// Tests round-trip bit-à-bit pour `zone_builder_lib` (M100.3).
///
/// Trois tests :
///   1. `Test_WriteThenReadHeader_RoundtripExact` : sérialise un
///      `OutputVersionHeader` en ostream, relit depuis le span d'octets,
///      vérifie l'égalité champ par champ. Garantit que le format binaire
///      du header ne dérive pas silencieusement (ce header est partagé
///      par zone.meta, chunk.meta, instances.bin, probes.bin — tout
///      changement casserait la compat client).
///   2. `Test_WriteChunkPackage_DeterministicBytes` : appelle deux fois
///      `WriteChunkPackage` avec exactement la même entrée (mode legacy,
///      coords (0,0)), dans deux dossiers temporaires distincts, hash
///      chaque fichier produit (FNV-1a 64 bits), exige l'égalité bit-à-bit
///      des hashs. Garantit le déterminisme — même éditeur et CLI doivent
///      produire le même output.
///   3. `Test_LayoutDocument_JsonRoundtrip` : construit un texte JSON
///      reproduisant le schéma layout (version + instances), le passe à
///      `ParseJsonDocument`, reconstruit un `LayoutDocument` à la main
///      depuis l'arbre JSON (mêmes vérifs que `LoadLayoutDocument` sans
///      la résolution disque), exige l'égalité champ-à-champ avec la
///      `LayoutDocument` source.
///
/// Pas de dépendance Vulkan ni ImGui : `zone_builder_lib` est statique pure
/// CPU (cf. M100.3 §"Dépendances de la bibliothèque"). Le pattern hand-rolled
/// REQUIRE / int main est aligné sur les tests M100.1 / M100.2 du même PR
/// chain (engine/editor/world/tests/CommandStackTests.cpp).

#include <zone_builder/ChunkPackageWriter.h>
#include <zone_builder/JsonDocument.h>
#include <zone_builder/LayoutImporter.h>

#include "engine/world/OutputVersion.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::OutputVersionHeader;
	using engine::world::ReadOutputVersionHeader;
	using engine::world::WriteOutputVersionHeader;
	using tools::zone_builder::FindObjectMember;
	using tools::zone_builder::JsonType;
	using tools::zone_builder::JsonValue;
	using tools::zone_builder::LayoutDocument;
	using tools::zone_builder::LayoutInstance;
	using tools::zone_builder::ParseJsonDocument;
	using tools::zone_builder::WriteChunkPackage;

	/// Crée un dossier temporaire unique pour les tests de fichiers (le test
	/// ne peut pas s'appuyer sur un cwd partagé : ctest peut paralléliser).
	/// \param prefix Préfixe lisible inséré dans le nom (ex. "zone_builder_test").
	/// \return Chemin absolu créé. Lance via filesystem::create_directories ;
	///         le test ne nettoie pas (le dossier reste sous TMPDIR pour
	///         post-mortem si nécessaire).
	std::filesystem::path MakeUniqueTempDir(const std::string& prefix)
	{
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<uint64_t> dist;
		const std::filesystem::path base = std::filesystem::temp_directory_path() /
			(prefix + "_" + std::to_string(dist(gen)));
		std::filesystem::create_directories(base);
		return base;
	}

	/// Lit un fichier binaire en entier dans un vecteur d'octets. Renvoie un
	/// vecteur vide si le fichier est absent ou illisible — les tests font
	/// alors échouer la REQUIRE qui vérifie la taille non nulle.
	std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open())
		{
			return {};
		}
		return std::vector<uint8_t>(
			(std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());
	}

	/// Hash FNV-1a 64 bits — implémentation naïve suffisante pour comparer
	/// deux outputs identiques bit-à-bit (collision astronomiquement
	/// improbable sur ces fichiers de quelques centaines d'octets, et de
	/// toute façon les deux côtés tournent le MÊME hash sur les MÊMES
	/// entrées — toute différence est détectée).
	uint64_t HashFnv1a64(const std::vector<uint8_t>& bytes)
	{
		uint64_t hash = 0xCBF29CE484222325ull;
		for (uint8_t b : bytes)
		{
			hash ^= static_cast<uint64_t>(b);
			hash *= 0x100000001B3ull;
		}
		return hash;
	}

	/// Test 1 : roundtrip exact du header binaire `OutputVersionHeader`.
	/// Vérifie que `WriteOutputVersionHeader` produit 24 octets relisibles
	/// par `ReadOutputVersionHeader` sans perte ni corruption.
	void Test_WriteThenReadHeader_RoundtripExact()
	{
		OutputVersionHeader original{};
		original.magic = engine::world::kZoneMetaMagic;
		original.formatVersion = engine::world::kZoneMetaVersion;
		original.builderVersion = engine::world::kZoneBuilderVersion;
		original.engineVersion = engine::world::kZoneEngineVersion;
		original.contentHash = 0xDEADBEEFCAFEBABEull;

		std::ostringstream oss(std::ios::binary);
		REQUIRE(WriteOutputVersionHeader(oss, original));

		const std::string serialized = oss.str();
		REQUIRE(serialized.size() == sizeof(OutputVersionHeader));

		std::vector<uint8_t> bytes(serialized.begin(), serialized.end());
		OutputVersionHeader rebuilt{};
		std::string error;
		REQUIRE(ReadOutputVersionHeader(bytes, rebuilt, error));
		REQUIRE(error.empty());

		REQUIRE(rebuilt.magic == original.magic);
		REQUIRE(rebuilt.formatVersion == original.formatVersion);
		REQUIRE(rebuilt.builderVersion == original.builderVersion);
		REQUIRE(rebuilt.engineVersion == original.engineVersion);
		REQUIRE(rebuilt.contentHash == original.contentHash);
	}

	/// Test 2 : `WriteChunkPackage` est déterministe — deux invocations
	/// avec la même entrée (mode legacy, chunk (0,0)) produisent des fichiers
	/// strictement identiques bit-à-bit. C'est la garantie centrale de
	/// M100.3 : éditeur et CLI doivent écrire le MÊME format binaire.
	void Test_WriteChunkPackage_DeterministicBytes()
	{
		const std::filesystem::path dirA = MakeUniqueTempDir("zb_det_a");
		const std::filesystem::path dirB = MakeUniqueTempDir("zb_det_b");

		REQUIRE(WriteChunkPackage(dirA.string(), 0, 0));
		REQUIRE(WriteChunkPackage(dirB.string(), 0, 0));

		// Tous les fichiers attendus du package legacy (cf.
		// ChunkPackageWriter.cpp::WriteChunkPackage).
		const char* const kFileNames[] = {
			"chunk.meta",
			"geo.pak",
			"tex.pak",
			"instances.bin",
			"navmesh.bin",
			"probes.bin",
		};

		for (const char* name : kFileNames)
		{
			const std::vector<uint8_t> a = ReadFileBytes(dirA / name);
			const std::vector<uint8_t> b = ReadFileBytes(dirB / name);

			REQUIRE(!a.empty());
			REQUIRE(!b.empty());
			REQUIRE(a.size() == b.size());

			const uint64_t hashA = HashFnv1a64(a);
			const uint64_t hashB = HashFnv1a64(b);
			REQUIRE(hashA == hashB);
		}
	}

	/// Test 3 : un `LayoutDocument` construit en mémoire, sérialisé en JSON
	/// (avec le schéma attendu par `LoadLayoutDocument`), puis re-parsé et
	/// reconstruit, doit retrouver exactement les mêmes champs.
	///
	/// Note : `JsonDocument` n'expose pas (encore) de writer, donc on émet
	/// le JSON manuellement (texte stable, le test ne valide que la roue
	/// parse → struct, pas write → parse → struct). Pour M100.5+ qui ajoutera
	/// un writer JSON éditeur, ce test pourra être généralisé.
	void Test_LayoutDocument_JsonRoundtrip()
	{
		// Source de vérité : un layout en mémoire avec deux instances aux
		// extrêmes du schéma (positions valides dans [0, kZoneSize), guids
		// et chemins glTF distincts).
		LayoutDocument source;
		source.version = 1;
		source.relativePath = "zones/_test/layout_roundtrip.json";

		LayoutInstance inst0;
		inst0.guid = "guid-0001";
		inst0.gltfPath = "models/test/cube.gltf";
		inst0.positionX = 100.5;
		inst0.positionY = 0.0;
		inst0.positionZ = 200.25;
		source.instances.push_back(inst0);

		LayoutInstance inst1;
		inst1.guid = "guid-0002";
		inst1.gltfPath = "models/test/sphere.gltf";
		inst1.positionX = 9000.125;
		inst1.positionY = -10.0;
		inst1.positionZ = 50.5;
		source.instances.push_back(inst1);

		// Sérialisation manuelle (mêmes noms de membres que LoadLayoutDocument
		// attend : version, instances[], guid, gltf, position[3]).
		std::ostringstream oss;
		oss << "{\n  \"version\": " << source.version << ",\n  \"instances\": [\n";
		for (size_t i = 0; i < source.instances.size(); ++i)
		{
			const LayoutInstance& inst = source.instances[i];
			oss << "    {\n"
				<< "      \"guid\": \"" << inst.guid << "\",\n"
				<< "      \"gltf\": \"" << inst.gltfPath << "\",\n"
				<< "      \"position\": ["
				<< inst.positionX << ", "
				<< inst.positionY << ", "
				<< inst.positionZ << "]\n"
				<< "    }";
			if (i + 1 < source.instances.size())
			{
				oss << ",";
			}
			oss << "\n";
		}
		oss << "  ]\n}\n";

		// Parse via le DOM de zone_builder_lib (l'API publique
		// `ParseJsonDocument`).
		JsonValue root;
		std::string error;
		REQUIRE(ParseJsonDocument(oss.str(), root, error));
		REQUIRE(error.empty());
		REQUIRE(root.type == JsonType::Object);

		// Reconstruction manuelle (réplique la logique de
		// LoadLayoutDocument sans la couche disque/Config).
		LayoutDocument rebuilt;
		rebuilt.relativePath = source.relativePath;

		const JsonValue* versionValue = FindObjectMember(root, "version");
		REQUIRE(versionValue != nullptr);
		REQUIRE(versionValue->type == JsonType::Number);
		rebuilt.version = static_cast<int>(versionValue->numberValue);

		const JsonValue* instancesValue = FindObjectMember(root, "instances");
		REQUIRE(instancesValue != nullptr);
		REQUIRE(instancesValue->type == JsonType::Array);
		REQUIRE(instancesValue->arrayValue.size() == source.instances.size());

		for (const JsonValue& jsonInstance : instancesValue->arrayValue)
		{
			REQUIRE(jsonInstance.type == JsonType::Object);

			const JsonValue* guid = FindObjectMember(jsonInstance, "guid");
			const JsonValue* gltf = FindObjectMember(jsonInstance, "gltf");
			const JsonValue* position = FindObjectMember(jsonInstance, "position");

			REQUIRE(guid != nullptr);
			REQUIRE(gltf != nullptr);
			REQUIRE(position != nullptr);
			REQUIRE(guid->type == JsonType::String);
			REQUIRE(gltf->type == JsonType::String);
			REQUIRE(position->type == JsonType::Array);
			REQUIRE(position->arrayValue.size() == 3u);

			LayoutInstance inst;
			inst.guid = guid->stringValue;
			inst.gltfPath = gltf->stringValue;
			inst.positionX = position->arrayValue[0].numberValue;
			inst.positionY = position->arrayValue[1].numberValue;
			inst.positionZ = position->arrayValue[2].numberValue;
			rebuilt.instances.push_back(inst);
		}

		// Égalité champ-à-champ.
		REQUIRE(rebuilt.version == source.version);
		REQUIRE(rebuilt.relativePath == source.relativePath);
		REQUIRE(rebuilt.instances.size() == source.instances.size());
		for (size_t i = 0; i < source.instances.size(); ++i)
		{
			REQUIRE(rebuilt.instances[i].guid == source.instances[i].guid);
			REQUIRE(rebuilt.instances[i].gltfPath == source.instances[i].gltfPath);
			REQUIRE(rebuilt.instances[i].positionX == source.instances[i].positionX);
			REQUIRE(rebuilt.instances[i].positionY == source.instances[i].positionY);
			REQUIRE(rebuilt.instances[i].positionZ == source.instances[i].positionZ);
		}
	}
}

int main()
{
	Test_WriteThenReadHeader_RoundtripExact();
	Test_WriteChunkPackage_DeterministicBytes();
	Test_LayoutDocument_JsonRoundtrip();

	if (g_failed == 0)
	{
		std::printf("[PASS] zone_builder_roundtrip_tests (3/3)\n");
		return 0;
	}
	std::printf("[FAIL] zone_builder_roundtrip_tests: %d failure(s)\n", g_failed);
	return 1;
}
