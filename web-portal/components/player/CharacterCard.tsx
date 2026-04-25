"use client";

import { useState } from "react";
import type { CharacterWithStats } from "@/lib/playerCharacters";

function formatPlayTime(seconds: number): string {
  if (seconds === 0) return "Aucune session enregistrée";
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (h === 0) return `${m}m`;
  return `${h}h ${m}m`;
}

type Props = {
  character: CharacterWithStats;
  onDeleted: (id: number) => void;
};

export function CharacterCard({ character, onDeleted }: Props) {
  const [step, setStep] = useState<"idle" | "confirm1" | "confirm2" | "deleting">("idle");
  const [error, setError] = useState("");

  async function handleDelete() {
    setStep("deleting");
    setError("");
    const res = await fetch(`/api/player/characters/${character.id}`, { method: "DELETE" });
    const data = (await res.json()) as { ok: boolean; message?: string };
    if (data.ok) {
      onDeleted(character.id);
    } else {
      setError(data.message ?? "Erreur lors de la suppression.");
      setStep("idle");
    }
  }

  return (
    <div className="wp-card" style={{ marginBottom: "1rem" }}>
      <div style={{ display: "flex", alignItems: "flex-start", justifyContent: "space-between", gap: "1rem" }}>
        <div>
          <div style={{ fontFamily: "var(--font-display)", fontSize: 16, color: "var(--ln-accent)" }}>
            {character.name}
          </div>
          <div style={{ fontSize: 12, color: "var(--ln-muted)", marginTop: 4 }}>
            Slot {character.slot + 1}
            {character.serverName && ` · ${character.serverName}`}
          </div>
          <div style={{ fontSize: 13, color: "var(--ln-text)", marginTop: 6 }}>
            Temps joué : <strong>{formatPlayTime(character.totalPlaySeconds)}</strong>
          </div>
          {character.lastSeen && (
            <div style={{ fontSize: 11, color: "var(--ln-muted)", marginTop: 2 }}>
              Dernière connexion : {new Date(character.lastSeen).toLocaleDateString("fr-FR")}
            </div>
          )}
        </div>

        <div>
          {step === "idle" && (
            <button
              className="btn btn-ghost"
              style={{ color: "var(--ln-danger)", fontSize: 12 }}
              onClick={() => setStep("confirm1")}
            >
              Supprimer
            </button>
          )}
          {step === "confirm1" && (
            <div style={{ textAlign: "right" }}>
              <p style={{ fontSize: 12, color: "var(--ln-warning)", marginBottom: 6 }}>
                Supprimer <strong>{character.name}</strong> ?
              </p>
              <div style={{ display: "flex", gap: "0.5rem", justifyContent: "flex-end" }}>
                <button className="btn btn-ghost" onClick={() => setStep("idle")}>Annuler</button>
                <button
                  className="btn btn-secondary"
                  style={{ background: "rgba(200,50,50,0.15)", color: "var(--ln-danger)" }}
                  onClick={() => setStep("confirm2")}
                >
                  Continuer
                </button>
              </div>
            </div>
          )}
          {step === "confirm2" && (
            <div style={{ textAlign: "right" }}>
              <p style={{ fontSize: 12, color: "var(--ln-danger)", marginBottom: 6 }}>
                Action irréversible. Confirmez la suppression définitive ?
              </p>
              <div style={{ display: "flex", gap: "0.5rem", justifyContent: "flex-end" }}>
                <button className="btn btn-ghost" onClick={() => setStep("idle")}>Annuler</button>
                <button
                  className="btn btn-secondary"
                  style={{ background: "rgba(200,50,50,0.25)", color: "var(--ln-danger)" }}
                  onClick={handleDelete}
                >
                  Supprimer définitivement
                </button>
              </div>
            </div>
          )}
          {step === "deleting" && (
            <span style={{ fontSize: 12, color: "var(--ln-muted)" }}>Suppression…</span>
          )}
        </div>
      </div>
      {error && <p style={{ fontSize: 12, color: "var(--ln-danger)", marginTop: 8 }}>{error}</p>}
    </div>
  );
}
