// Shared primitives for Lune Noire UI kit

function Button({ kind = 'primary', children, onClick, keycap, style }) {
  const base = {
    fontFamily: 'var(--font-ui)', fontSize: 12.5, letterSpacing: '.16em',
    textTransform: 'uppercase', padding: '10px 18px', borderRadius: 'var(--radius-md)',
    cursor: 'pointer', display: 'inline-flex', alignItems: 'center', gap: 8,
    transition: 'all .18s cubic-bezier(.2,.6,.2,1)',
  };
  const variants = {
    primary: {
      background: 'linear-gradient(180deg, #5a8bc6 0%, #3E689E 100%)',
      color: '#F2F4F8', border: '1px solid #6b9bd4',
      boxShadow: 'inset 0 1px 0 rgba(255,255,255,.18), 0 4px 14px rgba(0,0,0,.45)',
    },
    ghost: {
      background: 'rgba(20,28,40,.5)', color: 'var(--ln-text)',
      border: '1px solid var(--ln-border)',
    },
    accent: {
      background: 'transparent', color: 'var(--ln-accent)',
      border: '1px solid var(--ln-accent)',
    },
    danger: {
      background: '#2a0a10', color: '#FF7A7A', border: '1px solid #5a1a28',
    },
  };
  return (
    <button onClick={onClick} style={{ ...base, ...variants[kind], ...style }}>
      {children}
      {keycap && <kbd style={{ fontFamily: 'var(--font-mono)', fontSize: 9.5, padding: '2px 5px', border: '1px solid rgba(255,255,255,.25)', borderRadius: 3, opacity: .8 }}>{keycap}</kbd>}
    </button>
  );
}

function Field({ label, value, onChange, type = 'text', tooltip, status, placeholder }) {
  const [focus, setFocus] = useState(false);
  const statusColor = status === 'ok' ? '#5FB86E' : status === 'err' ? '#C44040' : null;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
      <label style={{ fontFamily: 'var(--font-ui)', fontSize: 10.5, letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>{label}</label>
      <div style={{
        display: 'flex', alignItems: 'center', gap: 8,
        background: 'rgba(10,13,18,.6)',
        border: `1px solid ${statusColor || (focus ? 'var(--ln-primary)' : 'var(--ln-border)')}`,
        borderRadius: 'var(--radius-md)', padding: '0 12px', height: 36,
        boxShadow: focus ? '0 0 0 2px rgba(74,123,184,.18)' : 'none',
        transition: 'all .18s cubic-bezier(.2,.6,.2,1)',
      }}>
        <input
          type={type} value={value} placeholder={placeholder}
          onChange={e => onChange && onChange(e.target.value)}
          onFocus={() => setFocus(true)} onBlur={() => setFocus(false)}
          style={{
            background: 'transparent', border: 0, outline: 0, color: 'var(--ln-text)',
            fontFamily: 'var(--font-body)', fontSize: 14, fontStyle: 'italic', width: '100%',
          }}
        />
      </div>
      {tooltip && <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 11.5, color: 'var(--ln-muted)' }}>{tooltip}</div>}
      {status === 'ok' && <div style={{ fontSize: 10, letterSpacing: '.15em', textTransform: 'uppercase', color: '#5FB86E' }}>✓ disponible</div>}
      {status === 'err' && <div style={{ fontSize: 10, letterSpacing: '.15em', textTransform: 'uppercase', color: '#C44040' }}>✗ invalide</div>}
    </div>
  );
}

function KeycapHint({ keyLabel, children }) {
  return (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 10.5, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>
      <kbd style={{ fontFamily: 'var(--font-mono)', fontSize: 9.5, padding: '2px 6px', border: '1px solid var(--ln-border)', borderRadius: 3, color: 'var(--ln-text)' }}>{keyLabel}</kbd>
      {children}
    </span>
  );
}

function Banner({ kind = 'info', children }) {
  const map = {
    error:   { color: '#FF7A7A', bg: 'rgba(196,64,64,.08)',  border: '#5a2020', icon: '✕' },
    warning: { color: '#FFD39A', bg: 'rgba(232,165,92,.08)', border: '#5a3a1a', icon: '!' },
    info:    { color: '#AFD0F5', bg: 'rgba(74,123,184,.1)',  border: '#2C4867', icon: 'i' },
    ok:      { color: '#9FE3AB', bg: 'rgba(95,184,110,.08)', border: '#245a2c', icon: '✓' },
  };
  const m = map[kind];
  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 10, padding: '10px 14px',
      borderRadius: 'var(--radius-md)', border: `1px solid ${m.border}`,
      background: m.bg, color: m.color,
      fontFamily: 'var(--font-body)', fontSize: 13,
    }}>
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 14, width: 18, textAlign: 'center' }}>{m.icon}</span>
      {children}
    </div>
  );
}

function MoonGlyph({ size = 20 }) {
  return (
    <svg viewBox="0 0 24 24" width={size} height={size} style={{ color: 'var(--ln-accent)', stroke: 'currentColor', fill: 'none', strokeWidth: 1.4 }}>
      <path d="M15 3a9 9 0 1 0 6 10c-4 0-6-4-6-10z" />
    </svg>
  );
}

Object.assign(window, { Button, Field, KeycapHint, Banner, MoonGlyph });
