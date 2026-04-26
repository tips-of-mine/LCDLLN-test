'use client'

import { useState, useTransition } from 'react'
import { useRouter } from 'next/navigation'

type RoadmapItem = {
  id: number
  title: string
  description: string | null
  status: 'completed' | 'in_progress' | 'planned'
  category: string | null
  display_order: number
}

const STATUS_LABELS: Record<string, string> = {
  completed: 'Terminé',
  in_progress: 'En cours',
  planned: 'Planifié',
}

const STATUS_BADGE: Record<string, string> = {
  completed: 'wp-badge done',
  in_progress: 'wp-badge active',
  planned: 'wp-badge planned',
}

type FormValues = {
  title: string
  description: string
  status: 'completed' | 'in_progress' | 'planned'
  category: string
  display_order: string
}

const emptyForm: FormValues = {
  title: '',
  description: '',
  status: 'planned',
  category: '',
  display_order: '',
}

function itemToForm(item: RoadmapItem): FormValues {
  return {
    title: item.title,
    description: item.description ?? '',
    status: item.status,
    category: item.category ?? '',
    display_order: String(item.display_order),
  }
}

export default function RoadmapAdmin({ items: initialItems }: { items: RoadmapItem[] }) {
  const router = useRouter()
  const [isPending, startTransition] = useTransition()
  const [items, setItems] = useState<RoadmapItem[]>(initialItems)
  const [showAddForm, setShowAddForm] = useState(false)
  const [addForm, setAddForm] = useState<FormValues>(emptyForm)
  const [editingId, setEditingId] = useState<number | null>(null)
  const [editForm, setEditForm] = useState<FormValues>(emptyForm)
  const [error, setError] = useState<string | null>(null)

  async function refreshItems() {
    const res = await fetch('/api/admin/roadmap')
    if (res.ok) {
      const data = await res.json() as RoadmapItem[]
      setItems(data)
    }
  }

  async function handleAdd(e: React.FormEvent) {
    e.preventDefault()
    setError(null)
    const body: Record<string, unknown> = {
      title: addForm.title.trim(),
      status: addForm.status,
    }
    if (addForm.description.trim()) body.description = addForm.description.trim()
    if (addForm.category.trim()) body.category = addForm.category.trim()
    if (addForm.display_order.trim()) body.display_order = parseInt(addForm.display_order, 10)

    const res = await fetch('/api/admin/roadmap', {
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

  function startEdit(item: RoadmapItem) {
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
      title: editForm.title.trim(),
      status: editForm.status,
      description: editForm.description.trim() || null,
      category: editForm.category.trim() || null,
      display_order: editForm.display_order.trim() ? parseInt(editForm.display_order, 10) : undefined,
    }
    // Remove undefined keys
    for (const k of Object.keys(body)) {
      if (body[k] === undefined) delete body[k]
    }
    const res = await fetch(`/api/admin/roadmap/${id}`, {
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

  async function handleDelete(id: number, title: string) {
    if (!window.confirm(`Supprimer "${title}" ? Cette action est irréversible.`)) return
    setError(null)
    const res = await fetch(`/api/admin/roadmap/${id}`, { method: 'DELETE' })
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
        <div className="wp-section-title" style={{ margin: 0 }}>Items ({items.length})</div>
        <button
          className="wp-badge active"
          style={{ cursor: 'pointer', border: 'none', padding: '6px 14px', fontSize: 13 }}
          onClick={() => { setShowAddForm(!showAddForm); setError(null) }}
        >
          {showAddForm ? 'Annuler' : '+ Ajouter un item'}
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
          <h3 style={{ margin: '0 0 1rem', fontFamily: 'var(--font-display)', color: 'var(--ln-accent)', fontSize: 15 }}>Nouvel item</h3>
          <form onSubmit={handleAdd} style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <RoadmapFormFields form={addForm} onChange={setAddForm} />
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
              <th style={thStyle}>Ordre</th>
              <th style={thStyle}>Titre</th>
              <th style={thStyle}>Catégorie</th>
              <th style={thStyle}>Statut</th>
              <th style={{ ...thStyle, textAlign: 'right' }}>Actions</th>
            </tr>
          </thead>
          <tbody>
            {items.length === 0 && (
              <tr>
                <td colSpan={5} style={{ textAlign: 'center', padding: '2rem', color: 'var(--ln-muted)' }}>
                  Aucun item dans la roadmap.
                </td>
              </tr>
            )}
            {items.map(item => (
              editingId === item.id ? (
                <tr key={item.id} style={{ borderBottom: '1px solid rgba(255,255,255,.07)' }}>
                  <td colSpan={5} style={{ padding: '1rem' }}>
                    <form onSubmit={e => handleEdit(e, item.id)} style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
                      <RoadmapFormFields form={editForm} onChange={setEditForm} />
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
                  <td style={{ ...tdStyle, fontWeight: 500 }}>
                    <div>{item.title}</div>
                    {item.description && (
                      <div style={{ fontSize: 11, color: 'var(--ln-muted)', marginTop: 2 }}>
                        {item.description.length > 80 ? item.description.slice(0, 80) + '…' : item.description}
                      </div>
                    )}
                  </td>
                  <td style={{ ...tdStyle, color: 'var(--ln-muted)' }}>{item.category ?? '—'}</td>
                  <td style={tdStyle}>
                    <span className={STATUS_BADGE[item.status] ?? 'wp-badge planned'}>
                      {STATUS_LABELS[item.status] ?? item.status}
                    </span>
                  </td>
                  <td style={{ ...tdStyle, textAlign: 'right' }}>
                    <div style={{ display: 'flex', gap: 6, justifyContent: 'flex-end' }}>
                      <button
                        className="wp-badge active"
                        style={{ cursor: 'pointer', border: 'none', padding: '4px 10px', fontSize: 12 }}
                        onClick={() => startEdit(item)}
                      >
                        Modifier
                      </button>
                      <button
                        className="wp-badge"
                        style={{ cursor: 'pointer', border: 'none', padding: '4px 10px', fontSize: 12, background: 'rgba(200,50,50,.2)', color: '#e05050' }}
                        onClick={() => handleDelete(item.id, item.title)}
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

function RoadmapFormFields({
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
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
        <div>
          <label style={labelStyle}>Titre *</label>
          <input
            style={inputStyle}
            value={form.title}
            onChange={e => onChange({ ...form, title: e.target.value })}
            required
            placeholder="Titre de l'item"
          />
        </div>
        <div>
          <label style={labelStyle}>Catégorie</label>
          <input
            style={inputStyle}
            value={form.category}
            onChange={e => onChange({ ...form, category: e.target.value })}
            placeholder="ex : Portail, Jeu, Infrastructure"
          />
        </div>
      </div>
      <div>
        <label style={labelStyle}>Description</label>
        <textarea
          style={{ ...inputStyle, minHeight: 60, resize: 'vertical' }}
          value={form.description}
          onChange={e => onChange({ ...form, description: e.target.value })}
          placeholder="Description courte (optionnel)"
        />
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
        <div>
          <label style={labelStyle}>Statut *</label>
          <select
            style={inputStyle}
            value={form.status}
            onChange={e => onChange({ ...form, status: e.target.value as FormValues['status'] })}
            required
          >
            <option value="planned">Planifié</option>
            <option value="in_progress">En cours</option>
            <option value="completed">Terminé</option>
          </select>
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
    </>
  )
}
