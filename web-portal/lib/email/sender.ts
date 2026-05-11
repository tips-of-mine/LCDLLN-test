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

function loadTemplate(name: string, vars: Record<string, string>, locale = 'fr'): string {
  const preferred = path.join(process.cwd(), 'email-templates', `${name}.${locale}.html`)
  const fallback = path.join(process.cwd(), 'email-templates', `${name}.fr.html`)
  const templatePath = fs.existsSync(preferred) ? preferred : fallback
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

const SUBJECTS: Record<string, Record<string, string>> = {
  fr: {
    verification: 'Vérification de votre compte — Lune Noire',
    welcome: 'Bienvenue sur Les Chroniques de la Lune Noire',
    passwordReset: 'Réinitialisation de mot de passe — Lune Noire',
    accountConfirmed: 'Compte confirmé — Lune Noire',
    accountDisabled: 'Votre compte Lune Noire a été suspendu',
    parentalValidation: 'Validation parentale requise — Lune Noire',
    emailChange: 'Confirmez votre nouvelle adresse email — Lune Noire',
  },
  en: {
    verification: 'Account verification — Lune Noire',
    welcome: 'Welcome to Les Chroniques de la Lune Noire',
    passwordReset: 'Password reset — Lune Noire',
    accountConfirmed: 'Account confirmed — Lune Noire',
    accountDisabled: 'Your Lune Noire account has been suspended',
    parentalValidation: 'Parental validation required — Lune Noire',
    emailChange: 'Confirm your new email address — Lune Noire',
  },
}

function getSubject(key: keyof typeof SUBJECTS['fr'], locale: string): string {
  return (SUBJECTS[locale] ?? SUBJECTS['fr'])[key]
}

export async function sendVerificationEmail(to: string, token: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('verification', { token }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('verification', locale), html })
}

export async function sendPasswordReset(to: string, link: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('password-reset', { link }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('passwordReset', locale), html })
}

export async function sendWelcome(to: string, login: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('welcome', { login }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('welcome', locale), html })
}

export async function sendAccountConfirmed(to: string, login: string, tagId: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('account-confirmed', { login, tag_id: tagId }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('accountConfirmed', locale), html })
}

export async function sendAccountDisabled(to: string, login: string, reason: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('account-disabled', { login, reason, contact_url: process.env.PORTAL_URL + '/support' }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('accountDisabled', locale), html })
}

export async function sendParentalValidation(to: string, playerLogin: string, validationLink: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('parental-validation', { player_login: playerLogin, validation_link: validationLink }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('parentalValidation', locale), html })
}

export async function sendEmailChange(to: string, login: string, newEmail: string, confirmationLink: string, locale = 'fr'): Promise<void> {
  const html = loadTemplate('email-change', { login, new_email: newEmail, confirmation_link: confirmationLink }, locale)
  await getTransporter().sendMail({ from: getFrom(), to, subject: getSubject('emailChange', locale), html })
}
