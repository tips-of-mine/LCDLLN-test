'use client'

import { useState, useTransition } from 'react'
import { useRouter } from 'next/navigation'

type FaqItem = {
  id: number
  question: string
  answer: string
  category: string | null
  display_order: number
  published: number
}

type FormValues = {
  question: string
  answer: string
  category: string
  display_order: string
  published: boolean
}

const emptyForm: FormValues = {
  question: '',
  answer: '',
  category: '',
  display_order: '',
  published: false,
}

function itemToForm(item: FaqItem): FormValues {
  return {
    question: item.question,
    answer: item.answer,
    category: item.category ?? '',
    display_order: String(item.display_order),
    published: item.published === 1,
  }
}

export default function FaqAdmin({ items: initialItems }: { items: FaqItem[] }) {
  const router = useRouter()
  const [isPending, startTransition] = useTransition()
  const [items, setItems] = useState<FaqItem[]>(initialItems)
  const [showAddForm, setShowAddForm] = useState(false)
  const [addForm, setAddForm] = useState<FormValues>(emptyForm)
  const [editingId, setEditingId] = useState<number | null>(null)
  const [editForm, setEditForm] = useState<FormValues>(emptyForm)
  const [error, setError] = useState<string | null>(null)

  async function refreshItems() {
    const res = await fetch('/api/admin/faq')
    if (res.ok) {
      const data = await res.json() as FaqItem[]
      setItems(data)
    }
  }

  async function handleAdd(e: React.FormEvent) {
    e.preventDefault()
    setError(null)
    const body: Record<string, unknown> = {
      question: addForm.question.trim(),
      answer: addForm.answer.trim(),
      published: addForm.published,
    }
    if (addForm.category.trim()) body.category = addForm.category.trim()
    if (addForm.display_order.trim()) body.displayOrder = parseInt(addForm.display_order, 10)

    const res = await fetch('/api/admin/faq', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    if (!res.ok) {
      const data = await res.json() as { error?: string }
      setError(data.error ?? 'Erreur lors de la création')
      return
    }
    setAddForm(emptyForm)
    setShowAddForm(false)
    startTransition(() => {
      router.refresh()
      void refreshItems()
    })
  }

  function startEdit(item: FaqItem) {
    setEditingId(item.id)
    setEditForm(itemToForm(item))
    setError(null)
  }

  function cancelEdit() {
    setEditingId(null)
    setError(null)
  }

  async function handleEdit(e: React.FormEvent, id: number) {
    e.preventDefault()
    setError(null)
    const body: Record<string, unknown> = {
      question: editForm.question.trim(),
      answer: editForm.answer.trim(),
      category: editForm.category.trim() || null,
      published: editForm.published,
    }
    if (editForm.display_order.trim()) {
      body.display_order = parseInt(editForm.display_order, 10)
    }
    const res = await fetch(`/api/admin/faq/${id}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
    if (!res.ok) {
      const data = await res.json() as { error?: string }
      setError(data.error ?? 'Erreur lors de la modification')
      return
    }
    setEditingId(null)
    startTransition(() => {
      router.refresh()
      void refreshItems()
    })
  }

  async function handleTogglePublished(item: FaqItem) {
    setError(null)
    const res = await fetch(`/api/admin/faq/${item.id}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ published: item.published === 0 }),
    })
    if (!res.ok) {
      const data = await res.json() as { error?: string }
      setError(data.error ?? 'Erreur lors du changement de statut')
      return
    }
    startTransition(() => {
      router.refresh()
      void refreshItems()
    })
  }

  async function handleDelete(id: number, question: string) {
    if (!window.confirm(`Supprimer cette question ?\n\n"${question}"\n\nCette action est irréversible.`)) return
    setError(null)
    const res = await fetch(`/api/admin/faq/${id}`, { method: 'DELETE' })
    if (!res.ok) {
      const data = await res.json() as { error?: string }
      setError(data.error ?? 'Erreur lors de la suppression')
      return
    }
    startTransition(() => {
      router.refresh()
      void refreshItems()
    })
  }

  return (
    <>
      {/* Action bar */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem', flexWrap: 'wrap', gap: 8 }}>
        <div className="wp-section-title" style={{ margin: 0 }}>Questions ({items.length})</div>
        <button
          className="wp-badge active"
          style={{ cursor: 'pointer', border: 'none', padding: '6px 14px', fontSize: 13 }}
          onClick={() => { setShowAddForm(!showAddForm); setError(null) }}
        >
          {showAddForm ? 'Annuler' : '+ Nouvelle question'}
        </button>
      </div>

      {error && (
        <div className="wp-card" style={{ borderColor: 'rgba(220,50,50,.5)', color: '#e05050', marginBottom: '1rem', padding: '0.75rem 1rem' }}>
          {error}
        </div>
      )}

      {/* Add form */}
      {showAddForm && (
        <div className="wp-card" style={{ marginBottom: '1.5rem', borderColor: 'rgba(232,165,92,.35)' }}>
          <h3 style={{ margin: '0 0 1rem', fontFamily: 'var(--font-display)', color: 'var(--ln-accent)', fontSize: 15 }}>Nouvelle question</h3>
          <form onSubmit={handleAdd} style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <FaqFormFields form={addForm} onChange={setAddForm} />
            <div style={{ display: 'flex', gap: 8, marginTop: 4 }}>
              <button type="submit" className="wp-badge done" style={{ cursor: 'pointer', border: 'none', padding: '6px 14px', fontSize: 13 }} disabled={isPending}>
                Créer
              </button>
              <button type="button" className="wp-badge planned" style={{ cursor: 'pointer', border: 'none', padding: '6px 14px', fontSize: 13 }} onClick={() => { setShowAddForm(false); setAddForm(emptyForm) }}>
                Annuler
              </button>
            </div>
          </form>
        </div>
      )}

      {/* Items table */}
      <div style={{ overflowX: 'auto' }}>
        <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
          <thead>
            <tr style={{ borderBottom: '1px solid rgba(255,255,255,.1)' }}>
              <th style={thStyle}>Ord.</th>
              <th style={thStyle}>Question</th>
              <th style={thStyle}>Catégorie</th>
              <th style={thStyle}>Publié</th>
              <th style={{ ...thStyle, textAlign: 'right' }}>Actions</th>
            </tr>
          </thead>
          <tbody>
            {items.length === 0 && (
              <tr>
                <td colSpan={5} style={{ textAlign: 'center', padding: '2rem', color: 'var(--ln-muted)' }}>
                  Aucune question dans la FAQ.
                </td>
              </tr>
            )}
            {items.map(item => (
              editingId === item.id ? (
                <tr key={item.id} style={{ borderBottom: '1px solid rgba(255,255,255,.07)' }}>
                  <td colSpan={5} style={{ padding: '1rem' }}>
                    <form onSubmit={e => handleEdit(e, item.id)} style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                      <FaqFormFields form={editForm} onChange={setEditForm} />
                      <div style={{ display: 'flex', gap: 8 }}>
                        <button type="submit" className="wp-badge done" style={{ cursor: 'pointer', border: 'none', padding: '5px 12px', fontSize: 12 }} disabled={isPending}>
                          Enregistrer
                        </button>
                        <button type="button" className="wp-badge planned" style={{ cursor: 'pointer', border: 'none', padding: '5px 12px', fontSize: 12 }} onClick={cancelEdit}>
                          Annuler
                        </button>
                      </div>
                    </form>
                  </td>
                </tr>
              ) : (
                <tr key={item.id} style={{ borderBottom: '1px solid rgba(255,255,255,.07)' }}>
                  <td style={tdStyle}>{item.display_order}</td>
                  <td style={{ ...tdStyle, fontWeight: 500, maxWidth: 320 }}>
                    <div>{item.question.length > 80 ? item.question.slice(0, 80) + '…' : item.question}</div>
                    <div style={{ fontSize: 11, color: 'var(--ln-muted)', marginTop: 2 }}>
                      {item.answer.length > 60 ? item.answer.slice(0, 60) + '…' : item.answer}
                    </div>
                  </td>
                  <td style={{ ...tdStyle, color: 'var(--ln-muted)' }}>{item.category ?? '—'}</td>
                  <td style={tdStyle}>
                    <span className={item.published === 1 ? 'wp-badge done' : 'wp-badge planned'}>
                      {item.published === 1 ? 'Oui' : 'Non'}
                    </span>
                  </td>
                  <td style={{ ...tdStyle, textAlign: 'right' }}>
                    <div style={{ display: 'flex', gap: 6, justifyContent: 'flex-end', flexWrap: 'wrap' }}>
                      <button
                        className="wp-badge active"
                        style={{ cursor: 'pointer', border: 'none', padding: '4px 10px', fontSize: 12 }}
                        onClick={() => startEdit(item)}
                      >
                        Modifier
                      </button>
                      <button
                        className={item.published === 1 ? 'wp-badge planned' : 'wp-badge done'}
                        style={{ cursor: 'pointer', border: 'none', padding: '4px 10px', fontSize: 12 }}
                        onClick={() => handleTogglePublished(item)}
                        disabled={isPending}
                      >
                        {item.published === 1 ? 'Archiver' : 'Publier'}
                      </button>
                      <button
                        className="wp-badge"
                        style={{ cursor: 'pointer', border: 'none', padding: '4px 10px', fontSize: 12, background: 'rgba(200,50,50,.2)', color: '#e05050' }}
                        onClick={() => handleDelete(item.id, item.question)}
                      >
                        Supprimer
                      </button>
                    </div>
                  </td>
                </tr>
              )
            ))}
          </tbody>
        </table>
      </div>
    </>
  )
}

const thStyle: React.CSSProperties = {
  textAlign: 'left',
  padding: '8px 12px',
  fontFamily: 'var(--font-display)',
  fontSize: 12,
  color: 'var(--ln-muted)',
  fontWeight: 600,
  textTransform: 'uppercase',
  letterSpacing: '0.05em',
}

const tdStyle: React.CSSProperties = {
  padding: '10px 12px',
  verticalAlign: 'top',
}

function FaqFormFields({
  form,
  onChange,
}: {
  form: FormValues
  onChange: (f: FormValues) => void
}) {
  const inputStyle: React.CSSProperties = {
    background: 'rgba(255,255,255,.06)',
    border: '1px solid rgba(255,255,255,.12)',
    borderRadius: 6,
    color: 'var(--ln-text)',
    padding: '6px 10px',
    fontSize: 13,
    width: '100%',
    boxSizing: 'border-box',
  }
  const labelStyle: React.CSSProperties = {
    fontSize: 12,
    color: 'var(--ln-muted)',
    display: 'block',
    marginBottom: 4,
  }
  return (
    <>
      <div>
        <label style={labelStyle}>Question *</label>
        <input
          style={inputStyle}
          value={form.question}
          onChange={e => onChange({ ...form, question: e.target.value })}
          required
          maxLength={500}
          placeholder="La question fréquemment posée"
        />
      </div>
      <div>
        <label style={labelStyle}>Réponse *</label>
        <textarea
          style={{ ...inputStyle, minHeight: 80, resize: 'vertical' }}
          value={form.answer}
          onChange={e => onChange({ ...form, answer: e.target.value })}
          required
          placeholder="La réponse détaillée"
        />
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
        <div>
          <label style={labelStyle}>Catégorie</label>
          <input
            style={inputStyle}
            value={form.category}
            onChange={e => onChange({ ...form, category: e.target.value })}
            maxLength={100}
            placeholder="ex : Compte, Jeu, Technique"
          />
        </div>
        <div>
          <label style={labelStyle}>Ordre d&apos;affichage</label>
          <input
            style={inputStyle}
            type="number"
            value={form.display_order}
            onChange={e => onChange({ ...form, display_order: e.target.value })}
            placeholder="Laisser vide = auto"
            min={0}
          />
        </div>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <input
          type="checkbox"
          id="faq-published"
          checked={form.published}
          onChange={e => onChange({ ...form, published: e.target.checked })}
          style={{ cursor: 'pointer' }}
        />
        <label htmlFor="faq-published" style={{ ...labelStyle, marginBottom: 0, cursor: 'pointer' }}>
          Publier immédiatement (visible sur la page support)
        </label>
      </div>
    </>
  )
}
