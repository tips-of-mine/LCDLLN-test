import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'

type RoadmapItem = RowDataPacket & {
  id: number
  title: string
  description: string | null
  status: 'completed' | 'in_progress' | 'planned'
  category: string | null
}

const STATUS_TL_CLASS: Record<string, string> = {
  completed: 'wp-tl-item done',
  in_progress: 'wp-tl-item active',
  planned: 'wp-tl-item',
}

const STATUS_BADGE_CLASS: Record<string, string> = {
  completed: 'wp-badge done',
  in_progress: 'wp-badge active',
  planned: 'wp-badge planned',
}

const STATUS_BADGE_LABEL: Record<string, string> = {
  completed: 'Terminé',
  in_progress: 'En cours',
  planned: 'Planifié',
}

const STATUS_CARD_STYLE: Record<string, React.CSSProperties> = {
  completed: {},
  in_progress: { borderColor: 'rgba(232,165,92,.35)' },
  planned: {},
}

export default async function RoadmapPage() {
  let items: RoadmapItem[] = []
  try {
    items = await query<RoadmapItem[]>(
      'SELECT id, title, description, status, category FROM roadmap_items ORDER BY display_order ASC'
    )
  } catch {
    // Fallback to empty list if DB is unavailable
  }

  const stats = {
    completed: items.filter(i => i.status === 'completed').length,
    in_progress: items.filter(i => i.status === 'in_progress').length,
    planned: items.filter(i => i.status === 'planned').length,
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Roadmap</h1>
        <p>
          Feuille de route indicative du projet. Les priorités peuvent évoluer
          en fonction des retours de la communauté.
        </p>
      </div>

      <div className="wp-stats">
        <div className="wp-stat">
          <div className="wp-stat-value">{stats.completed}</div>
          <div className="wp-stat-label">Complétés</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{stats.in_progress}</div>
          <div className="wp-stat-label">En cours</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{stats.planned}</div>
          <div className="wp-stat-label">Planifiés</div>
        </div>
      </div>

      <div className="wp-section-title" style={{ marginTop: '2rem' }}>Historique &amp; prochaines étapes</div>

      <div className="wp-timeline">
        {items.length === 0 && (
          <div className="wp-tl-item">
            <div className="wp-card">
              <p style={{ margin: 0, color: 'var(--ln-muted)', fontSize: 14 }}>
                La roadmap est en cours de chargement ou aucun item n&apos;a encore été ajouté.
              </p>
            </div>
          </div>
        )}
        {items.map(item => (
          <div key={item.id} className={STATUS_TL_CLASS[item.status] ?? 'wp-tl-item'}>
            <div className="wp-card" style={STATUS_CARD_STYLE[item.status]}>
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: 8 }}>
                <strong style={{ fontFamily: 'var(--font-display)' }}>{item.title}</strong>
                <span className={STATUS_BADGE_CLASS[item.status] ?? 'wp-badge planned'}>
                  {STATUS_BADGE_LABEL[item.status] ?? item.status}
                </span>
              </div>
              {item.description && (
                <p style={{ fontSize: 13, margin: '8px 0 0', color: 'var(--ln-muted)' }}>
                  {item.description}
                </p>
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
