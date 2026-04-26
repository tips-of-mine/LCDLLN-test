'use client'

import { useState } from 'react'

type Props = {
  hasPending: boolean
  parentalEmail: string
}

export function ParentalForm({ hasPending, parentalEmail }: Props) {
  const [email, setEmail] = useState('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [success, setSuccess] = useState(false)
  const [resent, setResent] = useState(false)

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setLoading(true)
    setError(null)

    try {
      const res = await fetch('/api/player/parental/request', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ parentalEmail: email }),
      })
      const data = await res.json()
      if (!res.ok || !data.ok) {
        setError(data.message ?? 'Une erreur est survenue.')
      } else {
        setSuccess(true)
      }
    } catch {
      setError('Erreur réseau. Veuillez réessayer.')
    } finally {
      setLoading(false)
    }
  }

  async function handleResend() {
    setLoading(true)
    setError(null)

    try {
      const res = await fetch('/api/player/parental/request', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ parentalEmail: parentalEmail }),
      })
      const data = await res.json()
      if (!res.ok || !data.ok) {
        setError(data.message ?? 'Une erreur est survenue.')
      } else {
        setResent(true)
      }
    } catch {
      setError('Erreur réseau. Veuillez réessayer.')
    } finally {
      setLoading(false)
    }
  }

  // After a fresh submit, show pending state
  if (success) {
    return (
      <div className="wp-card" style={{ borderColor: 'rgba(250,200,50,.3)', background: 'rgba(250,200,50,.04)' }}>
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12 }}>
          <span style={{ fontSize: '1.25rem' }}>⏳</span>
          <div>
            <div style={{ fontFamily: 'var(--font-display)', fontSize: 12, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-warning, #f5a623)', marginBottom: 4 }}>
              En attente de validation
            </div>
            <p style={{ margin: 0, fontFamily: 'var(--font-body)', fontSize: 14, color: 'var(--ln-muted)' }}>
              Demande envoyée à {email}. En attente de validation du tuteur légal.
            </p>
          </div>
        </div>
      </div>
    )
  }

  // Pending state (email already set)
  if (hasPending) {
    return (
      <div className="wp-card" style={{ borderColor: 'rgba(250,200,50,.3)', background: 'rgba(250,200,50,.04)' }}>
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 12, marginBottom: resent ? 0 : 16 }}>
          <span style={{ fontSize: '1.25rem' }}>⏳</span>
          <div>
            <div style={{ fontFamily: 'var(--font-display)', fontSize: 12, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-warning, #f5a623)', marginBottom: 4 }}>
              En attente de validation
            </div>
            <p style={{ margin: 0, fontFamily: 'var(--font-body)', fontSize: 14, color: 'var(--ln-muted)' }}>
              Demande envoyée à {parentalEmail}. En attente de validation du tuteur légal.
            </p>
          </div>
        </div>

        {error && (
          <p style={{ color: 'var(--ln-error, #e05252)', fontSize: 13, margin: '0 0 12px' }}>{error}</p>
        )}

        {resent ? (
          <p style={{ fontSize: 13, color: 'var(--ln-success)', margin: 0 }}>Email renvoyé avec succès.</p>
        ) : (
          <button
            className="btn btn-ghost"
            onClick={handleResend}
            disabled={loading}
            style={{ marginTop: 4 }}
          >
            {loading ? 'Envoi…' : 'Renvoyer l\'email'}
          </button>
        )}
      </div>
    )
  }

  // No pending: show form
  return (
    <div className="wp-card">
      <p className="wp-section-title" style={{ marginTop: 0 }}>Saisir l&apos;email du tuteur légal</p>
      <p style={{ fontSize: 14, color: 'var(--ln-muted)', marginBottom: 16, fontFamily: 'var(--font-body)' }}>
        Un email de validation sera envoyé à votre tuteur légal. Sans validation, l&apos;accès au jeu est bloqué.
      </p>

      <form onSubmit={handleSubmit} style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div>
          <label
            htmlFor="parental-email"
            style={{ display: 'block', fontSize: 13, fontWeight: 600, marginBottom: 6, color: 'var(--ln-text)' }}
          >
            Adresse email du tuteur légal
          </label>
          <input
            id="parental-email"
            type="email"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            placeholder="parent@example.com"
            required
            style={{
              width: '100%',
              padding: '8px 12px',
              background: 'var(--ln-surface)',
              border: '1px solid var(--ln-border)',
              borderRadius: 6,
              color: 'var(--ln-text)',
              fontSize: 14,
              fontFamily: 'var(--font-body)',
              boxSizing: 'border-box',
            }}
          />
        </div>

        {error && (
          <p style={{ color: 'var(--ln-error, #e05252)', fontSize: 13, margin: 0 }}>{error}</p>
        )}

        <button
          type="submit"
          className="btn btn-primary"
          disabled={loading}
          style={{ alignSelf: 'flex-start' }}
        >
          {loading ? 'Envoi en cours…' : 'Envoyer la demande'}
        </button>
      </form>
    </div>
  )
}
