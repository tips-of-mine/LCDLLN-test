// lava_hazard_tests — v1.0.123 : lave mortelle des Enfers (masque, mort en 3 s).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/LavaHazard.h"

#include <cstdio>
#include <string>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	/// Masque 4×4 de 10 m à l'origine (100, 200) : lave pure sur la moitié est (i ≥ 2),
	/// lave faible (0,3) en (1, 0), sol à 50 m.
	engine::world::LavaMask Masque()
	{
		engine::world::LavaMask m;
		m.size = 4; m.originX = 100.0; m.originZ = 200.0; m.texelM = 10.0;
		m.lava.assign(16, 0); m.groundY.assign(16, 50.0f);
		for (int j = 0; j < 4; ++j)
			for (int i = 2; i < 4; ++i)
				m.lava[j * 4 + i] = 255;
		m.lava[1] = 77;
		return m;
	}
}

int main()
{
	using namespace engine::server;
	using engine::world::LavaMask;

	// Format : aller-retour et refus.
	const LavaMask m = Masque();
	const std::vector<uint8_t> bytes = engine::world::SaveLavaMask(m);
	LavaMask r;
	std::string err;
	Check(engine::world::LoadLavaMask(bytes, r, err) && r.size == 4 && r.lava == m.lava && r.groundY == m.groundY
		&& r.originX == 100.0 && r.texelM == 10.0, "lvmk : aller-retour identique");
	std::vector<uint8_t> court(bytes.begin(), bytes.end() - 1);
	Check(!engine::world::LoadLavaMask(court, r, err) && r.Empty(), "lvmk : fichier tronque refuse");
	std::vector<uint8_t> faux = bytes; faux[0] ^= 0xFF;
	Check(!engine::world::LoadLavaMask(faux, r, err), "lvmk : magic invalide refuse");

	LavaHazard h;
	h.AddMask(LavaMask{});
	Check(h.Count() == 0, "masque vide ignore");
	h.AddMask(m);
	Check(h.InLava(125.0, 50.0, 215.0), "lave pure : brule");
	Check(!h.InLava(105.0, 50.0, 215.0), "sans lave : ne brule pas");
	Check(!h.InLava(115.0, 50.0, 205.0), "lave faible (0,3) : ne brule pas");
	Check(!h.InLava(125.0, 52.0, 215.0), "a plus de 1,5 m au-dessus du sol : ne brule pas");
	Check(!h.InLava(155.0, 50.0, 215.0) && !h.InLava(99.0, 50.0, 215.0), "hors du masque : ne brule pas");

	// Mort en 3 s cumulées, entrée signalée une fois, sortie remet à zéro.
	LavaExposureState s;
	Check(h.Update(s, true, true, 125.0, 50.0, 215.0, 0.1f) == LavaEvent::Entered, "entree signalee");
	LavaEvent last = LavaEvent::None;
	int ticks = 1;
	while ((last = h.Update(s, true, true, 125.0, 50.0, 215.0, 0.1f)) == LavaEvent::None) ++ticks;
	Check(last == LavaEvent::Killed && ticks >= 29 && ticks <= 30 && s.seconds == 0.0f, "mort apres 3 s");
	h.Update(s, true, true, 125.0, 50.0, 215.0, 2.0f);
	Check(h.Update(s, true, true, 105.0, 50.0, 215.0, 0.1f) == LavaEvent::Left && s.seconds == 0.0f, "sortie : compteur remis a zero");
	h.Update(s, true, true, 125.0, 50.0, 215.0, 2.0f);
	Check(h.Update(s, true, true, 125.0, 50.0, 215.0, 0.9f) == LavaEvent::None, "2,9 s apres la reentree : vivant");
	Check(h.Update(s, false, true, 125.0, 50.0, 215.0, 5.0f) == LavaEvent::Left, "hors des Enfers : pas de lave");
	Check(h.Update(s, true, false, 125.0, 50.0, 215.0, 5.0f) == LavaEvent::None && s.seconds == 0.0f, "mort : ne brule pas");

	if (g_fail == 0) std::printf("lava_hazard_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
