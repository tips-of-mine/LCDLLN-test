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
      {/* Lot carte v1.0.132 — ce qui attend le voyageur (texte vitrine, sans lieu secret). */}
      <section className="wp-map-intro">
        <h2>Ce qui attend le voyageur</h2>
        <ul>
          <li>Des montagnes à l&apos;échelle des Pyrénées, sommets enneigés vers 3 400 m, franchies par des cols.</li>
          <li>Huit grandes rivières et près de deux cents ruisseaux, enjambés par des ponts de pierre et de bois.</li>
          <li>Des routes qui serpentent entre les grandes collines, les forêts et les déserts.</li>
          <li>La Tour Carrée de la Lune Noire, le château de Garundel et ses douves, le Palais des Maîtres des Dragons, des tours de guet, des phares et des cascades en gradins, intacts ou en ruine.</li>
          <li>Des ports au bord de la mer, d&apos;où partent les bateaux vers les îles.</li>
        </ul>
      </section>
      <p className="wp-map-cta">
        <Link href="/monde/lieux" className="btn btn-accent">Voir les villes et lieux vus du ciel</Link>
      </p>
      {maps.map((m) => (
        <WorldMapView key={m.world} map={m} />
      ))}
    </div>
  )
}
