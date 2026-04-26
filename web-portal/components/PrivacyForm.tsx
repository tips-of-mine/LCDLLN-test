'use client'
import { useState } from 'react'

type Visibility = 'public' | 'friends' | 'none'

interface Props {
  initialVisibility: Visibility
}

const options: { value: Visibility; label: string; desc: string }[] = [
  { value: 'public', label: 'Public', desc: 'Visible par tous les joueurs' },
  { value: 'friends', label: 'Amis uniquement', desc: 'Visible par vos amis uniquement' },
  { value: 'none', label: 'Personne', desc: 'Profil masqué' },
]

export function PrivacyForm({ initialVisibility }: Props) {
  const [visibility, setVisibility] = useState<Visibility>(initialVisibility)
  const [saving, setSaving] = useState(false)
  const [saved, setSaved] = useState(false)

  async function handleChange(value: Visibility) {
    setVisibility(value)
    setSaving(true)
    setSaved(false)
    try {
      await fetch('/api/player/privacy', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ visibility: value }),
      })
      setSaved(true)
    } finally {
      setSaving(false)
    }
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
      {options.map((opt) => (
        <label key={opt.value} style={{
          display: 'flex', alignItems: 'center', gap: 12,
          padding: '12px 16px', borderRadius: 'var(--radius-md)',
          border: `1px solid ${visibility === opt.value ? 'var(--ln-accent)' : 'var(--ln-border)'}`,
          background: visibility === opt.value ? 'rgba(232,197,110,.06)' : 'transparent',
          cursor: 'pointer', transition: 'all .18s',
        }}>
          <input
            type="radio"
            name="visibility"
            value={opt.value}
            checked={visibility === opt.value}
            onChange={() => handleChange(opt.value)}
            style={{ accentColor: 'var(--ln-accent)' }}
          />
          <div>
            <div style={{ fontFamily: 'var(--font-display)', fontSize: '13px', letterSpacing: '.1em', color: 'var(--ln-text)' }}>{opt.label}</div>
            <div style={{ fontSize: '13px', color: 'var(--ln-muted)', fontStyle: 'italic' }}>{opt.desc}</div>
          </div>
        </label>
      ))}
      {saving && <span style={{ fontSize: 13, color: 'var(--ln-muted)', fontStyle: 'italic' }}>Enregistrement...</span>}
      {saved && !saving && <span style={{ fontSize: 13, color: 'var(--ln-success)' }}>&#x2713; Préférence enregistrée</span>}
    </div>
  )
}
