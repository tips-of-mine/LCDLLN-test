/* Lune Noire — Auth Flow shared primitives (FR)
   Responsive 1280x720 ↔ 1920x1080, fond animé + fil d'Ariane.
   Style matches colors_and_type.css (Windlass + Morpheus). */

const { useState, useEffect, useMemo, useRef } = React;

// =============== BACKDROP (fond animé togglable) ===============
function Backdrop({ animated = true, children }) {
  return (
    <div className="ln-backdrop">
      {animated && <AnimatedFog />}
      {/* arches + brume statiques */}
      <svg className="ln-arches" preserveAspectRatio="xMidYMax slice" viewBox="0 0 1920 1080">
        <defs>
          <linearGradient id="arch2" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#000" stopOpacity="0" />
            <stop offset="1" stopColor="#000" stopOpacity="1" />
          </linearGradient>
        </defs>
        <path fill="url(#arch2)" d="M0,1080 L0,720 L140,720 L140,600 Q200,510 260,600 L260,720 L380,720 L380,560 Q440,460 500,560 L500,720 L640,720 L640,520 Q720,400 800,520 L800,720 L960,720 L960,490 Q1060,360 1160,490 L1160,720 L1320,720 L1320,560 Q1400,460 1480,560 L1480,720 L1620,720 L1620,600 Q1680,510 1740,600 L1740,720 L1920,720 L1920,1080 Z" />
      </svg>
      {/* lune */}
      <div className="ln-moon" />
      {/* fog at bottom */}
      <div className="ln-fog-bottom" />
      {children}
    </div>
  );
}

function AnimatedFog() {
  return (
    <>
      <div className="ln-fog-layer ln-fog-a" />
      <div className="ln-fog-layer ln-fog-b" />
      <div className="ln-particles">
        {Array.from({ length: 22 }).map((_, i) => (
          <span key={i} style={{
            left: `${(i * 47) % 100}%`,
            animationDelay: `${(i * 0.7) % 8}s`,
            animationDuration: `${10 + (i % 6) * 2}s`,
          }} />
        ))}
      </div>
    </>
  );
}

// =============== FIL D'ARIANE ===============
function Breadcrumb({ steps, current }) {
  return (
    <nav className="ln-breadcrumb" aria-label="Progression">
      {steps.map((s, i) => {
        const state = i < current ? 'done' : i === current ? 'active' : 'todo';
        return (
          <React.Fragment key={s.key}>
            <div className={`ln-crumb ${state}`}>
              <span className="ln-crumb-num">{String(i + 1).padStart(2, '0')}</span>
              <span className="ln-crumb-label">{s.label}</span>
            </div>
            {i < steps.length - 1 && <span className={`ln-crumb-sep ${i < current ? 'done' : ''}`} />}
          </React.Fragment>
        );
      })}
    </nav>
  );
}

// =============== PANEL ===============
function Panel({ title, subtitle, versionLabel, children, footer, width, tight, onInfo, infoText }) {
  const [showInfo, setShowInfo] = useState(false);
  return (
    <div className="ln-auth-panel" style={{ width: width || 'min(520px, 92vw)' }}>
      {(title || versionLabel) && (
        <div className="ln-auth-panel-header">
          <div>
            {title && <h2 className="ln-auth-title">{title}</h2>}
            {subtitle && <p className="ln-auth-subtitle">{subtitle}</p>}
          </div>
          <div className="ln-auth-panel-header-right">
            {infoText && (
              <button className="ln-info-icon" onClick={() => setShowInfo(s => !s)} aria-label="Aide">i</button>
            )}
            {versionLabel && <span className="ln-version">{versionLabel}</span>}
          </div>
        </div>
      )}
      {showInfo && infoText && (
        <div className="ln-info-popup"><p>{infoText}</p></div>
      )}
      <div className={`ln-auth-panel-body ${tight ? 'tight' : ''}`}>{children}</div>
      {footer && <div className="ln-auth-panel-footer">{footer}</div>}
    </div>
  );
}

// =============== CHAMPS ===============
function Field({ label, value, onChange, type = 'text', tooltip, status, statusMsg, placeholder, autoFocus, hint }) {
  const [focus, setFocus] = useState(false);
  const ref = useRef(null);
  useEffect(() => { if (autoFocus && ref.current) ref.current.focus(); }, [autoFocus]);
  const border = status === 'ok' ? 'var(--ln-success)' : status === 'err' ? 'var(--ln-error)' : status === 'pending' ? 'var(--ln-warning)' : (focus ? 'var(--ln-primary)' : 'var(--ln-border)');
  return (
    <div className="ln-field">
      <label>{label}</label>
      <div className="ln-field-input" style={{ borderColor: border, boxShadow: focus ? '0 0 0 2px rgba(74,123,184,.18)' : 'none' }}>
        <input
          ref={ref}
          type={type} value={value} placeholder={placeholder}
          onChange={e => onChange && onChange(e.target.value)}
          onFocus={() => setFocus(true)} onBlur={() => setFocus(false)}
        />
        {status === 'ok' && <span className="ln-field-icon ok">✓</span>}
        {status === 'err' && <span className="ln-field-icon err">✕</span>}
        {status === 'pending' && <span className="ln-field-icon pending">…</span>}
      </div>
      {statusMsg && <div className={`ln-field-msg ${status}`}>{statusMsg}</div>}
      {tooltip && !statusMsg && <div className="ln-field-tooltip">{tooltip}</div>}
      {hint && <div className="ln-field-hint">{hint}</div>}
    </div>
  );
}

function Dropdown({ label, value, options, onChange, tooltip }) {
  const [open, setOpen] = useState(false);
  return (
    <div className="ln-field">
      <label>{label}</label>
      <div className="ln-dropdown" onClick={() => setOpen(o => !o)} tabIndex={0}>
        <span>{options.find(o => o.value === value)?.label || '—'}</span>
        <span className="ln-dropdown-chev">▾</span>
        {open && (
          <div className="ln-dropdown-menu">
            {options.map(o => (
              <div key={o.value} className={`ln-dropdown-item ${o.value === value ? 'active' : ''}`}
                onClick={(e) => { e.stopPropagation(); onChange(o.value); setOpen(false); }}>
                {o.label}
              </div>
            ))}
          </div>
        )}
      </div>
      {tooltip && <div className="ln-field-tooltip">{tooltip}</div>}
    </div>
  );
}

// =============== BOUTONS ===============
// ============ BOUTON AVEC SPINNER ============
function Button({ kind = 'primary', children, onClick, keycap, fullWidth, disabled, size = 'md', loading }) {
  return (
    <button
      className={`ln-btn ln-btn-${kind} ln-btn-${size} ${fullWidth ? 'full' : ''}`}
      onClick={onClick} disabled={disabled || loading}
    >
      {loading && <span className="ln-spinner" />}
      <span>{children}</span>
      {keycap && !loading && <kbd>{keycap}</kbd>}
    </button>
  );
}

function KeycapHint({ keyLabel, children }) {
  return <span className="ln-keycap-hint"><kbd>{keyLabel}</kbd>{children}</span>;
}

// =============== BANNIÈRES ===============
function Banner({ kind = 'info', title, children, action }) {
  const icon = { error: '✕', warning: '!', info: 'i', ok: '✓' }[kind];
  return (
    <div className={`ln-banner ln-banner-${kind}`}>
      <span className="ln-banner-icon">{icon}</span>
      <div className="ln-banner-body">
        {title && <div className="ln-banner-title">{title}</div>}
        {children && <div className="ln-banner-text">{children}</div>}
      </div>
      {action}
    </div>
  );
}

// =============== SLIDER ===============
function Slider({ label, value, onChange, min = 0, max = 100, step = 1, unit = '', hint }) {
  return (
    <div className="ln-slider">
      <div className="ln-slider-head">
        <label>{label}</label>
        <span className="ln-slider-val">{value}{unit}</span>
      </div>
      <input type="range" min={min} max={max} step={step} value={value} onChange={e => onChange(+e.target.value)} />
      {hint && <div className="ln-field-tooltip">{hint}</div>}
    </div>
  );
}

function Toggle({ label, checked, onChange, hint }) {
  return (
    <div className="ln-toggle-row" onClick={() => onChange(!checked)}>
      <div>
        <div className="ln-toggle-label">{label}</div>
        {hint && <div className="ln-field-tooltip">{hint}</div>}
      </div>
      <div className={`ln-toggle ${checked ? 'on' : ''}`}><div className="ln-toggle-thumb" /></div>
    </div>
  );
}

Object.assign(window, { Backdrop, Breadcrumb, Panel, Field, Dropdown, Button, KeycapHint, Banner, Slider, Toggle });
