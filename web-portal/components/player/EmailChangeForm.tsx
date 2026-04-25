"use client";

import { useState } from "react";

export function EmailChangeForm() {
  const [step, setStep] = useState<"request" | "verify">("request");
  const [newEmail, setNewEmail] = useState("");
  const [code, setCode] = useState("");
  const [status, setStatus] = useState<"idle" | "loading" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleRequest(e: React.FormEvent) {
    e.preventDefault();
    setStatus("loading");
    setMessage("");
    const res = await fetch("/api/player/profile/email", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ newEmail }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStep("verify");
      setStatus("idle");
      setMessage("Un code à 6 chiffres a été envoyé à " + newEmail);
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur.");
    }
  }

  async function handleVerify(e: React.FormEvent) {
    e.preventDefault();
    setStatus("loading");
    setMessage("");
    const res = await fetch("/api/player/profile/email/verify", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Email mis à jour avec succès. Reconnectez-vous.");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Code invalide.");
    }
  }

  if (status === "ok") {
    return (
      <p style={{ color: "var(--ln-success)", fontSize: 13 }}>
        {message}
      </p>
    );
  }

  return (
    <div style={{ display: "grid", gap: "0.75rem" }}>
      {step === "request" ? (
        <form onSubmit={handleRequest} style={{ display: "grid", gap: "0.75rem" }}>
          <div>
            <label className="wp-label">Nouvel email</label>
            <input
              className="wp-input"
              type="email"
              value={newEmail}
              onChange={(e) => setNewEmail(e.target.value)}
              required
              placeholder="nouveau@example.com"
            />
          </div>
          {message && <p style={{ fontSize: 13, color: "var(--ln-danger)" }}>{message}</p>}
          <button type="submit" className="btn btn-secondary" disabled={status === "loading"}>
            {status === "loading" ? "Envoi…" : "Envoyer le code de vérification"}
          </button>
        </form>
      ) : (
        <form onSubmit={handleVerify} style={{ display: "grid", gap: "0.75rem" }}>
          {message && (
            <p style={{ fontSize: 13, color: status === "error" ? "var(--ln-danger)" : "var(--ln-muted)" }}>
              {message}
            </p>
          )}
          <div>
            <label className="wp-label">Code reçu par email</label>
            <input
              className="wp-input"
              value={code}
              onChange={(e) => setCode(e.target.value)}
              maxLength={6}
              pattern="\d{6}"
              required
              placeholder="123456"
            />
          </div>
          <div style={{ display: "flex", gap: "0.5rem" }}>
            <button type="submit" className="btn btn-primary" disabled={status === "loading"}>
              {status === "loading" ? "Vérification…" : "Valider le code"}
            </button>
            <button type="button" className="btn btn-ghost" onClick={() => { setStep("request"); setMessage(""); }}>
              Recommencer
            </button>
          </div>
        </form>
      )}
    </div>
  );
}
