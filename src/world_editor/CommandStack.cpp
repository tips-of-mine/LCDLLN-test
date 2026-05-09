#include "src/world_editor/world/CommandStack.h"

#include <chrono>
#include <utility>

namespace engine::editor::world
{
	namespace
	{
		/// Renvoie le timestamp courant en millisecondes (steady_clock pour
		/// éviter les sauts liés aux changements d'heure système). Utilisé pour
		/// le panneau History (affichage relatif). Hors hot-path : appelé une
		/// fois par Push.
		uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(
				duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
		}
	}

	/// Applique simplement la config. N'évince pas rétroactivement (les
	/// évictions auront lieu au prochain Push si la pile dépasse les limites).
	void CommandStack::Configure(const CommandStackConfig& cfg)
	{
		m_cfg = cfg;
	}

	/// Exécute `cmd`, vide la pile redo (nouvelle branche timeline), tente le
	/// coalescing avec la commande au sommet si `mergeKey` non-nul et
	/// identique, puis empile et évince les plus anciennes tant que la
	/// capacité ou la mémoire dépasse. Effet de bord : prend ownership de cmd
	/// (sauf en cas de coalesce où il est libéré par unique_ptr en sortie de
	/// scope).
	void CommandStack::Push(std::unique_ptr<ICommand> cmd)
	{
		if (!cmd) return;

		cmd->Execute();
		m_redo.clear();
		m_redoTimestamps.clear();

		// Coalescing si mergeKey non-nul et identique à la commande au sommet.
		if (!m_undo.empty() && cmd->GetMergeKey() != 0 &&
			m_undo.back()->GetMergeKey() == cmd->GetMergeKey())
		{
			if (m_undo.back()->TryMerge(*cmd))
			{
				// La commande au sommet a absorbé. Recompute totalBytes (la
				// commande au sommet peut avoir grandi via TryMerge).
				m_totalBytes = 0;
				for (auto& c : m_undo) m_totalBytes += c->GetMemoryFootprint();
				// Mettre à jour le timestamp du sommet pour refléter la
				// dernière interaction (utile pour l'affichage History).
				if (!m_undoTimestamps.empty())
					m_undoTimestamps.back() = NowMs();
				return;
			}
		}

		m_totalBytes += cmd->GetMemoryFootprint();
		m_undo.push_back(std::move(cmd));
		m_undoTimestamps.push_back(NowMs());

		while (!m_undo.empty() && (m_undo.size() > m_cfg.capacity || m_totalBytes > m_cfg.maxBytes))
		{
			EvictOldest();
		}
	}

	/// Retourne true si la pile undo n'est pas vide.
	bool CommandStack::CanUndo() const { return !m_undo.empty(); }

	/// Retourne true si la pile redo n'est pas vide.
	bool CommandStack::CanRedo() const { return !m_redo.empty(); }

	/// Dépile le sommet de la pile undo, appelle son Undo(), recalcule
	/// `m_totalBytes`, et le pousse sur la pile redo. No-op si pile vide.
	void CommandStack::Undo()
	{
		if (m_undo.empty()) return;
		auto cmd = std::move(m_undo.back());
		m_undo.pop_back();
		const uint64_t ts = m_undoTimestamps.back();
		m_undoTimestamps.pop_back();
		cmd->Undo();
		const size_t fp = cmd->GetMemoryFootprint();
		if (m_totalBytes >= fp) m_totalBytes -= fp; else m_totalBytes = 0;
		m_redo.push_back(std::move(cmd));
		m_redoTimestamps.push_back(ts);
	}

	/// Symétrique de Undo : dépile le sommet de redo, appelle son Execute(),
	/// le repousse sur la pile undo, met à jour `m_totalBytes`. No-op si vide.
	void CommandStack::Redo()
	{
		if (m_redo.empty()) return;
		auto cmd = std::move(m_redo.back());
		m_redo.pop_back();
		const uint64_t ts = m_redoTimestamps.back();
		m_redoTimestamps.pop_back();
		cmd->Execute();
		m_totalBytes += cmd->GetMemoryFootprint();
		m_undo.push_back(std::move(cmd));
		m_undoTimestamps.push_back(ts);
	}

	/// Annule en cascade tant que `m_undo.size() > targetIndex && CanUndo()`.
	/// Si `targetIndex >= m_undo.size()` au moment de l'appel, no-op.
	void CommandStack::RewindTo(size_t targetIndex)
	{
		while (m_undo.size() > targetIndex && CanUndo())
		{
			Undo();
		}
	}

	/// Vide les deux piles (undo et redo) et remet `m_totalBytes` à 0. Les
	/// destructeurs des commandes sont appelés par les `clear()`.
	void CommandStack::Clear()
	{
		m_undo.clear();
		m_redo.clear();
		m_undoTimestamps.clear();
		m_redoTimestamps.clear();
		m_totalBytes = 0;
	}

	/// Tailles des piles et empreinte mémoire totale.
	size_t CommandStack::UndoSize() const { return m_undo.size(); }
	size_t CommandStack::RedoSize() const { return m_redo.size(); }
	size_t CommandStack::TotalBytes() const { return m_totalBytes; }

	/// Snapshot de la pile undo (ordre chronologique : index 0 = plus ancien)
	/// pour le panneau History. Copie strings + bytes + timestamps.
	std::vector<CommandStack::HistoryEntry> CommandStack::SnapshotHistory() const
	{
		std::vector<HistoryEntry> out;
		out.reserve(m_undo.size());
		for (size_t i = 0; i < m_undo.size(); ++i)
		{
			HistoryEntry e;
			e.label = m_undo[i]->GetLabel();
			e.bytes = m_undo[i]->GetMemoryFootprint();
			e.timestampMs = (i < m_undoTimestamps.size()) ? m_undoTimestamps[i] : 0ull;
			out.push_back(std::move(e));
		}
		return out;
	}

	/// Évince la commande la plus ancienne (front) si la pile n'est pas vide.
	/// Met à jour `m_totalBytes`. Appelée en boucle par `Push` jusqu'à respect
	/// des limites de capacité et mémoire.
	void CommandStack::EvictOldest()
	{
		if (m_undo.empty()) return;
		const size_t fp = m_undo.front()->GetMemoryFootprint();
		if (m_totalBytes >= fp) m_totalBytes -= fp; else m_totalBytes = 0;
		m_undo.erase(m_undo.begin());
		if (!m_undoTimestamps.empty()) m_undoTimestamps.erase(m_undoTimestamps.begin());
	}
}
