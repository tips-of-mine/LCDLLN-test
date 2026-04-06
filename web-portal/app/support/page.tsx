"use client";

import { useState } from "react";
import Link from "next/link";

const faqItems = [
  {
    q: "Comment créer un compte ?",
    a: "Les comptes sont créés directement dans le jeu lors de votre première connexion. Une fois votre compte créé, vous pouvez vous connecter au portail web avec les mêmes identifiants.",
  },
  {
    q: "Comment récupérer mon mot de passe ?",
    a: "Rendez-vous sur la page de récupération de mot de passe. Vous devrez vérifier votre identité en répondant aux questions secrètes que vous avez configurées dans votre profil de récupération. Un lien de réinitialisation sera envoyé par e-mail.",
  },
  {
    q: "Comment configurer mon profil de récupération ?",
    a: "Connectez-vous à votre espace joueur, puis accédez à la section « Profil de récupération ». Renseignez vos informations personnelles et définissez 3 questions secrètes. Ces informations seront utilisées pour vérifier votre identité en cas de perte de mot de passe.",
  },
  {
    q: "Que sont les exploits ?",
    a: "Les exploits sont des succès que vous débloquez en jeu en accomplissant des objectifs spécifiques. Certains sont publics (visibles dès le départ), d'autres sont secrets (révélés uniquement après déblocage). Signaler des bugs vous permet aussi de débloquer des exploits spéciaux par paliers (5, 10, 15… jusqu'à 100).",
  },
  {
    q: "Comment signaler un bug ?",
    a: "Utilisez la page « Signaler un bug » accessible depuis le menu. Le formulaire nécessite d'être connecté. Chaque signalement est enregistré et contribue à votre progression vers les exploits de signalement.",
  },
  {
    q: "Comment accepter les CGU ?",
    a: "Lorsqu'une nouvelle version des CGU est publiée, vous serez invité à l'accepter soit en jeu, soit depuis votre espace joueur sur le portail. L'historique de vos acceptations est consultable à tout moment.",
  },
  {
    q: "Les données des autres joueurs sont-elles visibles ?",
    a: "Non. Le portail applique une isolation stricte par compte : vous ne pouvez accéder qu'à vos propres données. Même les administrateurs ont un accès en lecture seule aux profils.",
  },
  {
    q: "Comment contacter l'équipe ?",
    a: "Utilisez la page de contact, rejoignez le serveur Discord ou envoyez un e-mail. Pour les problèmes techniques urgents, le Discord est le canal le plus rapide.",
  },
];

function AccordionItem({ q, a }: { q: string; a: string }) {
  const [open, setOpen] = useState(false);

  return (
    <div className="accordion-item">
      <button
        className="accordion-trigger"
        onClick={() => setOpen((v) => !v)}
        aria-expanded={open}
      >
        <span>{q}</span>
        <span className="chevron">{open ? "\u25B2" : "\u25BC"}</span>
      </button>
      {open && (
        <div className="accordion-content animate-fade">
          {a}
        </div>
      )}
    </div>
  );
}

export default function SupportPage() {
  return (
    <>
      <div className="page-header">
        <h1>Support &amp; FAQ</h1>
        <p>
          Trouvez des réponses à vos questions ci-dessous. Si votre problème
          persiste, n&apos;hésitez pas à nous contacter.
        </p>
      </div>

      {/* Quick links */}
      <div className="card-grid-3">
        <Link href="/password-recovery" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", margin: 0, padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#128274;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Mot de passe oublié</div>
            <p className="text-xs mt-1 mb-0">Récupérer l&apos;accès à votre compte</p>
          </div>
        </Link>
        <Link href="/bugs" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", margin: 0, padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#128027;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Signaler un bug</div>
            <p className="text-xs mt-1 mb-0">Aidez-nous à améliorer le jeu</p>
          </div>
        </Link>
        <Link href="/contact" style={{ textDecoration: "none" }}>
          <div className="card card-interactive" style={{ textAlign: "center", margin: 0, padding: "1.5rem 1rem" }}>
            <div style={{ fontSize: "1.5rem", marginBottom: "0.5rem" }}>&#9993;</div>
            <div style={{ fontWeight: 600, color: "var(--fg)", fontSize: "0.9rem" }}>Contacter l&apos;équipe</div>
            <p className="text-xs mt-1 mb-0">Nous sommes là pour vous</p>
          </div>
        </Link>
      </div>

      {/* FAQ */}
      <h2>Questions fréquentes</h2>
      <p className="section-subtitle">
        Cliquez sur une question pour afficher la réponse.
      </p>

      <div className="accordion">
        {faqItems.map((item) => (
          <AccordionItem key={item.q} q={item.q} a={item.a} />
        ))}
      </div>
    </>
  );
}
