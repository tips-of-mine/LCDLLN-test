"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";

export default function LoginPage() {
  const router = useRouter();
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
      router.push(data.redirect || "/player");
      router.refresh();
    } catch {
      setError("Erreur réseau. Réessayez.");
    } finally {
      setLoading(false);
    }
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
              value={identifier}
              onChange={(ev) => setIdentifier(ev.target.value)}
              placeholder="Votre login ou adresse e-mail"
              required
              autoComplete="username"
            />
          </div>

          <div className="field">
            <label>Mot de passe</label>
            <input
              type="password"
              value={password}
              onChange={(ev) => setPassword(ev.target.value)}
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
        Pas encore de compte ? Créez-le dans le client jeu. Le même mot de passe que
        pour vous connecter au jeu fonctionne ici (hash Argon2). Ancien mot de passe
        défini uniquement par le portail (scrypt) reste accepté jusqu’à une
        réinitialisation.
      </p>
    </div>
  );
}
