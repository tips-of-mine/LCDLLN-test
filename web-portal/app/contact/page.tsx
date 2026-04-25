"use client";

import { useState, type FormEvent } from "react";

export default function ContactPage() {
  const [sent, setSent] = useState(false);

  function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setSent(true);
  }

  return (
    <div className="wp-main">
      <div className="wp-page-header">
        <h1>Contact</h1>
        <p>
          Une question, une suggestion ou besoin d&apos;aide&nbsp;? Utilisez le
          formulaire ci-dessous ou contactez-nous directement.
        </p>
      </div>

      <div className="wp-grid wp-grid-2" style={{ alignItems: "start" }}>
        {/* Contact info */}
        <div style={{ display: "grid", gap: "1rem" }}>
          <div className="wp-card">
            <div style={{ fontSize: 28, marginBottom: 8 }}>✉</div>
            <h3 style={{ margin: "0 0 4px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>E-mail</h3>
            <p style={{ margin: 0, fontSize: 14, color: "var(--ln-muted)" }}>contact@lcdlln.example.com</p>
          </div>

          <div className="wp-card">
            <div style={{ fontSize: 28, marginBottom: 8 }}>💬</div>
            <h3 style={{ margin: "0 0 4px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Discord</h3>
            <p style={{ margin: 0, fontSize: 14, color: "var(--ln-muted)" }}>Rejoignez la communauté sur notre serveur Discord pour des réponses rapides.</p>
          </div>

          <div className="wp-card">
            <div style={{ fontSize: 28, marginBottom: 8 }}>📖</div>
            <h3 style={{ margin: "0 0 4px", fontFamily: "var(--font-display)", color: "var(--ln-accent)" }}>Documentation</h3>
            <p style={{ margin: 0, fontSize: 14, color: "var(--ln-muted)" }}>Consultez le support et la FAQ avant de nous contacter.</p>
          </div>
        </div>

        {/* Contact form */}
        <div className="wp-card">
          {sent ? (
            <div className="wp-alert success">
              <strong>Message envoyé&nbsp;!</strong>
              <p style={{ margin: "4px 0 0", fontSize: 14 }}>
                Nous reviendrons vers vous dans les plus brefs délais.
              </p>
            </div>
          ) : (
            <form onSubmit={handleSubmit} className="form-stack">
              <div className="field">
                <label>Nom / Pseudo</label>
                <input type="text" placeholder="Votre nom ou pseudo" required />
              </div>
              <div className="field">
                <label>E-mail</label>
                <input type="email" placeholder="votre@email.com" required />
              </div>
              <div className="field">
                <label>Sujet</label>
                <select defaultValue="">
                  <option value="" disabled>Choisir un sujet…</option>
                  <option>Question générale</option>
                  <option>Bug ou problème technique</option>
                  <option>Suggestion</option>
                  <option>Partenariat / Presse</option>
                  <option>Autre</option>
                </select>
              </div>
              <div className="field">
                <label>Message</label>
                <textarea rows={5} placeholder="Décrivez votre demande…" required />
              </div>
              <button type="submit" className="btn btn-primary">Envoyer le message</button>
            </form>
          )}
        </div>
      </div>
    </div>
  );
}
