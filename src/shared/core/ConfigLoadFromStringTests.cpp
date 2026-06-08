// Test de Config::LoadFromString : parsing JSON depuis un buffer mémoire
// (aucun accès disque). Sert l'embarquement anti-triche (tables compilées
// dans le binaire shardd, lues via LoadFromString au boot).
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <string>

namespace
{
	using engine::core::Config;

	bool TestParsesFlatObject()
	{
		Config cfg;
		const std::string json = R"({ "a": { "b": 42 }, "name": "x" })";
		if (!cfg.LoadFromString(json)) return false;
		if (cfg.GetInt("a.b", -1) != 42) return false;
		if (cfg.GetString("name", "") != "x") return false;
		return true;
	}

	bool TestParsesArrayIndices()
	{
		Config cfg;
		const std::string json = R"({ "items": [ { "id": "u" }, { "id": "v" } ] })";
		if (!cfg.LoadFromString(json)) return false;
		if (!cfg.Has("items[0].id")) return false;
		if (cfg.GetString("items[0].id", "") != "u") return false;
		if (cfg.GetString("items[1].id", "") != "v") return false;
		if (cfg.Has("items[2].id")) return false;
		return true;
	}

	bool TestRejectsNonObjectAndGarbage()
	{
		Config a; if (a.LoadFromString("not json")) return false;
		Config b; if (b.LoadFromString("[1,2,3]")) return false; // racine non-objet
		return true;
	}
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);
	const bool ok = TestParsesFlatObject() && TestParsesArrayIndices() && TestRejectsNonObjectAndGarbage();
	if (ok) LOG_INFO(Core, "[ConfigLoadFromStringTests] ALL OK");
	else    LOG_ERROR(Core, "[ConfigLoadFromStringTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
