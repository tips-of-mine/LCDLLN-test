// Helper centralisé pour les variables d'environnement obligatoires.
// L'objectif : un message d'erreur clair et explicite quand une variable
// critique manque, plutôt qu'un fallback silencieux vers une valeur de dev.
//
// Usage :
//   const secret = requireEnv("AUTH_SECRET");
//
// Lance : Error("Missing required environment variable: AUTH_SECRET")

export function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value === null || value.trim().length === 0) {
    throw new Error(
      `Missing required environment variable: ${name}. ` +
      `Set it in the deployment environment (Docker .env, systemd EnvironmentFile, etc.) before starting the web portal.`
    );
  }
  return value;
}
