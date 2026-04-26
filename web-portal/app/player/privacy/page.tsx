import { query } from '@/lib/db'
import { getSession } from '@/lib/session'
import { redirect } from 'next/navigation'
import type { RowDataPacket } from 'mysql2/promise'
import { PrivacyForm } from '@/components/PrivacyForm'
import { CguAcceptButton } from '@/components/CguAcceptButton'

type CguRow = RowDataPacket & {
  id: number
  version_label: string
  published_at: string
  status: string
  accepted_at: string | null
  title: string
}

type PrivacyRow = RowDataPacket & {
  profile_visibility: 'public' | 'friends' | 'none'
}

export default async function PrivacyPage() {
  const session = await getSession()
  if (!session) redirect('/login')

  const accountId = session.accountId

  let cguRows: CguRow[] = []
  try {
    cguRows = await query<CguRow[]>(
      `SELECT te.id, te.version_label, te.published_at, te.status,
              ata.accepted_at,
              COALESCE(tl.title, te.version_label) as title
       FROM terms_editions te
       LEFT JOIN terms_localizations tl ON tl.edition_id = te.id AND tl.locale = 'fr'
       LEFT JOIN account_terms_acceptances ata ON ata.edition_id = te.id AND ata.account_id = ?
       WHERE te.status = 'published'
       ORDER BY te.published_at DESC, te.id DESC`,
      [accountId]
    )
  } catch (err) {
    console.error('[PrivacyPage] CGU query error:', err)
  }

  let privacyRows: PrivacyRow[] = []
  try {
    privacyRows = await query<PrivacyRow[]>(
      'SELECT profile_visibility FROM account_privacy_settings WHERE account_id = ?',
      [accountId]
    )
  } catch (err) {
    console.error('[PrivacyPage] Privacy query error:', err)
  }

  const currentVisibility: 'public' | 'friends' | 'none' =
    privacyRows[0]?.profile_visibility ?? 'public'

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Vie Privée</h1>
        <p>Gérez vos conditions d&apos;utilisation et la visibilité de votre profil.</p>
      </div>

      {/* Section 1: CGU */}
      <div className="wp-section-title">Conditions Générales d&apos;Utilisation</div>
      <div className="wp-card" style={{ padding: 0, overflow: 'hidden' }}>
        {cguRows.length === 0 ? (
          <p style={{ padding: '1rem', color: 'var(--ln-muted)', fontStyle: 'italic' }}>
            Aucune condition générale publiée.
          </p>
        ) : (
          <table style={{ width: '100%', borderCollapse: 'collapse' }}>
            <thead>
              <tr style={{ borderBottom: '1px solid var(--ln-border)', background: 'rgba(0,0,0,.15)' }}>
                {['Version', 'Titre', 'Date de publication', 'Statut', ''].map((h) => (
                  <th
                    key={h}
                    style={{
                      padding: '10px 16px',
                      textAlign: 'left',
                      fontSize: 11,
                      letterSpacing: '.15em',
                      textTransform: 'uppercase',
                      color: 'var(--ln-muted)',
                      fontFamily: 'var(--font-display)',
                    }}
                  >
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {cguRows.map((row, i) => (
                <tr
                  key={row.id}
                  style={{
                    borderBottom: i < cguRows.length - 1 ? '1px solid var(--ln-border)' : 'none',
                  }}
                >
                  <td style={{ padding: '12px 16px', fontSize: 13, color: 'var(--ln-muted)', fontFamily: 'var(--font-display)', letterSpacing: '.05em' }}>
                    {row.version_label}
                  </td>
                  <td style={{ padding: '12px 16px', fontSize: 13, color: 'var(--ln-text)' }}>
                    {row.title}
                  </td>
                  <td style={{ padding: '12px 16px', fontSize: 13, color: 'var(--ln-muted)' }}>
                    {new Date(row.published_at).toLocaleDateString('fr-FR', {
                      day: '2-digit', month: 'long', year: 'numeric',
                    })}
                  </td>
                  <td style={{ padding: '12px 16px' }}>
                    {row.accepted_at ? (
                      <span className="wp-badge active">
                        Accepté le{' '}
                        {new Date(row.accepted_at).toLocaleDateString('fr-FR', {
                          day: '2-digit', month: 'long', year: 'numeric',
                        })}
                      </span>
                    ) : (
                      <span className="wp-badge planned">Non accepté</span>
                    )}
                  </td>
                  <td style={{ padding: '12px 16px' }}>
                    {!row.accepted_at && <CguAcceptButton editionId={row.id} />}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {/* Section 2: Profile visibility */}
      <div className="wp-section-title" style={{ marginTop: '2rem' }}>Visibilité du profil</div>
      <div className="wp-card">
        <p style={{ margin: '0 0 16px', fontSize: 14, color: 'var(--ln-muted)' }}>
          Choisissez qui peut voir votre profil de joueur.
        </p>
        <PrivacyForm initialVisibility={currentVisibility} />
      </div>
    </div>
  )
}
