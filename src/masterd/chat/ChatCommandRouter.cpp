#include "src/masterd/chat/ChatCommandRouter.h"
#include "src/masterd/account/AccountRoleService.h"

#include <cctype>

namespace engine::server::chat
{
	std::string ChatCommandRouter::Normalize(std::string_view name)
	{
		std::string out;
		out.reserve(name.size());
		size_t i = 0;
		// Strip leading '/' (au moins un, au plus un — '/'+'/' devient '/'+'rest')
		if (i < name.size() && name[i] == '/')
			++i;
		for (; i < name.size(); ++i)
		{
			const auto c = static_cast<unsigned char>(name[i]);
			if (c >= 'A' && c <= 'Z')
				out.push_back(static_cast<char>(c + ('a' - 'A')));
			else
				out.push_back(static_cast<char>(c));
		}
		return out;
	}

	void ChatCommandRouter::Register(std::string_view name, AccountRole minRole, CommandHandlerFn handler)
	{
		const auto key = Normalize(name);
		if (key.empty())
			return;
		m_table[key] = CommandEntry{minRole, std::move(handler)};
	}

	void ChatCommandRouter::Unregister(std::string_view name)
	{
		const auto key = Normalize(name);
		if (key.empty())
			return;
		m_table.erase(key);
	}

	bool ChatCommandRouter::IsRegistered(std::string_view name) const
	{
		return m_table.find(Normalize(name)) != m_table.end();
	}

	CommandDispatchResult ChatCommandRouter::Dispatch(std::string_view text,
		uint64_t accountId, AccountRole role, std::string* outName) const
	{
		// Doit commencer par '/' — sinon pas une commande, le caller
		// continue le routage normal.
		if (text.empty() || text.front() != '/')
			return CommandDispatchResult::NotACommand;

		// Skip leading '/'. Si juste '/' tout seul → unknown.
		text.remove_prefix(1);
		if (text.empty())
			return CommandDispatchResult::UnknownCommand;

		// Sépare nom et args. Le premier "mot" (jusqu'au premier espace)
		// est le nom ; le reste est les args. ASCII space + tab.
		size_t end = 0;
		while (end < text.size() && text[end] != ' ' && text[end] != '\t')
			++end;

		const std::string_view rawName = text.substr(0, end);
		std::string_view args = (end < text.size()) ? text.substr(end + 1) : std::string_view{};

		// Trim leading whitespace from args.
		while (!args.empty() && (args.front() == ' ' || args.front() == '\t'))
			args.remove_prefix(1);
		while (!args.empty() && (args.back() == ' ' || args.back() == '\t'))
			args.remove_suffix(1);

		const auto key = Normalize(rawName);
		if (outName)
			*outName = key;

		auto it = m_table.find(key);
		if (it == m_table.end())
			return CommandDispatchResult::UnknownCommand;

		// Check rôle.
		if (!engine::server::AccountRoleService::RequireMinRole(role, it->second.minRole))
			return CommandDispatchResult::InsufficientRole;

		// Dispatch.
		if (it->second.handler)
			it->second.handler(accountId, args);
		return CommandDispatchResult::Dispatched;
	}
}
