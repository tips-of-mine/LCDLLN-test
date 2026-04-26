'use client'
import { useState } from 'react'

const CATEGORIES = [
  { value: 'gameplay', label: 'Gameplay' },
  { value: 'graphique', label: 'Graphique' },
  { value: 'reseau', label: 'Réseau / Connexion' },
  { value: 'interface', label: 'Interface (UI)' },
  { value: 'autre', label: 'Autre' },
]

export function BugReportForm() {
  const [title, setTitle] = useState('')
  const [body, setBody] = useState('')
  const [category, setCategory] = useState('autre')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [success, setSuccess] = useState(false)

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setLoading(true)
    setError(null)
    try {
      const res = await fetch('/api/bugs', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ title, body, category }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) {
        setError(data.message ?? 'Erreur inconnue')
      } else {
        setSuccess(true)
        setTitle('')
        setBody('')
        setCategory('autre')
      }
    } catch {
      setError('Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  if (success) {
    return (
      <div className="wp-card" style={{ textAlign: 'center', padding: 32 }}>
        <div style={{ fontSize: 36, marginBottom: 12 }}>✅</div>
        <div style={{ fontFamily: 'var(--font-display)', fontSize: 14, letterSpacing: '.12em', color: 'var(--ln-success)', marginBottom: 8 }}>
          Rapport envoyé
        </div>
        <p style={{ color: 'var(--ln-muted)', fontSize: 13.5, margin: '0 0 20px' }}>
          Merci pour votre signalement. Il sera examiné par l&apos;équipe.
        </p>
        <button className="btn btn-ghost" onClick={() => setSuccess(false)}>
          Signaler un autre bug
        </button>
      </div>
    )
  }

  return (
    <form onSubmit={handleSubmit} className="wp-card" style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
      <div className="field">
        <label htmlFor="bug-category">Catégorie</label>
        <select id="bug-category" value={category} onChange={e => setCategory(e.target.value)} required>
          {CATEGORIES.map(c => (
            <option key={c.value} value={c.value}>{c.label}</option>
          ))}
        </select>
      </div>
      <div className="field">
        <label htmlFor="bug-title">Titre *</label>
        <input
          id="bug-title"
          type="text"
          value={title}
          onChange={e => setTitle(e.target.value)}
          placeholder="Résumé concis du problème"
          required
          maxLength={200}
        />
      </div>
      <div className="field">
        <label htmlFor="bug-body">Description *</label>
        <textarea
          id="bug-body"
          value={body}
          onChange={e => setBody(e.target.value)}
          rows={7}
          placeholder="Décrivez le bug en détail : étapes pour reproduire, comportement attendu vs observé, contexte (serveur, personnage, heure)..."
          required
          style={{ width: '100%', fontFamily: 'var(--font-body)', fontSize: 13.5 }}
        />
      </div>
      {error && (
        <p style={{ color: 'var(--ln-error)', margin: 0, fontSize: 13.5 }}>&#x26A0; {error}</p>
      )}
      <div>
        <button type="submit" className="btn btn-primary" disabled={loading}>
          {loading ? 'Envoi en cours…' : 'Soumettre le rapport'}
        </button>
      </div>
    </form>
  )
}
