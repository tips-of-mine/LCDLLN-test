// PORTAL.1 — page publique « Le monde » : cartes des Royaumes et des Enfers,
// telles qu'elles existent dans le jeu (sans aucun lieu secret).

import type { Metadata } from 'next'
import Link from 'next/link'
import { loadWorldMap, WORLD_MAP_IDS } from '@/lib/world/worldMaps'
import WorldMapView from './WorldMapView'

export const metadata: Metadata = {
  title: 'Le monde — Les Chroniques de la Lune Noire',
  description: 'Les cartes des Royaumes de la Lune Noire et des Enfers, telles qu’elles existent dans le jeu.',
}

// Lecture des fichiers à chaque requête : une nouvelle affiche publiée par la CI
// s'affiche sans reconstruire l'image du portail.
export const dynamic = 'force-dynamic'

export default async function WorldPage() {
  const maps = await Promise.all(WORLD_MAP_IDS.map((w) => loadWorldMap(w)))

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Le monde</h1>
        <p>
          Les Royaumes de la Lune Noire, des forêts de l&apos;ouest aux déserts du sud, et
          les Enfers, domaine de la Légion. Ces cartes sont générées à partir du monde du jeu
          et évoluent avec lui : certains lieux restent à découvrir en jouant.
        </p>
      </div>
      <p className="wp-map-cta">
        <Link href="/monde/lieux" className="btn btn-accent">Voir les villes et lieux vus du ciel</Link>
      </p>
      {maps.map((m) => (
        <WorldMapView key={m.world} map={m} />
      ))}
    </div>
  )
}
