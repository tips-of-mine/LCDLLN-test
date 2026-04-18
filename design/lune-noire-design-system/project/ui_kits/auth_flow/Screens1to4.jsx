/* Lune Noire — Écrans 1 à 4 : Langue, Connexion, Inscription, Erreurs.
   Règles strictes : boutons à libellés courts (≤ 16 car),
   pas de texte tronqué, explications longues → icône "i". */

const { useState: useS1, useEffect: useE1 } = React;

// ============ ÉCRAN 1 — PREMIER LANCEMENT : CHOIX DE LANGUE ============
function LangSelectScreen({ onNext }) {
  const [lang, setLang] = useS1('fr');
  const langs = [
    { id: 'fr', name: 'Français', native: 'Français', welcome: 'Bienvenue, voyageur.' },
    { id: 'en', name: 'English',  native: 'English',  welcome: 'Welcome, traveller.' },
  ];
  const current = langs.find(l => l.id === lang);
  return (
    <div className="ln-stage">
      <div className="ln-stage-col" style={{ width: 'min(720px, 94vw)' }}>
        <div className="ln-hero">
          <div className="ln-hero-line1">Les Chroniques</div>
          <div className="ln-hero-line2">de la Lune Noire</div>
        </div>
        <Panel
          title="Choisissez votre langue"
          subtitle={current.welcome}
          versionLabel="1 / 2"
          infoText="Vous pourrez modifier ce choix plus tard dans les Options > Langue."
          footer={
            <>
              <KeycapHint keyLabel="← →">naviguer</KeycapHint>
              <KeycapHint keyLabel="↵">valider</KeycapHint>
            </>
          }
        >
          <div className="ln-lang-grid">
            {langs.map(l => (
              <div key={l.id} className={`ln-lang-card ${lang === l.id ? 'selected' : ''}`} onClick={() => setLang(l.id)}>
                <FlagSVG code={l.id} />
                <div className="ln-lang-name">{l.name}</div>
                <div className="ln-lang-native">{l.native}</div>
              </div>
            ))}
          </div>
          <div className="ln-actions" style={{ justifyContent: 'flex-end' }}>
            <Button kind="primary" size="lg" keycap="↵" onClick={onNext}>Continuer</Button>
          </div>
        </Panel>
      </div>
    </div>
  );
}

function FlagSVG({ code }) {
  if (code === 'fr') return (
    <svg width="54" height="38" viewBox="0 0 54 38"><rect width="18" height="38" fill="#002654"/><rect x="18" width="18" height="38" fill="#fff"/><rect x="36" width="18" height="38" fill="#ED2939"/></svg>
  );
  return (
    <svg width="54" height="38" viewBox="0 0 60 36">
      <rect width="60" height="36" fill="#012169"/>
      <path d="M0,0 L60,36 M60,0 L0,36" stroke="#fff" strokeWidth="7"/>
      <path d="M0,0 L60,36 M60,0 L0,36" stroke="#C8102E" strokeWidth="3"/>
      <path d="M30,0 V36 M0,18 H60" stroke="#fff" strokeWidth="10"/>
      <path d="M30,0 V36 M0,18 H60" stroke="#C8102E" strokeWidth="5"/>
    </svg>
  );
}

// ============ ÉCRAN 2 — AUTHENTIFICATION ============
function LoginScreen({ onRegister, onOptions, onShard }) {
  const [id, setId] = useS1('');
  const [pw, setPw] = useS1('');
  const [remember, setRemember] = useS1(true);
  const [submitting, setSubmitting] = useS1(false);
  const [err, setErr] = useS1(null);

  const submit = () => {
    if (!id || !pw) { setErr('Veuillez remplir tous les champs.'); return; }
    setErr(null); setSubmitting(true);
    setTimeout(() => { setSubmitting(false); onShard && onShard(); }, 900);
  };

  return (
    <div className="ln-stage">
      <div className="ln-stage-col" style={{ width: 'min(460px, 94vw)' }}>
        <div className="ln-hero">
          <div className="ln-hero-line1">Les Chroniques</div>
          <div className="ln-hero-line2">de la Lune Noire</div>
        </div>
        <Panel title="Connexion" versionLabel="v0.8.4" infoText="Saisissez l'identifiant et le mot de passe liés à votre compte. Pas de compte ? Utilisez « Créer un compte » ci-dessous."
          footer={<><KeycapHint keyLabel="Tab">champ suivant</KeycapHint><KeycapHint keyLabel="↵">se connecter</KeycapHint><KeycapHint keyLabel="Échap">quitter</KeycapHint></>}
        >
          {err && <Banner kind="error" title="Échec de la connexion">{err}</Banner>}
          {submitting && <Banner kind="info" title="Vérification en cours">Contact du serveur maître…</Banner>}
          <Field label="Identifiant" value={id} onChange={setId} placeholder="votre nom d'aventurier" autoFocus />
          <Field label="Mot de passe" type="password" value={pw} onChange={setPw} placeholder="••••••••" />
          <Toggle label="Se souvenir de moi" checked={remember} onChange={setRemember} hint="Conserve l'identifiant à la prochaine ouverture." />
          <div className="ln-actions">
            <Button kind="text" size="sm" onClick={() => alert('Envoi du lien de récupération — maquette')}>Mot de passe oublié ?</Button>
            <div className="ln-actions-right">
              <Button kind="ghost" size="md" onClick={onRegister} keycap="Ctrl+R">Créer un compte</Button>
              <Button kind="primary" size="md" onClick={submit} keycap="↵" disabled={submitting}>Se connecter</Button>
            </div>
          </div>
        </Panel>
        <div style={{ display: 'flex', gap: 20 }}>
          <Button kind="text" size="sm" onClick={onOptions}>⚙ Options</Button>
          <Button kind="text" size="sm">Quitter</Button>
        </div>
      </div>
    </div>
  );
}

// ============ ÉCRAN 3 — INSCRIPTION ============
function pwStrength(pw) {
  let s = 0;
  if (pw.length >= 8) s++;
  if (/[A-Z]/.test(pw)) s++;
  if (/[0-9]/.test(pw)) s++;
  if (/[^A-Za-z0-9]/.test(pw)) s++;
  return s;
}
function checkUsername(u) {
  if (!u) return { status: undefined, msg: '' };
  if (u.length < 3) return { status: 'err', msg: '3 caractères minimum' };
  if (['aldric', 'korvath', 'morwenna'].includes(u.toLowerCase())) return { status: 'err', msg: 'Identifiant déjà pris' };
  if (u.length < 5) return { status: 'pending', msg: 'Vérification…' };
  return { status: 'ok', msg: 'Identifiant disponible' };
}
function checkEmail(e) {
  if (!e) return { status: undefined, msg: '' };
  const ok = /^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(e);
  return ok ? { status: 'ok', msg: '' } : { status: 'err', msg: 'Adresse non valide' };
}

function RegisterScreen({ onBack, onConfirm, onError }) {
  const breadcrumb = [
    { key: 'lang',   label: 'Langue' },
    { key: 'auth',   label: 'Compte' },
    { key: 'verify', label: 'Courriel' },
    { key: 'world',  label: 'Monde' },
  ];

  const [f, setF] = useS1({ id: '', email: '', pw: '', pw2: '', day: '01', month: '01', year: '2000', country: 'FR' });
  const set = (k) => (v) => setF(s => ({ ...s, [k]: v }));

  const uCheck = checkUsername(f.id);
  const eCheck = checkEmail(f.email);
  const strength = pwStrength(f.pw);
  const pwMatch = f.pw && f.pw2 && f.pw === f.pw2;

  const canSubmit = uCheck.status === 'ok' && eCheck.status === 'ok' && strength >= 3 && pwMatch;

  return (
    <div className="ln-stage" style={{ padding: 'clamp(12px, 2vh, 24px)' }}>
      <div className="ln-stage-col" style={{ width: 'min(680px, 96vw)', gap: 14 }}>
        <Breadcrumb steps={breadcrumb} current={1} />
        <Panel
          title="Créer un compte"
          subtitle="Forger votre identité dans les terres de la Lune Noire."
          versionLabel="2 / 4"
          infoText="Le code d'accès de l'Édition des morts est facultatif pour les nouveaux joueurs ; laissez vide si vous n'en avez pas."
        >
          <div className="ln-form-grid cols-2">
            <div className="span-2">
              <Field label="Identifiant" value={f.id} onChange={set('id')} placeholder="3 à 16 caractères" status={uCheck.status} statusMsg={uCheck.msg} autoFocus />
            </div>
            <div className="span-2">
              <Field label="Adresse courriel" value={f.email} onChange={set('email')} placeholder="vous@exemple.fr" status={eCheck.status} statusMsg={eCheck.msg} tooltip="Utilisée pour récupérer votre mot de passe." />
            </div>
            <div>
              <Field label="Mot de passe" type="password" value={f.pw} onChange={set('pw')} placeholder="8 caractères minimum" />
              <div className="ln-pw-strength">
                {[0,1,2,3].map(i => (
                  <span key={i} className={i < strength ? (strength <= 1 ? 'on-weak' : strength === 2 ? 'on-med' : 'on-strong') : ''} />
                ))}
              </div>
            </div>
            <Field
              label="Confirmation"
              type="password"
              value={f.pw2}
              onChange={set('pw2')}
              placeholder="à l'identique"
              status={f.pw2 ? (pwMatch ? 'ok' : 'err') : undefined}
              statusMsg={f.pw2 ? (pwMatch ? 'Correspond' : 'Les mots de passe diffèrent') : ''}
            />
            <Dropdown label="Jour" value={f.day} onChange={set('day')}
              options={Array.from({ length: 31 }, (_, i) => ({ value: String(i+1).padStart(2,'0'), label: String(i+1).padStart(2,'0') }))} />
            <Dropdown label="Mois" value={f.month} onChange={set('month')}
              options={['Janv.','Févr.','Mars','Avr.','Mai','Juin','Juil.','Août','Sept.','Oct.','Nov.','Déc.'].map((m,i) => ({ value: String(i+1).padStart(2,'0'), label: m }))} />
            <Dropdown label="Année" value={f.year} onChange={set('year')}
              options={Array.from({ length: 90 }, (_, i) => { const y = 2010 - i; return { value: String(y), label: String(y) }; })} />
          </div>
          <div className="ln-actions">
            <Button kind="ghost" size="md" onClick={onBack} keycap="Échap">Retour</Button>
            <div className="ln-actions-right">
              <Button kind="text" size="sm" onClick={onError}>Voir les erreurs</Button>
              <Button kind="primary" size="md" keycap="↵" disabled={!canSubmit} onClick={onConfirm}>Créer le compte</Button>
            </div>
          </div>
        </Panel>
        <div style={{ display: 'flex', gap: 20, justifyContent: 'center' }}>
          <KeycapHint keyLabel="Tab">champ suivant</KeycapHint>
          <KeycapHint keyLabel="↵">valider</KeycapHint>
          <KeycapHint keyLabel="Échap">retour</KeycapHint>
        </div>
      </div>
    </div>
  );
}

// ============ ÉCRAN 4 — ERREURS D'INSCRIPTION ============
function RegisterErrorScreen({ onBack }) {
  const [active, setActive] = useS1('taken');
  const errors = {
    taken:   { title: 'Identifiant déjà pris',   msg: 'Un aventurier porte déjà ce nom. Choisissez-en un autre.', field: 'Identifiant', fix: 'Essayez une variante : ajoutez un chiffre ou un suffixe (ex. « MorwennaDuNord »).' },
    weak:    { title: 'Mot de passe trop faible', msg: '8 caractères minimum, avec majuscule, chiffre et caractère spécial.', field: 'Mot de passe', fix: 'Utilisez au moins : 1 majuscule, 1 chiffre, 1 symbole.' },
    email:   { title: 'Courriel invalide',         msg: 'Le format de l\'adresse courriel n\'est pas reconnu.', field: 'Adresse courriel', fix: 'Exemple attendu : « vous@exemple.fr ».' },
    network: { title: 'Serveur injoignable',       msg: 'Le serveur maître ne répond pas. Vérifiez votre connexion.', field: null, fix: 'Réessayez dans quelques instants, ou consultez status.lcdlln.fr.' },
  };
  const e = errors[active];

  return (
    <div className="ln-stage">
      <div className="ln-stage-col" style={{ width: 'min(640px, 96vw)' }}>
        <Panel title="Inscription impossible" versionLabel="Erreur" infoText="Cette vue illustre les 4 types d'erreurs d'inscription. Dans le jeu, l'erreur pertinente s'affiche automatiquement.">
          {/* sélecteur de démo */}
          <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
            {Object.keys(errors).map(k => (
              <button key={k} onClick={() => setActive(k)}
                style={{
                  padding: '6px 12px', borderRadius: 'var(--radius-xs)',
                  border: `1px solid ${active === k ? 'var(--ln-accent)' : 'var(--ln-border)'}`,
                  background: active === k ? 'rgba(232,197,110,.1)' : 'transparent',
                  color: active === k ? 'var(--ln-accent)' : 'var(--ln-muted)',
                  fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.18em', textTransform: 'uppercase', cursor: 'pointer',
                }}>
                {errors[k].title}
              </button>
            ))}
          </div>
          <Banner kind={active === 'network' ? 'warning' : 'error'} title={e.title}>
            {e.msg}
          </Banner>
          {e.field && (
            <div style={{ padding: '12px 14px', background: 'rgba(10,13,18,.5)', border: '1px solid var(--ln-border)', borderRadius: 'var(--radius-md)' }}>
              <div style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-muted)', marginBottom: 6 }}>Champ à corriger</div>
              <div style={{ fontFamily: 'var(--font-display)', fontSize: 14, fontWeight: 600, letterSpacing: '.14em', textTransform: 'uppercase', color: 'var(--ln-accent)' }}>{e.field}</div>
            </div>
          )}
          <div style={{ padding: '12px 14px', borderLeft: '3px solid var(--ln-accent)', background: 'rgba(232,197,110,.04)' }}>
            <div style={{ fontFamily: 'var(--font-ui)', fontSize: 10, letterSpacing: '.22em', textTransform: 'uppercase', color: 'var(--ln-accent)', marginBottom: 6 }}>Comment corriger</div>
            <div style={{ fontFamily: 'var(--font-body)', fontSize: 13.5, fontStyle: 'italic', color: 'var(--ln-text)' }}>{e.fix}</div>
          </div>
          <div className="ln-actions">
            <Button kind="ghost" size="md" onClick={onBack} keycap="Échap">Retour au formulaire</Button>
            {active === 'network' && <Button kind="primary" size="md">Réessayer</Button>}
          </div>
        </Panel>
      </div>
    </div>
  );
}

Object.assign(window, { LangSelectScreen, LoginScreen, RegisterScreen, RegisterErrorScreen });
