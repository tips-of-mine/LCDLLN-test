'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'

export interface PlayerCharacter {
  id: number
  name: string
  slot: number
  deleted_at: string | null
  force_rename: number
}

export interface PlayerRow {
  id: number
  login: string
  email: string
  tag_id: string | null
  account_status: string
  email_verified: number
  role: string
  birth_date: string | null
  disabled_reason: string | null
  accepted_cgu_count: number
  published_cgu_count: number
  characters?: PlayerCharacter[]
}

const BTN: React.CSSProperties = {
  background: 'none',
  border: '1px solid var(--ln-border)',
  color: 'var(--ln-muted)',
  fontFamily: 'var(--font-ui)',
  fontSize: '10px',
  letterSpacing: '.18em',
  textTransform: 'uppercase' as const,
  padding: '5px 11px',
  borderRadius: 'var(--radius-sm)',
  cursor: 'pointer',
  transition: 'all .15s',
  whiteSpace: 'nowrap' as const,
}
const BTN_WARN: React.CSSProperties = { ...BTN, borderColor: 'rgba(232,165,92,.5)', color: 'var(--ln-warning)' }
const BTN_DANGER: React.CSSProperties = { ...BTN, borderColor: 'rgba(196,64,64,.5)', color: 'var(--ln-error)' }
const BTN_SUCCESS: React.CSSProperties = { ...BTN, borderColor: 'rgba(95,184,110,.5)', color: 'var(--ln-success)' }
const BTN_INFO: React.CSSProperties = { ...BTN, borderColor: 'rgba(74,123,184,.5)', color: 'var(--ln-primary)' }

/* ── Character item with force-rename ── */
function CharacterItem({ char }: { char: PlayerCharacter }) {
  const [loading, setLoading] = useState(false)
  const [done, setDone] = useState(char.force_rename === 1)
  const router = useRouter()

  async function handleForceRename() {
    setLoading(true)
    try {
      const res = await fetch(`/api/admin/characters/${char.id}/force-rename`, { method: 'PATCH' })
      if (res.ok) { setDone(true); router.refresh() }
    } finally {
      setLoading(false)
    }
  }

  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 10,
      padding: '8px 0',
      borderBottom: '1px solid rgba(61,79,102,.2)',
    }}>
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: 'var(--ln-muted)', minWidth: 28 }}>
        #{char.slot}
      </span>
      <span style={{ fontFamily: 'var(--font-body)', fontSize: 13, color: 'var(--ln-text)', flex: 1 }}>
        {char.name}
      </span>
      {done ? (
        <span className="wp-badge done">Renommage forcé</span>
      ) : (
        <button
          onClick={handleForceRename}
          disabled={loading}
          style={BTN_WARN}
        >
          {loading ? '...' : 'Forcer renommage'}
        </button>
      )}
    </div>
  )
}

/* ── Characters accordion section ── */
function CharactersSection({ playerId }: { playerId: number }) {
  const [open, setOpen] = useState(false)
  const [loading, setLoading] = useState(false)
  const [chars, setChars] = useState<PlayerCharacter[] | null>(null)

  async function toggle() {
    if (open) { setOpen(false); return }
    if (chars !== null) { setOpen(true); return }
    setLoading(true)
    try {
      const res = await fetch(`/api/admin/players/${playerId}/characters`)
      if (res.ok) {
        const data = await res.json()
        setChars(data.characters ?? [])
      }
    } finally {
      setLoading(false)
      setOpen(true)
    }
  }

  return (
    <div style={{ marginTop: 10 }}>
      <button onClick={toggle} style={BTN_INFO} disabled={loading}>
        {loading ? '...' : open ? 'Masquer personnages' : 'Voir personnages'}
      </button>
      {open && chars !== null && (
        <div style={{
          marginTop: 8,
          background: 'rgba(10,13,18,.4)',
          border: '1px solid rgba(61,79,102,.4)',
          borderRadius: 'var(--radius-sm)',
          padding: '10px 14px',
        }}>
          {chars.length === 0 ? (
            <p style={{ margin: 0, fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 13, color: 'var(--ln-muted)' }}>
              Aucun personnage actif.
            </p>
          ) : (
            chars.map(c => <CharacterItem key={c.id} char={c} />)
          )}
        </div>
      )}
    </div>
  )
}

/* ── Main PlayerActions component ── */
export function PlayerActions({ player }: { player: PlayerRow }) {
  const router = useRouter()
  const [loading, setLoading] = useState<string | null>(null)
  const [showDisableForm, setShowDisableForm] = useState(false)
  const [motif, setMotif] = useState('')
  const [motifError, setMotifError] = useState('')

  async function patch(endpoint: string, body?: Record<string, unknown>) {
    setLoading(endpoint)
    try {
      const res = await fetch(`/api/admin/players/${player.id}/${endpoint}`, {
        method: 'PATCH',
        headers: body ? { 'Content-Type': 'application/json' } : undefined,
        body: body ? JSON.stringify(body) : undefined,
      })
      if (res.ok) router.refresh()
    } finally {
      setLoading(null)
    }
  }

  async function handleDisable() {
    if (!motif.trim()) { setMotifError('Le motif est obligatoire.'); return }
    setMotifError('')
    await patch('disable', { reason: motif.trim() })
    setShowDisableForm(false)
    setMotif('')
  }

  return (
    <div>
      {/* Action buttons row */}
      <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', alignItems: 'center' }}>
        {player.email_verified === 0 && (
          <button
            style={BTN_SUCCESS}
            disabled={loading !== null}
            onClick={() => patch('verify-email')}
          >
            {loading === 'verify-email' ? '...' : 'Valider email'}
          </button>
        )}

        {player.account_status === 'disabled' && (
          <button
            style={BTN_SUCCESS}
            disabled={loading !== null}
            onClick={() => patch('activate')}
          >
            {loading === 'activate' ? '...' : 'Réactiver'}
          </button>
        )}

        {player.account_status === 'active' && !showDisableForm && (
          <button
            style={BTN_DANGER}
            disabled={loading !== null}
            onClick={() => setShowDisableForm(true)}
          >
            Désactiver
          </button>
        )}

        {player.account_status === 'active' && showDisableForm && (
          <button
            style={BTN}
            onClick={() => { setShowDisableForm(false); setMotif(''); setMotifError('') }}
          >
            Annuler
          </button>
        )}
      </div>

      {/* Inline disable form */}
      {showDisableForm && (
        <div style={{
          marginTop: 10,
          background: 'rgba(196,64,64,.05)',
          border: '1px solid rgba(196,64,64,.3)',
          borderRadius: 'var(--radius-sm)',
          padding: '12px 14px',
          display: 'flex',
          flexDirection: 'column',
          gap: 8,
        }}>
          <label style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>
            Motif de désactivation
          </label>
          <input
            type="text"
            value={motif}
            onChange={e => setMotif(e.target.value)}
            placeholder="Raison visible par l'équipe…"
            style={{
              background: 'rgba(10,13,18,.6)',
              border: `1px solid ${motifError ? 'var(--ln-error)' : 'var(--ln-border)'}`,
              borderRadius: 'var(--radius-sm)',
              padding: '7px 11px',
              color: 'var(--ln-text)',
              fontFamily: 'var(--font-body)',
              fontSize: 13,
              outline: 'none',
            }}
          />
          {motifError && (
            <span style={{ fontFamily: 'var(--font-body)', fontSize: 12, color: 'var(--ln-error)' }}>{motifError}</span>
          )}
          <button
            style={{ ...BTN_DANGER, alignSelf: 'flex-start' }}
            disabled={loading !== null}
            onClick={handleDisable}
          >
            {loading === 'disable' ? '...' : 'Confirmer la désactivation'}
          </button>
        </div>
      )}

      {/* Characters accordion */}
      <CharactersSection playerId={player.id} />
    </div>
  )
}
