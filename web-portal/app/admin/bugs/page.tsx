import Link from 'next/link'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'
import BugAdmin from '@/components/admin/BugAdmin'

export type BugReport = RowDataPacket & {
  id: number
  title: string
  body: string
  category: string
  created_at: string
  admin_status: 'pending' | 'confirmed' | 'in_progress' | 'resolved' | 'not_a_bug'
  admin_comment: string | null
  exploit_awarded: number
  reporter_login: string | null
  reporter_id: number | null
}

const VALID_STATUSES = ['all', 'pending', 'confirmed', 'in_progress', 'resolved', 'not_a_bug'] as const
type StatusFilter = (typeof VALID_STATUSES)[number]

export default async function AdminBugsPage({
  searchParams,
}: {
  searchParams: { status?: string }
}) {
  const rawStatus = searchParams.status ?? 'all'
  const status: StatusFilter = VALID_STATUSES.includes(rawStatus as StatusFilter)
    ? (rawStatus as StatusFilter)
    : 'all'

  let bugs: BugReport[] = []
  let dbError = false

  try {
    bugs = await query<BugReport[]>(
      `SELECT br.id, br.title, br.body, br.category, br.created_at,
              br.admin_status, br.admin_comment, br.exploit_awarded,
              a.login AS reporter_login, a.id AS reporter_id
       FROM bug_reports br
       LEFT JOIN accounts a ON a.id = br.account_id
       WHERE (? = 'all' OR br.admin_status = ?)
       ORDER BY br.created_at DESC
       LIMIT 50`,
      [status, status]
    )
  } catch {
    dbError = true
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Bugs — Administration</h1>
        <p>
          Suivez et gérez les signalements de bugs. Changez le statut, ajoutez un commentaire et attribuez des exploits aux joueurs méritants.
        </p>
      </div>

      {dbError ? (
        <div
          className="wp-card"
          style={{ borderColor: 'rgba(220,50,50,.5)', color: '#e05050' }}
        >
          Impossible de charger les signalements (base de données non disponible).
        </div>
      ) : (
        <BugAdmin bugs={bugs} currentStatus={status} />
      )}

      <div style={{ marginTop: '2rem' }}>
        <Link href="/admin" style={{ textDecoration: 'none' }}>
          <span className="wp-badge planned" style={{ cursor: 'pointer' }}>
            &larr; Retour à l&apos;administration
          </span>
        </Link>
      </div>
    </div>
  )
}
