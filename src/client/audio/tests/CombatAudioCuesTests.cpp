// SND.5 — Tests des sons de combat et de sorts (CombatAudioCues).

#include "src/client/audio/CombatAudioCues.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	using engine::audio::CombatAudioCue;

	bool Has(const std::vector<CombatAudioCue>& v, std::string_view id, uint64_t entity = ~0ull)
	{
		return std::any_of(v.begin(), v.end(), [&](const CombatAudioCue& c)
			{ return c.soundId == id && (entity == ~0ull || c.entityId == entity); });
	}

	constexpr uint64_t kMs = 1000000ull;
}

int main()
{
	using namespace engine::audio;
	namespace s = combat_sounds;

	CombatAudioSnapshot a;
	a.valid = true;
	a.playerId = 1;
	a.playerProfile = "melee";
	a.playerHealth = 100;
	a.playerMaxHealth = 100;
	a.entityHealth[50] = 40;
	a.entityIsCreature[50] = true;
	a.hits.push_back({ 10, 1, 50, true, false, false, false });

	// Entrée en jeu / rien ne change : silence.
	Check(DiffCombatAudio(CombatAudioSnapshot{}, a, 0).empty(), "premiere photo : silence");
	Check(DiffCombatAudio(a, a, 0).empty(), "rien ne change : silence");

	// Profils → impact.
	Check(HitSoundForProfile("melee") == s::kHitBlade, "melee = lame");
	Check(HitSoundForProfile("distance") == s::kHitArrow, "distance = fleche");
	Check(HitSoundForProfile("lanceur") == s::kHitMagic, "lanceur = magie");
	Check(HitSoundForProfile("") == s::kHitBlunt, "inconnu = coup sourd");

	// Le joueur frappe : lame à la position de la cible ; critique en plus.
	CombatAudioSnapshot b = a;
	b.hits.push_back({ 11, 1, 50, true, false, true, false });
	auto cues = DiffCombatAudio(a, b, 0);
	Check(Has(cues, s::kHitBlade, 50), "coup porte : lame sur la cible");
	Check(Has(cues, s::kCrit, 50), "critique");
	Check(cues.front().soundId == s::kCrit, "critique prioritaire sur l'impact");

	// Raté : sifflement côté joueur (2D).
	CombatAudioSnapshot c = a;
	c.hits.push_back({ 11, 1, 50, true, false, false, true });
	cues = DiffCombatAudio(a, c, 0);
	Check(cues.size() == 1 && cues[0].soundId == s::kWhoosh && cues[0].entityId == 0, "rate : whoosh 2D");

	// Le joueur encaisse un coup de créature : choc 2D + griffes.
	CombatAudioSnapshot d = a;
	d.hits.push_back({ 11, 50, 1, false, true, false, false });
	cues = DiffCombatAudio(a, d, 0);
	Check(Has(cues, s::kPlayerHurt, 0) && Has(cues, s::kHitClaw, 0), "joueur touche par une creature");

	// Combat entre tiers : impact discret.
	CombatAudioSnapshot e = a;
	e.hits.push_back({ 11, 77, 50, false, false, false, false });
	cues = DiffCombatAudio(a, e, 0);
	Check(cues.size() == 1 && cues[0].volume < 1.0f && cues[0].entityId == 50, "combat entre tiers discret");

	// Les anciennes entrées ne rejouent pas (journal tronqué/réordonné).
	CombatAudioSnapshot f = b;
	std::reverse(f.hits.begin(), f.hits.end());
	Check(DiffCombatAudio(b, f, 0).empty(), "journal reordonne : silence");

	// Mort d'une créature et du joueur.
	CombatAudioSnapshot g = a;
	g.entityHealth[50] = 0;
	cues = DiffCombatAudio(a, g, 0);
	Check(Has(cues, s::kDeath, 50), "mort de creature");
	CombatAudioSnapshot h = a;
	h.playerHealth = 0;
	cues = DiffCombatAudio(a, h, 0);
	Check(!cues.empty() && cues.front().soundId == s::kPlayerDeath, "mort du joueur prioritaire");
	// Une entité qui apparaît déjà morte ne sonne pas.
	CombatAudioSnapshot i = a;
	i.entityHealth[60] = 0;
	Check(DiffCombatAudio(a, i, 0).empty(), "entite deja morte : silence");

	// Incantation : début, fin complète, interruption.
	CombatAudioSnapshot cast = a;
	cast.castActive = true;
	cast.castStartNs = 1000 * kMs;
	cast.castDurationMs = 2000;
	Check(Has(DiffCombatAudio(a, cast, 1000 * kMs), s::kSpellCast), "debut d'incantation");
	Check(Has(DiffCombatAudio(cast, a, 3000 * kMs), s::kSpellRelease), "sort lache");
	Check(Has(DiffCombatAudio(cast, a, 1500 * kMs), s::kSpellFizzle), "sort interrompu");

	// Auras du joueur.
	CombatAudioSnapshot buff = a;
	buff.playerAuras["benediction"] = 3;
	Check(Has(DiffCombatAudio(a, buff, 0), s::kAuraBuff), "bienfait");
	CombatAudioSnapshot debuff = a;
	debuff.playerAuras["poison"] = 1;
	Check(Has(DiffCombatAudio(a, debuff, 0), s::kAuraDebuff), "malediction");
	Check(DiffCombatAudio(buff, buff, 0).empty(), "aura deja active : silence");
	Check(IsBeneficialAura(9) && !IsBeneficialAura(7) && !IsBeneficialAura(5), "types d'aura");

	// Plafond : une grosse AoE ne déclenche pas 30 sons.
	CombatAudioSnapshot aoe = a;
	for (uint64_t k = 0; k < 30; ++k)
		aoe.hits.push_back({ 100 + k, 1, 200 + k, true, false, false, false });
	Check(DiffCombatAudio(a, aoe, 0, 6).size() == 6, "plafond de sons par image");

	// Changement de personnage : silence.
	CombatAudioSnapshot other = b;
	other.playerId = 2;
	Check(DiffCombatAudio(a, other, 0).empty(), "autre personnage : silence");

	// Battement de cœur.
	Check(LowHealthHeartbeat(20, 100), "coeur sous 25 %");
	Check(!LowHealthHeartbeat(25, 100), "pas de coeur a 25 %");
	Check(!LowHealthHeartbeat(0, 100), "mort : plus de coeur");
	Check(!LowHealthHeartbeat(10, 0), "PV max inconnus");

	if (s_failCount == 0)
		std::cout << "combat_audio_cues_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
