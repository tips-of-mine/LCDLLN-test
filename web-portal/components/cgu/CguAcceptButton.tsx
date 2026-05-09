'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'

export function CguAcceptButton({ editionId }: { editionId: number }) {
  const [loading, setLoading] = useState(false)
  const router = useRouter()

  async function handleAccept() {
    setLoading(true)
    try {
      await fetch('/api/player/cgu/accept', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ editionId }),
      })
      router.refresh()
    } finally {
      setLoading(false)
    }
  }

  return (
    <button onClick={handleAccept} disabled={loading} style={{
      background: 'linear-gradient(180deg, #5a8bc6 0%, #3E689E 100%)',
      border: '1px solid #6b9bd4', color: '#F2F4F8',
      fontFamily: 'var(--font-ui)', fontSize: '11px', letterSpacing: '.2em',
      textTransform: 'uppercase', padding: '6px 16px',
      borderRadius: 'var(--radius-sm)', cursor: loading ? 'wait' : 'pointer',
    }}>
      {loading ? '...' : 'Accepter'}
    </button>
  )
}
