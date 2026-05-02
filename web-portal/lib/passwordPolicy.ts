// Politique commune de force du mot de passe : utilisée à l'inscription/reset
// (lib/passwordRecovery.ts) ET au changement connecté (api/player/password).

const MIN_LENGTH = 8;
const MAX_LENGTH = 256;

export function verifyPasswordStrength(password: string): string | null {
  if (typeof password !== "string") return "Mot de passe invalide.";
  if (password.length < MIN_LENGTH) {
    return `Le mot de passe doit contenir au moins ${MIN_LENGTH} caracteres.`;
  }
  if (password.length > MAX_LENGTH) {
    return "Le mot de passe depasse la longueur maximale autorisee.";
  }
  if (!/[A-Za-z]/.test(password) || !/\d/.test(password)) {
    return "Le mot de passe doit contenir au moins une lettre et un chiffre.";
  }
  return null;
}
