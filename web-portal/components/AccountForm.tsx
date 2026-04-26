'use client'

import { useState } from 'react'

type AccountFormProps = {
  firstName: string
  lastName: string
  email: string
  emailPending: string | null
  addressStreet: string
  addressCity: string
  addressZip: string
  addressCountry: string
}

type Status = { ok: boolean; message: string } | null

export function AccountForm({
  firstName: initialFirstName,
  lastName: initialLastName,
  email,
  emailPending: initialEmailPending,
  addressStreet: initialAddressStreet,
  addressCity: initialAddressCity,
  addressZip: initialAddressZip,
  addressCountry: initialAddressCountry,
}: AccountFormProps) {
  // Personal info state
  const [firstName, setFirstName] = useState(initialFirstName)
  const [lastName, setLastName] = useState(initialLastName)
  const [personalPending, setPersonalPending] = useState(false)
  const [personalStatus, setPersonalStatus] = useState<Status>(null)

  // Email change state
  const [newEmail, setNewEmail] = useState('')
  const [emailPending, setEmailPending] = useState<string | null>(initialEmailPending)
  const [emailChangePending, setEmailChangePending] = useState(false)
  const [emailStatus, setEmailStatus] = useState<Status>(null)
  const [cancelPending, setCancelPending] = useState(false)

  // Address state
  const [addressStreet, setAddressStreet] = useState(initialAddressStreet)
  const [addressCity, setAddressCity] = useState(initialAddressCity)
  const [addressZip, setAddressZip] = useState(initialAddressZip)
  const [addressCountry, setAddressCountry] = useState(initialAddressCountry)
  const [addressSavePending, setAddressSavePending] = useState(false)
  const [addressStatus, setAddressStatus] = useState<Status>(null)

  async function handlePersonalSubmit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setPersonalPending(true)
    setPersonalStatus(null)
    try {
      const res = await fetch('/api/player/account', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ firstName, lastName }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      setPersonalStatus({
        ok: data.ok,
        message: data.ok ? 'Informations personnelles mises à jour.' : (data.message ?? 'Erreur lors de la mise à jour.'),
      })
    } catch {
      setPersonalStatus({ ok: false, message: 'Erreur réseau. Veuillez réessayer.' })
    } finally {
      setPersonalPending(false)
    }
  }

  async function handleEmailChangeSubmit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setEmailChangePending(true)
    setEmailStatus(null)
    try {
      const res = await fetch('/api/player/account/request-email-change', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ newEmail }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      if (data.ok) {
        setEmailPending(newEmail)
        setNewEmail('')
        setEmailStatus({ ok: true, message: 'Un email de confirmation a été envoyé à votre nouvelle adresse.' })
      } else {
        setEmailStatus({ ok: false, message: data.message ?? 'Erreur lors de la demande de changement.' })
      }
    } catch {
      setEmailStatus({ ok: false, message: 'Erreur réseau. Veuillez réessayer.' })
    } finally {
      setEmailChangePending(false)
    }
  }

  async function handleCancelEmailChange() {
    setCancelPending(true)
    setEmailStatus(null)
    try {
      const cancelRes = await fetch('/api/player/account/cancel-email-change', { method: 'POST' })
      if (cancelRes.ok) {
        setEmailPending(null)
        setEmailStatus({ ok: true, message: 'La demande de changement d\'email a été annulée.' })
      } else {
        setEmailStatus({ ok: false, message: 'Impossible d\'annuler la demande.' })
      }
    } catch {
      setEmailStatus({ ok: false, message: 'Erreur réseau. Veuillez réessayer.' })
    } finally {
      setCancelPending(false)
    }
  }

  async function handleAddressSubmit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setAddressSavePending(true)
    setAddressStatus(null)
    try {
      const res = await fetch('/api/player/account', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ addressStreet, addressCity, addressZip, addressCountry }),
      })
      const data = await res.json() as { ok: boolean; message?: string }
      setAddressStatus({
        ok: data.ok,
        message: data.ok ? 'Adresse postale mise à jour.' : (data.message ?? 'Erreur lors de la mise à jour.'),
      })
    } catch {
      setAddressStatus({ ok: false, message: 'Erreur réseau. Veuillez réessayer.' })
    } finally {
      setAddressSavePending(false)
    }
  }

  return (
    <>
      {/* Informations personnelles */}
      <div className="wp-section-title" style={{ marginBottom: 6, marginTop: 24 }}>Informations personnelles</div>
      <div className="wp-section-sub">Prénom et nom associés à votre compte.</div>
      <div className="wp-card" style={{ marginBottom: 16 }}>
        <form className="form-stack" onSubmit={handlePersonalSubmit}>
          <div className="form-grid">
            <label className="field">
              <span>Prénom</span>
              <input
                type="text"
                value={firstName}
                onChange={(e) => setFirstName(e.target.value)}
                placeholder="Votre prénom"
                autoComplete="given-name"
              />
            </label>
            <label className="field">
              <span>Nom</span>
              <input
                type="text"
                value={lastName}
                onChange={(e) => setLastName(e.target.value)}
                placeholder="Votre nom de famille"
                autoComplete="family-name"
              />
            </label>
          </div>
          {personalStatus && (
            <p className={personalStatus.ok ? 'success-box' : 'error-box'} style={{ margin: 0 }}>
              {personalStatus.message}
            </p>
          )}
          <div>
            <button type="submit" className="btn btn-primary" disabled={personalPending}>
              {personalPending ? 'Enregistrement…' : 'Enregistrer'}
            </button>
          </div>
        </form>
      </div>

      {/* Adresse email */}
      <div className="wp-section-title" style={{ marginBottom: 6, marginTop: 24 }}>Adresse email</div>
      <div className="wp-section-sub">Modifiez l&apos;adresse email associée à votre compte.</div>
      <div className="wp-card" style={{ marginBottom: 16 }}>
        <div style={{ marginBottom: 16 }}>
          <div style={{ fontFamily: 'var(--font-ui)', fontSize: 10.5, letterSpacing: '0.22em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginBottom: 6 }}>
            Email actuel
          </div>
          <div style={{ fontFamily: 'var(--font-body)', fontSize: 15, color: 'var(--ln-text)' }}>{email}</div>
        </div>

        {emailPending && (
          <div className="wp-alert warning" style={{ marginBottom: 16 }}>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600, marginBottom: 4 }}>Confirmation en attente pour&nbsp;: {emailPending}</div>
              <div style={{ fontSize: 13 }}>
                Un email de confirmation a été envoyé. L&apos;ancienne adresse reste active jusqu&apos;à validation.
              </div>
            </div>
            <button
              type="button"
              className="btn btn-ghost"
              style={{ flexShrink: 0, fontSize: 11, padding: '6px 12px' }}
              onClick={handleCancelEmailChange}
              disabled={cancelPending}
            >
              {cancelPending ? 'Annulation…' : 'Annuler la demande'}
            </button>
          </div>
        )}

        <form className="form-stack" onSubmit={handleEmailChangeSubmit}>
          <label className="field">
            <span>Nouvelle adresse email</span>
            <input
              type="email"
              value={newEmail}
              onChange={(e) => setNewEmail(e.target.value)}
              placeholder="nouvelle@adresse.fr"
              autoComplete="email"
              required
            />
          </label>
          <p style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 13, color: 'var(--ln-muted)', margin: 0 }}>
            Un email de confirmation sera envoyé. L&apos;ancienne adresse reste active jusqu&apos;à validation.
          </p>
          {emailStatus && (
            <p className={emailStatus.ok ? 'success-box' : 'error-box'} style={{ margin: 0 }}>
              {emailStatus.message}
            </p>
          )}
          <div>
            <button type="submit" className="btn btn-primary" disabled={emailChangePending}>
              {emailChangePending ? 'Envoi…' : 'Demander le changement'}
            </button>
          </div>
        </form>
      </div>

      {/* Adresse postale */}
      <div className="wp-section-title" style={{ marginBottom: 6, marginTop: 24 }}>Adresse postale</div>
      <div className="wp-section-sub">Adresse de livraison ou de contact physique.</div>
      <div className="wp-card" style={{ marginBottom: 16 }}>
        <form className="form-stack" onSubmit={handleAddressSubmit}>
          <label className="field">
            <span>Rue</span>
            <input
              type="text"
              value={addressStreet}
              onChange={(e) => setAddressStreet(e.target.value)}
              placeholder="123 rue de la Lune"
              autoComplete="street-address"
            />
          </label>
          <div className="form-grid">
            <label className="field">
              <span>Ville</span>
              <input
                type="text"
                value={addressCity}
                onChange={(e) => setAddressCity(e.target.value)}
                placeholder="Paris"
                autoComplete="address-level2"
              />
            </label>
            <label className="field">
              <span>Code postal</span>
              <input
                type="text"
                value={addressZip}
                onChange={(e) => setAddressZip(e.target.value)}
                placeholder="75000"
                autoComplete="postal-code"
              />
            </label>
          </div>
          <label className="field">
            <span>Pays</span>
            <input
              type="text"
              value={addressCountry}
              onChange={(e) => setAddressCountry(e.target.value)}
              placeholder="France"
              autoComplete="country-name"
            />
          </label>
          {addressStatus && (
            <p className={addressStatus.ok ? 'success-box' : 'error-box'} style={{ margin: 0 }}>
              {addressStatus.message}
            </p>
          )}
          <div>
            <button type="submit" className="btn btn-primary" disabled={addressSavePending}>
              {addressSavePending ? 'Enregistrement…' : 'Enregistrer l\'adresse'}
            </button>
          </div>
        </form>
      </div>
    </>
  )
}
