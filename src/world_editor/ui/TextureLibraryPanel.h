#pragma once

namespace engine::editor
{
    class WorldEditorSession;
    class TexturePreviewCache;

    /// Dessine le panneau ImGui "Bibliotheque de textures" (Win32 uniquement,
    /// no-op ailleurs). Affiche les 4 procedurales builtin + toutes les .texr
    /// importees, avec assignation au layer actif (m_session->SplatLayer())
    /// au clic. Dirty propage via MarkSplatRefsDirty.
    /// \param session Session editeur active (Doc().textureAssets, SplatLayer()).
    /// \param cache Cache de vignettes (peut etre nul -> grilles d'attente).
    /// \param openFlag Flag controlant l'ouverture du panneau ; modifie par la
    ///   case de fermeture ImGui ou par l'item de menu (cf. WorldEditorImGui).
    void DrawTextureLibrary(WorldEditorSession& session,
                            TexturePreviewCache* cache,
                            bool& openFlag);

} // namespace engine::editor
