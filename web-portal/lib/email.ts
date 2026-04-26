import nodemailer from 'nodemailer'
import fs from 'node:fs'
import path from 'node:path'

function getTransporter() {
  return nodemailer.createTransport({
    host: process.env.SMTP_HOST,
    port: Number(process.env.SMTP_PORT ?? 587),
    secure: process.env.SMTP_SECURE === 'true',
    auth: process.env.SMTP_USER ? {
      user: process.env.SMTP_USER,
      pass: process.env.SMTP_PASS,
    } : undefined,
  })
}

function loadTemplate(filename: string, vars: Record<string, string>): string {
  const templatePath = path.join(process.cwd(), '..', 'design', 'lune-noire-design-system', 'ui_kits', 'email', filename)
  let html = fs.readFileSync(templatePath, 'utf-8')
  for (const [key, value] of Object.entries(vars)) {
    html = html.replaceAll(`{{${key}}}`, value)
  }
  return html
}

const FROM = process.env.SMTP_FROM ?? '"Les Chroniques de la Lune Noire" <noreply@lune-noire.fr>'

export async function sendVerificationEmail(to: string, token: string): Promise<void> {
  const html = loadTemplate('verification.html', { token })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Vérification de votre compte — Lune Noire', html })
}

export async function sendPasswordReset(to: string, link: string): Promise<void> {
  const html = loadTemplate('password-reset.html', { link })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Réinitialisation de mot de passe — Lune Noire', html })
}

export async function sendWelcome(to: string, login: string): Promise<void> {
  const html = loadTemplate('welcome.html', { login })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Bienvenue sur Les Chroniques de la Lune Noire', html })
}

export async function sendAccountConfirmed(to: string, login: string): Promise<void> {
  const html = loadTemplate('account-confirmed.html', { login })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Compte confirmé — Lune Noire', html })
}

export async function sendAccountDisabled(to: string, login: string, reason: string): Promise<void> {
  const html = loadTemplate('account-disabled.html', { login, reason, contact_url: process.env.PORTAL_URL + '/support' })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Votre compte Lune Noire a été suspendu', html })
}

export async function sendParentalValidation(to: string, playerLogin: string, validationLink: string): Promise<void> {
  const html = loadTemplate('parental-validation.html', { player_login: playerLogin, validation_link: validationLink })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Validation parentale requise — Lune Noire', html })
}

export async function sendEmailChange(to: string, login: string, newEmail: string, confirmationLink: string): Promise<void> {
  const html = loadTemplate('email-change.html', { login, new_email: newEmail, confirmation_link: confirmationLink })
  await getTransporter().sendMail({ from: FROM, to, subject: 'Confirmez votre nouvelle adresse email — Lune Noire', html })
}
