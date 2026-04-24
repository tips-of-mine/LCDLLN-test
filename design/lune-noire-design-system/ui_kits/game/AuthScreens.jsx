const { useState } = React;

function LoginScreen({ onRegister, onShard }) {
  const [id, setId] = useState('aldric_le_gris');
  const [pw, setPw] = useState('••••••••••');
  const [err, setErr] = useState(false);
  return (
    <AuthBackdrop>
      <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', flexDirection: 'column', gap: 28 }}>
        <div style={{ textAlign: 'center' }}>
          <div style={{ fontFamily: 'var(--font-display-broken)', fontSize: 54, color: '#F2F4F8', lineHeight: 1 }}>Les Chroniques</div>
          <div style={{ fontFamily: 'var(--font-display)', fontWeight: 700, fontSize: 18, letterSpacing: '.38em', textTransform: 'uppercase', color: 'var(--ln-accent)', marginTop: 6 }}>de la Lune Noire</div>
        </div>
        <AuthPanel title="Connexion" versionLabel="v0.8.4 · Édition des morts">
          <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
            {err && <Banner kind="error">Identifiant ou mot de passe incorrect.</Banner>}
            <Field label="Identifiant" value={id} onChange={setId} tooltip="Votre nom d'aventurier." />
            <Field label="Mot de passe" type="password" value={pw} onChange={setPw} />
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 4 }}>
              <Button kind="ghost" onClick={onRegister} keycap="Ctrl+R">Créer un compte</Button>
              <Button kind="primary" onClick={() => { setErr(true); setTimeout(onShard, 900); }} keycap="↵">Entrer</Button>
            </div>
          </div>
        </AuthPanel>
        <div style={{ display: 'flex', gap: 28 }}>
          <KeycapHint keyLabel="F1">Aide</KeycapHint>
          <KeycapHint keyLabel="F11">Plein écran</KeycapHint>
          <KeycapHint keyLabel="Esc">Quitter</KeycapHint>
        </div>
      </div>
    </AuthBackdrop>
  );
}

function RegisterScreen({ onBack }) {
  const [form, setForm] = useState({ id: 'morwenna', email: 'm@exemple.fr', pw: '', pw2: '', code: '' });
  const set = (k) => (v) => setForm({ ...form, [k]: v });
  return (
    <AuthBackdrop>
      <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        <AuthPanel title="Créer un compte" versionLabel="v0.8.4" width={560}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 14 }}>
            <div style={{ gridColumn: '1 / -1' }}>
              <Field label="Identifiant" value={form.id} onChange={set('id')} tooltip="3-16 caractères, lettres et chiffres." status="ok" />
            </div>
            <div style={{ gridColumn: '1 / -1' }}>
              <Field label="Courriel" value={form.email} onChange={set('email')} tooltip="Pour récupérer votre mot de passe." />
            </div>
            <Field label="Mot de passe" type="password" value={form.pw} onChange={set('pw')} placeholder="8 caractères minimum" />
            <Field label="Confirmation" type="password" value={form.pw2} onChange={set('pw2')} status={form.pw2 && form.pw !== form.pw2 ? 'err' : undefined} />
            <div style={{ gridColumn: '1 / -1' }}>
              <Field label="Code d'accès" value={form.code} onChange={set('code')} tooltip="Édition des morts — clé d'invitation à 12 caractères." />
            </div>
            <div style={{ gridColumn: '1 / -1', display: 'flex', justifyContent: 'space-between', marginTop: 6 }}>
              <Button kind="ghost" onClick={onBack} keycap="Esc">Retour</Button>
              <Button kind="primary" keycap="↵">Ouvrir les portes</Button>
            </div>
          </div>
        </AuthPanel>
      </div>
    </AuthBackdrop>
  );
}

function ShardPickScreen({ onPick }) {
  const shards = [
    { id: 'MORNEPLAINE', name: 'Morneplaine',  endpoint: 'eu-01.lcdlln.fr:7421', load: 0.42, status: 'ok',   desc: 'Brumes perpétuelles — PvE.' },
    { id: 'KORVATH',     name: 'Korvath',      endpoint: 'eu-02.lcdlln.fr:7421', load: 0.88, status: 'warn', desc: 'Forteresse orc — PvP ouvert.' },
    { id: 'CENDREBOIS',  name: 'Cendrebois',   endpoint: 'eu-03.lcdlln.fr:7421', load: 0.15, status: 'ok',   desc: 'Forêt maudite — RP.' },
    { id: 'SOUS-OMBRE',  name: 'Sous-Ombre',   endpoint: 'eu-04.lcdlln.fr:7421', load: 0.00, status: 'err',  desc: 'Maintenance en cours.' },
  ];
  return (
    <AuthBackdrop>
      <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        <AuthPanel title="Choix du royaume" versionLabel="4 serveurs" width={620}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {shards.map(s => (
              <div key={s.id} onClick={() => s.status !== 'err' && onPick && onPick()} style={{
                display: 'grid', gridTemplateColumns: '1fr auto auto', gap: 16, alignItems: 'center',
                padding: '12px 14px', borderRadius: 'var(--radius-md)',
                border: '1px solid var(--ln-border)', background: 'rgba(10,13,18,.5)',
                cursor: s.status === 'err' ? 'not-allowed' : 'pointer',
                opacity: s.status === 'err' ? 0.5 : 1,
              }}>
                <div>
                  <div style={{ fontFamily: 'var(--font-display)', fontWeight: 600, fontSize: 15, letterSpacing: '.12em', textTransform: 'uppercase', color: 'var(--ln-text)' }}>{s.name}</div>
                  <div style={{ fontFamily: 'var(--font-body)', fontStyle: 'italic', fontSize: 12, color: 'var(--ln-muted)', marginTop: 2 }}>{s.desc}</div>
                  <div style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--ln-muted-dim)', marginTop: 4, letterSpacing: '.05em' }}>{s.endpoint}</div>
                </div>
                <div style={{ width: 140, display: 'flex', flexDirection: 'column', gap: 4 }}>
                  <div style={{ fontSize: 9.5, letterSpacing: '.2em', textTransform: 'uppercase', color: 'var(--ln-muted)' }}>Charge</div>
                  <div style={{ height: 4, background: 'rgba(255,255,255,.06)', borderRadius: 2, overflow: 'hidden' }}>
                    <div style={{ width: `${s.load * 100}%`, height: '100%', background: s.load > 0.8 ? '#E8A55C' : '#5FB86E' }} />
                  </div>
                </div>
                <div style={{ width: 90, textAlign: 'right', fontSize: 10, letterSpacing: '.2em', textTransform: 'uppercase', color: s.status === 'ok' ? '#5FB86E' : s.status === 'warn' ? '#E8A55C' : '#C44040' }}>
                  {s.status === 'ok' ? '● stable' : s.status === 'warn' ? '● saturé' : '● offline'}
                </div>
              </div>
            ))}
          </div>
        </AuthPanel>
      </div>
    </AuthBackdrop>
  );
}

Object.assign(window, { LoginScreen, RegisterScreen, ShardPickScreen });
