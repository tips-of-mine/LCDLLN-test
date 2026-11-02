#pragma once
// ADM.0 — Exécuteur des commandes de jeu (MJ) du shard : déclaration interne.
//
// GmCommandExecutor vit le temps d'UNE commande (ServerApp::ExecuteGmCommand) ;
// il est ami de ServerApp et agit directement sur l'état du tick. Les handlers
// sont répartis par ticket :
//   - ServerAppGm.cpp          : point d'entrée, table de dispatch, ADM.0 ;
//   - ServerAppGmMovement.cpp  : ADM.1 (déplacement) ;
//   - … un fichier par catégorie dans les tickets suivants.
// En-tête INTERNE au shard : à n'inclure que depuis ces fichiers.

#include "src/shared/server_bootstrap/ServerApp.h"

#include "src/shardd/gameplay/gm/GmCommandCatalog.h"
#include "src/shared/admin/GmCommandText.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::server
{
	using engine::network::GmCommandStatus;

	/// Exécuteur d'UNE commande MJ (durée de vie : l'appel à ServerApp::ExecuteGmCommand).
	/// Ami de ServerApp : lit et modifie l'état du tick directement.
	struct GmCommandExecutor
	{
		ServerApp& app;
		const engine::network::GmCommandRequestPayload& request;
		engine::network::GmCommandResultPayload& out;
		/// Arguments découpés (guillemets pris en charge, cf. GmCommandText.h).
		std::vector<std::string> args;
		/// Rôle de l'appelant tel que transmis par le master.
		gm::GmRole role = gm::GmRole::Player;
		/// Personnage de l'appelant s'il est connecté à CE shard (nullptr sinon).
		ConnectedClient* actor = nullptr;

		using Handler = void (GmCommandExecutor::*)();

		// ---- Outils communs -------------------------------------------------

		/// Ajoute une ligne de réponse (affichée telle quelle dans le chat de l'appelant).
		void Say(std::string line) { out.lines.push_back(std::move(line)); }

		/// Fixe le statut d'échec et ajoute le message d'explication.
		void Fail(GmCommandStatus status, std::string line)
		{
			out.status = static_cast<uint8_t>(status);
			Say(std::move(line));
		}

		/// Erreur de syntaxe : rappelle la forme attendue (\p syntax, ex. « /tp <x> <z> »).
		void Usage(std::string_view syntax)
		{
			Fail(GmCommandStatus::Usage, "Syntaxe : " + std::string(syntax) + " — détail : /help /" + request.command);
		}

		/// L'appelant doit être en jeu sur ce shard (commandes qui agissent sur lui).
		/// \return false (et message) sinon.
		bool RequireActor()
		{
			if (actor != nullptr)
				return true;
			Fail(GmCommandStatus::NotFound, "Votre personnage n'est pas en jeu sur ce shard.");
			return false;
		}

		/// Rôle de l'appelant au moins \p minRole ?
		bool HasRole(gm::GmRole minRole) const
		{
			return static_cast<uint8_t>(role) >= static_cast<uint8_t>(minRole);
		}

		/// Nom affiché d'un joueur (nom du personnage, sinon « P<id> »).
		static std::string NameOf(const ConnectedClient& client)
		{
			return client.characterName.empty() ? "P" + std::to_string(client.clientId) : client.characterName;
		}

		/// Joueur connecté à ce shard par nom de personnage (sans casse) ou « P<id> ».
		/// Un « @ » de tête est ignoré (« @Hubert »). nullptr si introuvable.
		ConnectedClient* FindPlayer(std::string_view name)
		{
			name = engine::admin::Trim(name);
			if (!name.empty() && name.front() == '@')
				name.remove_prefix(1);
			if (name.empty())
				return nullptr;
			for (ConnectedClient& c : app.m_clients)
			{
				if (!c.characterName.empty() && engine::admin::EqualsIgnoreCase(c.characterName, name))
					return &c;
			}
			for (ConnectedClient& c : app.m_clients)
			{
				if (engine::admin::EqualsIgnoreCase("P" + std::to_string(c.clientId), name))
					return &c;
			}
			return nullptr;
		}

		/// Cible facultative : \p args[index] s'il existe (joueur nommé), sinon l'appelant.
		/// \return nullptr (et message) si le joueur nommé est introuvable ou si
		///         l'appelant n'est pas en jeu.
		ConnectedClient* TargetOrSelf(size_t index)
		{
			if (index < args.size())
			{
				ConnectedClient* target = FindPlayer(args[index]);
				if (target == nullptr)
					Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[index]);
				return target;
			}
			return RequireActor() ? actor : nullptr;
		}

		/// Prévient \p target (message système dans son chat) s'il n'est pas l'appelant.
		void NotifyTarget(ConnectedClient& target, std::string_view text)
		{
			if (&target == actor)
				return;
			app.SendChatSystemNotice(target, text);
		}

		// ---- ADM.0 — socle ---------------------------------------------------

		/// /shardinfo — état du shard (ServerAppGm.cpp).
		void CmdShardInfo();

		// ---- ADM.1 — déplacement (ServerAppGmMovement.cpp) --------------------
		/// Hauteur envoyée quand le shard ne connaît pas le sol (il n'a pas le terrain) :
		/// le client recale le personnage au sol (max(y, sol + 0,9 m)) et la hauteur
		/// de sa commande suivante est reprise telle quelle (trustNextHeight).
		static constexpr float kGroundSnapY = -100000.0f;

		/// Téléporte \p client en (\p x, \p y, \p z) (m, coordonnées globales).
		/// \param groundSnap  vrai : hauteur inconnue, recalage au sol par le client.
		/// \param saveBack    vrai : mémorise la position de départ pour /back.
		/// \return false (et message) si la destination est hors du monde généré.
		/// Effets : ForcePosition (nouvelle époque de mouvement), anti-triche remis à
		/// zéro, grille d'intérêt et région mises à jour. Thread de tick.
		bool Teleport(ConnectedClient& client, float x, float y, float z, bool groundSnap, bool saveBack);
		/// Position actuelle de \p client sous forme mémorisable.
		static gm::GmSavedPosition SavedPositionOf(const ConnectedClient& client);
		/// Annuaire des lieux, chargé au premier appel (places.json + villes de départ).
		const gm::GmPlaceDirectory& Places();
		/// Recalcule la vitesse autoritaire de \p client et lui pousse GmStateUpdate.
		void ApplyGmState(ConnectedClient& client);
		/// Distance horizontale entre deux joueurs (m).
		static float DistanceXZ(const ConnectedClient& a, const ConnectedClient& b);
		/// « 1250.4, 37.0, -820.9 » (m, une décimale).
		static std::string FormatPosition(float x, float y, float z);

		/// /tp <x> <z> | <x> <y> <z> | <lieu> | @<joueur>
		void CmdTeleport();
		/// /goto <joueur> — rejoindre un joueur.
		void CmdGoto();
		/// /summon <joueur> — faire venir un joueur.
		void CmdSummon();
		/// /recall <joueur> — renvoyer un joueur là où il était avant /summon.
		void CmdRecall();
		/// /back — revenir à la position d'avant le dernier déplacement MJ.
		void CmdBack();
		/// /places [filtre] [page] — lieux connus du shard.
		void CmdPlaces();
		/// /mark <nom> — mémoriser la position courante.
		void CmdMark();
		/// /marks — lister ses favoris.
		void CmdMarks();
		/// /gomark <nom> — aller à un favori.
		void CmdGoMark();
		/// /unmark <nom> — supprimer un favori.
		void CmdUnmark();
		/// /forward <m> — avancer tout droit.
		void CmdForward();
		/// /up <m> — monter.
		void CmdUp();
		/// /down <m> — descendre.
		void CmdDown();
		/// /ground — se recaler au sol.
		void CmdGround();
		/// /spawn — revenir au point d'entrée en monde.
		void CmdSpawn();
		/// /speed <×> | reset — multiplicateur de vitesse.
		void CmdSpeed();
		/// /fly [on|off] — vol libre.
		void CmdFly();
		/// /freeze <joueur> — geler un joueur.
		void CmdFreeze();
		/// /unfreeze <joueur> — dégeler un joueur.
		void CmdUnfreeze();
		/// Commun à /up et /down : déplacement vertical signé.
		void VerticalMove(float sign);

		// ---- ADM.2 — informations (ServerAppGmInfo.cpp) ------------------------
		/// Cellule « N003_W009 » d'une position (ou « monde historique »).
		std::string CellLabel(float x, float z) const;
		/// « Loup gris niv. 4 #1234 » : archétype, niveau, entité.
		std::string MobLabel(const MobEntity& mob) const;
		/// /pos [joueur] — position, orientation, cellule, région, lieu proche.
		void CmdPos();
		/// /where — lieu le plus proche et direction, région.
		void CmdWhere();
		/// /info <joueur> — fiche d'un joueur connecté.
		void CmdInfo();
		/// /target — fiche de la dernière cible de l'appelant.
		void CmdTarget();
		/// /zone — résumé de la zone courante.
		void CmdZone();
		/// /nearby [rayon] — entités proches.
		void CmdNearby();

		// ---- ADM.3 — personnage (ServerAppGmCharacter.cpp) ----------------------
		/// Pousse les PV et l'état mort de \p client à tous les clients intéressés
		/// (CombatEvent « résurrection » : sans ligne de journal de combat).
		void SyncHealth(ConnectedClient& client);
		/// Lit « <n> [joueur] » ou « <joueur> <n> » ; \return la cible (nullptr + message
		/// sinon). \param allowNegative accepte un nombre négatif.
		ConnectedClient* ParseAmountAndTarget(int64_t& outAmount, std::string_view syntax, bool allowNegative);
		/// Fixe le niveau (borné aux tables), XP à 0, stats recalculées, soin complet,
		/// fiche / XP / compétences poussées, sauvegarde.
		void SetLevel(ConnectedClient& target, uint32_t level);
		/// Bascule (ou fixe avec on/off) un pouvoir booléen de GmPlayerState.
		/// \param field membre visé (invincible, invisible, noCost).
		void TogglePower(bool gm::GmPlayerState::* field, const char* label, const char* syntax);
		void CmdLevel();       ///< /level <n> [joueur]
		void CmdXp();          ///< /xp <points> [joueur]
		void CmdHeal();        ///< /heal [joueur]
		void CmdRevive();      ///< /revive [joueur]
		void CmdKill();        ///< /kill [joueur] (sans argument : la cible)
		void CmdHp();          ///< /hp <n> [joueur]
		void CmdMana();        ///< /mana <n> [joueur]
		void CmdInvincible();  ///< /invincible [on|off] [joueur]
		void CmdInvisible();   ///< /invisible [on|off] [joueur]
		void CmdNoCost();      ///< /nocost [on|off] [joueur]
		void CmdCooldown();    ///< /cooldown [joueur]
		void CmdLearn();       ///< /learn <compétence>|all [joueur]
		void CmdUnlearn();     ///< /unlearn <compétence> [joueur]
		void CmdResetSkills(); ///< /resetskills [joueur]
		void CmdProfession();  ///< /profession <métier> <niveau> [joueur]

		// ---- ADM.4 — objets et argent (ServerAppGmItems.cpp) -------------------
		/// Objets du catalogue désignés par \p query (numéro exact, sinon nom exact,
		/// début ou partie du nom, sans casse ni accents), au plus \p maxResults.
		std::vector<const engine::items::ItemDefinition*> FindItems(std::string_view query, size_t maxResults) const;
		/// « Épée longue #1203 ».
		std::string ItemLabel(uint32_t itemId) const;
		/// Lit « <objet> [quantité] [joueur] » ; \return false (et message) si invalide.
		bool ParseItemCommand(std::string_view syntax, const engine::items::ItemDefinition*& outItem,
			uint32_t& outQuantity, ConnectedClient*& outTarget);
		void CmdAddItem();    ///< /additem <objet> [qté] [joueur]
		void CmdRemoveItem(); ///< /removeitem <objet> [qté] [joueur]
		void CmdMoney();      ///< /money <±montant> [monnaie] [joueur]
		void CmdInventory();  ///< /inventory [joueur]
		void CmdEquipment();  ///< /equipment [joueur]
		void CmdClearBag();   ///< /clearbag [joueur] confirm
		void CmdItemSearch(); ///< /itemsearch <mot|n°>

		// ---- ADM.5 — monture (ServerAppGmAppearance.cpp) ------------------------
		void CmdMount();      ///< /mount [on|off|<vitesse>] [joueur]
		void CmdDismount();   ///< /dismount [joueur]

		/// Point d'entrée : catalogue, rôle, cible par défaut, handler, journal.
		void Run();
	};

}
