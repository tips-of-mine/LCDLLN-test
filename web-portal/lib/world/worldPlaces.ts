// PORTAL.2 — galerie des villes et lieux du monde (vues de dessus générées par la CI).
//
// Fichiers publiés par scripts/publish-portal-maps.sh dans public/maps/<monde>/places/ :
//   index.json      liste des vues (nom, genre, région, description du lore), sans secrets
//   <id>.webp       vue de dessus (repli : <id>.png si la conversion WebP n'a pas eu lieu)
// Absents en développement : la galerie affiche « bientôt disponible ».

import fs from 'node:fs/promises'
import path from 'node:path'

export type WorldPlace = {
  id: string
  name: string
  /** Genre du lore (capitale, ville, hameau, temple, ruine…). */
  kind: string
  /** Libellé français du genre, prêt à afficher. */
  kindLabel: string
  city: boolean
  region: string
  description: string
  imageUrl: string
}

export type WorldPlacesGroup = { region: string; places: WorldPlace[] }

const KIND_LABELS: Record<string, string> = {
  capitale: 'Capitale',
  grande_ville: 'Grande ville',
  ville: 'Ville',
  forteresse: 'Forteresse',
  village: 'Village',
  hameau: 'Hameau',
  camp: 'Camp',
  temple: 'Temple',
  cite: 'Cité',
  volcan: 'Volcan',
  champ_bataille: 'Champ de bataille',
  port: 'Port',
  foret: 'Forêt',
  faille: 'Faille',
  sanctuaire: 'Sanctuaire',
  ruine: 'Ruines',
  defile: 'Défilé',
}

/** Libellé lisible d'un genre du lore (repli : le genre, première lettre en majuscule). */
export function kindLabel(kind: string): string {
  return KIND_LABELS[kind] ?? (kind ? kind.charAt(0).toUpperCase() + kind.slice(1).replace(/_/g, ' ') : '')
}

/** Nom de fichier sûr (le générateur n'écrit que [A-Za-z0-9_-] + extension). */
function safeFile(f: unknown): string | null {
  return typeof f === 'string' && /^[A-Za-z0-9_-]+\.png$/.test(f) ? f : null
}

/**
 * Lit la galerie publiée d'un monde, groupée par région (ordre de l'index :
 * régions, villes avant lieux, noms). Ne lève jamais : galerie vide si absente.
 */
export async function loadWorldPlaces(world: string): Promise<WorldPlacesGroup[]> {
  const dir = path.join(process.cwd(), 'public', 'maps', world, 'places')
  let raw: unknown
  try {
    raw = JSON.parse(await fs.readFile(path.join(dir, 'index.json'), 'utf8'))
  } catch {
    return []
  }
  const list = (raw as { places?: unknown }).places
  if (!Array.isArray(list)) return []
  let files: Set<string>
  try {
    files = new Set(await fs.readdir(dir))
  } catch {
    return []
  }
  const groups = new Map<string, WorldPlace[]>()
  for (const e of list as Record<string, unknown>[]) {
    const png = safeFile(e.file)
    if (!png || typeof e.name !== 'string') continue
    const webp = png.replace(/\.png$/, '.webp')
    const file = files.has(webp) ? webp : files.has(png) ? png : null
    if (!file) continue
    const kind = typeof e.kind === 'string' ? e.kind : ''
    const region = typeof e.region === 'string' && e.region ? e.region : 'Autres terres'
    const place: WorldPlace = {
      id: typeof e.id === 'string' ? e.id : png,
      name: e.name,
      kind,
      kindLabel: kindLabel(kind),
      city: e.city === true,
      region,
      description: typeof e.description === 'string' ? e.description : '',
      imageUrl: `/maps/${world}/places/${file}`,
    }
    if (!groups.has(region)) groups.set(region, [])
    groups.get(region)!.push(place)
  }
  return Array.from(groups, ([region, places]) => ({ region, places }))
}
