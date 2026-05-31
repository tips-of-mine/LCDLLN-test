"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";

export default function LoginPage() {
  const [identifier, setIdentifier] = useState("");
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  async function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setLoading(true);
    setError("");
    try {
      const res = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ identifier, password }),
      });
      const data = (await res.json()) as { ok?: boolean; message?: string; redirect?: string };
      if (!res.ok || !data.ok) {
        setError(data.message || "Connexion impossible.");
        return;
      }
      // Navigation "dure" volontaire (et non router.push + router.refresh).
      // /api/auth/login pose le cookie de session via Set-Cookie sur la réponse
      // du fetch ; une navigation client App Router peut alors rendre la cible
      // depuis le cache router/RSC produit AVANT que le cookie existe — le
      // middleware Edge et SiteHeader voient l'état "déconnecté", obligeant
      // l'utilisateur à recharger manuellement. window.location.assign force une
      // requête HTTP complète : le cookie part, middleware + getSession voient
      // la session, et la page cible est rendue fraîche, authentifiée.
      window.location.assign(data.redirect || "/player");
      return;
    } catch {
      setError("Erreur réseau. Réessayez.");
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="wp-main narrow" style={{ display: "flex", flexDirection: "column", alignItems: "center", paddingTop: 48 }}>
      <div style={{ textAlign: "center", marginBottom: 32 }}>
        <div style={{
          width: 64, height: 64, borderRadius: "50%",
          background: "radial-gradient(circle at 32% 30%, #2a2330 0%, #0B0712 55%, #000 100%)",
          border: "1px solid #1a1420",
          boxShadow: "0 0 30px rgba(74,123,184,.4)",
          margin: "0 auto 18px",
        }} />
        <div style={{ fontFamily: "var(--font-display)", fontWeight: 700, fontSize: 20, letterSpacing: ".2em", textTransform: "uppercase", color: "var(--ln-text)", marginBottom: 6 }}>
          Connexion
        </div>
        <div style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 14, color: "var(--ln-muted)" }}>
          Accédez à votre espace joueur
        </div>
      </div>

      <div className="wp-card" style={{ width: "100%", maxWidth: 440, padding: 28 }}>
        <form onSubmit={handleSubmit} style={{ display: "flex", flexDirection: "column", gap: 16 }}>
          {error && (
            <div className="wp-alert error">
              <span className="wp-alert-icon">✕</span>
              {error}
            </div>
          )}
          <div className="field">
            <label htmlFor="identifier">Identifiant ou e-mail</label>
            <input
              id="identifier"
              type="text"
              value={identifier}
              onChange={(ev) => setIdentifier(ev.target.value)}
              placeholder="Votre login ou adresse e-mail"
              required
              autoComplete="username"
              autoFocus
            />
          </div>
          <div className="field">
            <label htmlFor="password">Mot de passe</label>
            <input
              id="password"
              type="password"
              value={password}
              onChange={(ev) => setPassword(ev.target.value)}
              placeholder="Votre mot de passe"
              required
              autoComplete="current-password"
            />
          </div>
          <button type="submit" className="btn btn-primary" style={{ width: "100%" }} disabled={loading}>
            {loading ? "Connexion…" : "Se connecter"}
          </button>
        </form>
        <div style={{ marginTop: 18, paddingTop: 18, borderTop: "1px solid var(--ln-border)", textAlign: "center" }}>
          <Link href="/password-recovery" style={{ fontFamily: "var(--font-ui)", fontSize: 10, letterSpacing: ".2em", textTransform: "uppercase", color: "var(--ln-muted)" }}>
            Mot de passe oublié ?
          </Link>
        </div>
      </div>

      <p style={{ fontFamily: "var(--font-body)", fontStyle: "italic", fontSize: 12.5, color: "var(--ln-muted)", textAlign: "center", maxWidth: 400, marginTop: 20, lineHeight: 1.6 }}>
        Pas encore de compte ? Créez-le dans le client jeu. Le même identifiant et mot de passe
        que pour le jeu fonctionnent ici.
      </p>
    </div>
  );
}
