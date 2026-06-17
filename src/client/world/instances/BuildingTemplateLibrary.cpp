#include "src/client/world/instances/BuildingTemplateLibrary.h"

#include "src/shared/core/Config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::world::instances
{
	namespace
	{
		/// Échappe une chaîne pour insertion dans un littéral JSON (guillemets +
		/// antislash). Les chemins de mesh n'ont pas d'autres caractères spéciaux.
		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (char c : s)
			{
				if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
				else out.push_back(c);
			}
			return out;
		}

		/// Formate un float sans zéros parasites (suffisant pour des coords m).
		std::string Num(float v)
		{
			std::ostringstream ss;
			ss << v;
			return ss.str();
		}

		BuildingVariant ParseVariant(const engine::core::Config& c, const std::string& base)
		{
			BuildingVariant var;
			var.id          = c.GetString(base + "id", "");
			var.displayName = c.GetString(base + "displayName", var.id);
			const int np = static_cast<int>(c.GetInt(base + "parts.count", 0));
			var.parts.reserve(static_cast<size_t>(np > 0 ? np : 0));
			for (int p = 0; p < np; ++p)
			{
				const std::string pb = base + "parts." + std::to_string(p) + ".";
				BuildingPart part;
				part.gltfRelativePath = c.GetString(pb + "mesh", "");
				if (part.gltfRelativePath.empty()) continue;
				part.localPosition = {
					static_cast<float>(c.GetDouble(pb + "x", 0.0)),
					static_cast<float>(c.GetDouble(pb + "y", 0.0)),
					static_cast<float>(c.GetDouble(pb + "z", 0.0)) };
				part.localEulerDeg = {
					static_cast<float>(c.GetDouble(pb + "rx", 0.0)),
					static_cast<float>(c.GetDouble(pb + "ry", 0.0)),
					static_cast<float>(c.GetDouble(pb + "rz", 0.0)) };
				part.localScale      = static_cast<float>(c.GetDouble(pb + "scale", 1.0));
				part.solid           = c.GetBool(pb + "solid", true);
				part.collisionRadius = static_cast<float>(c.GetDouble(pb + "collision_radius", 0.0));
				var.parts.push_back(std::move(part));
			}
			return var;
		}
	}

	const BuildingTemplate* BuildingTemplateLibrary::FindType(const std::string& type) const
	{
		auto it = std::find_if(m_templates.begin(), m_templates.end(),
			[&type](const BuildingTemplate& t) { return t.type == type; });
		return (it == m_templates.end()) ? nullptr : &*it;
	}

	const BuildingVariant* BuildingTemplateLibrary::Resolve(const std::string& type,
		const std::string& variantId) const
	{
		const BuildingTemplate* t = FindType(type);
		return t ? t->FindVariant(variantId) : nullptr;
	}

	void BuildingTemplateLibrary::UpsertTemplate(BuildingTemplate tmpl)
	{
		auto it = std::find_if(m_templates.begin(), m_templates.end(),
			[&tmpl](const BuildingTemplate& t) { return t.type == tmpl.type; });
		if (it == m_templates.end()) m_templates.push_back(std::move(tmpl));
		else *it = std::move(tmpl);
	}

	bool BuildingTemplateLibrary::LoadTemplateFromJson(const std::string& jsonText,
		std::string& outError)
	{
		engine::core::Config c;
		if (!c.LoadFromString(jsonText))
		{
			outError = "BuildingTemplate: JSON invalide";
			return false;
		}
		BuildingTemplate t;
		t.type = c.GetString("type", "");
		if (t.type.empty())
		{
			outError = "BuildingTemplate: champ 'type' manquant";
			return false;
		}
		t.displayName = c.GetString("displayName", t.type);
		const int nv = static_cast<int>(c.GetInt("variants.count", 0));
		t.variants.reserve(static_cast<size_t>(nv > 0 ? nv : 0));
		for (int v = 0; v < nv; ++v)
		{
			const std::string base = "variants." + std::to_string(v) + ".";
			BuildingVariant var = ParseVariant(c, base);
			if (!var.id.empty()) t.variants.push_back(std::move(var));
		}
		UpsertTemplate(std::move(t));
		return true;
	}

	bool BuildingTemplateLibrary::LoadFromContent(const std::string& contentRoot,
		std::string& outError)
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::path(contentRoot) / "buildings" / "templates";
		std::error_code ec;
		if (!fs::exists(dir, ec))
		{
			// Dossier absent = bibliothèque vide, pas une erreur.
			return true;
		}
		bool anyError = false;
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() == ".json") files.push_back(entry.path());
		}
		// Ordre déterministe (les itérateurs de répertoire ne le garantissent pas).
		std::sort(files.begin(), files.end());
		for (const auto& path : files)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f.good()) continue;
			std::string json((std::istreambuf_iterator<char>(f)),
				std::istreambuf_iterator<char>());
			std::string err;
			if (!LoadTemplateFromJson(json, err))
			{
				anyError = true;
				outError += "[" + path.filename().string() + "] " + err + "; ";
			}
		}
		return !anyError;
	}

	bool BuildingTemplateLibrary::SaveVariant(const std::string& contentRoot,
		const std::string& type, const std::string& typeDisplayName,
		const BuildingVariant& variant, std::string& outError)
	{
		if (type.empty() || variant.id.empty())
		{
			outError = "SaveVariant: type et variant.id requis";
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path dir = fs::path(contentRoot) / "buildings" / "templates";
		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec)
		{
			outError = "SaveVariant: mkdir failed: " + ec.message();
			return false;
		}

		// Charge le type existant (s'il existe) pour upsert de la variante.
		const fs::path path = dir / (type + ".json");
		BuildingTemplate tmpl;
		tmpl.type = type;
		tmpl.displayName = typeDisplayName.empty() ? type : typeDisplayName;
		{
			std::ifstream f(path, std::ios::binary);
			if (f.good())
			{
				std::string json((std::istreambuf_iterator<char>(f)),
					std::istreambuf_iterator<char>());
				BuildingTemplateLibrary tmp;
				std::string err;
				if (tmp.LoadTemplateFromJson(json, err) && !tmp.m_templates.empty())
				{
					tmpl = tmp.m_templates.front();
					if (!typeDisplayName.empty()) tmpl.displayName = typeDisplayName;
				}
			}
		}
		// Upsert de la variante (par id).
		{
			auto it = std::find_if(tmpl.variants.begin(), tmpl.variants.end(),
				[&variant](const BuildingVariant& v) { return v.id == variant.id; });
			if (it == tmpl.variants.end()) tmpl.variants.push_back(variant);
			else *it = variant;
		}

		// Sérialise en JSON nesté lisible (format Config : count + indexé).
		std::ostringstream os;
		os << "{\n";
		os << "  \"type\": \"" << JsonEscape(tmpl.type) << "\",\n";
		os << "  \"displayName\": \"" << JsonEscape(tmpl.displayName) << "\",\n";
		os << "  \"variants\": {\n";
		os << "    \"count\": " << tmpl.variants.size() << ",\n";
		for (size_t vi = 0; vi < tmpl.variants.size(); ++vi)
		{
			const BuildingVariant& var = tmpl.variants[vi];
			os << "    \"" << vi << "\": {\n";
			os << "      \"id\": \"" << JsonEscape(var.id) << "\",\n";
			os << "      \"displayName\": \"" << JsonEscape(var.displayName) << "\",\n";
			os << "      \"parts\": {\n";
			os << "        \"count\": " << var.parts.size() << ",\n";
			for (size_t pi = 0; pi < var.parts.size(); ++pi)
			{
				const BuildingPart& pt = var.parts[pi];
				os << "        \"" << pi << "\": { "
				   << "\"mesh\": \"" << JsonEscape(pt.gltfRelativePath) << "\", "
				   << "\"x\": " << Num(pt.localPosition.x) << ", "
				   << "\"y\": " << Num(pt.localPosition.y) << ", "
				   << "\"z\": " << Num(pt.localPosition.z) << ", "
				   << "\"rx\": " << Num(pt.localEulerDeg.x) << ", "
				   << "\"ry\": " << Num(pt.localEulerDeg.y) << ", "
				   << "\"rz\": " << Num(pt.localEulerDeg.z) << ", "
				   << "\"scale\": " << Num(pt.localScale) << ", "
				   << "\"solid\": " << (pt.solid ? "true" : "false") << ", "
				   << "\"collision_radius\": " << Num(pt.collisionRadius)
				   << " }" << (pi + 1 < var.parts.size() ? "," : "") << "\n";
			}
			os << "      }\n";
			os << "    }" << (vi + 1 < tmpl.variants.size() ? "," : "") << "\n";
		}
		os << "  }\n";
		os << "}\n";

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.good())
		{
			outError = "SaveVariant: cannot open " + path.string();
			return false;
		}
		const std::string text = os.str();
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!out.good())
		{
			outError = "SaveVariant: write failed";
			return false;
		}

		// Reflète en mémoire.
		UpsertTemplate(std::move(tmpl));
		return true;
	}
}
