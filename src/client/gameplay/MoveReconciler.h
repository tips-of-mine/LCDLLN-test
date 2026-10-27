#pragma once
// ETAPE3.3 (chantier 4.1) — recalage du client sur la position officielle du shard.
//
// Le client simule son personnage à chaque image (CharacterController) et envoie à 20 Hz
// des commandes numérotées portant la position obtenue. Il garde ici la position
// HORIZONTALE envoyée pour chaque commande (coordonnées du protocole). Quand le shard
// renvoie une position officielle pour la commande N (MoveCorrection) :
//   1. l'écart = position officielle - position envoyée pour N ;
//   2. le personnage courant est DÉCALÉ de cet écart (CharacterController::Translate) :
//      sa vitesse, son saut, sa chute, sa nage et son esquive sont conservés, et le
//      mouvement fait depuis N reste acquis (équivaut à rejouer les entrées depuis la
//      position officielle, sans perdre l'état) ;
//   3. les positions des commandes postérieures à N, déjà parties, sont décalées
//      d'autant (une correction de suite, pour une commande en route, ne recale donc
//      que le reste de l'écart) ; celles <= N sont oubliées.
// La hauteur n'est jamais recalée : le shard n'a pas la géométrie du monde et ne fait
// que freiner la hauteur qu'il diffuse (toits, ponts, marches restent justes côté
// client).
//
// Pas de dépendance au rendu ni au réseau : testable seul (cf. move_reconciler_tests).

#include <cstddef>
#include <cstdint>
#include <deque>

namespace engine::gameplay
{
	class MoveReconciler
	{
	public:
		/// Commandes gardées au plus (~12 s à 20 Hz) : une correction plus ancienne est
		/// ignorée (la suivante, plus récente, recale).
		static constexpr size_t kMaxCommands = 256u;

		/// Écart horizontal à appliquer après une correction (m, coordonnées du protocole).
		struct Offset
		{
			double dx = 0.0;
			double dz = 0.0;
		};

		/// Enregistre la position horizontale envoyée avec la commande \p sequence
		/// (numéros croissants ; à appeler à chaque envoi d'Input).
		void RecordCommand(uint32_t sequence, double x, double z);

		/// Correction du shard pour la commande \p sequence (position officielle
		/// \p officialX / \p officialZ). Remplit \p out avec l'écart à appliquer au
		/// personnage, décale les commandes postérieures, oublie les commandes <= N.
		/// \return false si la commande n'est plus (ou pas) connue : rien n'est modifié
		///         hormis l'oubli des commandes <= N.
		bool ApplyCorrection(uint32_t sequence, double officialX, double officialZ, Offset& out);

		/// Oublie tout (téléport : ForcePosition, nouvelle session).
		void Clear() { m_commands.clear(); }

		/// Commandes gardées.
		size_t Size() const { return m_commands.size(); }

	private:
		struct Sent
		{
			uint32_t sequence = 0;
			double x = 0.0;
			double z = 0.0;
		};
		std::deque<Sent> m_commands; ///< trié par numéro croissant
	};
}
