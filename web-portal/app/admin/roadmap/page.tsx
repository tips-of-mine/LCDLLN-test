import Link from 'next/link'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'
import RoadmapAdmin from './RoadmapAdmin'

type RoadmapItem = RowDataPacket & {
  id: number
  title: string
  description: string | null
  status: 'completed' | 'in_progress' | 'planned'
  category: string | null
  display_order: number
}

export default async function AdminRoadmapPage() {
  let items: RoadmapItem[] = []
  let dbError = false
  try {
    items = await query<RoadmapItem[]>(
      'SELECT id, title, description, status, category, display_order FROM roadmap_items ORDER BY display_order ASC'
    )
  } catch {
    dbError = true
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Roadmap — Administration</h1>
        <p>
          Gérez les items de la feuille de route. Les modifications sont répercutées
          immédiatement sur la page publique.
        </p>
      </div>

      {dbError ? (
        <div className="wp-card" style={{ borderColor: 'rgba(220,50,50,.5)', color: '#e05050' }}>
          Impossible de charger les items (base de données non disponible).
        </div>
      ) : (
        <RoadmapAdmin items={items} />
      )}

      <div style={{ marginTop: '2rem' }}>
        <Link href="/admin" style={{ textDecoration: 'none' }}>
          <span className="wp-badge planned" style={{ cursor: 'pointer' }}>&larr; Retour à l&apos;administration</span>
        </Link>
        <span style={{ margin: '0 8px', color: 'var(--ln-muted)' }}>|</span>
        <Link href="/roadmap" style={{ textDecoration: 'none' }}>
          <span className="wp-badge active" style={{ cursor: 'pointer' }}>Voir la page publique &rarr;</span>
        </Link>
      </div>
    </div>
  )
}
