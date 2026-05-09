import Link from 'next/link'
import { redirect } from 'next/navigation'
import { query } from '@/lib/db/connection'
import { getSession } from '@/lib/auth/session'
import type { RowDataPacket } from 'mysql2/promise'
import FaqAdmin from '@/components/admin/FaqAdmin'

type FaqItem = RowDataPacket & {
  id: number
  question: string
  answer: string
  category: string | null
  display_order: number
  published: number
}

export default async function AdminFaqPage() {
  const session = await getSession();
  if (!session) redirect('/login?redirect=/admin/faq');
  if (session.role !== 'admin') redirect('/');

  let items: FaqItem[] = []
  let dbError = false
  try {
    items = await query<FaqItem[]>(
      'SELECT id, question, answer, category, display_order, published FROM faq_items ORDER BY display_order ASC, id ASC'
    )
  } catch {
    dbError = true
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>FAQ — Administration</h1>
        <p>
          Gérez les questions fréquemment posées. Les questions publiées sont affichées
          immédiatement sur la page support publique.
        </p>
      </div>

      {dbError ? (
        <div className="wp-card" style={{ borderColor: 'rgba(220,50,50,.5)', color: '#e05050' }}>
          Impossible de charger les questions (base de données non disponible).
        </div>
      ) : (
        <FaqAdmin items={items} />
      )}

      <div style={{ marginTop: '2rem' }}>
        <Link href="/admin" style={{ textDecoration: 'none' }}>
          <span className="wp-badge planned" style={{ cursor: 'pointer' }}>&larr; Retour à l&apos;administration</span>
        </Link>
        <span style={{ margin: '0 8px', color: 'var(--ln-muted)' }}>|</span>
        <Link href="/support" style={{ textDecoration: 'none' }}>
          <span className="wp-badge active" style={{ cursor: 'pointer' }}>Voir la page support &rarr;</span>
        </Link>
      </div>
    </div>
  )
}
