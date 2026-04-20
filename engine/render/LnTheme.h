#pragma once

namespace LnTheme
{
	/// Couleurs RGBA 0–1, sans dépendance Dear ImGui (utilisables sur toutes les plateformes).
	struct Rgba
	{
		float r;
		float g;
		float b;
		float a;
	};

	// Palette de base (dérivée de colors_and_type.css — spec auth-imgui).
	inline constexpr Rgba kPrimary{0.290f, 0.482f, 0.722f, 1.f};    // #4A7BB8
	inline constexpr Rgba kSecondary{0.361f, 0.420f, 0.549f, 1.f};  // #5C6B8C
	inline constexpr Rgba kAccent{0.910f, 0.773f, 0.431f, 1.f};     // #E8C56E
	inline constexpr Rgba kBackground{0.039f, 0.051f, 0.071f, 1.f}; // #0A0D12
	inline constexpr Rgba kSurface{0.071f, 0.094f, 0.133f, 1.f};    // #121822
	inline constexpr Rgba kPanel{0.078f, 0.110f, 0.157f, 1.f};      // #141C28
	inline constexpr Rgba kText{0.949f, 0.957f, 0.973f, 1.f};       // #F2F4F8
	inline constexpr Rgba kMuted{0.608f, 0.659f, 0.722f, 1.f};      // #9BA8B8
	inline constexpr Rgba kBorder{0.239f, 0.310f, 0.400f, 1.f};    // #3D4F66

	inline constexpr Rgba kSuccess{0.373f, 0.722f, 0.431f, 1.f}; // #5FB86E
	inline constexpr Rgba kWarning{0.910f, 0.647f, 0.361f, 1.f}; // #E8A55C
	/// Nommé error_col pour éviter une collision avec std::error dans certains contextes.
	inline constexpr Rgba kErrorCol{0.769f, 0.251f, 0.251f, 1.f}; // #C44040

	inline constexpr Rgba PanelBg(float alpha = 0.72f)
	{
		return Rgba{kPanel.r, kPanel.g, kPanel.b, alpha};
	}

	inline constexpr Rgba AccentDim(float alpha = 0.10f)
	{
		return Rgba{kAccent.r, kAccent.g, kAccent.b, alpha};
	}

	inline constexpr Rgba BorderActive()
	{
		return kAccent;
	}
} // namespace LnTheme
