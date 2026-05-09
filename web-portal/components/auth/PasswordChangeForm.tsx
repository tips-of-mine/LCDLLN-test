'use client'

import { useState } from 'react'

type Status = { ok: boolean; message: string } | null

export function PasswordChangeForm() {
  const [currentPassword, setCurrentPassword] = useState('')
  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [pending, setPending] = useState(false)
  const [status, setStatus] = useState<Status>(null)

  const mismatch = confirmPassword.length > 0 && newPassword !== confirmPassword

  async function handleSubmit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    if (mismatch) return

    setPending(true)
    setStatus(null)
    try {
      const res = await fetch('/api/player/password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ currentPassword, newPassword }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (data.ok) {
        setStatus({ ok: true, message: 'Mot de passe modifié avec succès.' })
        setCurrentPassword('')
        setNewPassword('')
        setConfirmPassword('')
      } else {
        setStatus({ ok: false, message: data.message ?? 'Erreur lors du changement de mot de passe.' })
      }
    } catch {
      setStatus({ ok: false, message: 'Erreur réseau. Veuillez réessayer.' })
    } finally {
      setPending(false)
    }
  }

  return (
    <div className="wp-card">
      <form className="form-stack" onSubmit={handleSubmit}>
        <label className="field">
          <span>Mot de passe actuel</span>
          <input
            type="password"
            value={currentPassword}
            onChange={(e) => setCurrentPassword(e.target.value)}
            placeholder="Votre mot de passe actuel"
            autoComplete="current-password"
            required
          />
        </label>
        <label className="field">
          <span>Nouveau mot de passe</span>
          <input
            type="password"
            value={newPassword}
            onChange={(e) => setNewPassword(e.target.value)}
            placeholder="Au moins 8 caractères"
            autoComplete="new-password"
            minLength={8}
            required
          />
        </label>
        <label className="field">
          <span>Confirmer le nouveau mot de passe</span>
          <input
            type="password"
            value={confirmPassword}
            onChange={(e) => setConfirmPassword(e.target.value)}
            placeholder="Répétez le nouveau mot de passe"
            autoComplete="new-password"
            required
          />
        </label>
        {mismatch && (
          <p className="error-box" style={{ margin: 0 }}>
            Les mots de passe ne correspondent pas.
          </p>
        )}
        {status && (
          <p className={status.ok ? 'success-box' : 'error-box'} style={{ margin: 0 }}>
            {status.message}
          </p>
        )}
        <div>
          <button
            type="submit"
            className="btn btn-primary"
            disabled={pending || mismatch}
          >
            {pending ? 'Modification…' : 'Modifier le mot de passe'}
          </button>
        </div>
      </form>
    </div>
  )
}
