// PORTAL.1 — affiche d'un monde avec ses étiquettes (villes, régions, lieux).
// Image statique : les noms sont posés en HTML par-dessus (u, v en %), ce qui
// les garde nets à toute taille. Composant serveur, sans JavaScript côté client.

import type { WorldMap } from '@/lib/world/worldMaps'

/** Classe CSS d'une ville selon son importance (taille du nom et du repère). */
function cityClass(kind?: string): string {
  if (kind === 'capitale') return 'wp-map-city capital'
  if (kind === 'grande_ville' || kind === 'camp') return 'wp-map-city major'
  return 'wp-map-city'
}

export default function WorldMapView({ map }: { map: WorldMap }) {
  // Une seule région couvrant toute la carte (Enfers) : son nom est déjà le titre.
  const regions = map.regions.length > 1 ? map.regions : []

  return (
    <section className="wp-map-block">
      <h2 className="wp-map-title">{map.displayName}</h2>
      {map.previewUrl ? (
        <>
          <div className="wp-map-frame">
            {/* eslint-disable-next-line @next/next/no-img-element */}
            <img src={map.previewUrl} alt={`Carte : ${map.displayName}`} className="wp-map-img" loading="lazy" />
            <div className="wp-map-banner">Carte en préparation</div>
            {regions.map((r) => (
              <span key={`r-${r.name}`} className="wp-map-region" style={{ left: `${r.u * 100}%`, top: `${r.v * 100}%` }}>
                {r.name}
              </span>
            ))}
            {map.pois.map((p) => (
              <span key={`p-${p.name}`} className="wp-map-poi" style={{ left: `${p.u * 100}%`, top: `${p.v * 100}%` }}>
                {p.name}
              </span>
            ))}
            {map.cities.map((c) => (
              <span key={`c-${c.name}`} className={cityClass(c.kind)} style={{ left: `${c.u * 100}%`, top: `${c.v * 100}%` }}>
                {c.name}
              </span>
            ))}
          </div>
          {map.fullUrl && map.fullUrl !== map.previewUrl && (
            <p className="wp-map-link">
              <a href={map.fullUrl} target="_blank" rel="noopener noreferrer">
                Ouvrir la carte en pleine définition
              </a>
            </p>
          )}
        </>
      ) : (
        <div className="wp-card wp-map-empty">Carte bientôt disponible.</div>
      )}
    </section>
  )
}
