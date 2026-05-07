#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::editor::world
{
	/// Identifiant de fusion (coalescing) des commandes consécutives. La valeur
	/// `0` indique « pas de fusion » : deux commandes consécutives avec la même
	/// clé non-nulle voient le `Push` tenter `TryMerge` sur la commande au
	/// sommet de la pile undo. Sert à coalescer les commandes haute fréquence
	/// (ex. tous les ticks d'un même brushstroke partagent le même mergeKey).
	using CommandMergeKey = uint64_t;

	/// Toute opération d'édition réversible passée à `CommandStack` implémente
	/// cette interface. M100.2 introduit l'interface ; les outils concrets
	/// (sculpt, splat, placement…) la peupleront dans les tickets aval.
	///
	/// Contraintes thread/timing : `Execute()` et `Undo()` sont appelés depuis
	/// le main thread (où vivent l'état ImGui et les ressources GPU éditées).
	class ICommand
	{
	public:
		virtual ~ICommand() = default;

		/// Description courte affichée dans le panneau History (ex. "Sculpt
		/// brush stroke", "Place tree"). La chaîne est référencée telle quelle
		/// par le panneau ; durée de vie attendue : au moins celle de la
		/// commande elle-même.
		virtual const char* GetLabel() const = 0;

		/// Empreinte mémoire approximative (octets), utilisée par la politique
		/// d'éviction de `CommandStack` (`maxBytes`). Doit être stable entre
		/// `Execute` et `Undo` (la pile recompute le total au fil des
		/// opérations).
		virtual size_t GetMemoryFootprint() const = 0;

		/// Clé de fusion : voir `CommandMergeKey`. Retourne `0` par défaut
		/// (pas de fusion). Les commandes coalesçables (brushstroke continu)
		/// renvoient une clé non-nulle stable pour la durée du geste.
		virtual CommandMergeKey GetMergeKey() const { return 0; }

		/// Applique la commande. Appelé une fois à `Push`, puis à chaque
		/// `Redo`. Effet de bord : modifie l'état du document (terrain, scene
		/// graph, atlas…) et éventuellement les ressources GPU sous-jacentes.
		virtual void Execute() = 0;

		/// Inverse de `Execute`. Doit ramener l'état du document à ce qu'il
		/// était juste avant `Execute`. Précondition : `Execute` a été appelé
		/// au moins une fois et l'état correspond au "post-Execute".
		virtual void Undo() = 0;

		/// Tente de fusionner `other` dans `*this`. Si retourne `true`, `other`
		/// sera détruit par `CommandStack::Push` (la commande au sommet
		/// absorbe). Retourne `false` par défaut (pas de fusion).
		/// \param other Commande candidate à fusionner ; on lit son état
		/// interne (delta, brush footprint…) sans transférer son ownership.
		virtual bool TryMerge(const ICommand& other) { (void)other; return false; }
	};

	/// Paramètres de la pile undo/redo, lus depuis `config.json` au démarrage
	/// du shell éditeur monde (voir `WorldEditorShell::Init`).
	struct CommandStackConfig
	{
		/// Nombre maximum de commandes dans la pile undo. Au-delà, la plus
		/// ancienne est évincée. Défaut 256.
		size_t capacity = 256;
		/// Taille mémoire totale maximum (octets) de toutes les commandes de
		/// la pile undo. Au-delà, la plus ancienne est évincée. Défaut 256 MiB.
		size_t maxBytes = 256ull * 1024ull * 1024ull;
	};

	/// Pile undo/redo en RAM uniquement (pas de persistance disque). Les piles
	/// `m_undo` et `m_redo` contiennent des `std::unique_ptr<ICommand>` ; la
	/// pile redo est vidée à chaque nouveau `Push` (branche timeline standard).
	///
	/// Coalescing : `Push` détecte si `cmd->GetMergeKey()` non-nul est égal au
	/// mergeKey de la commande au sommet et appelle `TryMerge`. Si succès, la
	/// commande au sommet absorbe et le nouveau pointer est libéré.
	///
	/// Eviction : si après push `UndoSize() > capacity` ou `TotalBytes() >
	/// maxBytes`, la commande la plus ancienne est jetée jusqu'à respect des
	/// limites.
	///
	/// Contraintes thread/timing : toutes les méthodes doivent être appelées
	/// depuis le main thread — les commandes peuvent toucher l'état ImGui et
	/// le device GPU.
	class CommandStack
	{
	public:
		/// Applique les paramètres `cfg` (capacity / maxBytes). N'évince pas
		/// rétroactivement : si la pile dépasse déjà les nouvelles limites,
		/// les évictions auront lieu au prochain `Push`. Effet de bord : copie
		/// `cfg` dans `m_cfg`.
		void Configure(const CommandStackConfig& cfg);

		/// Exécute `cmd` puis l'empile. Vide la pile redo. Tente `TryMerge`
		/// avec la commande au sommet si `mergeKey` non-nul et identique.
		/// Évince les commandes les plus anciennes tant que `m_undo.size() >
		/// capacity` ou `m_totalBytes > maxBytes`.
		/// Effet de bord : prend ownership de `cmd`, appelle `cmd->Execute()`.
		void Push(std::unique_ptr<ICommand> cmd);

		/// Retourne true si la pile undo n'est pas vide.
		bool CanUndo() const;
		/// Retourne true si la pile redo n'est pas vide.
		bool CanRedo() const;

		/// Dépile la commande au sommet de la pile undo, appelle son `Undo()`,
		/// et la pousse sur la pile redo. No-op si la pile undo est vide.
		void Undo();

		/// Symétrique de `Undo` : dépile la commande au sommet de redo, appelle
		/// son `Execute()`, et la pousse sur la pile undo. No-op si vide.
		void Redo();

		/// Annule en cascade jusqu'à ce que `m_undo.size() == targetIndex`. Ne
		/// va pas au-delà : si `targetIndex >= m_undo.size()`, no-op.
		/// \param targetIndex index "exclu" : après l'appel, la pile undo
		/// contient `[0, targetIndex)` (ou moins si elle était plus courte).
		/// Effet de bord : appelle `Undo()` autant de fois que nécessaire.
		void RewindTo(size_t targetIndex);

		/// Vide les deux piles. Remet `m_totalBytes` à 0.
		/// Effet de bord : libère toutes les commandes (déclenche leurs
		/// destructeurs).
		void Clear();

		/// Nombre de commandes dans la pile undo.
		size_t UndoSize() const;
		/// Nombre de commandes dans la pile redo.
		size_t RedoSize() const;
		/// Somme des `GetMemoryFootprint()` de toutes les commandes undo.
		size_t TotalBytes() const;

		/// Entrée d'historique exposée par `SnapshotHistory()` pour le panneau
		/// History et les tests.
		struct HistoryEntry
		{
			std::string label;
			size_t bytes;
			uint64_t timestampMs;
		};

		/// Copie l'état courant de la pile undo (ordre chronologique : index 0
		/// = plus ancien, dernier = plus récent / "active"). Sert au panneau
		/// History pour rendre la liste sans exposer les pointeurs internes.
		std::vector<HistoryEntry> SnapshotHistory() const;

	private:
		/// Évince la commande la plus ancienne (front) si la pile n'est pas
		/// vide. Met à jour `m_totalBytes`. Appelé en boucle par `Push`.
		void EvictOldest();

		std::vector<std::unique_ptr<ICommand>> m_undo;
		std::vector<std::unique_ptr<ICommand>> m_redo;
		std::vector<uint64_t> m_undoTimestamps;
		std::vector<uint64_t> m_redoTimestamps;
		size_t m_totalBytes = 0;
		CommandStackConfig m_cfg;
	};
}
