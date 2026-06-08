// Tests de la fondation theming runtime (sous-projet 1).
// Header-only, sans ImGui ni assert (NDEBUG strip les assert en CI Release).
#include "src/client/render/LnTheme.h"

#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
    int g_failures = 0;

    void Expect(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("[FAIL] %s\n", what);
            ++g_failures;
        }
    }

    bool Near(float a, float b) { return std::fabs(a - b) < 0.002f; }
}

int main()
{
    using namespace LnTheme;

    // 1. Le registre expose les deux thèmes attendus.
    const std::vector<std::string_view> names = Names();
    bool hasOr = false, hasSylve = false;
    for (std::string_view n : names)
    {
        if (n == "or_royal") hasOr = true;
        if (n == "sylve_emeraude") hasSylve = true;
    }
    Expect(hasOr, "Names() contient or_royal");
    Expect(hasSylve, "Names() contient sylve_emeraude");

    // 2. Défaut = or_royal, accent doré ~ #E8C56E.
    Expect(SetActive("or_royal"), "SetActive(or_royal) renvoie true");
    Expect(ActiveName() == "or_royal", "ActiveName == or_royal");
    Expect(Near(Active().accent.r, 0.910f) && Near(Active().accent.g, 0.773f)
        && Near(Active().accent.b, 0.431f), "or_royal accent dore");

    // 3. Bascule vers sylve_emeraude : l'accent change.
    Expect(SetActive("sylve_emeraude"), "SetActive(sylve) renvoie true");
    Expect(ActiveName() == "sylve_emeraude", "ActiveName == sylve_emeraude");
    Expect(!(Near(Active().accent.r, 0.910f) && Near(Active().accent.g, 0.773f)),
        "sylve accent != or_royal accent");

    // 4. Les alias-références suivent le thème actif (point clé du refactor).
    Expect(Near(kAccent.r, Active().accent.r) && Near(kAccent.g, Active().accent.g)
        && Near(kAccent.b, Active().accent.b), "kAccent suit Active() apres SetActive");

    // 5. Nom inconnu : pas de changement, renvoie false.
    Expect(!SetActive("inconnu"), "SetActive(inconnu) renvoie false");
    Expect(ActiveName() == "sylve_emeraude", "theme inchange apres nom invalide");

    // 6. Invariants palette : danger reste un rouge distinct de l'accent, alpha=1.
    for (std::string_view n : names)
    {
        SetActive(n);
        const Palette& p = Active();
        Expect(p.accent.a == 1.f && p.errorCol.a == 1.f, "alpha opaque accent/error");
        const bool distinct = std::fabs(p.errorCol.r - p.accent.r) > 0.1f
            || std::fabs(p.errorCol.g - p.accent.g) > 0.1f
            || std::fabs(p.errorCol.b - p.accent.b) > 0.1f;
        Expect(distinct, "errorCol distinct de accent");
    }

    if (g_failures == 0) std::printf("[OK] LnThemeTests\n");
    return g_failures == 0 ? 0 : 1;
}
