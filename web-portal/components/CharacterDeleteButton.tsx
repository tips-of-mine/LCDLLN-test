'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'

interface Props {
  characterId: number
  characterName: string
}

export function CharacterDeleteButton({ characterId, characterName }: Props) {
  const [step, setStep] = useState<0 | 1 | 2>(0) // 0=idle, 1=first confirm, 2=second confirm
  const [loading, setLoading] = useState(false)
  const router = useRouter()

  async function handleDelete() {
    if (step === 0) { setStep(1); return }
    if (step === 1) { setStep(2); return }
    // step === 2: actually delete
    setLoading(true)
    try {
      const res = await fetch(`/api/player/characters/${characterId}/delete`, { method: 'PATCH' })
      if (res.ok) { router.refresh() }
    } finally {
      setLoading(false)
      setStep(0)
    }
  }

  const labels = [
    'Supprimer',
    `Êtes-vous sûr de vouloir supprimer "${characterName}" ?`,
    'Confirmer la suppression définitive',
  ]

  return (
    <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
      <button
        onClick={handleDelete}
        disabled={loading}
        style={{
          background: step > 0 ? 'rgba(196,64,64,.15)' : 'none',
          border: `1px solid ${step > 0 ? 'var(--ln-error)' : 'var(--ln-border)'}`,
          color: step > 0 ? 'var(--ln-error)' : 'var(--ln-muted)',
          fontFamily: 'var(--font-ui)',
          fontSize: '11px',
          letterSpacing: '.15em',
          textTransform: 'uppercase',
          padding: '6px 12px',
          borderRadius: 'var(--radius-sm)',
          cursor: loading ? 'wait' : 'pointer',
          transition: 'all .18s',
        }}
      >
        {loading ? '...' : labels[step]}
      </button>
      {step > 0 && (
        <button
          onClick={() => setStep(0)}
          style={{
            background: 'none', border: '1px solid var(--ln-border)',
            color: 'var(--ln-muted)', fontFamily: 'var(--font-ui)',
            fontSize: '11px', letterSpacing: '.15em', textTransform: 'uppercase',
            padding: '6px 12px', borderRadius: 'var(--radius-sm)', cursor: 'pointer',
          }}
        >
          Annuler
        </button>
      )}
    </div>
  )
}
