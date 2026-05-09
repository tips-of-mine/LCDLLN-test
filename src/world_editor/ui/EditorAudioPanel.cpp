#include "src/world_editor/EditorAudioPanel.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor
{
	namespace
	{
		/// Helper indexed-key accès, mirror de l'usage dans AudioEngine.cpp.
		bool HasIndexedKey(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field)
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field;
			// GetString avec sentinelle distincte ; si la clé existe (même vide) Config la
			// retournera. Sinon la sentinelle sort.
			static const std::string kSentinel = "\x01__missing__\x01";
			return cfg.GetString(key, kSentinel) != kSentinel;
		}

		std::string GetIndexed(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field, std::string_view fallback = {})
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field;
			return cfg.GetString(key, fallback);
		}

		double GetIndexedDouble(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field, double fallback)
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field;
			return cfg.GetDouble(key, fallback);
		}

		int64_t GetIndexedInt(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field, int64_t fallback)
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field;
			return cfg.GetInt(key, fallback);
		}

		bool GetIndexedBool(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field, bool fallback)
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field;
			return cfg.GetBool(key, fallback);
		}

		float GetIndexedVec3Component(const engine::core::Config& cfg, const char* arrayName, size_t index, const char* field, size_t comp)
		{
			std::string key = std::string(arrayName) + "[" + std::to_string(index) + "]." + field + "[" + std::to_string(comp) + "]";
			return static_cast<float>(cfg.GetDouble(key, 0.0));
		}

		/// Échappe les caractères JSON spéciaux (mêmes règles que M44.4).
		void AppendJsonEscaped(std::string& out, std::string_view s)
		{
			for (char c : s)
			{
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:   out += c;      break;
				}
			}
		}
	}

	bool EditorAudioPanel::Load(const engine::core::Config& cfg)
	{
		m_buses.clear();
		m_sounds.clear();
		m_zones.clear();
		m_dirty = false;
		m_loaded = false;

		const std::string relPath = cfg.GetString("audio.zone_audio_path", "audio/zone_audio.json");
		const std::filesystem::path full = engine::platform::FileSystem::ResolveContentPath(cfg, relPath);
		m_resolvedPath = full.string();

		engine::core::Config audioCfg;
		if (!audioCfg.LoadFromFile(m_resolvedPath))
		{
			LOG_WARN(Core, "[EditorAudio] Load failed (missing or invalid JSON): {}", m_resolvedPath);
			m_loaded = true; // permet l'édition même fichier absent
			return false;
		}

		for (size_t i = 0; HasIndexedKey(audioCfg, "buses", i, "id"); ++i)
		{
			Bus b;
			b.id     = GetIndexed(audioCfg, "buses", i, "id");
			b.volume = static_cast<float>(GetIndexedDouble(audioCfg, "buses", i, "volume", 1.0));
			m_buses.push_back(std::move(b));
		}
		for (size_t i = 0; HasIndexedKey(audioCfg, "sounds", i, "id"); ++i)
		{
			Sound s;
			s.id          = GetIndexed(audioCfg, "sounds", i, "id");
			s.path        = GetIndexed(audioCfg, "sounds", i, "path");
			s.busId       = GetIndexed(audioCfg, "sounds", i, "bus");
			s.minDistance = static_cast<float>(GetIndexedDouble(audioCfg, "sounds", i, "minDistance", 1.0));
			s.maxDistance = static_cast<float>(GetIndexedDouble(audioCfg, "sounds", i, "maxDistance", 25.0));
			s.loop        = GetIndexedBool(audioCfg, "sounds", i, "loop", false);
			m_sounds.push_back(std::move(s));
		}
		for (size_t i = 0; HasIndexedKey(audioCfg, "zones", i, "zoneId"); ++i)
		{
			Zone z;
			z.zoneId      = static_cast<uint32_t>(GetIndexedInt(audioCfg, "zones", i, "zoneId", 0));
			z.soundId     = GetIndexed(audioCfg, "zones", i, "ambienceSoundId");
			z.position[0] = GetIndexedVec3Component(audioCfg, "zones", i, "position", 0);
			z.position[1] = GetIndexedVec3Component(audioCfg, "zones", i, "position", 1);
			z.position[2] = GetIndexedVec3Component(audioCfg, "zones", i, "position", 2);
			m_zones.push_back(std::move(z));
		}

		LOG_INFO(Core, "[EditorAudio] Loaded {} buses, {} sounds, {} zones from {}",
			m_buses.size(), m_sounds.size(), m_zones.size(), m_resolvedPath);
		m_loaded = true;
		return true;
	}

	bool EditorAudioPanel::Save(const engine::core::Config& cfg)
	{
		if (m_resolvedPath.empty())
		{
			const std::string relPath = cfg.GetString("audio.zone_audio_path", "audio/zone_audio.json");
			m_resolvedPath = engine::platform::FileSystem::ResolveContentPath(cfg, relPath).string();
		}

		std::ostringstream ss;
		ss << "{\n  \"buses\": [\n";
		for (size_t i = 0; i < m_buses.size(); ++i)
		{
			ss << "    { \"id\": \"";
			std::string esc; AppendJsonEscaped(esc, m_buses[i].id); ss << esc;
			ss << "\", \"volume\": " << m_buses[i].volume << " }";
			if (i + 1 < m_buses.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n  \"sounds\": [\n";
		for (size_t i = 0; i < m_sounds.size(); ++i)
		{
			std::string idEsc, pathEsc, busEsc;
			AppendJsonEscaped(idEsc, m_sounds[i].id);
			AppendJsonEscaped(pathEsc, m_sounds[i].path);
			AppendJsonEscaped(busEsc, m_sounds[i].busId);
			ss << "    {\n";
			ss << "      \"id\": \"" << idEsc << "\",\n";
			ss << "      \"path\": \"" << pathEsc << "\",\n";
			ss << "      \"bus\": \"" << busEsc << "\",\n";
			ss << "      \"minDistance\": " << m_sounds[i].minDistance << ",\n";
			ss << "      \"maxDistance\": " << m_sounds[i].maxDistance << ",\n";
			ss << "      \"loop\": " << (m_sounds[i].loop ? "true" : "false") << "\n";
			ss << "    }";
			if (i + 1 < m_sounds.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ],\n  \"zones\": [\n";
		for (size_t i = 0; i < m_zones.size(); ++i)
		{
			std::string soundEsc;
			AppendJsonEscaped(soundEsc, m_zones[i].soundId);
			ss << "    { \"zoneId\": " << m_zones[i].zoneId
			   << ", \"ambienceSoundId\": \"" << soundEsc << "\""
			   << ", \"position\": [" << m_zones[i].position[0] << ", " << m_zones[i].position[1] << ", " << m_zones[i].position[2] << "] }";
			if (i + 1 < m_zones.size()) ss << ",";
			ss << "\n";
		}
		ss << "  ]\n}\n";

		std::ofstream out(m_resolvedPath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			LOG_ERROR(Core, "[EditorAudio] Save failed: cannot open {}", m_resolvedPath);
			return false;
		}
		out << ss.str();
		out.close();
		LOG_INFO(Core, "[EditorAudio] Saved {} buses, {} sounds, {} zones to {}",
			m_buses.size(), m_sounds.size(), m_zones.size(), m_resolvedPath);
		m_dirty = false;
		return true;
	}

#if defined(_WIN32)
	namespace
	{
		void DrawTextEdit(const char* label, std::string& s, float widgetWidth = 200.f)
		{
			char buf[256]{};
			const size_t n = (s.size() < sizeof(buf) - 1u) ? s.size() : (sizeof(buf) - 1u);
			std::memcpy(buf, s.data(), n);
			buf[n] = '\0';
			ImGui::SetNextItemWidth(widgetWidth);
			if (ImGui::InputText(label, buf, sizeof(buf)))
				s.assign(buf);
		}
	}

	void EditorAudioPanel::Draw()
	{
		if (ImGui::BeginTabBar("##audio_panel_tabs"))
		{
			if (ImGui::BeginTabItem("Buses"))
			{
				DrawBusesTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Sounds"))
			{
				DrawSoundsTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Zones"))
			{
				DrawZonesTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		if (m_dirty)
			ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "*modified — Ctrl+S to save*");
		else
			ImGui::TextDisabled("(saved)");
	}

	void EditorAudioPanel::DrawBusesTab()
	{
		if (ImGui::Button("[+] Add Bus"))
		{
			m_buses.push_back({ "new_bus", 1.0f });
			m_dirty = true;
		}
		ImGui::Separator();
		int toRemove = -1;
		for (size_t i = 0; i < m_buses.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			DrawTextEdit("##id", m_buses[i].id, 180.f);
			if (ImGui::IsItemEdited()) m_dirty = true;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(180.f);
			if (ImGui::SliderFloat("##volume", &m_buses[i].volume, 0.0f, 1.0f, "vol=%.2f"))
				m_dirty = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("X##rm"))
				toRemove = static_cast<int>(i);
			ImGui::PopID();
		}
		if (toRemove >= 0)
		{
			m_buses.erase(m_buses.begin() + toRemove);
			m_dirty = true;
		}
	}

	void EditorAudioPanel::DrawSoundsTab()
	{
		if (ImGui::Button("[+] Add Sound"))
		{
			m_sounds.push_back({ "new_sound", "audio/new_sound.wav", m_buses.empty() ? "" : m_buses[0].id, 1.0f, 25.0f, false });
			m_dirty = true;
		}
		ImGui::Separator();
		int toRemove = -1;
		for (size_t i = 0; i < m_sounds.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			DrawTextEdit("id##sid", m_sounds[i].id, 180.f);
			if (ImGui::IsItemEdited()) m_dirty = true;
			ImGui::SameLine();
			DrawTextEdit("path##spath", m_sounds[i].path, 240.f);
			if (ImGui::IsItemEdited()) m_dirty = true;

			// Bus combo : liste les buses disponibles + l'option de saisie libre.
			ImGui::SetNextItemWidth(140.f);
			if (ImGui::BeginCombo("bus##sbus", m_sounds[i].busId.c_str()))
			{
				for (const auto& b : m_buses)
				{
					const bool selected = (b.id == m_sounds[i].busId);
					if (ImGui::Selectable(b.id.c_str(), selected))
					{
						if (b.id != m_sounds[i].busId)
						{
							m_sounds[i].busId = b.id;
							m_dirty = true;
						}
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SetNextItemWidth(80.f);
			if (ImGui::InputFloat("min##smin", &m_sounds[i].minDistance, 0.0f, 0.0f, "%.1f"))
				m_dirty = true;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.f);
			if (ImGui::InputFloat("max##smax", &m_sounds[i].maxDistance, 0.0f, 0.0f, "%.1f"))
				m_dirty = true;
			ImGui::SameLine();
			if (ImGui::Checkbox("loop##sloop", &m_sounds[i].loop))
				m_dirty = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("X##rm"))
				toRemove = static_cast<int>(i);
			ImGui::PopID();
		}
		if (toRemove >= 0)
		{
			m_sounds.erase(m_sounds.begin() + toRemove);
			m_dirty = true;
		}
	}

	void EditorAudioPanel::DrawZonesTab()
	{
		if (ImGui::Button("[+] Add Zone"))
		{
			m_zones.push_back({ 0u, m_sounds.empty() ? "" : m_sounds[0].id, { 0.f, 0.f, 0.f } });
			m_dirty = true;
		}
		ImGui::Separator();
		int toRemove = -1;
		for (size_t i = 0; i < m_zones.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::SetNextItemWidth(80.f);
			int zid = static_cast<int>(m_zones[i].zoneId);
			if (ImGui::InputInt("zoneId##zid", &zid, 0, 0))
			{
				m_zones[i].zoneId = static_cast<uint32_t>(std::max(0, zid));
				m_dirty = true;
			}
			ImGui::SameLine();

			// Sound combo
			ImGui::SetNextItemWidth(180.f);
			if (ImGui::BeginCombo("sound##zs", m_zones[i].soundId.c_str()))
			{
				for (const auto& s : m_sounds)
				{
					if (ImGui::Selectable(s.id.c_str(), s.id == m_zones[i].soundId))
					{
						if (s.id != m_zones[i].soundId)
						{
							m_zones[i].soundId = s.id;
							m_dirty = true;
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.f);
			if (ImGui::InputFloat3("pos##zpos", m_zones[i].position, "%.2f"))
				m_dirty = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("X##rm"))
				toRemove = static_cast<int>(i);
			ImGui::PopID();
		}
		if (toRemove >= 0)
		{
			m_zones.erase(m_zones.begin() + toRemove);
			m_dirty = true;
		}
	}

#else // !_WIN32

	void EditorAudioPanel::Draw()             {}
	void EditorAudioPanel::DrawBusesTab()     {}
	void EditorAudioPanel::DrawSoundsTab()    {}
	void EditorAudioPanel::DrawZonesTab()     {}

#endif
}
