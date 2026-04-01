#include "engine/client/LocalizationService.h"
#include "engine/platform/FileSystem.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	engine::core::Config MakeTestConfig(const std::filesystem::path& contentRoot)
	{
		engine::core::Config cfg;
		cfg.SetValue("paths.content", contentRoot.string());
		return cfg;
	}

	bool WriteCatalog(const std::filesystem::path& root, std::string_view locale, std::string_view json)
	{
		const std::filesystem::path file = root / "localization" / "text" / (std::string(locale) + ".json");
		return engine::platform::FileSystem::WriteAllText(file, json);
	}

	void TestCatalogLoadAndTranslate()
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "lcdlln_localization_test_valid";
		std::filesystem::remove_all(root);
		Assert(WriteCatalog(root, "en", "{\n\"common.apply\": \"Apply\",\n\"auth.login_title\": \"Login\"\n}\n"), "write en catalog");
		Assert(WriteCatalog(root, "fr", "{\n\"common.apply\": \"Appliquer\",\n\"auth.login_title\": \"Connexion\"\n}\n"), "write fr catalog");

		engine::client::LocalizationService svc;
		Assert(svc.Init(MakeTestConfig(root), "fr"), "init localization service");
		Assert(svc.GetCurrentLocale() == "fr", "current locale fr");
		Assert(svc.Translate("common.apply") == "Appliquer", "translate fr key");
		Assert(svc.Translate("auth.login_title") == "Connexion", "translate fr login");
		svc.Shutdown();
		std::filesystem::remove_all(root);
	}

	void TestFallbackLocaleAndKey()
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "lcdlln_localization_test_fallback";
		std::filesystem::remove_all(root);
		Assert(WriteCatalog(root, "en", "{\n\"common.apply\": \"Apply\",\n\"only.default\": \"Default\"\n}\n"), "write fallback en catalog");
		Assert(WriteCatalog(root, "fr", "{\n\"common.apply\": \"Appliquer\"\n}\n"), "write fallback fr catalog");

		engine::client::LocalizationService svc;
		Assert(svc.Init(MakeTestConfig(root), "de"), "init with unavailable locale falls back");
		Assert(svc.GetCurrentLocale() == "en", "fallback locale uses en");
		Assert(svc.SetLocale("fr"), "set locale fr");
		Assert(svc.Translate("only.default") == "Default", "missing key falls back to default locale");
		Assert(svc.Translate("missing.absolute") == "missing.absolute", "missing key returns key");
		svc.Shutdown();
		std::filesystem::remove_all(root);
	}

	void TestInvalidCatalogRejected()
	{
		const std::filesystem::path root = std::filesystem::temp_directory_path() / "lcdlln_localization_test_invalid";
		std::filesystem::remove_all(root);
		Assert(WriteCatalog(root, "en", "{\n\"common.apply\": \"Apply\"\n}\n"), "write valid en catalog");
		Assert(WriteCatalog(root, "fr", "{\n\"broken\": 42\n}\n"), "write invalid fr catalog");

		engine::client::LocalizationService svc;
		Assert(svc.Init(MakeTestConfig(root), "en"), "init service with valid default catalog");
		Assert(!svc.SetLocale("fr"), "invalid fr catalog rejected");
		svc.Shutdown();
		std::filesystem::remove_all(root);
	}

	void TestNormalizeLocaleTag()
	{
		using engine::client::LocalizationService;
		Assert(LocalizationService::NormalizeLocaleTag("fr_FR.UTF-8") == "fr", "normalize fr_FR");
		Assert(LocalizationService::NormalizeLocaleTag("en-US") == "en", "normalize en-US");
		Assert(LocalizationService::NormalizeLocaleTag("ES") == "es", "normalize ES");
	}
}

int main()
{
	TestCatalogLoadAndTranslate();
	TestFallbackLocaleAndKey();
	TestInvalidCatalogRejected();
	TestNormalizeLocaleTag();
	if (s_failCount != 0)
		return 1;
	std::cout << "LocalizationService tests: all passed." << std::endl;
	return 0;
}
