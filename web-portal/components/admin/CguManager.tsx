'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'

type Edition = {
  id: number
  version_label: string
  published_at: string | null
  status: 'draft' | 'published' | 'retired'
  retired_reason: string | null
  title_fr: string | null
  acceptance_count: number
}

type FormMode =
  | { type: 'none' }
  | { type: 'create' }
  | { type: 'edit'; edition: Edition }
  | { type: 'retire'; edition: Edition }

export function CguManager({ editions }: { editions: Edition[] }) {
  const router = useRouter()
  const [mode, setMode] = useState<FormMode>({ type: 'none' })
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)

  // Form fields
  const [versionLabel, setVersionLabel] = useState('')
  const [titleFr, setTitleFr] = useState('')
  const [contentFr, setContentFr] = useState('')
  const [titleEn, setTitleEn] = useState('')
  const [contentEn, setContentEn] = useState('')
  const [retireReason, setRetireReason] = useState('')

  function openCreate() {
    setVersionLabel('')
    setTitleFr('')
    setContentFr('')
    setTitleEn('')
    setContentEn('')
    setError(null)
    setMode({ type: 'create' })
  }

  function openEdit(edition: Edition) {
    setVersionLabel(edition.version_label)
    setTitleFr(edition.title_fr ?? '')
    setContentFr('')
    setTitleEn('')
    setContentEn('')
    setError(null)
    setMode({ type: 'edit', edition })
  }

  function openRetire(edition: Edition) {
    setRetireReason('')
    setError(null)
    setMode({ type: 'retire', edition })
  }

  function closeForm() {
    setMode({ type: 'none' })
    setError(null)
  }

  async function handleCreate(e: React.FormEvent) {
    e.preventDefault()
    setLoading(true)
    setError(null)
    try {
      const res = await fetch('/api/admin/cgu', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ versionLabel, titleFr, contentFr, titleEn, contentEn }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) { setError(data.message ?? 'Erreur inconnue'); return }
      closeForm()
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  async function handleEdit(e: React.FormEvent) {
    e.preventDefault()
    if (mode.type !== 'edit') return
    setLoading(true)
    setError(null)
    try {
      const res = await fetch(`/api/admin/cgu/${mode.edition.id}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ versionLabel, titleFr, contentFr, titleEn, contentEn }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) { setError(data.message ?? 'Erreur inconnue'); return }
      closeForm()
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  async function handlePublish(edition: Edition) {
    if (!confirm(`Publier l'édition "${edition.version_label}" ? Cette action est irréversible.`)) return
    setLoading(true)
    setError(null)
    try {
      const res = await fetch(`/api/admin/cgu/${edition.id}/publish`, { method: 'POST' })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) { setError(data.message ?? 'Erreur inconnue'); return }
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  async function handleDelete(edition: Edition) {
    if (!confirm(`Supprimer définitivement l'édition "${edition.version_label}" ?`)) return
    setLoading(true)
    setError(null)
    try {
      const res = await fetch(`/api/admin/cgu/${edition.id}`, { method: 'DELETE' })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) { setError(data.message ?? 'Erreur inconnue'); return }
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  async function handleRetire(e: React.FormEvent) {
    e.preventDefault()
    if (mode.type !== 'retire') return
    setLoading(true)
    setError(null)
    try {
      const res = await fetch(`/api/admin/cgu/${mode.edition.id}/retire`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ reason: retireReason }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (!data.ok) { setError(data.message ?? 'Erreur inconnue'); return }
      closeForm()
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Erreur réseau')
    } finally {
      setLoading(false)
    }
  }

  const statusBadge: Record<string, string> = {
    draft: 'badge badge-muted',
    published: 'badge badge-warning',
    retired: 'badge badge-error',
  }
  const statusLabel: Record<string, string> = {
    draft: 'Brouillon',
    published: 'Publié',
    retired: 'Retiré',
  }

  return (
    <>
      {/* Action bar */}
      <div className="flex items-center justify-between flex-wrap gap-1 mb-2">
        <button
          className="btn btn-primary"
          onClick={openCreate}
          disabled={loading}
        >
          Nouvelle CGU
        </button>
      </div>

      {/* Table */}
      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Version</th>
              <th>Statut</th>
              <th>Date de publication</th>
              <th>Acceptations</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {editions.length === 0 && (
              <tr>
                <td colSpan={5} style={{ textAlign: 'center', color: 'var(--muted)', padding: '2rem 1rem' }}>
                  Aucune édition CGU. Créez la première via le bouton ci-dessus.
                </td>
              </tr>
            )}
            {editions.map((ed) => (
              <tr key={ed.id}>
                <td>
                  <strong>{ed.version_label}</strong>
                  {ed.title_fr && (
                    <div style={{ fontSize: 12, color: 'var(--muted)', marginTop: 2 }}>{ed.title_fr}</div>
                  )}
                </td>
                <td>
                  <span className={statusBadge[ed.status] ?? 'badge'}>{statusLabel[ed.status] ?? ed.status}</span>
                </td>
                <td>
                  {ed.published_at
                    ? new Date(ed.published_at).toLocaleDateString('fr-FR', {
                        day: '2-digit', month: '2-digit', year: 'numeric',
                      })
                    : <span style={{ color: 'var(--muted)' }}>—</span>}
                </td>
                <td>{ed.acceptance_count}</td>
                <td>
                  {ed.status === 'draft' && (
                    <div className="flex items-center gap-1">
                      <button
                        className="btn btn-secondary btn-sm"
                        onClick={() => openEdit(ed)}
                        disabled={loading}
                      >
                        Modifier
                      </button>
                      <button
                        className="btn btn-primary btn-sm"
                        onClick={() => handlePublish(ed)}
                        disabled={loading}
                      >
                        Publier
                      </button>
                      <button
                        className="btn btn-ghost btn-sm"
                        onClick={() => handleDelete(ed)}
                        disabled={loading}
                        style={{ color: 'var(--error)' }}
                      >
                        Supprimer
                      </button>
                    </div>
                  )}
                  {ed.status === 'published' && (
                    <button
                      className="btn btn-ghost btn-sm"
                      onClick={() => openRetire(ed)}
                      disabled={loading}
                    >
                      Retirer
                    </button>
                  )}
                  {ed.status === 'retired' && ed.retired_reason && (
                    <span style={{ fontSize: 12, color: 'var(--muted)' }}>
                      Motif : {ed.retired_reason}
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {/* Create / Edit form */}
      {(mode.type === 'create' || mode.type === 'edit') && (
        <div className="card" style={{ marginTop: '1.5rem' }}>
          <h2 style={{ marginTop: 0 }}>
            {mode.type === 'create' ? 'Nouvelle édition CGU' : `Modifier — ${mode.edition.version_label}`}
          </h2>
          <form onSubmit={mode.type === 'create' ? handleCreate : handleEdit}>
            <div className="field">
              <label htmlFor="cgu-version">Identifiant de version *</label>
              <input
                id="cgu-version"
                type="text"
                value={versionLabel}
                onChange={(e) => setVersionLabel(e.target.value)}
                placeholder="ex: v3.0, 2026-Q1"
                required
              />
            </div>
            <div className="field">
              <label htmlFor="cgu-title-fr">Titre (français) *</label>
              <input
                id="cgu-title-fr"
                type="text"
                value={titleFr}
                onChange={(e) => setTitleFr(e.target.value)}
                placeholder="Conditions Générales d'Utilisation"
                required={mode.type === 'create'}
              />
            </div>
            <div className="field">
              <label htmlFor="cgu-content-fr">Contenu (français) *</label>
              <textarea
                id="cgu-content-fr"
                value={contentFr}
                onChange={(e) => setContentFr(e.target.value)}
                rows={8}
                placeholder="Texte complet des CGU en français…"
                required={mode.type === 'create'}
                style={{ width: '100%', fontFamily: 'var(--font-mono, monospace)', fontSize: 13 }}
              />
            </div>
            <div className="field">
              <label htmlFor="cgu-title-en">Titre (anglais) — optionnel</label>
              <input
                id="cgu-title-en"
                type="text"
                value={titleEn}
                onChange={(e) => setTitleEn(e.target.value)}
                placeholder="Terms of Service"
              />
            </div>
            <div className="field">
              <label htmlFor="cgu-content-en">Contenu (anglais) — optionnel</label>
              <textarea
                id="cgu-content-en"
                value={contentEn}
                onChange={(e) => setContentEn(e.target.value)}
                rows={6}
                placeholder="Full ToS text in English…"
                style={{ width: '100%', fontFamily: 'var(--font-mono, monospace)', fontSize: 13 }}
              />
            </div>
            {error && (
              <p style={{ color: 'var(--error)', margin: '0.5rem 0' }}>{error}</p>
            )}
            <div className="flex items-center gap-1">
              <button type="submit" className="btn btn-primary" disabled={loading}>
                {loading ? '…' : mode.type === 'create' ? 'Créer le brouillon' : 'Enregistrer'}
              </button>
              <button type="button" className="btn btn-ghost" onClick={closeForm} disabled={loading}>
                Annuler
              </button>
            </div>
          </form>
        </div>
      )}

      {/* Retire form */}
      {mode.type === 'retire' && (
        <div className="card" style={{ marginTop: '1.5rem' }}>
          <h2 style={{ marginTop: 0 }}>Retirer — {mode.edition.version_label}</h2>
          <p style={{ color: 'var(--muted)', fontSize: 14 }}>
            Le retrait est définitif. Les joueurs ne seront plus invités à accepter cette version.
            Un motif est obligatoire.
          </p>
          <form onSubmit={handleRetire}>
            <div className="field">
              <label htmlFor="cgu-retire-reason">Motif de retrait *</label>
              <input
                id="cgu-retire-reason"
                type="text"
                value={retireReason}
                onChange={(e) => setRetireReason(e.target.value)}
                placeholder="ex: Remplacée par v3.1, mise à jour RGPD"
                required
                autoFocus
              />
            </div>
            {error && (
              <p style={{ color: 'var(--error)', margin: '0.5rem 0' }}>{error}</p>
            )}
            <div className="flex items-center gap-1">
              <button
                type="submit"
                className="btn btn-primary"
                disabled={loading || !retireReason.trim()}
                style={{ background: 'var(--error)' }}
              >
                {loading ? '…' : 'Confirmer le retrait'}
              </button>
              <button type="button" className="btn btn-ghost" onClick={closeForm} disabled={loading}>
                Annuler
              </button>
            </div>
          </form>
        </div>
      )}
    </>
  )
}
