"use client";

import { useState, type FormEvent } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";

export function LoginForm({ nextPath }: { nextPath: string }) {
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
        body: JSON.stringify({ identifier, password, next: nextPath }),
      });
      const data = (await res.json()) as {
        ok?: boolean;
        message?: string;
        redirect?: string;
      };
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
        <button
          type="submit"
          className="btn btn-primary"
          style={{ width: "100%" }}
          disabled={loading}
        >
          {loading ? "Connexion…" : "Se connecter"}
        </button>
      </form>
      <div
        style={{
          marginTop: 18,
          paddingTop: 18,
          borderTop: "1px solid var(--ln-border)",
          textAlign: "center",
        }}
      >
        <Link
          href="/password-recovery"
          style={{
            fontFamily: "var(--font-ui)",
            fontSize: 10,
            letterSpacing: ".2em",
            textTransform: "uppercase",
            color: "var(--ln-muted)",
          }}
        >
          Mot de passe oublié ?
        </Link>
      </div>
    </div>
  );
}
