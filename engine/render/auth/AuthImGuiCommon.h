#pragma once

#include <string_view>

struct ImDrawList;

namespace engine::render
{
#if defined(_WIN32)
	/// Bandeau style maquette (erreur / info / warning) — tokens Lune Noire.
	void DrawAuthBanner(std::string_view title, std::string_view message, float r, float g, float b);

	/// Toggle « se souvenir » + ligne d’aide optionnelle.
	bool DrawAuthToggle(std::string_view label, bool* checked, std::string_view hint);

	/// Bouton texte discret (liens pied de panneau, « Options », « Quitter »).
	bool DrawAuthButtonText(std::string_view label, std::string_view idSuffix);

	/// Bouton primaire (connexion) ; \p disabled pour phase Submitting.
	bool DrawAuthButtonPrimary(std::string_view label, std::string_view idSuffix, bool disabled);

	/// Bouton ghost (créer un compte).
	bool DrawAuthButtonGhost(std::string_view label, std::string_view idSuffix);

	/// Puces raccourcis clavier (Tab / Entrée / Échap).
	void DrawAuthKeycapRow(std::string_view leftKey, std::string_view leftDesc, std::string_view midKey, std::string_view midDesc,
		std::string_view rightKey, std::string_view rightDesc);

	/// Ligne raccourci [action …………… touche] — AUTH-UI.6.
	void DrawAuthKeybind(std::string_view actionName, std::string_view keyLabel);

	/// Bouton danger (ex. déconnexion).
	bool DrawAuthButtonDanger(std::string_view label, std::string_view idSuffix);

	/// AUTH-UI.7 — drapeaux premier lancement (primitives DrawList).
	void DrawFlagFR(ImDrawList* dl, float posX, float posY, float sizeW, float sizeH);
	void DrawFlagEN(ImDrawList* dl, float posX, float posY, float sizeW, float sizeH);
#endif
} // namespace engine::render
