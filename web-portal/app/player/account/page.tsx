import { redirect } from 'next/navigation'
import { getSession } from '@/lib/session'
import { query } from '@/lib/db'
import type { RowDataPacket } from 'mysql2/promise'
import { AccountForm } from '@/components/AccountForm'

type AccountRow = RowDataPacket & {
  id: number
  login: string
  tag_id: string | null
  email: string
  first_name: string | null
  last_name: string | null
  address_street: string | null
  address_city: string | null
  address_zip: string | null
  address_country: string | null
  email_pending: string | null
}

type PageProps = {
  searchParams: { success?: string; error?: string }
}

export default async function AccountPage({ searchParams }: PageProps) {
  const session = await getSession()
  if (!session) redirect('/login')

  const rows = await query<AccountRow[]>(
    'SELECT id, login, tag_id, email, first_name, last_name, address_street, address_city, address_zip, address_country, email_pending FROM accounts WHERE id = ? LIMIT 1',
    [session.accountId]
  )
  const account = rows[0]
  if (!account) redirect('/login')

  const successParam = searchParams.success
  const errorParam = searchParams.error

  const errorMessages: Record<string, string> = {
    token_manquant: 'Lien de confirmation invalide (token manquant).',
    token_invalide: 'Lien de confirmation invalide ou expiré.',
    token_expire: 'Le lien de confirmation a expiré. Veuillez faire une nouvelle demande.',
    erreur_serveur: 'Une erreur serveur est survenue. Veuillez réessayer.',
  }

  return (
    <div className="wp-main narrow">
      <div className="wp-page-header">
        <h1>Détail du compte</h1>
        <p>Gérez vos informations personnelles, votre adresse email et postale.</p>
      </div>

      {successParam === 'email_confirme' && (
        <div className="wp-alert success" style={{ marginBottom: 24 }}>
          <span className="wp-alert-icon">✓</span>
          Votre nouvelle adresse email a été confirmée avec succès.
        </div>
      )}
      {errorParam && errorMessages[errorParam] && (
        <div className="wp-alert error" style={{ marginBottom: 24 }}>
          <span className="wp-alert-icon">✕</span>
          {errorMessages[errorParam]}
        </div>
      )}

      {/* TAG-ID — display only */}
      <div className="wp-card" style={{ marginBottom: 16 }}>
        <div className="wp-section-title" style={{ marginBottom: 12 }}>Identifiant public</div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <span style={{ fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '0.2em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>TAG-ID</span>
          <span style={{ fontFamily: 'var(--font-display)', fontSize: 20, fontWeight: 700, letterSpacing: '0.12em', color: 'var(--ln-accent)' }}>
            {account.tag_id ?? '—'}
          </span>
          <span style={{ fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '0.2em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginLeft: 4 }}>
            (non modifiable)
          </span>
        </div>
        <div style={{ marginTop: 8, fontFamily: 'var(--font-body)', fontSize: 13, color: 'var(--ln-muted)', fontStyle: 'italic' }}>
          Login&nbsp;: <strong style={{ color: 'var(--ln-text)', fontStyle: 'normal' }}>{account.login}</strong>
        </div>
      </div>

      <AccountForm
        firstName={account.first_name ?? ''}
        lastName={account.last_name ?? ''}
        email={account.email}
        emailPending={account.email_pending ?? null}
        addressStreet={account.address_street ?? ''}
        addressCity={account.address_city ?? ''}
        addressZip={account.address_zip ?? ''}
        addressCountry={account.address_country ?? ''}
      />
    </div>
  )
}
