# Asset pipeline — Mixamo → glTF

Procédure pour intégrer un personnage Mixamo dans le client de jeu.

## Pré-requis

- Compte Adobe gratuit pour télécharger depuis https://www.mixamo.com.
- PowerShell 5+.

## Première fois : récupérer le convertisseur FBX→glTF

```powershell
.\tools\asset_pipeline\download_fbx2gltf.ps1
```

Télécharge `FBX2glTF.exe` (~8 MB, Godot fork, MIT) dans `tools/asset_pipeline/bin/` avec
vérification SHA256. Le binaire est gitignored (pas dans le repo).

## Workflow par asset

1. Sur Mixamo, choisir un personnage et **télécharger en format "FBX Binary"** (PAS
   FBX ASCII, PAS FBX for Unity, PAS FBX 7.4 / 6.1, PAS Collada).
2. Pour ajouter une animation au personnage : sur Mixamo, choisir l'animation puis cocher
   "with skin" → l'export FBX contient mesh + skeleton + clip baked.
3. Déposer le `.fbx` dans `tools/asset_pipeline/inbox/<nom>.fbx`.
4. Convertir (script ajouté en Task 9) :
   ```powershell
   .\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot -Category avatars
   ```
   → produit `game/data/models/<Category>/<EntityName>/<EntityName>.glb`.
5. Commit le `.glb` (le `.fbx` source reste gitignored).

## Versions pinned

- FBX2glTF : v0.13.0 (Godot fork). Pour mettre à jour : modifier `$Version` dans `download_fbx2gltf.ps1` + recalculer SHA256.
