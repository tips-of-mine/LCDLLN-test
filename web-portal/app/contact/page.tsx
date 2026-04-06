"use client";

import { useState, type FormEvent } from "react";

export default function ContactPage() {
  const [sent, setSent] = useState(false);

  function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setSent(true);
  }

  return (
    <>
      <div className="page-header">
        <h1>Contact</h1>
        <p>
          Une question, une suggestion ou besoin d&apos;aide ? Utilisez le formulaire
          ci-dessous ou contactez-nous directement.
        </p>
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "1.5rem", alignItems: "start" }}>
        {/* Contact info */}
        <div style={{ display: "grid", gap: "1rem" }}>
          <div className="card" style={{ margin: 0 }}>
            <div className="feature-icon" style={{ marginBottom: "0.5rem" }}>&#9993;</div>
            <h3 className="mt-0 mb-1">E-mail</h3>
            <p className="text-sm mb-0">contact@lcdlln.example.com</p>
          </div>

          <div className="card" style={{ margin: 0 }}>
            <div className="feature-icon purple" style={{ marginBottom: "0.5rem" }}>&#128172;</div>
            <h3 className="mt-0 mb-1">Discord</h3>
            <p className="text-sm mb-0">Rejoignez la communauté sur notre serveur Discord pour des réponses rapides.</p>
          </div>

          <div className="card" style={{ margin: 0 }}>
            <div className="feature-icon orange" style={{ marginBottom: "0.5rem" }}>&#128214;</div>
            <h3 className="mt-0 mb-1">Documentation</h3>
            <p className="text-sm mb-0">Consultez le support et la FAQ avant de nous contacter.</p>
          </div>
        </div>

        {/* Contact form */}
        <div className="card" style={{ margin: 0 }}>
          {sent ? (
            <div className="success-box">
              <strong>Message envoyé !</strong>
              <p className="text-sm mt-1" style={{ color: "inherit", margin: 0 }}>
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
              <button type="submit" className="btn-primary">Envoyer le message</button>
            </form>
          )}
        </div>
      </div>

      <style>{`
        @media (max-width: 768px) {
          div[style*="grid-template-columns: 1fr 1fr"] {
            grid-template-columns: 1fr !important;
          }
        }
      `}</style>
    </>
  );
}
