#pragma once

#include <array>
#include <string_view>
#include <vector>

namespace LnTheme
{
	/// Couleur RGBA 0–1, sans dépendance Dear ImGui (utilisable partout).
	struct Rgba
	{
		float r;
		float g;
		float b;
		float a;
	};

	/// Palette complète d'un thème : les 12 rôles de couleur de l'UI.
	struct Palette
	{
		Rgba primary;
		Rgba secondary;
		Rgba accent;
		Rgba background;
		Rgba surface;
		Rgba panel;
		Rgba text;
		Rgba muted;
		Rgba border;
		Rgba success;
		Rgba warning;
		Rgba errorCol;
	};

	namespace detail
	{
		// --- Définition des thèmes (data-driven : ajouter un thème = +1 entrée). ---

		// or_royal : palette historique (dérivée de colors_and_type.css — spec auth).
		inline constexpr Palette kOrRoyal{
			/*primary   */ {0.290f, 0.482f, 0.722f, 1.f}, // #4A7BB8
			/*secondary */ {0.361f, 0.420f, 0.549f, 1.f}, // #5C6B8C
			/*accent    */ {0.910f, 0.773f, 0.431f, 1.f}, // #E8C56E
			/*background */ {0.039f, 0.051f, 0.071f, 1.f}, // #0A0D12
			/*surface   */ {0.071f, 0.094f, 0.133f, 1.f}, // #121822
			/*panel     */ {0.078f, 0.110f, 0.157f, 1.f}, // #141C28
			/*text      */ {0.949f, 0.957f, 0.973f, 1.f}, // #F2F4F8
			/*muted     */ {0.608f, 0.659f, 0.722f, 1.f}, // #9BA8B8
			/*border    */ {0.239f, 0.310f, 0.400f, 1.f}, // #3D4F66
			/*success   */ {0.373f, 0.722f, 0.431f, 1.f}, // #5FB86E
			/*warning   */ {0.910f, 0.647f, 0.361f, 1.f}, // #E8A55C
			/*errorCol  */ {0.769f, 0.251f, 0.251f, 1.f}, // #C44040
		};

		// sylve_emeraude : ambiance nature/elfique. Accent vert-or pâle, surfaces
		// vert-gris sombre. text/muted restent lisibles ; success/warning/errorCol
		// conservés sémantiquement (le danger reste un rouge distinct de l'accent).
		inline constexpr Palette kSylveEmeraude{
			/*primary   */ {0.180f, 0.431f, 0.310f, 1.f}, // #2E6E4F
			/*secondary */ {0.290f, 0.420f, 0.341f, 1.f}, // #4A6B57
			/*accent    */ {0.796f, 0.851f, 0.561f, 1.f}, // #CBD98F
			/*background */ {0.031f, 0.067f, 0.047f, 1.f}, // #08110C
			/*surface   */ {0.067f, 0.110f, 0.086f, 1.f}, // #111C16
			/*panel     */ {0.086f, 0.141f, 0.106f, 1.f}, // #16241B
			/*text      */ {0.937f, 0.957f, 0.925f, 1.f}, // #EFF4EC
			/*muted     */ {0.576f, 0.659f, 0.608f, 1.f}, // #93A89B
			/*border    */ {0.239f, 0.373f, 0.286f, 1.f}, // #3D5F49
			/*success   */ {0.373f, 0.722f, 0.431f, 1.f}, // #5FB86E
			/*warning   */ {0.910f, 0.647f, 0.361f, 1.f}, // #E8A55C
			/*errorCol  */ {0.769f, 0.251f, 0.251f, 1.f}, // #C44040
		};

		/// Une entrée du registre : nom interne (clé config) + palette.
		struct Entry
		{
			std::string_view name;
			Palette palette;
		};

		/// Registre ordonné des thèmes disponibles (ordre d'affichage UI).
		inline const std::array<Entry, 2>& Registry()
		{
			static const std::array<Entry, 2> kRegistry{{
				{"or_royal", kOrRoyal},
				{"sylve_emeraude", kSylveEmeraude},
			}};
			return kRegistry;
		}

		/// État du thème actif : copie MUTÉE EN PLACE par SetActive. Les alias
		/// kXxx référencent les membres de CETTE instance ; muter en place (et non
		/// rebinder) garde les références valides et à jour. Initialisé via Meyers
		/// singleton, donc construit avant tout usage (pas de SIOF : tous les call
		/// sites lisent à l'exécution, jamais en initialisation statique).
		inline Palette& ActiveStorage()
		{
			static Palette s = kOrRoyal;
			return s;
		}

		/// Nom du thème actif (string_view vers un littéral du registre).
		inline std::string_view& ActiveNameStorage()
		{
			static std::string_view n = "or_royal";
			return n;
		}
	} // namespace detail

	/// Palette du thème actuellement actif (jamais nulle ; défaut or_royal).
	inline const Palette& Active()
	{
		return detail::ActiveStorage();
	}

	/// Nom interne du thème actif (ex. "or_royal") — pour persistance et UI.
	inline std::string_view ActiveName()
	{
		return detail::ActiveNameStorage();
	}

	/// Noms des thèmes disponibles, dans l'ordre d'affichage.
	inline std::vector<std::string_view> Names()
	{
		std::vector<std::string_view> out;
		out.reserve(detail::Registry().size());
		for (const auto& e : detail::Registry())
		{
			out.push_back(e.name);
		}
		return out;
	}

	/// Bascule le thème actif. Renvoie false (et ne change rien) si name inconnu.
	/// Effet de bord : recolore tout l'UI à la frame suivante (via les alias kXxx).
	inline bool SetActive(std::string_view name)
	{
		for (const auto& e : detail::Registry())
		{
			if (e.name == name)
			{
				detail::ActiveStorage() = e.palette; // copie EN PLACE -> alias suivent
				detail::ActiveNameStorage() = e.name;
				return true;
			}
		}
		return false;
	}

	// --- Alias de compatibilité : références VIVANTES vers le thème actif. ---
	// Conservent la syntaxe LnTheme::kAccent sur ~249 call sites existants, mais
	// reflètent désormais le thème courant (références vers les membres de
	// ActiveStorage(), muté en place par SetActive). Aucune édition de call site.
	inline const Rgba& kPrimary = detail::ActiveStorage().primary;
	inline const Rgba& kSecondary = detail::ActiveStorage().secondary;
	inline const Rgba& kAccent = detail::ActiveStorage().accent;
	inline const Rgba& kBackground = detail::ActiveStorage().background;
	inline const Rgba& kSurface = detail::ActiveStorage().surface;
	inline const Rgba& kPanel = detail::ActiveStorage().panel;
	inline const Rgba& kText = detail::ActiveStorage().text;
	inline const Rgba& kMuted = detail::ActiveStorage().muted;
	inline const Rgba& kBorder = detail::ActiveStorage().border;
	inline const Rgba& kSuccess = detail::ActiveStorage().success;
	inline const Rgba& kWarning = detail::ActiveStorage().warning;
	inline const Rgba& kErrorCol = detail::ActiveStorage().errorCol;

	/// Fond de panneau semi-transparent dérivé du panel du thème actif.
	inline Rgba PanelBg(float alpha = 0.72f)
	{
		const Palette& p = Active();
		return Rgba{p.panel.r, p.panel.g, p.panel.b, alpha};
	}

	/// Variante atténuée de l'accent du thème actif (overlays de survol).
	inline Rgba AccentDim(float alpha = 0.10f)
	{
		const Palette& p = Active();
		return Rgba{p.accent.r, p.accent.g, p.accent.b, alpha};
	}

	/// Couleur de bordure « active » : l'accent du thème courant.
	inline Rgba BorderActive()
	{
		return Active().accent;
	}
} // namespace LnTheme
