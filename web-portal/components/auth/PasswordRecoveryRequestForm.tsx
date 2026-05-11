"use client";

import { useMemo, useState } from "react";

type FormState = {
  loginOrEmail: string;
  birthDate: string;
  age: string;
  address: string;
  city: string;
  postalCode: string;
  secretAnswers: [string, string, string];
};

const initialState: FormState = {
  loginOrEmail: "",
  birthDate: "",
  age: "",
  address: "",
  city: "",
  postalCode: "",
  secretAnswers: ["", "", ""],
};

export function PasswordRecoveryRequestForm() {
  const [form, setForm] = useState<FormState>(initialState);
  const [pending, setPending] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");

  const secretLabels = useMemo(
    () => [
      "Reponse a la question secrete 1",
      "Reponse a la question secrete 2",
      "Reponse a la question secrete 3",
    ],
    [],
  );

  async function onSubmit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setPending(true);
    setError("");
    setMessage("");
    try {
      const response = await fetch("/api/password-recovery/request", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(form),
      });
      const payload = (await response.json()) as { message?: string };
      if (!response.ok) {
        throw new Error(payload.message || "La demande n'a pas pu etre traitee.");
      }
      setMessage(payload.message || "Si les informations correspondent, un e-mail a ete envoye.");
      setForm(initialState);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Erreur inconnue.");
    } finally {
      setPending(false);
    }
  }

  return (
    <form className="card form-stack" onSubmit={onSubmit}>
      <label className="field">
        <span>Login ou email</span>
        <input
          value={form.loginOrEmail}
          onChange={(event) => setForm((current) => ({ ...current, loginOrEmail: event.target.value }))}
          placeholder="Votre login ou votre email"
          required
        />
      </label>

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
          <span>Age</span>
          <input
            inputMode="numeric"
            value={form.age}
            onChange={(event) => setForm((current) => ({ ...current, age: event.target.value }))}
            placeholder="Ex. 38"
          />
        </label>
      </div>

      <div className="form-grid">
        <label className="field">
          <span>Adresse</span>
          <input
            value={form.address}
            onChange={(event) => setForm((current) => ({ ...current, address: event.target.value }))}
          />
        </label>
        <label className="field">
          <span>Ville</span>
          <input
            value={form.city}
            onChange={(event) => setForm((current) => ({ ...current, city: event.target.value }))}
          />
        </label>
      </div>

      <label className="field">
        <span>Code postal</span>
        <input
          value={form.postalCode}
          onChange={(event) => setForm((current) => ({ ...current, postalCode: event.target.value }))}
        />
      </label>

      {secretLabels.map((label, index) => (
        <label className="field" key={label}>
          <span>{label}</span>
          <input
            type="password"
            value={form.secretAnswers[index]}
            onChange={(event) =>
              setForm((current) => {
                const next = [...current.secretAnswers] as [string, string, string];
                next[index] = event.target.value;
                return { ...current, secretAnswers: next };
              })
            }
          />
        </label>
      ))}

      <button type="submit" disabled={pending}>
        {pending ? "Verification en cours..." : "Recevoir un lien de reinitialisation"}
      </button>

      {message ? <p className="success-box">{message}</p> : null}
      {error ? <p className="error-box">{error}</p> : null}
    </form>
  );
}
