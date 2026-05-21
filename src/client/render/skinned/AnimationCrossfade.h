#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <optional>
#include <vector>

namespace engine::render::skinned
{

/// Une clip en cours de lecture (current ou previous pendant un crossfade).
///
/// La duree de vie du AnimationClip pointe par `clip` doit etre superieure ou
/// egale a celle de cette struct : ActiveAnimation ne possede pas le clip.
struct ActiveAnimation
{
    const AnimationClip* clip = nullptr;  ///< Non-owning ; duree de vie >= cette struct.
    float startTime = 0.0f;               ///< Temps absolu (secondes) au moment du Play.
    bool  loops = true;                   ///< true = wrap (idle, walk) ; false = clamp (jump, land).
};

/// Crossfade entre deux animations.
///
/// Quand Play() est appele alors qu'une autre clip est en cours, l'ancienne est
/// conservee comme "previous" et un blend lineaire commence sur
/// kCrossfadeDuration secondes. Au-dela de cette duree, previous est libere et
/// seul current reste actif.
///
/// Le blend est fait au niveau TRS (translation lerp, rotation slerp, scale
/// lerp) puis recompose via AnimationSampler::ComposeTRS — un lerp naif sur les
/// matrices serait faux pour la rotation.
class AnimationCrossfade
{
public:
    /// Duree (secondes) du blend declenche par Play() pendant qu'une clip joue
    /// deja. 150 ms = compromis "imperceptible" / "snap perceptible" valide par
    /// la litterature anim runtime.
    static constexpr float kCrossfadeDuration = 0.15f;

    /// Demarre une nouvelle clip. Si une clip est deja en cours et qu'elle est
    /// differente de `newClip`, declenche un blend de kCrossfadeDuration secondes.
    /// Si la meme clip est deja active : no-op (evite reset du temps + glitch
    /// visuel quand un caller appelle Play() chaque frame).
    ///
    /// \param newClip Clip a jouer. Sa duree de vie doit couvrir tous les futurs
    ///                appels a Sample() qui referencent cette clip.
    /// \param loops   true = boucle (idle, walk, run) ; false = clamp a la fin
    ///                (jump, land, attack one-shot).
    /// \param now     Temps absolu (secondes) — usuellement Engine::time.
    void Play(const AnimationClip& newClip, bool loops, float now);

    /// Echantillonne la pose locale (matrice 4x4 par bone) au temps `now`.
    ///
    /// Si un crossfade est en cours (now - crossfadeStartTime < kCrossfadeDuration),
    /// interpole entre les poses previous et current par TRS (lerp/slerp/lerp).
    /// Sinon, renvoie la pose de current. Si aucune clip n'est active, renvoie
    /// la bind pose (bindLocal de chaque bone).
    ///
    /// \param skeleton Squelette de reference (definit le nombre de bones et la bind pose).
    /// \param now      Temps absolu (secondes) — meme horloge que Play().
    /// \return Vecteur de matrices locales 4x4 (column-major), aligne sur skeleton.bones.
    std::vector<engine::math::Mat4> Sample(const Skeleton& skeleton, float now) const;

    /// B.1 (fix audit §1/§3) — Active le verrouillage du root motion horizontal.
    /// Quand `true`, la translation X/Z du/des bone(s) racine (parentIndex == -1,
    /// typiquement le Hips) est reforcee sur sa bind pose a chaque Sample(), la
    /// composante verticale (Y, bob de marche) etant conservee.
    ///
    /// Raison : les clips Mixamo importes ne sont pas "In Place" ; leur Hips
    /// translate horizontalement (root motion). Or la position monde du perso est
    /// pilotee par CharacterController. Sans ce lock, le mesh glissait par-dessus
    /// la position CC puis snappait au loop -> impression que la camera "decroche"
    /// du modele. Off par defaut (les autres usages / tests gardent le root motion
    /// brut) ; l'Engine l'active pour l'avatar joueur.
    void SetRootMotionLockXZ(bool enabled) { m_lockRootMotionXZ = enabled; }

    /// Indique si le verrouillage du root motion horizontal est actif.
    bool IsRootMotionLockXZ() const { return m_lockRootMotionXZ; }

private:
    ActiveAnimation                m_current;
    std::optional<ActiveAnimation> m_previous;
    float                          m_crossfadeStartTime = 0.0f;
    /// Cf. SetRootMotionLockXZ. Off par defaut pour ne pas changer le comportement
    /// des usages generiques (et des tests qui observent la translation root brute).
    bool                           m_lockRootMotionXZ = false;

    /// Echantillonne une seule clip a `now` en respectant loops vs clamp.
    /// Si anim.clip est null ou de duree nulle, renvoie un vecteur vide.
    static std::vector<engine::math::Mat4> SampleSingle(const Skeleton& skel,
                                                         const ActiveAnimation& anim,
                                                         float now);
};

}  // namespace engine::render::skinned
