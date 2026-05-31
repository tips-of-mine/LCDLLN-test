// Questions secrètes du profil de récupération.
//
// Liste figée présentée à l'utilisateur via des listes déroulantes dans
// `RecoveryProfileForm`. Le stockage reste une simple chaîne (`question`),
// donc l'API et la base ne dépendent pas de cette liste : on peut l'enrichir
// librement sans migration. Les profils enregistrés avant l'introduction des
// listes (texte libre) sont gérés par `availableQuestions` (voir plus bas).

export const SECRET_QUESTIONS: readonly string[] = [
  "Quel est le nom de ton premier animal de compagnie ?",
  "Quel est le nom de jeune fille de ta mère ?",
  "Dans quelle ville es-tu né(e) ?",
  "Quel était ton surnom d'enfance ?",
  "Quel est le prénom de ton meilleur ami d'enfance ?",
  "Quel est le nom de ta première école ?",
  "Quel est le nom de la rue où tu vivais enfant ?",
  "Quel est le prénom de ton professeur préféré ?",
  "Quelle était ta matière préférée à l'école ?",
  "Quel est le titre de ton film préféré ?",
  "Quel est ton plat préféré ?",
  "Quel est le nom de ton groupe de musique ou artiste préféré ?",
  "Quelle est la marque/le modèle de ta première voiture ?",
  "Dans quelle ville as-tu eu ton premier emploi ?",
  "Quel est le prénom de ton premier amour ?",
  "Quel est le deuxième prénom de ton père ?",
  "Quel est le nom de la ville où tes parents se sont rencontrés ?",
  "Quel est ton numéro de département ou code postal d'enfance ?",
  "Quel est le prénom de ton parrain ou de ta marraine ?",
  "Quelle est la destination de tes premières vacances dont tu te souviens ?",
];

/**
 * Calcule les questions sélectionnables pour le slot `currentIndex`, en
 * appliquant l'exclusion mutuelle : une question déjà choisie dans un AUTRE
 * slot n'est pas proposée ici. La valeur actuelle du slot courant reste
 * toujours présente (sinon le `<select>` perdrait sa sélection).
 *
 * Compatibilité ascendante : si la valeur du slot courant est une question en
 * texte libre absente de `SECRET_QUESTIONS` (profil enregistré avant cette
 * fonctionnalité), elle est ajoutée en tête pour ne pas écraser la donnée.
 *
 * @param allQuestions liste de référence (typiquement `SECRET_QUESTIONS`).
 * @param selected     les valeurs des 3 slots (chaîne vide = non choisi).
 * @param currentIndex index du slot pour lequel on calcule les options.
 * @returns la liste ordonnée des questions affichables dans ce slot.
 */
export function availableQuestions(
  allQuestions: readonly string[],
  selected: readonly string[],
  currentIndex: number,
): string[] {
  const current = selected[currentIndex] ?? "";
  const takenByOthers = new Set(
    selected.filter((value, index) => index !== currentIndex && value.trim().length > 0),
  );

  const options = allQuestions.filter((question) => !takenByOthers.has(question));

  // Valeur libre/héritée non présente dans la liste de référence : la conserver.
  if (current.trim().length > 0 && !allQuestions.includes(current)) {
    return [current, ...options];
  }
  return options;
}
