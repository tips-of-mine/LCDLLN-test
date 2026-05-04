#include "engine/editor/TextureLibraryPanel.h"

#include "engine/editor/WorldEditorSession.h"
#include "engine/editor/TexturePreviewCache.h"
#include "engine/editor/WorldMapEditDocument.h"

#if defined(_WIN32)
#   include "imgui.h"
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace engine::editor
{
#if defined(_WIN32)
    namespace
    {
        constexpr float kThumbSize     = 96.0f;
        constexpr int   kColumnsPerRow = 5;

        /// Couleur de l'encadrement (liserer accent ImGui) pour la vignette
        /// actuellement assignee au layer actif.
        constexpr ImU32 kHighlightColor = IM_COL32(80, 180, 255, 255);

        /// Dessine une vignette + label cliquable. Renvoie true si cliquee.
        /// \param id ImTextureID retournee par le cache (peut etre nullptr -> placeholder gris).
        /// \param label Texte court affiche sous la vignette + utilise comme ID ImGui.
        /// \param highlighted Si true, encadre la vignette d'un liserer accent.
        bool DrawThumbButton(ImTextureID id, const char* label, bool highlighted)
        {
            const ImVec2 size(kThumbSize, kThumbSize);
            ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, highlighted ? 3.0f : 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, kHighlightColor);
            bool clicked = false;
            if (id != nullptr)
            {
                clicked = ImGui::ImageButton(label, id, size);
            }
            else
            {
                clicked = ImGui::Button(label, ImVec2(kThumbSize, kThumbSize));
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::TextWrapped("%s", label);
            ImGui::EndGroup();
            return clicked;
        }
    } // namespace

    void DrawTextureLibrary(WorldEditorSession& session,
                            TexturePreviewCache* cache,
                            bool& openFlag)
    {
        if (!openFlag) return;
        if (!ImGui::Begin("Bibliotheque de textures", &openFlag))
        {
            ImGui::End();
            return;
        }

        // Header : layer actif (radios sync sur SplatLayer()).
        static const char* kLayerNames[4] = { "Herbe", "Terre", "Roc", "Neige" };
        int& activeLayer = session.SplatLayer();
        activeLayer = std::clamp(activeLayer, 0, 3);
        ImGui::TextUnformatted("Layer actif :");
        for (int i = 0; i < 4; ++i)
        {
            ImGui::SameLine();
            if (ImGui::RadioButton(kLayerNames[i], activeLayer == i))
            {
                activeLayer = i;
            }
        }
        ImGui::Separator();

        // Section 1 : procedurales builtin.
        ImGui::TextUnformatted("Procedurales (par defaut moteur)");
        for (int li = 0; li < 4; ++li)
        {
            ImGui::PushID(li);
            ImTextureID id = (cache != nullptr)
                ? cache->GetProceduralThumb(static_cast<uint32_t>(li))
                : nullptr;
            const bool highlighted = (li == activeLayer)
                && session.Doc().splatLayerTextureRefs[static_cast<size_t>(li)].empty();
            if (DrawThumbButton(id, kLayerNames[li], highlighted))
            {
                // Clic sur procedurale = reset ref a "" pour le layer actif.
                session.MutableDoc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)].clear();
                session.MarkSplatRefsDirty();
            }
            ImGui::PopID();
            if (li < 3) ImGui::SameLine();
        }
        ImGui::Separator();

        // Section 2 : importees.
        const auto& assets = session.Doc().textureAssets;
        ImGui::Text("Importees (%zu)", assets.size());
        if (assets.empty())
        {
            ImGui::TextDisabled("(aucune) - Importez via 'Import assets'");
        }
        else
        {
            int col = 0;
            for (size_t i = 0; i < assets.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImTextureID id = (cache != nullptr) ? cache->GetTexrThumb(assets[i]) : nullptr;
                // Highlight si cette texture est assignee au layer actif.
                const bool highlighted =
                    (session.Doc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)] == assets[i]);
                // Label court : basename apres dernier slash.
                std::string baseLabel = assets[i];
                const size_t slash = baseLabel.find_last_of("/\\");
                if (slash != std::string::npos) baseLabel = baseLabel.substr(slash + 1);
                if (DrawThumbButton(id, baseLabel.c_str(), highlighted))
                {
                    session.MutableDoc().splatLayerTextureRefs[static_cast<size_t>(activeLayer)] = assets[i];
                    session.MarkSplatRefsDirty();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", assets[i].c_str());
                }
                ImGui::PopID();
                ++col;
                if (col % kColumnsPerRow != 0 && i + 1 < assets.size()) ImGui::SameLine();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Cliquez une vignette pour l'assigner au layer actif (%s).",
                            kLayerNames[activeLayer]);
        ImGui::End();
    }
#else
    void DrawTextureLibrary(WorldEditorSession&, TexturePreviewCache*, bool&) {}
#endif

} // namespace engine::editor
