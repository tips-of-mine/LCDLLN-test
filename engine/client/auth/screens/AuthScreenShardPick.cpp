#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <string>

namespace engine::client
{
#if defined(_WIN32)

	void AuthUiPresenter::ImGuiSetShardPickChoiceShardId(uint32_t shardId)
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		m_shardPickChoiceShardId = shardId;
	}

	void AuthUiPresenter::ImGuiSubmitShardPick(const engine::core::Config& cfg)
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		SubmitCurrentPhase(cfg);
	}

	void AuthUiPresenter::ImGuiBackFromShardPickToLogin()
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: ShardPick -> Login");
		m_userErrorText.clear();
		SetPhase(Phase::Login);
		m_shardPickChoiceShardId = 0;
		m_shardPickEntries.clear();
	}

	void AuthUiPresenter::BuildModel_ShardPick(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.phase.shard_pick");
		{
			RenderBodyLine hint{};
			hint.text = Tr("auth.shard_pick.hint");
			model.bodyLines.push_back(std::move(hint));
		}
		for (const auto& e : m_shardPickEntries)
		{
			RenderBodyLine line{};
			line.text = Tr("auth.shard_pick.line",
				{ { "id", std::to_string(e.shard_id) },
					{ "load", std::to_string(e.current_load) + "/" + std::to_string(e.max_capacity) },
					{ "endpoint", e.endpoint.empty() ? std::string("-") : e.endpoint } });
			const bool rowSelectable = (e.status == 1u && !e.endpoint.empty());
			line.active = (m_shardPickChoiceShardId == e.shard_id);
			line.link = rowSelectable;
			model.bodyLines.push_back(std::move(line));
		}
		{
			RenderAction submit{};
			submit.labelKey = "common.submit";
			submit.primary = true;
			submit.active = (m_shardPickChoiceShardId != 0u);
			submit.emphasized = false;
			submit.hovered = (m_hoveredActionIndex == 0);
			model.actions.push_back(std::move(submit));
		}
		{
			RenderAction back{};
			back.labelKey = "common.back";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = (m_hoveredActionIndex == 1);
			model.actions.push_back(std::move(back));
		}
	}

	void AuthUiPresenter::Update_ShardPick(engine::platform::Input& input, const engine::core::Config& cfg,
		engine::platform::Window& window, bool usingNativeAuth, bool authUiImguiMode)
	{
		(void)window;
		if (usingNativeAuth || m_phase != Phase::ShardPick)
		{
			return;
		}
		const auto& entries = m_shardPickEntries;
		auto countEligible = [&entries]() -> uint32_t {
			uint32_t n = 0;
			for (const auto& e : entries)
			{
				if (e.status == 1u && !e.endpoint.empty())
				{
					++n;
				}
			}
			return n;
		};
		const uint32_t nElig = countEligible();
		if (nElig > 0u && (input.WasPressed(engine::platform::Key::Up) || input.WasPressed(engine::platform::Key::Left)))
		{
			uint32_t idx = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (e.shard_id == m_shardPickChoiceShardId)
				{
					break;
				}
				++idx;
			}
			idx = (idx == 0u) ? (nElig - 1u) : (idx - 1u);
			uint32_t j = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (j == idx)
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
				++j;
			}
		}
		if (nElig > 0u && (input.WasPressed(engine::platform::Key::Down) || input.WasPressed(engine::platform::Key::Right)))
		{
			uint32_t idx = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (e.shard_id == m_shardPickChoiceShardId)
				{
					break;
				}
				++idx;
			}
			idx = (idx + 1u) % nElig;
			uint32_t j = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (j == idx)
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
				++j;
			}
		}
		if (authUiImguiMode && m_shardPickChoiceShardId != 0u && input.WasPressed(engine::platform::Key::Enter))
		{
			ImGuiSubmitShardPick(cfg);
		}
	}

#else

	void AuthUiPresenter::BuildModel_ShardPick(RenderModel&) const {}

	void AuthUiPresenter::Update_ShardPick(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

#endif
} // namespace engine::client
