'use server'

import Link from 'next/link'
import { redirect } from 'next/navigation'
import { getSession } from '@/lib/session'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'
import { ParentalForm } from './ParentalForm'

type AccountRow = RowDataPacket & {
  login: string
  birth_date: string | null
  parental_email: string | null
  parental_validated: number
  parental_token_expires_at: string | null
}

function isMinor(birthDate: string | null): boolean {
  if (!birthDate) return false
  const birth = new Date(birthDate)
  const now = new Date()
  const age = now.getFullYear() - birth.getFullYear()
  const monthDiff = now.getMonth() - birth.getMonth()
  const dayDiff = now.getDate() - birth.getDate()
  const actualAge = monthDiff < 0 || (monthDiff === 0 && dayDiff < 0) ? age - 1 : age
  return actualAge < 18
}

export default async function ParentalPage() {
  const session = await getSession()
  if (!session) redirect('/login')

  const rows = await query<AccountRow[]>(
    'SELECT login, birth_date, parental_email, parental_validated, parental_token_expires_at FROM accounts WHERE id = ? LIMIT 1',
    [session.accountId]
  )
  const account = rows[0]
  if (!account) redirect('/login')

  const minor = isMinor(account.birth_date)

  // Adult state
  if (!minor) {
    return (
      <div className="wp-main narrow">
        <div className="wp-page-header">
          <h1>Contrôle parental</h1>
          <p>Gestion de la validation parentale pour les joueurs mineurs.</p>
        </div>

        <div className="wp-card">
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <span style={{ fontSize: '1.5rem' }}>ℹ️</span>
            <p style={{ margin: 0, color: 'var(--ln-muted)', fontFamily: 'var(--font-body)' }}>
              Cette section est réservée aux joueurs mineurs. Votre compte n&apos;est pas concerné par le contrôle parental.
            </p>
          </div>
        </div>

        <div style={{ marginTop: 24 }}>
          <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
        </div>
      </div>
    )
  }

  // Minor, validated state
  if (account.parental_validated === 1) {
    return (
      <div className="wp-main narrow">
        <div className="wp-page-header">
          <h1>Contrôle parental</h1>
          <p>Gestion de la validation parentale pour les joueurs mineurs.</p>
        </div>

        <div className="wp-card" style={{ borderColor: 'rgba(95,184,110,.3)', background: 'rgba(95,184,110,.04)' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <span style={{ fontSize: '1.25rem' }}>✅</span>
            <div>
              <div style={{ fontFamily: 'var(--font-display)', fontSize: 12, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-success)', marginBottom: 4 }}>
                Contrôle parental validé
              </div>
              <p style={{ margin: 0, fontFamily: 'var(--font-body)', fontSize: 14, color: 'var(--ln-muted)' }}>
                Contrôle parental validé par votre tuteur légal ({account.parental_email}).
              </p>
            </div>
          </div>
        </div>

        <div style={{ marginTop: 24 }}>
          <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
        </div>
      </div>
    )
  }

  const hasPending = !!account.parental_email

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Contrôle parental</h1>
        <p>
          En tant que joueur mineur, la validation d&apos;un tuteur légal est requise pour accéder au jeu.
        </p>
      </div>

      <ParentalForm
        hasPending={hasPending}
        parentalEmail={account.parental_email ?? ''}
      />

      <div style={{ marginTop: 24 }}>
        <Link href="/player" className="btn btn-ghost">&larr; Retour à l&apos;espace joueur</Link>
      </div>
    </div>
  )
}
