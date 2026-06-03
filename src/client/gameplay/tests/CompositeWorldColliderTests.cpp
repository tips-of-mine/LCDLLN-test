#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>
#include <cstdio>

using namespace engine::gameplay;
using engine::math::Vec3;

namespace
{
	// Terrain factice : ne collisionne jamais ; QueryWater renvoie inWater=true (pour
	// vérifier la délégation).
	class FakeTerrain final : public IWorldCollider
	{
	public:
		bool SweepCapsule(const Capsule&, const Vec3&, const Vec3&, SweepHit& out) const override
		{ out = SweepHit{}; return false; }
		bool QueryWater(const Vec3&, WaterQuery& out) const override
		{ out.inWater = true; out.surfaceY = 42.0f; return true; }
	};

	int g_fail = 0;
	void check(bool cond, const char* msg)
	{ if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; } }
}

int main()
{
	FakeTerrain terrain;
	IWorldCollider::Capsule cap; cap.radius = 0.3f; cap.height = 1.8f;

	// Cylindre centré en (5,0), rayon 0.5, de Y=0 à Y=3.
	const PropCylinder cyl{ 5.0f, 0.0f, 0.5f, 0.0f, 3.0f };

	// 1) Capsule traversant le cylindre : doit être arrêtée, normale horizontale.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(h && hit.hit, "traversee: hit attendu");
		check(hit.fraction < 1.0f, "traversee: fraction < 1");
		check(std::fabs(hit.normal.y) < 1e-3f, "traversee: normale horizontale");
		// Contact attendu vers x ~ 5 - (0.3+0.5) = 4.2 -> fraction ~0.42.
		check(hit.fraction > 0.3f && hit.fraction < 0.55f, "traversee: fraction plausible");
	}

	// 2) Capsule passant à côté (z=5) : pas de hit.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 5 }, Vec3{ 10, 1, 5 }, hit);
		check(!h && !hit.hit, "a_cote: pas de hit");
	}

	// 3) Capsule au-dessus du cylindre (Y=10) : pas de hit (hors [baseY, topY]).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 10, 0 }, Vec3{ 10, 10, 0 }, hit);
		check(!h, "au_dessus: pas de hit");
	}

	// 4) Sans cylindre : comportement = terrain seul (ici pas de hit).
	{
		CompositeWorldCollider c(&terrain);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 0, 1, 0 }, Vec3{ 10, 1, 0 }, hit);
		check(!h, "sans_cylindre: pas de hit");
	}

	// 4bis) RÉGRESSION (téléport en hauteur) : un sweep VERTICAL traversant le
	// cylindre par son axe (ex. sonde de récupération anti-encastrement qui sonde
	// depuis 50 m au-dessus) NE DOIT PAS générer de hit. Sinon le CharacterController
	// téléporte le perso au sommet de la sonde.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 5, 20, 0 }, Vec3{ 5, 1, 0 }, hit);
		check(!h && !hit.hit, "sweep vertical dans cylindre: pas de hit (anti-teleport)");
	}

	// 4ter) Déjà en chevauchement, mouvement s'ÉLOIGNANT de l'axe : pas de hit
	// (le perso doit pouvoir ressortir / glisser, pas rester collé).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		// Départ au bord du cylindre (x=5.5 ~ R=0.8 de l'axe), on s'éloigne vers +x.
		bool h = c.SweepCapsule(cap, Vec3{ 5.5f, 1, 0 }, Vec3{ 7.0f, 1, 0 }, hit);
		check(!h, "chevauchement + eloignement: pas de hit (peut ressortir)");
	}

	// 6) ATTERRISSAGE sur le DESSUS : sweep descendant court dont le bas de capsule
	//    (center.y - halfH) franchit topY de haut en bas, dans l'empreinte XZ.
	//    -> hit "sol" (normale +Y) au niveau topY.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl); // topY = 3
		IWorldCollider::SweepHit hit;
		// halfH = 0.9 ; start bottom = 4.5-0.9 = 3.6 (>3) ; end bottom = 3.5-0.9 = 2.6 (<3).
		bool h = c.SweepCapsule(cap, Vec3{ 5, 4.5f, 0 }, Vec3{ 5, 3.5f, 0 }, hit);
		check(h && hit.hit, "dessus: hit attendu");
		check(hit.normal.y > 0.99f, "dessus: normale verticale (sol)");
		check(hit.fraction > 0.5f && hit.fraction < 0.7f, "dessus: fraction ~0.6");
	}

	// 6b) Atterrissage en BORD : centre hors du rayon cylindre (0.5) mais dans le
	//     rayon combiné (cyl 0.5 + capsule 0.3 = 0.8) -> on se pose quand même
	//     ("si ça te bloque latéralement, tu peux te poser dessus").
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl); // topY = 3, radius 0.5
		IWorldCollider::SweepHit hit;
		// ex = 5.7 - 5.0 = 0.7 : hors 0.5 mais dans 0.8.
		bool h = c.SweepCapsule(cap, Vec3{ 5.7f, 4.5f, 0 }, Vec3{ 5.7f, 3.5f, 0 }, hit);
		check(h && hit.hit, "bord: hit attendu (rayon combine)");
		check(hit.normal.y > 0.99f, "bord: normale verticale");
	}

	// 7) Déjà posé sur le dessus (start bottom <= topY) : hit immédiat (frac ~0) ->
	//    le sticky ground probe garde le perso "grounded" sur la caisse.
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		// start bottom = 3.9-0.9 = 3.0 (== topY) ; end bottom = 3.8-0.9 = 2.9 (<3).
		bool h = c.SweepCapsule(cap, Vec3{ 5, 3.9f, 0 }, Vec3{ 5, 3.8f, 0 }, hit);
		check(h && hit.hit, "dessus pose: hit attendu");
		check(hit.fraction < 0.01f, "dessus pose: fraction ~0");
		check(hit.normal.y > 0.99f, "dessus pose: normale verticale");
	}

	// 8) Sweep descendant HORS empreinte XZ : pas de hit dessus (ni horizontal).
	{
		CompositeWorldCollider c(&terrain); c.AddCylinder(cyl);
		IWorldCollider::SweepHit hit;
		bool h = c.SweepCapsule(cap, Vec3{ 7, 4.5f, 0 }, Vec3{ 7, 3.5f, 0 }, hit);
		check(!h && !hit.hit, "dessus hors empreinte: pas de hit");
	}

	// 5) QueryWater délégué au terrain.
	{
		CompositeWorldCollider c(&terrain);
		IWorldCollider::WaterQuery wq;
		bool inW = c.QueryWater(Vec3{ 0, 0, 0 }, wq);
		check(inW && wq.inWater && std::fabs(wq.surfaceY - 42.0f) < 1e-3f, "querywater: delegation");
	}

	if (g_fail == 0) std::printf("CompositeWorldColliderTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
