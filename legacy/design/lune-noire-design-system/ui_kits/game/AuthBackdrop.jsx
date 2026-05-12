const { useState, useEffect, useMemo } = React;

// =============== AUTH BACKDROP ===============
function AuthBackdrop({ children }) {
  return (
    <div style={{
      position: 'absolute', inset: 0, overflow: 'hidden',
      background: `
        radial-gradient(ellipse 80% 60% at 50% 110%, rgba(74,123,184,.15), transparent 60%),
        radial-gradient(ellipse 60% 40% at 50% -10%, rgba(232,197,110,.08), transparent 70%),
        linear-gradient(180deg, #05070A 0%, #0A0D12 60%, #06080C 100%)
      `,
    }}>
      {/* Moon */}
      <div style={{
        position: 'absolute', top: '6%', right: '8%', width: 180, height: 180, borderRadius: '50%',
        background: 'radial-gradient(circle at 32% 30%, #2a2330 0%, #0B0712 55%, #000 100%)',
        boxShadow: 'inset -12px -18px 40px rgba(0,0,0,.9), inset 12px 14px 50px rgba(110,70,183,.18), 0 0 0 1px #1a1420, 0 0 60px rgba(74,123,184,.25)',
      }} />
      {/* Gothic arches silhouette */}
      <svg style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', opacity: 0.35 }} preserveAspectRatio="xMidYMax slice" viewBox="0 0 1920 1080">
        <defs>
          <linearGradient id="arch" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#000" stopOpacity="0" />
            <stop offset="1" stopColor="#000" stopOpacity="1" />
          </linearGradient>
        </defs>
        <path fill="url(#arch)" d="M0,1080 L0,740 L140,740 L140,620 Q200,530 260,620 L260,740 L380,740 L380,580 Q440,480 500,580 L500,740 L640,740 L640,540 Q720,420 800,540 L800,740 L960,740 L960,510 Q1060,380 1160,510 L1160,740 L1320,740 L1320,580 Q1400,480 1480,580 L1480,740 L1620,740 L1620,620 Q1680,530 1740,620 L1740,740 L1920,740 L1920,1080 Z" />
      </svg>
      {/* Fog */}
      <div style={{ position: 'absolute', inset: 0, background: 'radial-gradient(ellipse 100% 40% at 50% 90%, rgba(10,13,18,.85), transparent 60%)' }} />
      <div style={{ position: 'absolute', inset: 0, background: 'radial-gradient(ellipse 80% 30% at 50% 100%, rgba(74,123,184,.1), transparent 70%)' }} />
      {children}
    </div>
  );
}

// =============== AUTH PANEL ===============
function AuthPanel({ title, versionLabel, children, footer, width = 420 }) {
  return (
    <div style={{
      width, background: 'rgba(20,28,40,.72)',
      backdropFilter: 'blur(14px)', WebkitBackdropFilter: 'blur(14px)',
      border: '1px solid var(--ln-border)',
      borderRadius: 'var(--radius-md)',
      boxShadow: 'inset 0 1px 0 rgba(255,255,255,.05), inset 0 -1px 0 rgba(0,0,0,.6), 0 20px 60px rgba(0,0,0,.7)',
      color: 'var(--ln-text)',
    }}>
      <div style={{ padding: '16px 22px', borderBottom: '1px solid var(--ln-border)', display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <h3 style={{ margin: 0, fontFamily: 'var(--font-display)', fontWeight: 600, fontSize: 18, letterSpacing: '.18em', textTransform: 'uppercase' }}>{title}</h3>
        {versionLabel && <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)', letterSpacing: '.1em' }}>{versionLabel}</span>}
      </div>
      <div style={{ padding: '18px 22px' }}>{children}</div>
      {footer && <div style={{ padding: '10px 22px', borderTop: '1px solid var(--ln-border)', display: 'flex', justifyContent: 'space-between', fontSize: 10.5, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>{footer}</div>}
    </div>
  );
}

Object.assign(window, { AuthBackdrop, AuthPanel });
