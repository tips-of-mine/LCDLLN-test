#include "src/client/dialogue/DialogueConfigLoader.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

namespace engine::client
{
	DialogueAction ParseDialogueAction(const std::string& s)
	{
		if (s == "accept_quest")   return DialogueAction::AcceptQuest;
		if (s == "complete_quest") return DialogueAction::CompleteQuest;
		if (s == "end")            return DialogueAction::End;
		return DialogueAction::Continue;
	}

	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines)
	{
		const std::string treeBase = base + "dialogue_tree.";
		const int nodeCount = static_cast<int>(cfg.GetInt(treeBase + "nodes.count", 0));

		if (nodeCount <= 0)
			return BuildTreeFromLegacyLines(legacyLines); // pas d'arbre => legacy

		DialogueTree tree;
		tree.startNodeId = cfg.GetString(treeBase + "start", "");

		for (int n = 0; n < nodeCount; ++n)
		{
			const std::string nb = treeBase + "nodes." + std::to_string(n) + ".";
			DialogueNode node;
			node.id = cfg.GetString(nb + "id", "");

			const int lineCount = static_cast<int>(cfg.GetInt(nb + "lines.count", 0));
			for (int l = 0; l < lineCount; ++l)
			{
				const std::string lb = nb + "lines." + std::to_string(l) + ".";
				DialogueLine line;
				line.text  = cfg.GetString(lb + "text", "");
				line.isCue = cfg.GetBool(lb + "cue", false);
				node.lines.push_back(std::move(line));
			}

			const int choiceCount = static_cast<int>(cfg.GetInt(nb + "choices.count", 0));
			for (int c = 0; c < choiceCount; ++c)
			{
				const std::string cb = nb + "choices." + std::to_string(c) + ".";
				DialogueChoice choice;
				choice.text       = cfg.GetString(cb + "text", "");
				choice.nextNodeId = cfg.GetString(cb + "next", "");
				choice.action     = ParseDialogueAction(cfg.GetString(cb + "action", "continue"));
				choice.questId    = static_cast<int>(cfg.GetInt(cb + "questId", -1));
				choice.icon       = cfg.GetString(cb + "icon", "");
				node.choices.push_back(std::move(choice));
			}

			tree.nodes.push_back(std::move(node));
		}

		const DialogueValidationResult vr = NormalizeDialogueTree(tree);
		if (!vr.ok)
		{
			for (const std::string& err : vr.errors)
				LOG_WARN(Core, "[DialogueConfigLoader] {}{} : {}", base, "dialogue_tree", err);
			LOG_WARN(Core, "[DialogueConfigLoader] arbre invalide pour '{}' — fallback legacy", base);
			return BuildTreeFromLegacyLines(legacyLines);
		}
		return tree;
	}

} // namespace engine::client
