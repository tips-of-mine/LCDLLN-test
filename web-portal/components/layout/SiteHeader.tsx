import Link from 'next/link'
import { getSession } from '@/lib/auth/session'
import { HeaderActions } from './HeaderActions'

export async function SiteHeader() {
  const session = await getSession()

  return (
    <header className="wp-header">
      <Link href="/" className="wp-logo">
        <div className="wp-logo-moon" />
        <span className="wp-logo-text">Les Chroniques de la Lune Noire</span>
      </Link>

      <HeaderActions session={session} />
    </header>
  )
}
