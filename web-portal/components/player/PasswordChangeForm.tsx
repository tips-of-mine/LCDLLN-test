"use client";

import { useState } from "react";

export function PasswordChangeForm() {
  const [current, setCurrent] = useState("");
  const [newPass, setNewPass] = useState("");
  const [confirm, setConfirm] = useState("");
  const [status, setStatus] = useState<"idle" | "loading" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    if (newPass !== confirm) {
      setStatus("error");
      setMessage("Les mots de passe ne correspondent pas.");
      return;
    }
    setStatus("loading");
    setMessage("");
    try {
      const res = await fetch("/api/player/security/password", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ currentPassword: current, newPassword: newPass }),
      });
      const data = (await res.json()) as { ok: boolean; message?: string };
      if (data.ok) {
        setStatus("ok");
        setMessage("Mot de passe modifié avec succès.");
        setCurrent(""); setNewPass(""); setConfirm("");
      } else {
        setStatus("error");
        setMessage(data.message ?? "Erreur.");
      }
    } catch {
      setStatus("error");
      setMessage("Erreur réseau. Veuillez réessayer.");
    }
  }

  return (
    <form onSubmit={handleSubmit} style={{ display: "grid", gap: "1rem" }}>
      <div>
        <label className="wp-label">Mot de passe actuel</label>
        <input
          className="wp-input"
          type="password"
          value={current}
          onChange={(e) => setCurrent(e.target.value)}
          required
          autoComplete="current-password"
        />
      </div>
      <div>
        <label className="wp-label">Nouveau mot de passe</label>
        <input
          className="wp-input"
          type="password"
          value={newPass}
          onChange={(e) => setNewPass(e.target.value)}
          required
          minLength={8}
          autoComplete="new-password"
        />
      </div>
      <div>
        <label className="wp-label">Confirmer le nouveau mot de passe</label>
        <input
          className="wp-input"
          type="password"
          value={confirm}
          onChange={(e) => setConfirm(e.target.value)}
          required
          autoComplete="new-password"
        />
      </div>
      {message && (
        <p style={{ fontSize: 13, color: status === "ok" ? "var(--ln-success)" : "var(--ln-danger)" }}>
          {message}
        </p>
      )}
      <button type="submit" className="btn btn-primary" disabled={status === "loading"}>
        {status === "loading" ? "Modification…" : "Changer le mot de passe"}
      </button>
    </form>
  );
}
