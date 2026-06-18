#include "src/client/localization/LocalizationService.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const std::string& msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	// Racine de contenu passée en argv[1] (la CI fournit le chemin game/data).
	std::set<std::string> LoadKeys(const std::filesystem::path& root, const std::string& tag)
	{
		const std::filesystem::path file = root / "localization" / tag / (tag + ".json");
		const std::string text = engine::platform::FileSystem::ReadAllText(file);
		engine::client::LocalizationService::Catalog cat;
		const bool ok = engine::client::LocalizationService::ParseFlatJsonCatalog(text, cat);
		Assert(ok, "parse catalogue " + tag + " (" + file.string() + ")");
		std::set<std::string> keys;
		for (const auto& [k, v] : cat)
		{
			(void)v;
			keys.insert(k);
		}
		return keys;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: catalog_parity_tests <content_root>" << std::endl;
		return 2;
	}
	const std::filesystem::path root = argv[1];
	const std::set<std::string> ref = LoadKeys(root, "en");
	Assert(!ref.empty(), "en.json non vide");

	for (const std::string& tag : { "fr", "es", "de", "it", "pl", "pt" })
	{
		const std::set<std::string> keys = LoadKeys(root, tag);
		// Clés manquantes par rapport à en.
		for (const std::string& k : ref)
			Assert(keys.count(k) == 1, "catalogue " + tag + " : clé manquante '" + k + "'");
		// Clés en trop.
		for (const std::string& k : keys)
			Assert(ref.count(k) == 1, "catalogue " + tag + " : clé en trop '" + k + "'");
	}

	if (s_failCount != 0)
		return 1;
	std::cout << "Catalog parity tests: all passed." << std::endl;
	return 0;
}
