import { PasswordChangeForm } from '@/components/auth/PasswordChangeForm'

export default function SecurityPage() {
  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Sécurité du compte</h1>
        <p>Gérez votre mot de passe et les options de sécurité de votre compte.</p>
      </div>

      {/* Section 1 — Changement de mot de passe */}
      <div className="wp-section-title">Changement de mot de passe</div>
      <PasswordChangeForm />

      {/* Section 2 — MFA (placeholder) */}
      <div className="wp-section-title" style={{ marginTop: '2rem' }}>Authentification multi-facteurs</div>
      <div className="wp-card" style={{ opacity: 0.6 }}>
        <h3 style={{ margin: '0 0 8px', fontFamily: 'var(--font-display)', color: 'var(--ln-accent)' }}>
          Authentification multi-facteurs
        </h3>
        <span className="wp-badge planned" style={{ marginBottom: 12, display: 'inline-block' }}>
          Bientôt disponible
        </span>
        <p style={{ margin: 0, fontSize: 14, color: 'var(--ln-muted)' }}>
          Renforcez la sécurité de votre compte avec une double authentification. Cette fonctionnalité sera disponible prochainement.
        </p>
      </div>
    </div>
  )
}
