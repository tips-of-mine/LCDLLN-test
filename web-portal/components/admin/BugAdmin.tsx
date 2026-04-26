'use client'

import { useState, useTransition } from 'react'
import { useRouter } from 'next/navigation'
import type { BugReport } from '@/app/admin/bugs/page'

type AdminStatus = 'pending' | 'confirmed' | 'in_progress' | 'resolved' | 'not_a_bug'
type StatusFilter = 'all' | AdminStatus

const STATUS_LABELS: Record<AdminStatus | 'all', string> = {
  all: 'Tous',
  pending: 'En attente',
  confirmed: 'Confirmé',
  in_progress: 'En cours',
  resolved: 'Résolu',
  not_a_bug: 'Pas un bug',
}

const STATUS_BADGE: Record<AdminStatus, string> = {
  pending: 'wp-badge planned',
  confirmed: 'wp-badge active',
  in_progress: 'wp-badge active',
  resolved: 'wp-badge done',
  not_a_bug: 'wp-badge',
}

const STATUS_BADGE_STYLE: Partial<Record<AdminStatus, React.CSSProperties>> = {
  not_a_bug: { background: 'rgba(120,120,120,.2)', color: 'var(--ln-muted)' },
}

const ALL_STATUSES: AdminStatus[] = ['pending', 'confirmed', 'in_progress', 'resolved', 'not_a_bug']
const FILTER_TABS: StatusFilter[] = ['all', ...ALL_STATUSES]

// Statuses where admin_comment is relevant
const SHOW_COMMENT_FOR: AdminStatus[] = ['not_a_bug', 'confirmed', 'resolved', 'in_progress']

type BugRowState = {
  adminStatus: AdminStatus
  adminComment: string
  awardExploit: boolean
  saving: boolean
  saved: boolean
  error: string | null
}

function formatDate(raw: string | null): string {
  if (!raw) return '—'
  const d = new Date(raw)
  return d.toLocaleDateString('fr-FR', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' })
}

export default function BugAdmin({
  bugs,
  currentStatus,
}: {
  bugs: BugReport[]
  currentStatus: StatusFilter
}) {
  const router = useRouter()
  const [, startTransition] = useTransition()

  // Per-bug edit state
  const [rowStates, setRowStates] = useState<Record<number, BugRowState>>(() => {
    const init: Record<number, BugRowState> = {}
    for (const bug of bugs) {
      init[bug.id] = {
        adminStatus: bug.admin_status,
        adminComment: bug.admin_comment ?? '',
        awardExploit: false,
        saving: false,
        saved: false,
        error: null,
      }
    }
    return init
  })

  function updateRow(id: number, patch: Partial<BugRowState>) {
    setRowStates(prev => ({ ...prev, [id]: { ...prev[id], ...patch } }))
  }

  function navigateStatus(status: StatusFilter) {
    const url = status === 'all' ? '/admin/bugs' : `/admin/bugs?status=${status}`
    startTransition(() => {
      router.push(url)
    })
  }

  async function handleSave(bug: BugReport) {
    const state = rowStates[bug.id]
    if (!state) return

    updateRow(bug.id, { saving: true, error: null, saved: false })

    try {
      const body: Record<string, unknown> = {
        adminStatus: state.adminStatus,
        adminComment: state.adminComment.trim() || null,
      }
      if (state.adminStatus === 'resolved' && state.awardExploit && !bug.exploit_awarded) {
        body.awardExploit = true
      }

      const res = await fetch(`/api/admin/bugs/${bug.id}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      })

      if (!res.ok) {
        const data = await res.json() as { error?: string }
        updateRow(bug.id, { saving: false, error: data.error ?? 'Erreur lors de la sauvegarde' })
        return
      }

      updateRow(bug.id, { saving: false, saved: true, awardExploit: false })
      startTransition(() => {
        router.refresh()
      })
    } catch {
      updateRow(bug.id, { saving: false, error: 'Erreur réseau' })
    }
  }

  const inputStyle: React.CSSProperties = {
    background: 'rgba(255,255,255,.06)',
    border: '1px solid rgba(255,255,255,.12)',
    borderRadius: 6,
    color: 'var(--ln-text)',
    padding: '5px 8px',
    fontSize: 12,
    width: '100%',
    boxSizing: 'border-box',
  }

  return (
    <>
      {/* Filter tabs */}
      <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', marginBottom: '1.25rem' }}>
        {FILTER_TABS.map(tab => {
          const isActive = tab === currentStatus
          return (
            <button
              key={tab}
              onClick={() => navigateStatus(tab)}
              className={isActive ? 'wp-badge active' : 'wp-badge planned'}
              style={{
                cursor: 'pointer',
                border: 'none',
                padding: '5px 12px',
                fontSize: 12,
                opacity: isActive ? 1 : 0.7,
              }}
            >
              {STATUS_LABELS[tab]}
            </button>
          )
        })}
      </div>

      {/* Count */}
      <div className="wp-section-title" style={{ marginBottom: '0.75rem' }}>
        {bugs.length} signalement{bugs.length !== 1 ? 's' : ''}
        {bugs.length === 50 ? ' (limite 50 — affinez le filtre)' : ''}
      </div>

      {bugs.length === 0 ? (
        <div className="wp-card" style={{ color: 'var(--ln-muted)', textAlign: 'center', padding: '2rem' }}>
          Aucun signalement pour ce filtre.
        </div>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
          {bugs.map(bug => {
            const state = rowStates[bug.id]
            if (!state) return null
            const showComment = SHOW_COMMENT_FOR.includes(state.adminStatus)
            const showAwardExploit = state.adminStatus === 'resolved' && !bug.exploit_awarded
            const isDirty =
              state.adminStatus !== bug.admin_status ||
              state.adminComment !== (bug.admin_comment ?? '')

            return (
              <div
                key={bug.id}
                className="wp-card"
                style={{ padding: '1rem', display: 'flex', flexDirection: 'column', gap: 10 }}
              >
                {/* Header row */}
                <div style={{ display: 'flex', alignItems: 'flex-start', justifyContent: 'space-between', gap: 12, flexWrap: 'wrap' }}>
                  <div style={{ flex: 1, minWidth: 0 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap', marginBottom: 4 }}>
                      <span style={{ fontSize: 11, color: 'var(--ln-muted)' }}>#{bug.id}</span>
                      <span
                        className={STATUS_BADGE[bug.admin_status]}
                        style={STATUS_BADGE_STYLE[bug.admin_status]}
                      >
                        {STATUS_LABELS[bug.admin_status]}
                      </span>
                      {bug.exploit_awarded ? (
                        <span className="wp-badge done" style={{ fontSize: 10 }}>Exploit attribué</span>
                      ) : null}
                      <span style={{ fontSize: 11, color: 'var(--ln-muted)' }}>{bug.category}</span>
                    </div>
                    <div style={{ fontWeight: 600, fontSize: 14, marginBottom: 2 }}>{bug.title}</div>
                    <div style={{ fontSize: 12, color: 'var(--ln-muted)' }}>
                      Par{' '}
                      <span style={{ color: 'var(--ln-text)' }}>
                        {bug.reporter_login ?? 'Inconnu'}
                      </span>
                      {' — '}{formatDate(bug.created_at)}
                    </div>
                  </div>
                </div>

                {/* Body preview */}
                <div style={{
                  fontSize: 12,
                  color: 'var(--ln-muted)',
                  background: 'rgba(255,255,255,.03)',
                  border: '1px solid rgba(255,255,255,.07)',
                  borderRadius: 6,
                  padding: '8px 10px',
                  whiteSpace: 'pre-wrap',
                  maxHeight: 100,
                  overflow: 'auto',
                }}>
                  {bug.body}
                </div>

                {/* Admin controls */}
                <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                  {/* Status selector */}
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                    <label style={{ fontSize: 12, color: 'var(--ln-muted)', whiteSpace: 'nowrap' }}>Statut admin :</label>
                    <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }}>
                      {ALL_STATUSES.map(s => (
                        <button
                          key={s}
                          onClick={() => updateRow(bug.id, { adminStatus: s, saved: false })}
                          className={state.adminStatus === s ? STATUS_BADGE[s] : 'wp-badge'}
                          style={{
                            cursor: 'pointer',
                            border: state.adminStatus === s ? undefined : '1px solid rgba(255,255,255,.1)',
                            padding: '3px 8px',
                            fontSize: 11,
                            opacity: state.adminStatus === s ? 1 : 0.5,
                            ...(state.adminStatus === s && s === 'not_a_bug' ? STATUS_BADGE_STYLE[s] : {}),
                          }}
                        >
                          {STATUS_LABELS[s]}
                        </button>
                      ))}
                    </div>
                  </div>

                  {/* Admin comment */}
                  {showComment && (
                    <div>
                      <label style={{ fontSize: 12, color: 'var(--ln-muted)', display: 'block', marginBottom: 4 }}>
                        Commentaire admin
                      </label>
                      <textarea
                        style={{ ...inputStyle, minHeight: 60, resize: 'vertical' }}
                        value={state.adminComment}
                        onChange={e => updateRow(bug.id, { adminComment: e.target.value, saved: false })}
                        placeholder="Commentaire visible par l'équipe (optionnel)"
                      />
                    </div>
                  )}

                  {/* Award exploit */}
                  {showAwardExploit && (
                    <label style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 13, cursor: 'pointer' }}>
                      <input
                        type="checkbox"
                        checked={state.awardExploit}
                        onChange={e => updateRow(bug.id, { awardExploit: e.target.checked, saved: false })}
                      />
                      Attribuer les points exploit au joueur
                    </label>
                  )}

                  {/* Error / success */}
                  {state.error && (
                    <div style={{ fontSize: 12, color: '#e05050' }}>{state.error}</div>
                  )}
                  {state.saved && !isDirty && (
                    <div style={{ fontSize: 12, color: 'var(--ln-success, #4caf50)' }}>Enregistré.</div>
                  )}

                  {/* Save button */}
                  <div>
                    <button
                      className="wp-badge done"
                      style={{
                        cursor: state.saving ? 'wait' : 'pointer',
                        border: 'none',
                        padding: '6px 14px',
                        fontSize: 12,
                        opacity: state.saving ? 0.6 : 1,
                      }}
                      disabled={state.saving}
                      onClick={() => handleSave(bug)}
                    >
                      {state.saving ? 'Enregistrement…' : 'Enregistrer'}
                    </button>
                  </div>
                </div>
              </div>
            )
          })}
        </div>
      )}
    </>
  )
}
