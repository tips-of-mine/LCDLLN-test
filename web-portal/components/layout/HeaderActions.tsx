'use client'

import Link from 'next/link'
import { useState } from 'react'
import { useRouter } from 'next/navigation'
import type { Session } from '@/lib/auth/session'
import { isStaff } from '@/lib/auth/roles'

interface Props {
  session: Session
}

export function HeaderActions({ session }: Props) {
  const [open, setOpen] = useState(false)
  const router = useRouter()

  async function handleLogout() {
    await fetch('/api/auth/logout', { method: 'POST' })
    router.push('/')
    router.refresh()
  }

  return (
    <>
      <button
        className="wp-nav-toggle"
        onClick={() => setOpen((v) => !v)}
        aria-label="Menu"
        aria-expanded={open}
      >
        {open ? '✕' : '☰'}
      </button>

      <nav className={`wp-nav${open ? ' open' : ''}`} onClick={() => setOpen(false)}>
        <Link href="/roadmap">Roadmap</Link>
        <Link href="/support">Support</Link>
        <Link href="/bugs">Signaler un bug</Link>

        {session && (
          <Link href="/player">Espace joueur</Link>
        )}

        {isStaff(session?.role) && (
          <Link href="/admin">Admin</Link>
        )}

        {!session ? (
          <Link href="/login" className="cta">Connexion</Link>
        ) : (
          <>
            <span style={{
              fontFamily: 'var(--font-display)',
              fontSize: '11px',
              letterSpacing: '.2em',
              textTransform: 'uppercase',
              color: 'var(--ln-accent)',
              padding: '7px 12px',
              whiteSpace: 'nowrap',
            }}>
              {session.tagId ?? session.login}
            </span>
            <button
              onClick={handleLogout}
              style={{
                background: 'none',
                border: '1px solid var(--ln-border)',
                color: 'var(--ln-muted)',
                fontFamily: 'var(--font-ui)',
                fontSize: '11px',
                letterSpacing: '.2em',
                textTransform: 'uppercase',
                padding: '7px 12px',
                borderRadius: 'var(--radius-sm)',
                cursor: 'pointer',
                transition: 'all .18s',
                whiteSpace: 'nowrap',
              }}
              onMouseEnter={(e) => {
                (e.target as HTMLButtonElement).style.color = 'var(--ln-text)'
                ;(e.target as HTMLButtonElement).style.borderColor = 'var(--ln-accent)'
              }}
              onMouseLeave={(e) => {
                (e.target as HTMLButtonElement).style.color = 'var(--ln-muted)'
                ;(e.target as HTMLButtonElement).style.borderColor = 'var(--ln-border)'
              }}
            >
              Déconnexion
            </button>
          </>
        )}
      </nav>
    </>
  )
}
