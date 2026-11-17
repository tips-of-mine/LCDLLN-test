// Engine — fonctions utilitaires internes partagées par les fichiers Engine*.cpp
// (horloge d'animation, touches remappables, projection monde -> écran,
// projection de l'input de déplacement, clips de locomotion).
//
// En-tête INTERNE au client : n'est inclus que par src/client/app/Engine*.cpp.
// Fonctions déplacées telles quelles depuis l'espace de noms anonyme
// d'Engine.cpp lors du découpage d'Engine::Update() (aucun changement de
// comportement). Une seule définition (EngineHelpers.cpp) : l'horloge de
// EngineNowSec() reste donc commune à tout le client.
#pragma once

#include "src/client/app/Engine.h"

#include <string>

namespace engine::helpers
{
	/// Retourne le temps ecoule en secondes depuis le premier appel a cette
	/// fonction, en float 32 bits. Normalise par un temps de reference
	/// capture au demarrage de l'application (static init).
	///
	/// Necessaire pour `AnimationCrossfade` : passer directement
	/// `steady_clock::now().time_since_epoch()` en float donnerait des
	/// valeurs ~10^8 - 10^9 (depuis boot machine), qui depassent la
	/// precision de la mantissa float32 (~7 chiffres significatifs).
	/// Resultat : (now - startTime) sautait par pas de plusieurs ms voire
	/// des dizaines de ms entre frames -> mesh qui tremble. Avec cette
	/// fonction, les valeurs restent < 10^4 secondes en pratique (uptime
	/// session), precision microseconde -> animation lisse.
	float EngineNowSec();

	/// Nom affichable/config d'une touche (ou "?" si hors table).
	const char* KeyName(engine::platform::Key k);

	/// Resout un nom de touche (config) en `Key`, `fallback` si inconnu.
	engine::platform::Key KeyFromName(const std::string& name, engine::platform::Key fallback);

	/// Glyphe d'AFFICHAGE d'une touche selon la disposition clavier active
	/// (AZERTY → la rangée du haut donne & é " ' ( - è _ ç à). Distinct de
	/// KeyName (nom de config stable/portable). Fallback : KeyName.
	std::string KeyGlyph(engine::platform::Key k);

	// Projette une position monde -> pixels ecran. Formule alignee sur
	// WorldEditorImGui::WorldToScreen (viewProj col-major .m, convention Vulkan).
	// false si derriere la camera ou hors near/far. Sert aux marqueurs interactibles.
	[[maybe_unused]] bool WorldToScreenPx(const float vp[16], float wx, float wy, float wz,
		int vw, int vh, float& sx, float& sy);

	/// Sous-projet B.1 (Task 9) — Projette l'input clavier WASD/ZQSD dans
	/// le repere camera courant pour produire la `MoveInput` passee au
	/// `CharacterController::Update`. Touche les directions XZ (Y ignore),
	/// normalise pour eviter la diagonale rapide (1.41x), et capture aussi
	/// le run (Shift) et le saut (Space, edge-triggered).
	///
	/// Le mapping des touches d'avant et de gauche depend de `layout`
	/// (cf. `controls.movement_layout` dans config) :
	/// - WASD : forward=W, left=A (clavier US par defaut)
	/// - ZQSD : forward=Z, left=Q (clavier FR/BE AZERTY)
	/// S et D sont identiques dans les deux layouts. L'ancien code mappait
	/// W ET Z (ou A ET Q) en OR sans discriminer ; resultat sur AZERTY :
	/// la touche W (qui devrait ne rien faire en ZQSD) faisait aussi
	/// avancer parce qu'elle envoie VK_W (rapport user 2026-05-19).
	///
	/// \param input  Input snapshot de la frame courante (clavier + souris).
	/// \param camera Camera orbitale (utilise GetForwardXZ / GetRightXZ pour
	///               le repere local au lieu d'un yaw monde fixe — les
	///               touches restent "vers ce que je regarde" meme apres
	///               rotation de la camera).
	/// \param layout Layout clavier (WASD ou ZQSD).
	/// \return MoveInput pret a etre consomme par CharacterController.
	///         `swim*/fly` sont hors-scope B.1 et restent false.
	///
	/// Effet de bord : aucun. Pure projection input -> intention.
	engine::gameplay::MoveInput BuildMoveInput(
		const engine::platform::Input& input,
		const engine::render::OrbitalCameraController& camera,
		engine::render::MovementLayout layout,
		engine::platform::Key sprintKey,
		engine::platform::Key crouchKey);

	/// Sous-projet B.1 — Detecte "pure back step" : touche back enfoncee
	/// seule, sans aucune autre direction de mouvement. La state machine
	/// de locomotion utilise ce flag pour declencher l'etat WalkBack
	/// (mesh inchange + anim Walking Backwards) ; tout autre input
	/// (forward, strafe, diagonale) repasse en free-mover (pivot mesh +
	/// Walk standard). On detecte les deux mappings AZERTY/QWERTY pour
	/// rester robuste quel que soit `controls.movement_layout`.
	bool IsPureBackInput(const engine::platform::Input& input);

	/// Sous-projet B.1 (Task 11) — Mapping etat de locomotion -> nom du clip
	/// charge dans `m_currentSkinnedMesh->clips`. Le nom doit matcher exactement
	/// la cle utilisee a l'insertion (cf. boot ~ligne 3863 ou les clips Mixamo
	/// "mixamo.com" sont renommes en "Idle" / "StartWalking" / "Walk").
	///
	/// Retourne "Idle" en fallback si un nouvel etat est ajoute a l'enum sans
	/// que ce switch soit mis a jour (defensif : evite un nullptr et un
	/// comportement non defini en runtime).
	const char* StateToClipName(engine::Engine::AvatarLocomotionState s);

	/// Sous-projet B.1 (Task 11) — Indique si le clip associe a un etat doit
	/// looper ou s'arreter a sa derniere keyframe (clamp). Consomme par
	/// `AnimationCrossfade::Play(clip, loops, now)`.
	///
	/// Idle / Walk / Run / Fall sont des etats "tenus" -> loop.
	/// StartWalking / Jump / Land sont des transitions one-shot -> clamp (la
	/// state machine transite hors de l'etat avant la fin du clip dans le cas
	/// normal ; le clamp evite un wrap visuel si la transition tarde).
	bool ClipLoops(engine::Engine::AvatarLocomotionState s);
} // namespace engine::helpers
