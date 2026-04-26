import nodemailer from 'nodemailer'
import fs from 'node:fs'
import path from 'node:path'

interface SmtpLocalConfig {
  smtp: {
    host: string
    port: number
    user?: string
    password?: string
    from?: string
    starttls?: number
    timeout_sec?: number
  }
}

function loadSmtpLocalJson(): SmtpLocalConfig['smtp'] | null {
  const jsonPath = path.join(process.cwd(), '..', 'config', 'smtp.local.json')
  if (!fs.existsSync(jsonPath)) return null
  try {
    const raw = fs.readFileSync(jsonPath, 'utf-8')
    const parsed: SmtpLocalConfig = JSON.parse(raw)
    return parsed.smtp ?? null
  } catch {
    return null
  }
}

function getTransporter() {
  const local = process.env.SMTP_HOST ? null : loadSmtpLocalJson()
  const host = process.env.SMTP_HOST ?? local?.host
  const port = Number(process.env.SMTP_PORT ?? local?.port ?? 587)
  const secure = process.env.SMTP_SECURE === 'true' // port 587 + starttls → secure stays false
  const user = process.env.SMTP_USER ?? local?.user
  const pass = process.env.SMTP_PASS ?? local?.password

  return nodemailer.createTransport({
    host,
    port,
    secure,
    auth: user ? { user, pass } : undefined,
  })
}

function loadTemplate(filename: string, vars: Record<string, string>): string {
  const templatePath = path.join(process.cwd(), 'email-templates', filename)
  let html = fs.readFileSync(templatePath, 'utf-8')
  for (const [key, value] of Object.entries(vars)) {
    html = html.replaceAll(`{{${key}}}`, value)
  }
  return html
}

function getFrom(): string {
  if (process.env.SMTP_FROM) return process.env.SMTP_FROM
  const local = loadSmtpLocalJson()
  if (local?.from) return local.from
  return '"Les Chroniques de la Lune Noire" <noreply@lune-noire.fr>'
}

export async function sendVerificationEmail(to: string, token: string): Promise<void> {
  const html = loadTemplate('verification.html', { token })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Vérification de votre compte — Lune Noire', html })
}

export async function sendPasswordReset(to: string, link: string): Promise<void> {
  const html = loadTemplate('password-reset.html', { link })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Réinitialisation de mot de passe — Lune Noire', html })
}

export async function sendWelcome(to: string, login: string): Promise<void> {
  const html = loadTemplate('welcome.html', { login })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Bienvenue sur Les Chroniques de la Lune Noire', html })
}

export async function sendAccountConfirmed(to: string, login: string): Promise<void> {
  const html = loadTemplate('account-confirmed.html', { login })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Compte confirmé — Lune Noire', html })
}

export async function sendAccountDisabled(to: string, login: string, reason: string): Promise<void> {
  const html = loadTemplate('account-disabled.html', { login, reason, contact_url: process.env.PORTAL_URL + '/support' })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Votre compte Lune Noire a été suspendu', html })
}

export async function sendParentalValidation(to: string, playerLogin: string, validationLink: string): Promise<void> {
  const html = loadTemplate('parental-validation.html', { player_login: playerLogin, validation_link: validationLink })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Validation parentale requise — Lune Noire', html })
}

export async function sendEmailChange(to: string, login: string, newEmail: string, confirmationLink: string): Promise<void> {
  const html = loadTemplate('email-change.html', { login, new_email: newEmail, confirmation_link: confirmationLink })
  await getTransporter().sendMail({ from: getFrom(),to, subject: 'Confirmez votre nouvelle adresse email — Lune Noire', html })
}
