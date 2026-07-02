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

	namespace
	{
		/// Construit un DialogueTree (NON normalisé) depuis une config à clés plates,
		/// l'arbre étant enraciné au préfixe \p treeBase (ex. "" pour un fichier de
		/// dialogue dédié dont la racine est `{ start, nodes }`).
		/// \param cfg config déjà chargée (fichier dialogue ou config monde).
		DialogueTree BuildTreeFromConfig(const engine::core::Config& cfg, const std::string& treeBase)
		{
			DialogueTree tree;
			tree.startNodeId = cfg.GetString(treeBase + "start", "");

			const int nodeCount = static_cast<int>(cfg.GetInt(treeBase + "nodes.count", 0));
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
					choice.questKey   = cfg.GetString(cb + "questKey", ""); // SP2 : id texte pour QuestAccept/TurnInRequest.
					choice.icon       = cfg.GetString(cb + "icon", "");
					node.choices.push_back(std::move(choice));
				}

				tree.nodes.push_back(std::move(node));
			}
			return tree;
		}
	} // namespace

	DialogueTree LoadDialogueTree(const engine::core::Config& cfg,
	                              const std::string& base,
	                              const std::vector<std::string>& legacyLines)
	{
		// Les dialogues vivent dans des fichiers dédiés : un par PNJ/zone, sous
		// `<paths.content>/dialogues/<id>.json`. La config monde ne porte qu'une
		// référence `world.interactables.<i>.dialogue_id`. À défaut d'id (ou si le
		// fichier est introuvable / invalide), on retombe sur l'arbre legacy
		// construit depuis l'ancien champ `dialogue` (\ref legacyLines).
		const std::string dialogueId = cfg.GetString(base + "dialogue_id", "");
		if (dialogueId.empty())
			return BuildTreeFromLegacyLines(legacyLines);

		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::string fullPath = contentRoot + "/dialogues/" + dialogueId + ".json";

		engine::core::Config fileCfg;
		if (!fileCfg.LoadFromFile(fullPath))
		{
			LOG_WARN(Core, "[DialogueConfigLoader] fichier dialogue introuvable '{}' (id '{}') — fallback legacy",
			         fullPath, dialogueId);
			return BuildTreeFromLegacyLines(legacyLines);
		}

		// Le fichier dédié a l'arbre à sa racine : { "start": ..., "nodes": { ... } }.
		DialogueTree tree = BuildTreeFromConfig(fileCfg, "");

		const DialogueValidationResult vr = NormalizeDialogueTree(tree);
		if (!vr.ok)
		{
			for (const std::string& err : vr.errors)
				LOG_WARN(Core, "[DialogueConfigLoader] dialogue '{}' : {}", dialogueId, err);
			LOG_WARN(Core, "[DialogueConfigLoader] arbre invalide '{}' — fallback legacy", dialogueId);
			return BuildTreeFromLegacyLines(legacyLines);
		}
		return tree;
	}

} // namespace engine::client
