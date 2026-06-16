# icons/skills/branches/

Images d'**en-tête de colonne** de l'arbre de compétences (remplacent le texte
« Branche : Single / AoE / Def »). **3 fichiers** attendus, partagés par toutes
les classes :

```
icons/skills/branches/single.png   (colonne 1 — mono-cible / unitaire)
icons/skills/branches/aoe.png      (colonne 2 — zone)
icons/skills/branches/def.png      (colonne 3 — défense)
```

- Le renderer dessine l'image en **bannière** sur toute la largeur de la colonne,
  hauteur ~52 px. **Conseil** : fournir des images **larges** (ex. ratio ~4:1,
  type 256×64 ou 512×128) pour un rendu net sans étirement vertical.
- **Repli** : si une image est absente, la colonne réaffiche le libellé texte.
- Suivies par **Git LFS** (cf. `.gitattributes` : `game/data/icons/skills/**/*.png`).
