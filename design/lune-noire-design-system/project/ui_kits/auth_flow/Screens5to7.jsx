/* Lune Noire — Écrans 5 à 7 : Confirmation, Options, Choix du serveur. */

const { useState: useS2 } = React;

// ============ ÉCRAN 5 — CONFIRMATION EMAIL ============
function ConfirmEmailScreen({ onBack, onDone }) {
  const [code, setCode] = useS2(['', '', '', '', '', '']);
  const [err, setErr] = useS2(false);
  const setDigit = (i, v) => {
    if (v.length > 1) v = v.slice(-1);
    if (!/^[0-9]?$/.test(v)) return;
    const next = [...code]; next[i] = v; setCode(next);
    if (v && i < 5) { const el = document.getElementById(`cd-${i+1}`); el && el.focus(); }
    setErr(false);
  };
  const full = code.join('');
  const breadcrumb = [
    { key: 'lang',   label: 'Langue' },
    { key: 'auth',   label: 'Compte' },
    { key: 'verify', label: 'Courriel' },
    { key: 'world',  label: 'Monde' },
  ];

  return (
    <div className="ln-stage">
      <div className="ln-stage-col" style={{ width: 'min(560px, 96vw)' }}>
        <Breadcrumb steps={breadcrumb} current={2} />
        <Panel
          title="Vérifiez votre courriel"
          subtitle="Nous avons envoyé un code à 6 chiffres à morwenna@exemple.fr."
          versionLabel="3 / 4"
          infoText="Le code expire dans 15 minutes. Vérifiez aussi vos courriers indésirables."
        >
          {err && <Banner kind="error" title="Code incorrect">Ce code ne correspond pas. Il est peut-être expiré.</Banner>}

          <div>
            <div style={{ fontFamily: 'var(--font-ui)', fontSize: 10.5, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginBottom: 8 }}>Code de vérification</div>
            <div style={{ display: 'flex', gap: 8, justifyContent: 'center' }}>
              {code.map((c, i) => (
                <input key={i} id={`cd-${i}`}
                  value={c} onChange={e => setDigit(i, e.target.value)}
                  maxLength={1} inputMode="numeric"
                  style={{
                    width: 'clamp(40px, 6vw, 56px)', height: 'clamp(48px, 7vw, 64px)',
                    textAlign: 'center',
                    fontFamily: 'var(--font-mono)', fontSize: 'clamp(20px, 2.2vw, 28px)',
                    background: 'rgba(10,13,18,.6)', color: 'var(--ln-text)',
                    border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)',
                    outline: 0, caretColor: 'var(--ln-accent)',
                  }}
                  onFocus={e => e.target.style.borderColor = 'var(--ln-primary)'}
                  onBlur={e => e.target.style.borderColor = 'var(--ln-border)'}
                />
              ))}
            </div>
          </div>

          <div style={{ display: 'flex', justifyContent: 'center', gap: 20, marginTop: 4 }}>
            <Button kind="text" size="sm">Renvoyer le code</Button>
            <Button kind="text" size="sm">Modifier le courriel</Button>
          </div>

          <div className="ln-actions">
            <Button kind="ghost" size="md" onClick={onBack} keycap="Échap">Retour</Button>
            <Button kind="primary" size="md" keycap="↵" disabled={full.length < 6}
              onClick={() => { if (full === '000000') setErr(true); else onDone && onDone(); }}>
              Valider le code
            </Button>
          </div>
        </Panel>
        <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 12, color: 'var(--ln-muted)', textAlign: 'center', maxWidth: 440 }}>
          Astuce : utilisez « 123456 » pour simuler la validation, « 000000 » pour simuler une erreur.
        </div>
      </div>
    </div>
  );
}

// ============ ÉCRAN 6 — OPTIONS ============
function OptionsScreen({ onBack }) {
  const TABS = [
    { id: 'graphics', label: 'Graphismes', icon: '▣' },
    { id: 'audio',    label: 'Son',        icon: '♫' },
    { id: 'controls', label: 'Contrôles',  icon: '⌨' },
    { id: 'lang',     label: 'Langue',     icon: 'Aa' },
    { id: 'ui',       label: 'Interface',  icon: '⌗' },
    { id: 'net',      label: 'Réseau',     icon: '⌘' },
    { id: 'account',  label: 'Compte',     icon: '✦' },
  ];
  const [tab, setTab] = useS2('graphics');
  const [g, setG] = useS2({ res: '1920x1080', quality: 'Haute', fullscreen: true, vsync: true, fov: 70 });
  const [a, setA] = useS2({ master: 80, music: 60, sfx: 75, voice: 90 });
  const [ui, setUi] = useS2({ scale: 100, opacity: 90, tips: true });
  const [dirty, setDirty] = useS2(false);

  const touch = (fn) => (v) => { setDirty(true); fn(v); };

  return (
    <div className="ln-stage">
      <div className="ln-options">
        {/* Sidebar */}
        <div className="ln-options-sidebar">
          <div className="ln-options-sidebar-title">Options</div>
          {TABS.map(t => (
            <div key={t.id} className={`ln-options-tab ${tab === t.id ? 'active' : ''}`} onClick={() => setTab(t.id)}>
              <span className="ln-options-tab-icon">{t.icon}</span>
              <span>{t.label}</span>
            </div>
          ))}
          <div style={{ flex: 1 }} />
          <div style={{ padding: '10px 14px', fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 11, color: 'var(--ln-muted)', lineHeight: 1.5 }}>
            Les changements prennent effet après « Appliquer ».
          </div>
        </div>

        {/* Panneau principal */}
        <div className="ln-options-main">
          <div className="ln-options-main-header">
            <div>
              <div style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>Catégorie</div>
              <h2 style={{ margin: '4px 0 0', fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 'clamp(18px, 1.6vw, 22px)', letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-text)' }}>
                {TABS.find(t => t.id === tab).label}
              </h2>
            </div>
            {dirty && <Banner kind="warning">Modifications non enregistrées</Banner>}
          </div>

          <div className="ln-options-main-body">
            {tab === 'graphics' && <>
              <div className="ln-options-section-title">Affichage</div>
              <Dropdown label="Résolution" value={g.res} onChange={touch(v => setG(s => ({ ...s, res: v })))}
                options={['1280x720','1600x900','1920x1080','2560x1440','3840x2160'].map(r => ({ value: r, label: r }))} />
              <Dropdown label="Qualité graphique" value={g.quality} onChange={touch(v => setG(s => ({ ...s, quality: v })))}
                options={['Basse','Moyenne','Haute','Ultra'].map(q => ({ value: q, label: q }))} tooltip="Ultra requiert une carte graphique récente." />
              <Slider label="Champ de vision" min={60} max={120} value={g.fov} onChange={touch(v => setG(s => ({ ...s, fov: v })))} unit="°" />
              <div className="ln-options-section-title">Modes</div>
              <Toggle label="Plein écran" checked={g.fullscreen} onChange={touch(v => setG(s => ({ ...s, fullscreen: v })))} hint="Alt+Entrée pour basculer rapidement." />
              <Toggle label="Synchronisation verticale" checked={g.vsync} onChange={touch(v => setG(s => ({ ...s, vsync: v })))} hint="Réduit le déchirement, limite à 60 i/s." />
            </>}

            {tab === 'audio' && <>
              <div className="ln-options-section-title">Volumes</div>
              <Slider label="Volume maître" min={0} max={100} value={a.master} onChange={touch(v => setA(s => ({ ...s, master: v })))} unit="%" />
              <Slider label="Musique" min={0} max={100} value={a.music} onChange={touch(v => setA(s => ({ ...s, music: v })))} unit="%" />
              <Slider label="Effets" min={0} max={100} value={a.sfx} onChange={touch(v => setA(s => ({ ...s, sfx: v })))} unit="%" />
              <Slider label="Voix" min={0} max={100} value={a.voice} onChange={touch(v => setA(s => ({ ...s, voice: v })))} unit="%" />
            </>}

            {tab === 'controls' && <>
              <div className="ln-options-section-title">Souris</div>
              <Slider label="Sensibilité" min={10} max={100} value={40} onChange={touch(() => {})} unit="%" />
              <Toggle label="Inverser l'axe Y" checked={false} onChange={touch(() => {})} />
              <Toggle label="Disposition ZQSD (AZERTY)" checked={true} onChange={touch(() => {})} />
              <div className="ln-options-section-title">Raccourcis clavier</div>
              {[['Avancer','Z'],['Reculer','S'],['Gauche','Q'],['Droite','D'],['Interagir','E'],['Sort 1','1'],['Sort 2','2'],['Inventaire','I'],['Carte','M']].map(([n,k]) => (
                <div className="ln-keybind" key={n}>
                  <span className="ln-keybind-name">{n}</span>
                  <span className="ln-keybind-key">{k}</span>
                </div>
              ))}
            </>}

            {tab === 'lang' && <>
              <div className="ln-options-section-title">Langue d'affichage</div>
              <Dropdown label="Interface et textes" value="fr" onChange={touch(() => {})}
                options={[{value:'fr',label:'Français'},{value:'en',label:'English'}]} tooltip="Redémarrage requis après modification." />
            </>}

            {tab === 'ui' && <>
              <div className="ln-options-section-title">Affichage de l'interface</div>
              <Slider label="Taille de l'interface" min={80} max={140} value={ui.scale} onChange={touch(v => setUi(s => ({ ...s, scale: v })))} unit="%" />
              <Slider label="Opacité des panneaux" min={40} max={100} value={ui.opacity} onChange={touch(v => setUi(s => ({ ...s, opacity: v })))} unit="%" />
              <Toggle label="Afficher les infobulles" checked={ui.tips} onChange={touch(v => setUi(s => ({ ...s, tips: v })))} hint="Survolez un élément pour voir son explication." />
            </>}

            {tab === 'net' && <>
              <div className="ln-options-section-title">Connexion</div>
              <Dropdown label="Serveur préféré" value="eu" onChange={touch(() => {})}
                options={[{value:'eu',label:'Europe (Morneplaine)'},{value:'eu2',label:'Europe (Korvath)'},{value:'auto',label:'Automatique (latence la plus basse)'}]} />
              <div style={{ display: 'flex', justifyContent: 'space-between', padding: '10px 0', borderBottom: '1px solid rgba(61,79,102,.25)' }}>
                <span style={{ fontFamily: 'var(--font-ui)', fontSize: 12, letterSpacing: '.18em', textTransform: 'uppercase', color: 'var(--ln-text)' }}>Latence actuelle</span>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: 13, color: 'var(--ln-success)' }}>34 ms</span>
              </div>
              <Toggle label="Mode gameplay UDP" checked={false} onChange={touch(() => {})} hint="Protocole expérimental à faible latence." />
            </>}

            {tab === 'account' && <>
              <div className="ln-options-section-title">Compte connecté</div>
              <div style={{ padding: '14px 16px', background: 'rgba(10,13,18,.5)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <div>
                  <div style={{ fontFamily: 'var(--font-display)', fontSize: 14, fontWeight: 600, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-accent)' }}>morwenna</div>
                  <div style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: 'var(--ln-muted)', marginTop: 2 }}>TAG-ID : MRWN-4F2A-81C7</div>
                </div>
                <Button kind="ghost" size="sm">Copier</Button>
              </div>
              <div className="ln-options-section-title">Actions</div>
              <Button kind="ghost">Changer le mot de passe</Button>
              <Button kind="ghost">Changer le courriel</Button>
              <Button kind="danger">Se déconnecter</Button>
            </>}
          </div>

          <div className="ln-options-footer">
            <Button kind="ghost" size="md" onClick={onBack} keycap="Échap">Retour</Button>
            <div style={{ display: 'flex', gap: 10 }}>
              <Button kind="text" size="sm" onClick={() => setDirty(false)} disabled={!dirty}>Annuler</Button>
              <Button kind="primary" size="md" disabled={!dirty} onClick={() => setDirty(false)}>Appliquer</Button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

// ============ ÉCRAN 7 — CHOIX DU SERVEUR ============
function ShardPickScreen({ onBack, onEnter }) {
  const shards = [
    { id: 'MORNE', name: 'Morneplaine', desc: 'Terres brumeuses, PvE coopératif.', players: 1842, cap: 3000, status: 'ok',   ping: 28, event: 'Chasse de la lune noire' },
    { id: 'KORVA', name: 'Korvath',     desc: 'Forteresse orc, PvP ouvert.',      players: 2734, cap: 3000, status: 'warn', ping: 42, event: null },
    { id: 'CENDR', name: 'Cendrebois',  desc: 'Forêt maudite, RP semi-hardcore.', players: 612,  cap: 2000, status: 'ok',   ping: 35, event: 'Festival des âmes' },
    { id: 'SOUOM', name: 'Sous-Ombre',  desc: 'Maintenance en cours — retour à 21h.', players: 0, cap: 2000, status: 'err',   ping: null, event: null },
  ];
  const [selected, setSelected] = useS2('MORNE');
  const breadcrumb = [
    { key: 'auth',   label: 'Compte' },
    { key: 'shard',  label: 'Royaume' },
    { key: 'char',   label: 'Personnage' },
    { key: 'world',  label: 'Entrée' },
  ];
  const pingClass = (p) => p == null ? 'bad' : p < 40 ? 'ok' : p < 80 ? 'med' : 'bad';

  return (
    <div className="ln-stage" style={{ padding: 'clamp(12px, 2vh, 24px)' }}>
      <div className="ln-stage-col" style={{ width: 'min(820px, 96vw)' }}>
        <Breadcrumb steps={breadcrumb} current={1} />
        <Panel
          title="Choisissez votre royaume"
          subtitle="Chaque monde possède sa population, ses règles et ses événements."
          versionLabel={`${shards.filter(s => s.status !== 'err').length} / ${shards.length} en ligne`}
          infoText="Vous pourrez changer de royaume plus tard via le portail de voyage inter-mondes (frais en or)."
        >
          <div className="ln-shard-list">
            {shards.map(s => (
              <div key={s.id} onClick={() => s.status !== 'err' && setSelected(s.id)}
                className={`ln-shard-row ${selected === s.id ? 'selected' : ''} ${s.status === 'err' ? 'disabled' : ''}`}>
                <div className="ln-shard-flag">{s.id.slice(0,1)}</div>
                <div style={{ minWidth: 0 }}>
                  <div className="ln-shard-name">{s.name}</div>
                  <div className="ln-shard-desc">{s.desc}</div>
                  {s.event && <div className="ln-shard-event">{s.event}</div>}
                </div>
                <div className="ln-shard-load">
                  <div className="ln-shard-load-label">Charge · {Math.round(s.players / s.cap * 100)}%</div>
                  <div className="ln-shard-load-bar">
                    <div style={{ width: `${Math.min(100, s.players / s.cap * 100)}%`, background: s.players / s.cap > 0.85 ? 'var(--ln-warning)' : 'var(--ln-success)' }} />
                  </div>
                  <div className="ln-shard-players">{s.players.toLocaleString('fr-FR')} / {s.cap.toLocaleString('fr-FR')}</div>
                </div>
                <div className={`ln-shard-ping ${pingClass(s.ping)}`}>
                  {s.ping != null ? `${s.ping} ms` : '—'}
                </div>
                <div className={`ln-shard-status ${s.status}`}>
                  {s.status === 'ok' ? 'En ligne' : s.status === 'warn' ? 'Saturé' : 'Hors ligne'}
                </div>
              </div>
            ))}
          </div>
          <div className="ln-actions">
            <Button kind="ghost" size="md" onClick={onBack} keycap="Échap">Retour</Button>
            <div className="ln-actions-right">
              <KeycapHint keyLabel="↑↓">naviguer</KeycapHint>
              <Button kind="primary" size="md" keycap="↵" onClick={onEnter}
                disabled={!selected || shards.find(s => s.id === selected)?.status === 'err'}>
                Entrer dans le monde
              </Button>
            </div>
          </div>
        </Panel>
      </div>
    </div>
  );
}

Object.assign(window, { ConfirmEmailScreen, OptionsScreen, ShardPickScreen });
