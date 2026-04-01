"use client";

import { useEffect, useState } from "react";

type RecoveryProfile = {
  accountId: number;
  birthDate: string;
  address: string;
  city: string;
  postalCode: string;
  secretQuestions: Array<{ id?: number; question: string; answer?: string }>;
};

function emptyProfile(accountId: number): RecoveryProfile {
  return {
    accountId,
    birthDate: "",
    address: "",
    city: "",
    postalCode: "",
    secretQuestions: [
      { question: "", answer: "" },
      { question: "", answer: "" },
      { question: "", answer: "" },
    ],
  };
}

export function RecoveryProfileForm({ accountId }: { accountId: number }) {
  const [form, setForm] = useState<RecoveryProfile>(() => emptyProfile(accountId));
  const [loading, setLoading] = useState(true);
  const [pending, setPending] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  useEffect(() => {
    let cancelled = false;
    async function load() {
      setLoading(true);
      setError("");
      try {
        const response = await fetch(`/api/password-recovery/profile?accountId=${accountId}`);
        const payload = (await response.json()) as { profile?: RecoveryProfile; error?: string };
        if (!response.ok) throw new Error(payload.error || "Impossible de charger le profil.");
        const loaded = payload.profile || emptyProfile(accountId);
        const paddedQuestions = [...loaded.secretQuestions];
        while (paddedQuestions.length < 3) paddedQuestions.push({ question: "", answer: "" });
        if (!cancelled) setForm({ ...loaded, secretQuestions: paddedQuestions.slice(0, 3) });
      } catch (err) {
        if (!cancelled) setError(err instanceof Error ? err.message : "Erreur inconnue.");
      } finally {
        if (!cancelled) setLoading(false);
      }
    }
    void load();
    return () => {
      cancelled = true;
    };
  }, [accountId]);

  async function onSubmit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setPending(true);
    setError("");
    setMessage("");
    try {
      const response = await fetch("/api/password-recovery/profile", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(form),
      });
      const payload = (await response.json()) as { ok?: boolean; error?: string };
      if (!response.ok || !payload.ok) throw new Error(payload.error || "Enregistrement impossible.");
      setMessage("Profil de recuperation enregistre.");
    } catch (err) {
      setError(err instanceof Error ? err.message : "Erreur inconnue.");
    } finally {
      setPending(false);
    }
  }

  if (loading) {
    return <div className="card">Chargement du profil de recuperation...</div>;
  }

  return (
    <form className="card form-stack" onSubmit={onSubmit}>
      <div className="form-grid">
        <label className="field">
          <span>Date de naissance</span>
          <input
            type="date"
            value={form.birthDate}
            onChange={(event) => setForm((current) => ({ ...current, birthDate: event.target.value }))}
          />
        </label>
        <label className="field">
          <span>Code postal</span>
          <input
            value={form.postalCode}
            onChange={(event) => setForm((current) => ({ ...current, postalCode: event.target.value }))}
          />
        </label>
      </div>

      <label className="field">
        <span>Adresse</span>
        <input value={form.address} onChange={(event) => setForm((current) => ({ ...current, address: event.target.value }))} />
      </label>

      <label className="field">
        <span>Ville</span>
        <input value={form.city} onChange={(event) => setForm((current) => ({ ...current, city: event.target.value }))} />
      </label>

      {form.secretQuestions.map((question, index) => (
        <div className="form-grid" key={`secret-${index}`}>
          <label className="field">
            <span>{`Question secrete ${index + 1}`}</span>
            <input
              value={question.question}
              onChange={(event) =>
                setForm((current) => {
                  const next = [...current.secretQuestions];
                  next[index] = { ...next[index], question: event.target.value };
                  return { ...current, secretQuestions: next };
                })
              }
            />
          </label>
          <label className="field">
            <span>{`Reponse ${index + 1}`}</span>
            <input
              type="password"
              value={question.answer || ""}
              onChange={(event) =>
                setForm((current) => {
                  const next = [...current.secretQuestions];
                  next[index] = { ...next[index], answer: event.target.value };
                  return { ...current, secretQuestions: next };
                })
              }
            />
          </label>
        </div>
      ))}

      <button type="submit" disabled={pending}>
        {pending ? "Enregistrement..." : "Enregistrer le profil de recuperation"}
      </button>
      {message ? <p className="success-box">{message}</p> : null}
      {error ? <p className="error-box">{error}</p> : null}
    </form>
  );
}
