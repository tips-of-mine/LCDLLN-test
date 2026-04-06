"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";

export default function LoginPage() {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setLoading(true);
    setError("");
    // Simulated — auth not yet implemented
    setTimeout(() => {
      setError("Authentification non encore implémentée. Fonctionnalité à venir.");
      setLoading(false);
    }, 800);
  }

  return (
    <div style={{ maxWidth: "24rem", margin: "3rem auto" }}>
      <div style={{ textAlign: "center", marginBottom: "2rem" }}>
        <div style={{ fontSize: "2.5rem", marginBottom: "0.75rem" }}>&#128274;</div>
        <h1 style={{ marginBottom: "0.25rem" }}>Connexion</h1>
        <p className="text-sm">Accédez à votre espace joueur</p>
      </div>

      <div className="card" style={{ margin: 0 }}>
        <form onSubmit={handleSubmit} className="form-stack">
          {error && <div className="error-box">{error}</div>}

          <div className="field">
            <label>Identifiant ou e-mail</label>
            <input
              type="text"
              placeholder="Votre login ou adresse e-mail"
              required
              autoComplete="username"
            />
          </div>

          <div className="field">
            <label>Mot de passe</label>
            <input
              type="password"
              placeholder="Votre mot de passe"
              required
              autoComplete="current-password"
            />
          </div>

          <button type="submit" className="btn-primary" disabled={loading}>
            {loading ? "Connexion…" : "Se connecter"}
          </button>
        </form>

        <div style={{ marginTop: "1.25rem", paddingTop: "1.25rem", borderTop: "1px solid var(--border)", textAlign: "center" }}>
          <Link href="/password-recovery" className="text-sm">
            Mot de passe oublié ?
          </Link>
        </div>
      </div>

      <p className="text-sm text-center mt-3" style={{ color: "var(--muted)" }}>
        Pas encore de compte ? Les comptes sont créés directement en jeu
        lors de votre première connexion.
      </p>
    </div>
  );
}
