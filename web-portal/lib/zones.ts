// Résolution zoneId → nom de région lisible pour l'affichage admin (présence
// enrichie). Le master/shard transmet un `zoneId` numérique ; la "présentation"
// (nom humain) vit ici, côté portail, pour rester facilement éditable sans
// toucher au serveur.
//
// Table volontairement maintenable à la main : compléter au fur et à mesure que
// des zones stables sont définies côté jeu (game/data/zones). Tout zoneId absent
// retombe sur « Zone {id} ».

const ZONE_NAMES: Record<number, string> = {
  // 42: "Forêt tempérée",   // exemple — à compléter avec les zoneId réels du jeu
};

/** Nom de région pour un zoneId. Fallback « Zone {id} ». null/0 → « — ». */
export function regionName(zoneId: number | null | undefined): string {
  if (zoneId === null || zoneId === undefined || zoneId <= 0) return "—";
  return ZONE_NAMES[zoneId] ?? `Zone ${zoneId}`;
}
