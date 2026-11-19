// PORTAL.2 — page publique « Villes et lieux » : vue de dessus de chaque ville du
// lore et des lieux remarquables construits, groupées par région. Aucun lieu secret.

import type { Metadata } from 'next'
import Link from 'next/link'
import { loadWorldPlaces } from '@/lib/world/worldPlaces'
import { WORLD_MAP_IDS, loadWorldMap } from '@/lib/world/worldMaps'

export const metadata: Metadata = {
  title: 'Villes et lieux — Les Chroniques de la Lune Noire',
  description: 'Les villes et les lieux des Royaumes de la Lune Noire et des Enfers, vus du ciel, tels qu’ils existent dans le jeu.',
}

// Lecture des fichiers à chaque requête : une galerie republiée s'affiche sans reconstruire le portail.
export const dynamic = 'force-dynamic'

export default async function WorldPlacesPage() {
  const worlds = await Promise.all(
    WORLD_MAP_IDS.map(async (w) => ({ world: w, name: (await loadWorldMap(w)).displayName, groups: await loadWorldPlaces(w) }))
  )
  const total = worlds.reduce((n, w) => n + w.groups.reduce((m, g) => m + g.places.length, 0), 0)

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Villes et lieux</h1>
        <p>
          Chaque ville et chaque lieu, vu du ciel tel qu&apos;il existe dans le jeu : rues, maisons,
          remparts, routes et forêts. Les vues sont régénérées avec le monde et s&apos;enrichissent à
          chaque version. <Link href="/monde">Voir les cartes du monde</Link>.
        </p>
      </div>
      {total === 0 && <div className="wp-card wp-map-empty">Galerie bientôt disponible.</div>}
      {worlds.map((w) =>
        w.groups.length === 0 ? null : (
          <section key={w.world} className="wp-places-world">
            <h2 className="wp-map-title">{w.name}</h2>
            {w.groups.map((g) => (
              <div key={g.region} className="wp-places-region">
                <h3 className="wp-places-region-title">{g.region}</h3>
                <div className="wp-places-grid">
                  {g.places.map((p) => (
                    <figure key={p.id} className="wp-place-card">
                      <a href={p.imageUrl} target="_blank" rel="noopener noreferrer" title="Agrandir">
                        {/* eslint-disable-next-line @next/next/no-img-element */}
                        <img src={p.imageUrl} alt={`Vue du ciel : ${p.name}`} loading="lazy" className="wp-place-img" />
                      </a>
                      <figcaption>
                        <span className="wp-place-name">{p.name}</span>
                        <span className="wp-place-kind">{p.kindLabel}</span>
                        {p.description && <span className="wp-place-desc">{p.description}</span>}
                      </figcaption>
                    </figure>
                  ))}
                </div>
              </div>
            ))}
          </section>
        )
      )}
    </div>
  )
}
