/* ============================================================
   Lune Noire — Template de base (pages authentifiées)
   Composant React partagé pour toutes les nouvelles pages
   du web-portal côté joueur connecté.

   UTILISATION :
   <AuthLayout player={...} page="monpage" setPage={fn} onLogout={fn}>
     <PageSection title="Titre" subtitle="sous-titre">
       …votre contenu…
     </PageSection>
   </AuthLayout>
============================================================ */

/* ============ DONNÉES JEUX ============ */

const RACES = [
  { id: 'humains',       name: 'Humains',       faction: 'lumiere', mood: 'Polyvalence, endurance',         primary: '#1E4B7A', accent: '#C21B1B',  border: '#2B3A4F' },
  { id: 'elfes',         name: 'Elfes',          faction: 'lumiere', mood: 'Magie ancienne, élégance froide', primary: '#6E46B7', accent: '#C7B4FF',  border: '#2A2740' },
  { id: 'nains',         name: 'Nains',          faction: 'lumiere', mood: 'Forge, endurance, tradition',     primary: '#C25A1A', accent: '#E7D18A',  border: '#3A2A1F' },
  { id: 'divins',        name: 'Divins',         faction: 'lumiere', mood: 'Destin, loi cosmique',            primary: '#E7C16A', accent: '#FFFFFF',  border: '#2A3555' },
  { id: 'demons',        name: 'Démons',         faction: 'ombres',  mood: 'Chaos, corruption, pouvoir',      primary: '#B0122B', accent: '#FF3B6A',  border: '#3A0F1A' },
  { id: 'corrompus',     name: 'Corrompus',      faction: 'ombres',  mood: 'Mutation, folie, métamorphose',   primary: '#7B2CBF', accent: '#B8FF4A',  border: '#2D2345' },
  { id: 'morts_vivants', name: 'Morts-Vivants',  faction: 'ombres',  mood: 'Nécromancie, froideur',           primary: '#2E7D7B', accent: '#A6FFEF',  border: '#213434' },
  { id: 'orcs',          name: 'Orcs',           faction: 'errants', mood: 'Survie, rage, domination',        primary: '#4F8A2B', accent: '#D6E35A',  border: '#2A3A2C' },
];

const FACTIONS = {
  lumiere: {
    id: 'lumiere',
    name: 'Alliance de la Lumière',
    shortName: 'Lumière',
    desc: 'Ordre, protection, foi. Les races de lumière défendent le monde contre la corruption.',
    color: '#E8C56E',
    bg: 'rgba(232,197,110,.08)',
    border: 'rgba(232,197,110,.35)',
    emblem: '☀',
  },
  ombres: {
    id: 'ombres',
    name: 'Pacte des Ombres',
    shortName: 'Ombres',
    desc: 'Corruption, pouvoir interdit, survie à tout prix. Les ombres cherchent à consumer le monde.',
    color: '#B0122B',
    bg: 'rgba(176,18,43,.08)',
    border: 'rgba(176,18,43,.35)',
    emblem: '☽',
  },
  errants: {
    id: 'errants',
    name: 'Hors-Pacte',
    shortName: 'Errants',
    desc: 'Ni lumière ni ombre. Ces races tracent leur propre voie, redoutées et respectées des deux camps.',
    color: '#4F8A2B',
    bg: 'rgba(79,138,43,.08)',
    border: 'rgba(79,138,43,.35)',
    emblem: '⚔',
  },
};

const CLASSES = [
  { id: 'guerrier',    name: 'Guerrier',    role: 'Tank / DPS',     roles: ['humains','orcs','nains','morts_vivants','demons'] },
  { id: 'mage',        name: 'Mage',        role: 'DPS',            roles: ['humains','elfes','morts_vivants','corrompus','divins'] },
  { id: 'voleur',      name: 'Voleur',      role: 'DPS furtif',     roles: ['humains','elfes','orcs','morts_vivants','corrompus'] },
  { id: 'pretre',      name: 'Prêtre',      role: 'Healer',         roles: ['humains','elfes','nains','divins','morts_vivants'] },
  { id: 'paladin',     name: 'Paladin',     role: 'Tank / Healer',  roles: ['humains','nains','divins'] },
  { id: 'chasseur',    name: 'Chasseur',    role: 'DPS distance',   roles: ['humains','elfes','orcs','nains'] },
  { id: 'necromant',   name: 'Nécromancien',role: 'DPS / Support',  roles: ['morts_vivants','corrompus','demons'] },
  { id: 'chaman',      name: 'Chaman',      role: 'Healer / DPS',   roles: ['orcs','nains','divins','demons'] },
];

const RACIALS = {
  humains:       ['Diplomatie : +5% réputation', 'Polyvalence : +1 tous attributs', 'Endurance : régénération accrue'],
  elfes:         ['Arcane : +10% puissance sorts', 'Grâce sylvestre : +5% esquive', 'Vision nocturne'],
  orcs:          ['Furie : +15% dégâts mêlée < 30% PV', 'Peau épaisse : -5% dégâts physiques', 'Résistance : immunité courte aux CCs'],
  nains:         ['Forge ancestrale : +10% artisanat', 'Constitution de pierre : +10% PV max', 'Résistance poison : -50% durée'],
  morts_vivants: ['Volonté des Damnés : immunité peur/charme', 'Toucher mortel : +5% sorts de mort', 'Respiration cadavérique'],
  corrompus:     ['Corruption : +10% sorts corrompus', 'Absorption : 5% dégâts → soins', 'Ombre : +5% furtivité zones sombres'],
  divins:        ['Bénédiction divine : +10% soins prodigués', 'Bouclier de foi : absorption passive', 'Lumière céleste : résistance mort'],
  demons:        ['Feu infernal : +10% dégâts de feu', 'Résistance au feu : -50% dégâts feu', 'Terreur : -10% résistance peur ennemie'],
};

const SKIN_COLORS = {
  humains:       ['#C68642','#F1C27D','#FDBCB4','#8D5524'],
  elfes:         ['#F5DEB3','#E8D0AA','#C4A882','#B8946F'],
  orcs:          ['#355E3B','#556B2F','#4B5320','#228B22'],
  nains:         ['#C68642','#A0522D','#8B4513','#D2691E'],
  morts_vivants: ['#C8BFB0','#A9A9A9','#708090','#778899'],
  corrompus:     ['#4A0E4E','#6B2D6B','#8B4788','#9932CC'],
  divins:        ['#FFEFD5','#FFE4C4','#FFDAB9','#FFD700'],
  demons:        ['#8B0000','#B22222','#CC2200','#FF4500'],
};

const HAIR_COLORS = {
  humains:       ['#1A0A00','#4E1A00','#B5651D','#D2B48C','#FFD700','#808080'],
  elfes:         ['#F5F5DC','#C0C0C0','#D4AF37','#98FB98','#ADD8E6'],
  orcs:          ['#1C1C1C','#2F4F4F','#8B0000','#000000'],
  nains:         ['#8B0000','#A52A2A','#D2691E','#CD853F','#F4A460','#808080'],
  morts_vivants: ['#000000','#36013F','#2F4F4F','#1C1C1C'],
  corrompus:     ['#000000','#2F0033','#4B0082','#800080'],
  divins:        ['#FFD700','#FFFACD','#FFFFFF','#F0E68C'],
  demons:        ['#000000','#8B0000','#B8860B','#1C1C1C'],
};

/* ============ LAYOUT AUTHENTIFIÉ ============ */

const NAV_ITEMS = [
  { id: 'player',    label: 'Tableau de bord', icon: 'home' },
  { id: 'character', label: 'Personnage',       icon: 'sword' },
  { id: 'exploits',  label: 'Exploits',         icon: 'trophy' },
  { id: 'bugs',      label: 'Signaler un bug',  icon: 'bug' },
  { id: 'recovery',  label: 'Récupération',     icon: 'key' },
  { id: 'player',    label: 'CGU',              icon: 'scroll', id2: 'cgu' },
  { id: 'support',   label: 'Support',          icon: 'chat' },
];

function AuthLayout({ player, page, setPage, onLogout, children }) {
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const race = RACES.find(r => r.id === player?.race) || null;
  const faction = race ? FACTIONS[race.faction] : null;

  // Applique le thème de race sur <html>
  React.useEffect(() => {
    if (player?.race) document.documentElement.setAttribute('data-race', player.race);
    else document.documentElement.removeAttribute('data-race');
  }, [player?.race]);

  return (
    <div style={{ minHeight: '100vh', display: 'flex', flexDirection: 'column' }}>

      {/* ── TOPBAR ── */}
      <header style={{
        position: 'sticky', top: 0, zIndex: 200, height: 60,
        display: 'flex', alignItems: 'center', justifyContent: 'space-between',
        padding: '0 clamp(14px, 3vw, 28px)',
        background: 'rgba(10,13,18,.92)',
        backdropFilter: 'blur(18px)', WebkitBackdropFilter: 'blur(18px)',
        borderBottom: `1px solid ${race ? race.border : 'var(--ln-border)'}`,
        transition: 'border-color .4s',
      }}>
        {/* Logo + nom de page */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
          <button onClick={() => setSidebarOpen(o => !o)} style={{ background: 'none', border: '1px solid var(--ln-border)', color: 'var(--ln-text)', width: 32, height: 32, borderRadius: 'var(--radius-sm)', cursor: 'pointer', fontSize: 14, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>☰</button>
          <div style={{ width: 28, height: 28, borderRadius: '50%', background: 'radial-gradient(circle at 32% 30%, #2a2330 0%, #0B0712 55%, #000 100%)', border: '1px solid #1a1420', boxShadow: `0 0 16px ${race ? race.primary + '66' : 'rgba(74,123,184,.4)'}`, transition: 'box-shadow .4s', flexShrink: 0 }} />
          <span style={{ fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 'clamp(11px, 1vw, 13px)', letterSpacing: '.24em', textTransform: 'uppercase', color: 'var(--ln-text)' }}>
            Portail joueur
          </span>
        </div>

        {/* Player info */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          {faction && (
            <span style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.22em', textTransform: 'uppercase', padding: '4px 10px', borderRadius: 100, border: `1px solid ${faction.border}`, background: faction.bg, color: faction.color, display: 'flex', alignItems: 'center', gap: 6 }}>
              <span>{faction.emblem}</span>{faction.shortName}
            </span>
          )}
          {race && (
            <span style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.22em', textTransform: 'uppercase', padding: '4px 10px', borderRadius: 100, border: `1px solid ${race.border}`, background: `${race.primary}18`, color: race.accent }}>
              {race.name}
            </span>
          )}
          <div style={{ fontFamily: 'var(--font-display)', fontSize: 13, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-accent)' }}>
            {player?.name || 'Inconnu'}
          </div>
          <button onClick={onLogout} style={{ background: 'none', border: '1px solid var(--ln-border)', color: 'var(--ln-muted)', fontSize: 10, letterSpacing: '.18em', textTransform: 'uppercase', padding: '5px 12px', borderRadius: 'var(--radius-sm)', cursor: 'pointer', fontFamily: 'var(--font-ui)' }}>
            Déconnexion
          </button>
        </div>
      </header>

      <div style={{ display: 'flex', flex: 1, minHeight: 0 }}>

        {/* ── SIDEBAR ── */}
        <aside style={{
          width: sidebarOpen ? 240 : 0,
          minWidth: sidebarOpen ? 240 : 0,
          overflow: 'hidden',
          background: 'rgba(12,16,22,.9)',
          borderRight: `1px solid ${race ? race.border : 'var(--ln-border)'}`,
          transition: 'all .25s cubic-bezier(.2,.6,.2,1)',
          display: 'flex', flexDirection: 'column',
          flexShrink: 0,
        }}>
          {/* Player card */}
          {sidebarOpen && (
            <div style={{ padding: '20px 18px', borderBottom: `1px solid ${race ? race.border : 'var(--ln-border)'}`, flexShrink: 0 }}>
              <div style={{ width: 52, height: 52, borderRadius: 'var(--radius-sm)', background: `radial-gradient(circle, ${race ? race.primary : '#4A7BB8'}, #0A0D12)`, border: `1px solid ${race ? race.border : 'var(--ln-border)'}`, marginBottom: 10 }} />
              <div style={{ fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 14, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-accent)' }}>{player?.name || 'Aventurier'}</div>
              <div style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)', marginTop: 3, letterSpacing: '.06em' }}>{player?.tagId || 'TAG-ID inconnu'}</div>
              {race && <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 11, color: race.accent, marginTop: 6 }}>{race.mood}</div>}
            </div>
          )}

          {/* Nav links */}
          {sidebarOpen && (
            <nav style={{ flex: 1, padding: '10px 8px', overflowY: 'auto' }}>
              {[
                { id: 'player',    label: 'Tableau de bord' },
                { id: 'character', label: 'Personnage' },
                { id: 'exploits',  label: 'Exploits' },
                { id: 'bugs',      label: 'Signaler un bug' },
                { id: 'recovery',  label: 'Récupération' },
                { id: 'support',   label: 'Support' },
              ].map(l => (
                <div key={l.id + l.label} onClick={() => setPage(l.id)}
                  style={{
                    padding: '10px 14px', borderRadius: 'var(--radius-sm)', cursor: 'pointer',
                    fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '.18em', textTransform: 'uppercase',
                    color: page === l.id ? (race ? race.accent : 'var(--ln-accent)') : 'var(--ln-muted)',
                    background: page === l.id ? `${race ? race.primary : '#4A7BB8'}18` : 'transparent',
                    border: `1px solid ${page === l.id ? (race ? race.border : 'rgba(74,123,184,.3)') : 'transparent'}`,
                    marginBottom: 2, transition: 'all .18s',
                  }}>
                  {l.label}
                </div>
              ))}
            </nav>
          )}

          {/* Version */}
          {sidebarOpen && (
            <div style={{ padding: '12px 18px', borderTop: `1px solid ${race ? race.border : 'var(--ln-border)'}` }}>
              <div style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)', letterSpacing: '.1em' }}>v0.8.4 · Édition des morts</div>
            </div>
          )}
        </aside>

        {/* ── CONTENU PRINCIPAL ── */}
        <main style={{ flex: 1, minWidth: 0, overflowY: 'auto', padding: 'clamp(24px, 3.5vh, 48px) clamp(20px, 4vw, 48px) 80px' }}>
          {children}
        </main>
      </div>

      {/* ── FOOTER ── */}
      <footer style={{ borderTop: `1px solid rgba(61,79,102,.3)`, padding: '16px clamp(16px,4vw,48px)', display: 'flex', justifyContent: 'space-between', alignItems: 'center', flexWrap: 'wrap', gap: 12 }}>
        <span style={{ fontFamily: 'var(--font-display)', fontSize: 10, letterSpacing: '.24em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>© 2026 · Les Chroniques de la Lune Noire</span>
        <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)' }}>v0.8.4</span>
      </footer>
    </div>
  );
}

/* ============ COMPOSANTS DE PAGE ============ */

function PageHeader({ title, subtitle, breadcrumb }) {
  return (
    <div style={{ marginBottom: 'clamp(20px, 3vh, 36px)', paddingBottom: 'clamp(16px, 2.5vh, 28px)', borderBottom: '1px solid rgba(61,79,102,.35)' }}>
      {breadcrumb && (
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 10 }}>
          {breadcrumb.map((b, i) => (
            <React.Fragment key={b}>
              <span style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.2em', textTransform: 'uppercase', color: i === breadcrumb.length - 1 ? 'var(--ln-accent)' : 'var(--ln-muted)' }}>{b}</span>
              {i < breadcrumb.length - 1 && <span style={{ color: 'var(--ln-border)', fontSize: 10 }}>›</span>}
            </React.Fragment>
          ))}
        </div>
      )}
      <h1 style={{ fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 'clamp(20px, 2.5vw, 32px)', letterSpacing: '.18em', textTransform: 'uppercase', color: 'var(--ln-text)', margin: '0 0 8px' }}>{title}</h1>
      {subtitle && <p style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 'clamp(13px, 1.1vw, 16px)', color: 'var(--ln-muted)', margin: 0, lineHeight: 1.6 }}>{subtitle}</p>}
    </div>
  );
}

function SectionTitle({ children, sub }) {
  return (
    <div style={{ marginBottom: 16 }}>
      <div style={{ fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 14, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-accent)', paddingBottom: 6, borderBottom: '1px solid rgba(61,79,102,.4)' }}>{children}</div>
      {sub && <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 13, color: 'var(--ln-muted)', marginTop: 6 }}>{sub}</div>}
    </div>
  );
}

function Card({ children, style, interactive, onClick }) {
  return (
    <div className={`wp-card${interactive ? ' interactive' : ''}`} onClick={onClick}
      style={{ padding: 'clamp(16px, 2vw, 24px)', ...style }}>
      {children}
    </div>
  );
}

function StatRow({ label, value, color }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '10px 0', borderBottom: '1px solid rgba(61,79,102,.2)' }}>
      <span style={{ fontFamily: 'var(--font-ui)', fontSize: 11, letterSpacing: '.18em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>{label}</span>
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 13, color: color || 'var(--ln-accent)' }}>{value}</span>
    </div>
  );
}

Object.assign(window, { RACES, FACTIONS, CLASSES, RACIALS, SKIN_COLORS, HAIR_COLORS, AuthLayout, PageHeader, SectionTitle, Card, StatRow });
