import Link from 'next/link'
import { redirect } from 'next/navigation'
import { query } from '@/lib/db'
import { getSession } from '@/lib/session'
import type { RowDataPacket } from 'mysql2/promise'
import { PlayerActions, type PlayerRow } from '@/components/admin/PlayerActions'

const PAGE_SIZE = 25

interface PageProps {
  searchParams: { page?: string; status?: string }
}

type AccountStatus = 'all' | 'active' | 'disabled'

function statusLabel(status: string) {
  if (status === 'active') return { label: 'Actif', color: 'var(--ln-success)', border: 'rgba(95,184,110,.4)', bg: 'rgba(95,184,110,.08)' }
  if (status === 'disabled') return { label: 'Désactivé', color: 'var(--ln-error)', border: 'rgba(196,64,64,.4)', bg: 'rgba(196,64,64,.08)' }
  return { label: status, color: 'var(--ln-muted)', border: 'var(--ln-border)', bg: 'transparent' }
}

function emailBadge(verified: number) {
  if (verified) return { label: 'Vérifié', color: 'var(--ln-success)', border: 'rgba(95,184,110,.4)', bg: 'rgba(95,184,110,.08)' }
  return { label: 'Non vérifié', color: 'var(--ln-warning)', border: 'rgba(232,165,92,.4)', bg: 'rgba(232,165,92,.08)' }
}

export default async function AdminPlayersPage({ searchParams }: PageProps) {
  const session = await getSession();
  if (!session) redirect('/login?redirect=/admin/players');
  if (session.role !== 'admin') redirect('/');

  const page = Math.max(1, parseInt(searchParams.page ?? '1', 10) || 1)
  const status: AccountStatus = (['active', 'disabled'].includes(searchParams.status ?? '') ? searchParams.status : 'all') as AccountStatus
  const offset = (page - 1) * PAGE_SIZE

  let players: PlayerRow[] = []
  let totalCount = 0
  let dbError = false

  try {
    const statusFilter = status === 'all' ? '' : "AND a.account_status = '" + status + "'"

    const countRows = await query<Array<RowDataPacket & { total: number }>>(
      `SELECT COUNT(DISTINCT a.id) as total FROM accounts a WHERE 1=1 ${statusFilter}`,
      []
    )
    totalCount = countRows[0]?.total ?? 0

    const rows = await query<Array<RowDataPacket & PlayerRow>>(
      `SELECT a.id, a.login, a.email, a.tag_id, a.account_status, a.email_verified,
              a.role, a.birth_date, a.disabled_reason,
              COUNT(DISTINCT ata.edition_id) as accepted_cgu_count,
              COUNT(DISTINCT te_pub.id) as published_cgu_count
       FROM accounts a
       LEFT JOIN account_terms_acceptances ata ON ata.account_id = a.id
       LEFT JOIN terms_editions te_pub ON te_pub.status = 'published'
       WHERE 1=1 ${statusFilter}
       GROUP BY a.id
       ORDER BY a.id DESC
       LIMIT ? OFFSET ?`,
      [PAGE_SIZE, offset]
    )
    players = rows as PlayerRow[]
  } catch (err) {
    console.error('[AdminPlayersPage] DB error:', err)
    dbError = true
  }

  const totalPages = Math.max(1, Math.ceil(totalCount / PAGE_SIZE))

  function pageUrl(p: number) {
    const params = new URLSearchParams()
    if (p > 1) params.set('page', String(p))
    if (status !== 'all') params.set('status', status)
    const qs = params.toString()
    return `/admin/players${qs ? '?' + qs : ''}`
  }

  function filterUrl(s: string) {
    const params = new URLSearchParams()
    if (s !== 'all') params.set('status', s)
    const qs = params.toString()
    return `/admin/players${qs ? '?' + qs : ''}`
  }

  const filterOptions: { value: AccountStatus; label: string }[] = [
    { value: 'all', label: 'Tous les joueurs' },
    { value: 'active', label: 'Actifs' },
    { value: 'disabled', label: 'Désactivés' },
  ]

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Gestion des joueurs</h1>
        <p>
          Liste paginée des comptes joueurs. Actions : validation email, activation / désactivation,
          gestion des personnages.
        </p>
      </div>

      {/* Stats */}
      <div className="wp-stats" style={{ marginBottom: '1.5rem' }}>
        <div className="wp-stat">
          <div className="wp-stat-value">{dbError ? '—' : totalCount}</div>
          <div className="wp-stat-label">Joueurs{status !== 'all' ? ` (${filterOptions.find(f => f.value === status)?.label})` : ''}</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{dbError ? '—' : page}</div>
          <div className="wp-stat-label">Page actuelle</div>
        </div>
        <div className="wp-stat">
          <div className="wp-stat-value">{dbError ? '—' : totalPages}</div>
          <div className="wp-stat-label">Pages totales</div>
        </div>
      </div>

      {/* Filter bar */}
      <div style={{ display: 'flex', gap: 8, marginBottom: '1.5rem', flexWrap: 'wrap', alignItems: 'center' }}>
        <span style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginRight: 4 }}>
          Filtre :
        </span>
        {filterOptions.map(opt => (
          <Link
            key={opt.value}
            href={filterUrl(opt.value)}
            style={{
              fontFamily: 'var(--font-ui)',
              fontSize: 10,
              letterSpacing: '.18em',
              textTransform: 'uppercase',
              padding: '5px 12px',
              borderRadius: 'var(--radius-sm)',
              border: `1px solid ${status === opt.value ? 'rgba(232,197,110,.5)' : 'var(--ln-border)'}`,
              color: status === opt.value ? 'var(--ln-accent)' : 'var(--ln-muted)',
              background: status === opt.value ? 'rgba(232,197,110,.06)' : 'none',
              textDecoration: 'none',
              transition: 'all .15s',
            }}
          >
            {opt.label}
          </Link>
        ))}
      </div>

      {/* Error state */}
      {dbError && (
        <div className="wp-alert error" style={{ marginBottom: '1.5rem' }}>
          <span>Impossible de charger les joueurs. Vérifiez la connexion à la base de données.</span>
        </div>
      )}

      {/* Players list */}
      {!dbError && players.length === 0 && (
        <div className="wp-card" style={{ textAlign: 'center', padding: '3rem 1rem' }}>
          <p style={{ margin: 0, color: 'var(--ln-muted)', fontStyle: 'italic' }}>
            Aucun joueur trouvé pour ce filtre.
          </p>
        </div>
      )}

      {!dbError && players.length > 0 && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          {players.map(player => {
            const st = statusLabel(player.account_status)
            const em = emailBadge(player.email_verified)
            const displayName = player.tag_id ?? player.login
            const cguOk = player.accepted_cgu_count >= player.published_cgu_count && player.published_cgu_count > 0
            const cguLabel = `${player.accepted_cgu_count}/${player.published_cgu_count} acceptée${player.published_cgu_count !== 1 ? 's' : ''}`

            return (
              <div key={player.id} className="wp-card" style={{ padding: '16px 20px' }}>
                {/* Header row */}
                <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12, flexWrap: 'wrap', marginBottom: 10 }}>
                  {/* Identity */}
                  <div style={{ flex: 1, minWidth: 180 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                      <span style={{ fontFamily: 'var(--font-display)', fontSize: 13, letterSpacing: '.12em', color: 'var(--ln-accent)' }}>
                        {displayName}
                      </span>
                      {player.tag_id && (
                        <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)' }}>
                          ({player.login})
                        </span>
                      )}
                      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-border)' }}>
                        #{player.id}
                      </span>
                    </div>
                    <div style={{ marginTop: 4, fontFamily: 'var(--font-body)', fontSize: 12, color: 'var(--ln-muted)' }}>
                      {player.email}
                    </div>
                  </div>

                  {/* Badges */}
                  <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', alignItems: 'center' }}>
                    {/* Role badge for admins */}
                    {player.role === 'admin' && (
                      <span style={{
                        display: 'inline-flex', alignItems: 'center',
                        fontFamily: 'var(--font-ui)', fontSize: '9.5px', letterSpacing: '.2em', textTransform: 'uppercase',
                        padding: '3px 10px', borderRadius: 100,
                        border: '1px solid rgba(232,197,110,.6)',
                        color: 'var(--ln-accent)', background: 'rgba(232,197,110,.1)', flexShrink: 0,
                      }}>
                        Admin
                      </span>
                    )}
                    {/* Account status */}
                    <span style={{
                      display: 'inline-flex', alignItems: 'center',
                      fontFamily: 'var(--font-ui)', fontSize: '9.5px', letterSpacing: '.2em', textTransform: 'uppercase',
                      padding: '3px 10px', borderRadius: 100, border: `1px solid ${st.border}`,
                      color: st.color, background: st.bg, flexShrink: 0,
                    }}>
                      {st.label}
                    </span>

                    {/* Email verified */}
                    <span style={{
                      display: 'inline-flex', alignItems: 'center',
                      fontFamily: 'var(--font-ui)', fontSize: '9.5px', letterSpacing: '.2em', textTransform: 'uppercase',
                      padding: '3px 10px', borderRadius: 100, border: `1px solid ${em.border}`,
                      color: em.color, background: em.bg, flexShrink: 0,
                    }}>
                      Email {em.label}
                    </span>

                    {/* CGU */}
                    <span style={{
                      display: 'inline-flex', alignItems: 'center',
                      fontFamily: 'var(--font-ui)', fontSize: '9.5px', letterSpacing: '.2em', textTransform: 'uppercase',
                      padding: '3px 10px', borderRadius: 100,
                      border: `1px solid ${cguOk ? 'rgba(95,184,110,.4)' : 'rgba(232,165,92,.4)'}`,
                      color: cguOk ? 'var(--ln-success)' : 'var(--ln-warning)',
                      background: cguOk ? 'rgba(95,184,110,.08)' : 'rgba(232,165,92,.08)',
                      flexShrink: 0,
                    }}>
                      CGU {cguLabel}
                    </span>
                  </div>
                </div>

                {/* Disabled reason */}
                {player.account_status === 'disabled' && player.disabled_reason && (
                  <div style={{
                    marginBottom: 10, padding: '7px 12px',
                    background: 'rgba(196,64,64,.06)', borderRadius: 'var(--radius-sm)',
                    border: '1px solid rgba(196,64,64,.2)',
                    fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 12, color: 'var(--ln-error)',
                  }}>
                    Motif : {player.disabled_reason}
                  </div>
                )}

                {/* Actions */}
                <PlayerActions player={player} />
              </div>
            )
          })}
        </div>
      )}

      {/* Pagination */}
      {!dbError && totalPages > 1 && (
        <div style={{ display: 'flex', gap: 6, justifyContent: 'center', alignItems: 'center', marginTop: '2rem', flexWrap: 'wrap' }}>
          {page > 1 && (
            <Link href={pageUrl(page - 1)} style={{
              fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.18em', textTransform: 'uppercase',
              padding: '6px 14px', borderRadius: 'var(--radius-sm)',
              border: '1px solid var(--ln-border)', color: 'var(--ln-muted)',
              background: 'none', textDecoration: 'none',
            }}>
              &larr; Préc.
            </Link>
          )}

          {Array.from({ length: Math.min(totalPages, 9) }, (_, i) => {
            // Show pages around current
            let p: number
            if (totalPages <= 9) {
              p = i + 1
            } else if (page <= 5) {
              p = i + 1
            } else if (page >= totalPages - 4) {
              p = totalPages - 8 + i
            } else {
              p = page - 4 + i
            }
            const isCurrent = p === page
            return (
              <Link key={p} href={pageUrl(p)} style={{
                fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.18em',
                padding: '6px 12px', borderRadius: 'var(--radius-sm)',
                border: `1px solid ${isCurrent ? 'rgba(232,197,110,.5)' : 'var(--ln-border)'}`,
                color: isCurrent ? 'var(--ln-accent)' : 'var(--ln-muted)',
                background: isCurrent ? 'rgba(232,197,110,.06)' : 'none',
                textDecoration: 'none',
                minWidth: 36, textAlign: 'center',
              }}>
                {p}
              </Link>
            )
          })}

          {page < totalPages && (
            <Link href={pageUrl(page + 1)} style={{
              fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.18em', textTransform: 'uppercase',
              padding: '6px 14px', borderRadius: 'var(--radius-sm)',
              border: '1px solid var(--ln-border)', color: 'var(--ln-muted)',
              background: 'none', textDecoration: 'none',
            }}>
              Suiv. &rarr;
            </Link>
          )}
        </div>
      )}

      {/* Back link */}
      <div style={{ marginTop: '2rem' }}>
        <Link href="/admin" style={{
          fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.18em', textTransform: 'uppercase',
          color: 'var(--ln-muted)', textDecoration: 'none',
        }}>
          &larr; Retour à l&apos;administration
        </Link>
      </div>
    </div>
  )
}
