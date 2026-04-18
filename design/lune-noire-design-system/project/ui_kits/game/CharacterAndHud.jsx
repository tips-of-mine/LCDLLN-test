const { useState: useStateCC } = React;

const RACES = [
  { id: 'humain',    name: 'Humain',      desc: "Polyvalent, mortel, ambitieux. Porte l'ombre sans s'y perdre." },
  { id: 'elfe',      name: 'Elfe',        desc: "Enfant des forêts de lune. Agile, farouche, à demi-sauvage." },
  { id: 'nain',      name: 'Nain',        desc: "Forgé dans la pierre. Endurant, rancunier, fidèle jusqu'à la mort." },
  { id: 'orc',       name: 'Orc',         desc: "Dur comme le basalte. Peu de mots, beaucoup de sang." },
  { id: 'mortvivant',name: 'Mort-Vivant', desc: "Revenu des ténèbres. Résiste à la mort elle-même." },
  { id: 'demon',     name: 'Démon',       desc: "Chair infernale. Brûle ceux qui l'approchent sans protection." },
  { id: 'divin',     name: 'Divin',       desc: "Éclat du ciel. Lumière, verdict, distance." },
  { id: 'sauvage',   name: 'Sauvage',     desc: "Crocs, griffes, nuit. N'obéit qu'à la lune." },
];

const CLASSES = [
  { id: 'guerrier',   name: 'Guerrier',   desc: "Acier et discipline. Se tient au front." },
  { id: 'voleur',     name: 'Voleur',     desc: "Dague et poison. Frappe dans le dos." },
  { id: 'mage',       name: 'Mage',       desc: "Mémorise l'étincelle. Brûle les rangs." },
  { id: 'pretre',     name: 'Prêtre',     desc: "Tient la mort à distance par la prière." },
  { id: 'necromant',  name: 'Nécromant',  desc: "Parle aux os. Commande aux tombes." },
  { id: 'ranger',     name: 'Rôdeur',     desc: "Arc et silence. Chasse à la lisière." },
];

function CharacterCreateScreen({ onEnter }) {
  const [race, setRace] = useStateCC('humain');
  const [klass, setKlass] = useStateCC('guerrier');
  const [name, setName] = useStateCC('Morwenna');
  const [gender, setGender] = useStateCC('F');

  React.useEffect(() => { document.documentElement.setAttribute('data-race', race); }, [race]);

  return (
    <AuthBackdrop>
      <div style={{ position: 'absolute', inset: 0, display: 'grid', gridTemplateColumns: '280px 1fr 320px', gap: 24, padding: 28 }}>
        {/* Races */}
        <AuthPanel title="Race" versionLabel={`${RACES.length} lignées`}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, maxHeight: 520, overflow: 'auto' }}>
            {RACES.map(r => (
              <div key={r.id} onClick={() => setRace(r.id)} style={{
                padding: '10px 12px', borderRadius: 'var(--radius-sm)', cursor: 'pointer',
                border: `1px solid ${race === r.id ? 'var(--ln-primary)' : 'transparent'}`,
                background: race === r.id ? 'rgba(74,123,184,.12)' : 'transparent',
              }}>
                <div style={{ fontFamily: 'var(--font-display)', fontWeight: 600, fontSize: 13.5, letterSpacing: '.14em', textTransform: 'uppercase', color: race === r.id ? 'var(--ln-accent)' : 'var(--ln-text)' }}>{r.name}</div>
                {race === r.id && <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 11.5, color: 'var(--ln-muted)', marginTop: 3, lineHeight: 1.45 }}>{r.desc}</div>}
              </div>
            ))}
          </div>
        </AuthPanel>

        {/* Portrait */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16, alignItems: 'center', justifyContent: 'center' }}>
          <div style={{
            width: 340, height: 420, borderRadius: 'var(--radius-lg)',
            background: `radial-gradient(ellipse 70% 80% at 50% 40%, var(--ln-primary), transparent 70%),
                         linear-gradient(180deg, rgba(20,28,40,.4), rgba(5,7,10,.9))`,
            border: '1px solid var(--ln-border)',
            boxShadow: 'inset 0 0 80px rgba(0,0,0,.7), 0 20px 60px rgba(0,0,0,.6)',
            position: 'relative', overflow: 'hidden',
          }}>
            {/* silhouette */}
            <svg viewBox="0 0 100 120" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', opacity: 0.55 }}>
              <ellipse cx="50" cy="38" rx="14" ry="18" fill="#000" />
              <path d="M20,120 Q20,70 50,62 Q80,70 80,120 Z" fill="#000" />
            </svg>
            <div style={{ position: 'absolute', top: 12, left: 14, fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)', letterSpacing: '.15em' }}>PORTRAIT · {race.toUpperCase()} · {gender}</div>
            <div style={{ position: 'absolute', bottom: 14, right: 14, display: 'flex', gap: 6 }}>
              {['F', 'M'].map(g => (
                <button key={g} onClick={() => setGender(g)} style={{
                  width: 28, height: 28, borderRadius: 'var(--radius-sm)',
                  background: gender === g ? 'var(--ln-primary)' : 'rgba(10,13,18,.6)',
                  border: '1px solid var(--ln-border)', color: 'var(--ln-text)',
                  fontFamily: 'var(--font-ui)', fontSize: 11, cursor: 'pointer',
                }}>{g}</button>
              ))}
            </div>
          </div>
          <div style={{ width: 340 }}>
            <Field label="Nom d'aventurier" value={name} onChange={setName} tooltip="Sera visible par tous les vivants." />
          </div>
        </div>

        {/* Classes */}
        <AuthPanel title="Classe" versionLabel={`${CLASSES.length} voies`}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginBottom: 16 }}>
            {CLASSES.map(c => (
              <div key={c.id} onClick={() => setKlass(c.id)} style={{
                padding: '10px 12px', borderRadius: 'var(--radius-sm)', cursor: 'pointer',
                border: `1px solid ${klass === c.id ? 'var(--ln-primary)' : 'transparent'}`,
                background: klass === c.id ? 'rgba(74,123,184,.12)' : 'transparent',
              }}>
                <div style={{ fontFamily: 'var(--font-display)', fontWeight: 600, fontSize: 13.5, letterSpacing: '.14em', textTransform: 'uppercase', color: klass === c.id ? 'var(--ln-accent)' : 'var(--ln-text)' }}>{c.name}</div>
                {klass === c.id && <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 11.5, color: 'var(--ln-muted)', marginTop: 3, lineHeight: 1.45 }}>{c.desc}</div>}
              </div>
            ))}
          </div>
          <Button kind="primary" keycap="↵" style={{ width: '100%', justifyContent: 'center' }} onClick={onEnter}>Entrer en lice</Button>
        </AuthPanel>
      </div>
    </AuthBackdrop>
  );
}

function HudOverlay({ onExit }) {
  return (
    <div style={{ position: 'absolute', inset: 0, background: 'radial-gradient(ellipse 80% 60% at 50% 60%, #1a2a1f 0%, #0a0d12 100%)' }}>
      {/* faux world */}
      <svg style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', opacity: 0.3 }} viewBox="0 0 1920 1080" preserveAspectRatio="xMidYMid slice">
        {Array.from({ length: 40 }).map((_, i) => (
          <path key={i} d={`M${i * 50},${600 + Math.sin(i) * 40} Q${i * 50 + 25},${560 + Math.cos(i) * 40} ${i * 50 + 50},${600 + Math.sin(i + 1) * 40}`} stroke="#2a3f2a" strokeWidth="1" fill="none" />
        ))}
      </svg>

      {/* Top-left portrait */}
      <div style={{ position: 'absolute', top: 18, left: 18, display: 'flex', gap: 10, alignItems: 'center', background: 'rgba(10,13,18,.75)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)', padding: 10 }}>
        <div style={{ width: 52, height: 52, borderRadius: 'var(--radius-sm)', background: 'radial-gradient(ellipse 70% 80% at 50% 40%, var(--ln-primary), #0a0d12)', border: '1px solid var(--ln-border)' }} />
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 180 }}>
          <div style={{ fontFamily: 'var(--font-display)', fontWeight: 600, fontSize: 12.5, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-accent)' }}>Morwenna · Niv. 12</div>
          <Bar color="#8B1E2D" pct={0.74} label="PV 740/1000" />
          <Bar color="#3E689E" pct={0.52} label="MN 260/500" />
          <Bar color="#E8A55C" pct={0.31} label="XP" />
        </div>
      </div>

      {/* Top-right minimap */}
      <div style={{ position: 'absolute', top: 18, right: 18, width: 180, background: 'rgba(10,13,18,.75)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)', padding: 10 }}>
        <div style={{ fontFamily: 'var(--font-ui)', fontSize: 9.5, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginBottom: 6 }}>Cendrebois · sud</div>
        <div style={{ width: '100%', aspectRatio: '1', background: 'radial-gradient(circle at 50% 50%, #1a2a1f, #05070A)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-sm)', position: 'relative' }}>
          <div style={{ position: 'absolute', top: '50%', left: '50%', width: 6, height: 6, background: 'var(--ln-accent)', borderRadius: '50%', transform: 'translate(-50%,-50%)', boxShadow: '0 0 8px var(--ln-accent)' }} />
          {[[30,40],[70,28],[62,72],[22,66]].map(([x,y],i) => (<div key={i} style={{ position: 'absolute', left: `${x}%`, top: `${y}%`, width: 4, height: 4, background: '#C44040', borderRadius: '50%' }} />))}
        </div>
      </div>

      {/* Bottom action bar */}
      <div style={{ position: 'absolute', bottom: 18, left: '50%', transform: 'translateX(-50%)', display: 'flex', gap: 6, background: 'rgba(10,13,18,.8)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)', padding: 8 }}>
        {['1','2','3','4','5','6','7','8','9','0'].map(k => (
          <div key={k} style={{ width: 46, height: 46, borderRadius: 'var(--radius-sm)', border: '1px solid var(--ln-border)', background: 'rgba(20,28,40,.6)', display: 'flex', alignItems: 'flex-end', justifyContent: 'flex-start', padding: 4, fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted)', position: 'relative' }}>
            {k}
            <div style={{ position: 'absolute', inset: 6, borderRadius: 2, background: k === '1' ? 'radial-gradient(circle, #8B1E2D, #2a0a10)' : k === '2' ? 'radial-gradient(circle, #3E689E, #0a1520)' : 'transparent', opacity: .8 }} />
          </div>
        ))}
      </div>

      {/* Chat */}
      <div style={{ position: 'absolute', bottom: 18, left: 18, width: 360, background: 'rgba(10,13,18,.75)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)', padding: 10, fontFamily: 'var(--font-body)', fontSize: 12 }}>
        <div style={{ display: 'flex', gap: 10, fontSize: 9.5, letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginBottom: 6 }}>
          <span style={{ color: 'var(--ln-accent)' }}>Général</span><span>Commerce</span><span>Guilde</span><span>MP</span>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 3, color: 'var(--ln-text)' }}>
          <div><span style={{ color: '#8ab4e5' }}>[Aldric]</span> <i style={{ color: 'var(--ln-muted)' }}>Les portes de Korvath sont tombées.</i></div>
          <div><span style={{ color: '#E8A55C' }}>[Système]</span> <i style={{ color: 'var(--ln-muted)' }}>La nuit s'épaissit. Prudence aux voyageurs.</i></div>
          <div><span style={{ color: '#C4A0E8' }}>[Morwenna]</span> <i style={{ color: 'var(--ln-muted)' }}>*dégaine sa lame*</i></div>
        </div>
      </div>

      <button onClick={onExit} style={{ position: 'absolute', top: 18, left: '50%', transform: 'translateX(-50%)', background: 'rgba(10,13,18,.7)', border: '1px solid var(--ln-border)', color: 'var(--ln-muted)', fontSize: 10, letterSpacing: '.2em', textTransform: 'uppercase', padding: '6px 12px', borderRadius: 'var(--radius-sm)', cursor: 'pointer' }}>Quitter le monde</button>
    </div>
  );
}

function Bar({ color, pct, label }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 9, letterSpacing: '.15em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>
        <span>{label}</span>
      </div>
      <div style={{ height: 5, background: 'rgba(255,255,255,.05)', borderRadius: 2, overflow: 'hidden', border: '1px solid rgba(0,0,0,.5)' }}>
        <div style={{ width: `${pct * 100}%`, height: '100%', background: color, boxShadow: `inset 0 1px 0 rgba(255,255,255,.2)` }} />
      </div>
    </div>
  );
}

Object.assign(window, { CharacterCreateScreen, HudOverlay });
