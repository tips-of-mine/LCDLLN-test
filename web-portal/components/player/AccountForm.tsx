"use client";

import { useState } from "react";
import type { AccountProfile } from "@/lib/playerProfile";

type Props = { profile: AccountProfile };

export function AccountForm({ profile }: Props) {
  const [firstName, setFirstName] = useState(profile.firstName);
  const [lastName, setLastName] = useState(profile.lastName);
  const [address, setAddress] = useState(profile.address);
  const [city, setCity] = useState(profile.city);
  const [postalCode, setPostalCode] = useState(profile.postalCode);
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setStatus("saving");
    setMessage("");
    const res = await fetch("/api/player/profile", {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ firstName, lastName, address, city, postalCode }),
    });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      setStatus("ok");
      setMessage("Profil mis à jour.");
    } else {
      setStatus("error");
      setMessage(data.message ?? "Erreur lors de la mise à jour.");
    }
  }

  return (
    <form onSubmit={handleSubmit} className="wp-form" style={{ display: "grid", gap: "1rem" }}>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1rem" }}>
        <div>
          <label className="wp-label">Prénom</label>
          <input
            className="wp-input"
            value={firstName}
            onChange={(e) => setFirstName(e.target.value)}
            required
            maxLength={100}
          />
        </div>
        <div>
          <label className="wp-label">Nom</label>
          <input
            className="wp-input"
            value={lastName}
            onChange={(e) => setLastName(e.target.value)}
            required
            maxLength={100}
          />
        </div>
      </div>

      <div>
        <label className="wp-label">TAG-ID</label>
        <input
          className="wp-input"
          value={profile.tagId || profile.login}
          readOnly
          style={{ opacity: 0.6, cursor: "default" }}
        />
      </div>

      <div>
        <label className="wp-label">Email actuel</label>
        <input className="wp-input" value={profile.email} readOnly style={{ opacity: 0.6, cursor: "default" }} />
        {profile.emailPending && (
          <p style={{ fontSize: 12, color: "var(--ln-warning)", marginTop: 4 }}>
            Changement en attente vers <strong>{profile.emailPending}</strong> — vérifiez votre boîte mail.
          </p>
        )}
      </div>

      <div>
        <label className="wp-label">Adresse</label>
        <input
          className="wp-input"
          value={address}
          onChange={(e) => setAddress(e.target.value)}
          maxLength={255}
          placeholder="1 rue de la Lune Noire"
        />
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 2fr", gap: "1rem" }}>
        <div>
          <label className="wp-label">Code postal</label>
          <input
            className="wp-input"
            value={postalCode}
            onChange={(e) => setPostalCode(e.target.value)}
            maxLength={20}
          />
        </div>
        <div>
          <label className="wp-label">Ville</label>
          <input
            className="wp-input"
            value={city}
            onChange={(e) => setCity(e.target.value)}
            maxLength={100}
          />
        </div>
      </div>

      {message && (
        <p style={{ fontSize: 13, color: status === "ok" ? "var(--ln-success)" : "var(--ln-danger)" }}>
          {message}
        </p>
      )}

      <button type="submit" className="btn btn-primary" disabled={status === "saving"}>
        {status === "saving" ? "Enregistrement…" : "Enregistrer"}
      </button>
    </form>
  );
}
