"use client";

import { useState } from "react";
import type { ProfileVisibility } from "@/lib/playerPrivacy";

const LABELS: Record<ProfileVisibility, string> = {
  public: "Public — tout le monde peut voir mon profil",
  friends: "Amis uniquement",
  private: "Privé — personne ne peut voir mon profil",
};

type Props = { initial: ProfileVisibility };

export function PrivacyForm({ initial }: Props) {
  const [visibility, setVisibility] = useState<ProfileVisibility>(initial);
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [message, setMessage] = useState("");

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setStatus("saving");
    setMessage("");
    try {
      const res = await fetch("/api/player/privacy", {
        method: "PATCH",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ profileVisibility: visibility }),
      });
      const data = (await res.json()) as { ok: boolean; message?: string };
      if (data.ok) {
        setStatus("ok");
        setMessage("Paramètres mis à jour.");
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
        <label className="wp-label">Visibilité du profil</label>
        {(["public", "friends", "private"] as ProfileVisibility[]).map((v) => (
          <label
            key={v}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 10,
              padding: "0.6rem 0",
              borderBottom: "1px solid var(--ln-border)",
              cursor: "pointer",
            }}
          >
            <input
              type="radio"
              name="visibility"
              value={v}
              checked={visibility === v}
              onChange={() => setVisibility(v)}
            />
            <span style={{ fontSize: 14 }}>{LABELS[v]}</span>
          </label>
        ))}
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
