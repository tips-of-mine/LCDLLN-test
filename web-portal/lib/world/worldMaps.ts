// PORTAL.1 — cartes du monde publiées sur le portail (vitrine du jeu).
//
// Les affiches sont produites par la CI (tools/world_gen --portal-out), converties
// en WebP et copiées dans public/maps/<monde>/ par scripts/pack-linux-docker-bundle.sh :
//   portal_map.webp        affiche pleine définition (4096 px Royaumes, 2048 px Enfers)
//   portal_map_1600.webp   aperçu affiché dans la page
//   portal_map.json        étiquettes (villes importantes, régions, lieux), SANS secrets
// En développement (pas de bundle CI), les fichiers sont absents : la page affiche
// « carte bientôt disponible ».

import fs from 'node:fs/promises'
import path from 'node:path'

export type MapLabel = { name: string; kind?: string; u: number; v: number }

export type WorldMap = {
  /** Identifiant du monde (`royaumes`, `enfers`). */
  world: string
  /** Nom affiché (lu dans portal_map.json). */
  displayName: string
  /** URL de l'aperçu (null si l'affiche n'a pas été publiée). */
  previewUrl: string | null
  /** URL de l'affiche pleine définition. */
  fullUrl: string | null
  cities: MapLabel[]
  regions: MapLabel[]
  pois: MapLabel[]
}

/** Mondes publiés, dans l'ordre d'affichage. */
export const WORLD_MAP_IDS = ['royaumes', 'enfers'] as const

const FALLBACK_NAMES: Record<string, string> = {
  royaumes: 'Les Royaumes de la Lune Noire',
  enfers: 'Les Enfers',
}

/** Répertoire public servi par Next (standalone : /app/public). */
function publicDir(): string {
  return path.join(process.cwd(), 'public')
}

async function exists(p: string): Promise<boolean> {
  try {
    await fs.access(p)
    return true
  } catch {
    return false
  }
}

/** Garde les seules étiquettes bien formées et dans l'image. */
function sanitizeLabels(raw: unknown): MapLabel[] {
  if (!Array.isArray(raw)) return []
  return raw
    .filter(
      (l): l is MapLabel =>
        typeof l === 'object' && l !== null &&
        typeof (l as MapLabel).name === 'string' &&
        typeof (l as MapLabel).u === 'number' && typeof (l as MapLabel).v === 'number' &&
        (l as MapLabel).u >= 0 && (l as MapLabel).u <= 1 && (l as MapLabel).v >= 0 && (l as MapLabel).v <= 1
    )
    .map((l) => ({ name: l.name, kind: typeof l.kind === 'string' ? l.kind : undefined, u: l.u, v: l.v }))
}

/**
 * Lit l'affiche publiée d'un monde. Ne lève jamais : un fichier absent ou
 * illisible donne une carte « bientôt disponible » (previewUrl = null).
 */
export async function loadWorldMap(world: string): Promise<WorldMap> {
  const dir = path.join(publicDir(), 'maps', world)
  const base: WorldMap = {
    world,
    displayName: FALLBACK_NAMES[world] ?? world,
    previewUrl: null,
    fullUrl: null,
    cities: [],
    regions: [],
    pois: [],
  }
  try {
    const json = JSON.parse(await fs.readFile(path.join(dir, 'portal_map.json'), 'utf8'))
    if (typeof json.display_name === 'string') base.displayName = json.display_name
    base.cities = sanitizeLabels(json.cities)
    base.regions = sanitizeLabels(json.regions)
    base.pois = sanitizeLabels(json.pois)
  } catch {
    return base
  }
  // Aperçu WebP de préférence ; repli PNG si la conversion n'a pas eu lieu.
  for (const [preview, full] of [
    ['portal_map_1600.webp', 'portal_map.webp'],
    ['portal_map.png', 'portal_map.png'],
  ]) {
    if (await exists(path.join(dir, preview))) {
      base.previewUrl = `/maps/${world}/${preview}`
      base.fullUrl = (await exists(path.join(dir, full))) ? `/maps/${world}/${full}` : base.previewUrl
      break
    }
  }
  return base
}
