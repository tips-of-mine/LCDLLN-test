"use client";

import { useState } from "react";

export function ResetPasswordForm({ token }: { token: string }) {
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [pending, setPending] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  async function onSubmit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setPending(true);
    setError("");
    setMessage("");

    if (password !== confirmPassword) {
      setPending(false);
      setError("Les deux mots de passe doivent etre identiques.");
      return;
    }

    try {
      const response = await fetch("/api/password-recovery/reset", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ token, newPassword: password }),
      });
      const payload = (await response.json()) as { ok?: boolean; message?: string };
      if (!response.ok || !payload.ok) {
        throw new Error(payload.message || "Le mot de passe n'a pas pu etre modifie.");
      }
      setMessage(payload.message || "Mot de passe modifie.");
      setPassword("");
      setConfirmPassword("");
    } catch (err) {
      setError(err instanceof Error ? err.message : "Erreur inconnue.");
    } finally {
      setPending(false);
    }
  }

  return (
    <form className="card form-stack" onSubmit={onSubmit}>
      <label className="field">
        <span>Nouveau mot de passe</span>
        <input type="password" value={password} onChange={(event) => setPassword(event.target.value)} required />
      </label>
      <label className="field">
        <span>Confirmer le mot de passe</span>
        <input
          type="password"
          value={confirmPassword}
          onChange={(event) => setConfirmPassword(event.target.value)}
          required
        />
      </label>
      <button type="submit" disabled={pending}>
        {pending ? "Mise a jour..." : "Changer mon mot de passe"}
      </button>
      {message ? <p className="success-box">{message}</p> : null}
      {error ? <p className="error-box">{error}</p> : null}
    </form>
  );
}
