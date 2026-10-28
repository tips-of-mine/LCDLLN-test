# FIX.1 — HUD en mode dégradé (pas de session de jeu UDP)

Origine : session du 28/10/2026. Le shard n'annonçait pas son endpoint UDP : le client
envoyait ses Hello au port TCP, ne recevait jamais de Welcome, et le HUD perdait la barre de
vie, la ressource, la barre d'action… sans aucune explication pour le joueur.

Cause côté client : ces éléments ne sont dessinés qu'avec des données du shard (stats,
classe, compétences). Sans Welcome, rien n'arrive, donc rien n'est dessiné. Le client
arrêtait en plus de renvoyer son Hello après 15 essais (15 s).

## Corrections (client uniquement)

- **Bandeau d'état** (haut de l'écran) après 2 s sans Welcome :
  « Connexion au serveur de jeu en cours... », puis, une fois les renvois rapides épuisés,
  « Serveur de jeu injoignable - nouvelle tentative toutes les 5 s (verifiez votre
  connexion) » sur fond rouge. Il disparaît dès le Welcome.
- **Jauge de vie** : sans stats, une jauge grisée « PV : en attente du serveur » occupe sa
  place au lieu de disparaître.
- **Barre d'action** : toujours dessinée ; sans kit, ses 10 cases sont vides avec leurs
  touches. Aucun envoi n'est fait tant qu'il n'y a pas de session (`ServerClientId() == 0`).
- **Renvoi du Hello** : 15 renvois à 1 s, puis toutes les 5 s pendant ~5 min (durée de vie
  de la clé de session côté shard). Un shard redémarré ou un pare-feu corrigé en cours de
  partie est rejoint sans relancer le client. Logs : un WARN par minute, puis un ERROR à
  l'abandon.

Textes ASCII sans accents : la police du jeu (Windlass) n'a pas tous les glyphes accentués.

Le correctif de fond (endpoint UDP annoncé et mis à jour) est côté serveur : voir FIX.3.
