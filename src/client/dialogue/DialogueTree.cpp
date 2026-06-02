#include "src/client/dialogue/DialogueTree.h"

namespace engine::client
{
	const DialogueNode* DialogueTree::FindNode(const std::string& id) const
	{
		for (const DialogueNode& n : nodes)
			if (n.id == id)
				return &n;
		return nullptr;
	}

	DialogueTree BuildTreeFromLegacyLines(const std::vector<std::string>& lines)
	{
		DialogueTree tree;
		DialogueNode node;
		node.id = "legacy";
		for (const std::string& l : lines)
			node.lines.push_back(DialogueLine{l, false});

		DialogueChoice bye;
		bye.text   = "Au revoir.";
		bye.action = DialogueAction::End;
		node.choices.push_back(bye);

		tree.startNodeId = node.id;
		tree.nodes.push_back(std::move(node));
		return tree;
	}

	DialogueValidationResult NormalizeDialogueTree(DialogueTree& tree)
	{
		DialogueValidationResult res;

		if (tree.nodes.empty())
		{
			res.ok = false;
			res.errors.push_back("arbre de dialogue sans nœud");
			return res;
		}

		if (tree.startNodeId.empty())
			tree.startNodeId = tree.nodes.front().id;

		if (tree.FindNode(tree.startNodeId) == nullptr)
		{
			res.ok = false;
			res.errors.push_back("startNodeId introuvable: " + tree.startNodeId);
		}

		for (DialogueNode& n : tree.nodes)
		{
			if (n.choices.empty() || n.choices.size() > 5)
			{
				res.ok = false;
				res.errors.push_back("nœud '" + n.id + "': nombre de choix invalide (attendu 1..5)");
			}

			for (DialogueChoice& c : n.choices)
			{
				if (c.action == DialogueAction::Continue)
				{
					if (c.nextNodeId.empty() || tree.FindNode(c.nextNodeId) == nullptr)
					{
						res.ok = false;
						res.errors.push_back("nœud '" + n.id + "': choix Continue vers nœud absent '"
						                     + c.nextNodeId + "'");
					}
				}
			}
		}
		return res;
	}

} // namespace engine::client
