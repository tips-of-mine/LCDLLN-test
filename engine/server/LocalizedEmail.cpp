#include "engine/server/LocalizedEmail.h"

#include <cctype>

namespace engine::server
{
	namespace
	{
		std::string_view TrimLower(std::string_view s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
				s.remove_prefix(1);
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
				s.remove_suffix(1);
			return s;
		}

		std::string LowerCopy(std::string_view s)
		{
			std::string out;
			out.reserve(s.size());
			for (unsigned char c : s)
				out.push_back(static_cast<char>(std::tolower(c)));
			return out;
		}
	} // namespace

	AccountEmailLocale ParseAccountEmailLocale(std::string_view tag)
	{
		tag = TrimLower(tag);
		if (tag.empty())
			return AccountEmailLocale::English;

		std::string t = LowerCopy(tag);
		const size_t dash = t.find('-');
		if (dash != std::string::npos)
			t.resize(dash);

		if (t == "en" || t == "eng")
			return AccountEmailLocale::English;
		if (t == "fr" || t == "fra")
			return AccountEmailLocale::French;
		if (t == "es" || t == "spa")
			return AccountEmailLocale::Spanish;
		if (t == "de" || t == "deu" || t == "ger")
			return AccountEmailLocale::German;
		if (t == "pt" || t == "por")
			return AccountEmailLocale::Portuguese;
		if (t == "it" || t == "ita")
			return AccountEmailLocale::Italian;

		return AccountEmailLocale::English;
	}

	void BuildVerificationEmail(AccountEmailLocale loc, const std::string& code, std::string& outSubject, std::string& outBody)
	{
		switch (loc)
		{
		case AccountEmailLocale::French:
			outSubject = "Code de vérification";
			outBody    = "Votre code de vérification est : " + code + "\r\n\r\n"
			             "Saisissez ce code dans le client du jeu pour confirmer votre adresse e-mail.\r\n"
			             "Le code est valable 24 heures.\r\n";
			break;
		case AccountEmailLocale::Spanish:
			outSubject = "Código de verificación";
			outBody    = "Su código de verificación es: " + code + "\r\n\r\n"
			             "Introduzca este código en el cliente del juego para verificar su correo.\r\n"
			             "El código es válido durante 24 horas.\r\n";
			break;
		case AccountEmailLocale::German:
			outSubject = "Bestätigungscode";
			outBody    = "Ihr Bestätigungscode lautet: " + code + "\r\n\r\n"
			             "Geben Sie diesen Code im Spielclient ein, um Ihre E-Mail-Adresse zu bestätigen.\r\n"
			             "Der Code ist 24 Stunden gültig.\r\n";
			break;
		case AccountEmailLocale::Portuguese:
			outSubject = "Código de verificação";
			outBody    = "O seu código de verificação é: " + code + "\r\n\r\n"
			             "Introduza este código no cliente do jogo para verificar o seu e-mail.\r\n"
			             "O código é válido por 24 horas.\r\n";
			break;
		case AccountEmailLocale::Italian:
			outSubject = "Codice di verifica";
			outBody    = "Il codice di verifica è: " + code + "\r\n\r\n"
			             "Inserite questo codice nel client di gioco per verificare l'indirizzo e-mail.\r\n"
			             "Il codice è valido per 24 ore.\r\n";
			break;
		case AccountEmailLocale::English:
		default:
			outSubject = "Email verification code";
			outBody    = "Your verification code is: " + code + "\r\n\r\n"
			             "Enter this code in the game client to verify your email address.\r\n"
			             "The code is valid for 24 hours.\r\n";
			break;
		}
	}

	void BuildPasswordResetEmail(AccountEmailLocale loc, const std::string& resetUrl, std::string& outSubject, std::string& outBody)
	{
		switch (loc)
		{
		case AccountEmailLocale::French:
			outSubject = "Réinitialisation du mot de passe";
			outBody    = "Vous avez demandé la réinitialisation du mot de passe de votre compte.\r\n\r\n"
			             "Cliquez sur le lien ci-dessous pour en définir un nouveau (valide 1 heure) :\r\n"
			             + resetUrl + "\r\n\r\n"
			             "Si vous n'êtes pas à l'origine de cette demande, ignorez cet e-mail.\r\n";
			break;
		case AccountEmailLocale::Spanish:
			outSubject = "Restablecimiento de contraseña";
			outBody    = "Ha solicitado restablecer la contraseña de su cuenta.\r\n\r\n"
			             "Haga clic en el siguiente enlace para establecer una nueva (válida 1 hora):\r\n"
			             + resetUrl + "\r\n\r\n"
			             "Si no realizó esta solicitud, ignore este mensaje.\r\n";
			break;
		case AccountEmailLocale::German:
			outSubject = "Passwort zurücksetzen";
			outBody    = "Sie haben die Zurücksetzung Ihres Passworts angefordert.\r\n\r\n"
			             "Klicken Sie auf den folgenden Link, um ein neues Passwort festzulegen (1 Stunde gültig):\r\n"
			             + resetUrl + "\r\n\r\n"
			             "Wenn Sie diese Anfrage nicht gestellt haben, ignorieren Sie diese E-Mail.\r\n";
			break;
		case AccountEmailLocale::Portuguese:
			outSubject = "Repor palavra-passe";
			outBody    = "Pediu a reposição da palavra-passe da sua conta.\r\n\r\n"
			             "Clique na ligação abaixo para definir uma nova (válida por 1 hora):\r\n"
			             + resetUrl + "\r\n\r\n"
			             "Se não fez este pedido, ignore este e-mail.\r\n";
			break;
		case AccountEmailLocale::Italian:
			outSubject = "Reimpostazione della password";
			outBody    = "È stata richiesta la reimpostazione della password del suo account.\r\n\r\n"
			             "Faccia clic sul collegamento seguente per impostarne una nuova (valida 1 ora):\r\n"
			             + resetUrl + "\r\n\r\n"
			             "Se non ha richiesto questa operazione, ignori questa e-mail.\r\n";
			break;
		case AccountEmailLocale::English:
		default:
			outSubject = "Password reset request";
			outBody    = "You requested a password reset for your account.\r\n\r\n"
			             "Click the link below to set a new password (valid for 1 hour):\r\n"
			             + resetUrl + "\r\n\r\n"
			             "If you did not request this, please ignore this email.\r\n";
			break;
		}
	}

	void BuildTermsAcceptanceEmail(AccountEmailLocale loc, const std::string& versionLabel, std::string& outSubject, std::string& outBody)
	{
		switch (loc)
		{
		case AccountEmailLocale::French:
			outSubject = "Confirmation d'acceptation des CGU (" + versionLabel + ")";
			outBody    = "Nous confirmons l'enregistrement de votre acceptation des conditions générales d'utilisation, version "
			             + versionLabel + ", à la date et l'heure du serveur.\r\n\r\n"
			             "Conservez cet e-mail comme preuve de votre consentement.\r\n";
			break;
		case AccountEmailLocale::Spanish:
			outSubject = "Confirmación de aceptación de términos (" + versionLabel + ")";
			outBody    = "Confirmamos el registro de su aceptación de los términos y condiciones, versión "
			             + versionLabel + ", con fecha y hora del servidor.\r\n\r\n"
			             "Conserve este correo como constancia de su consentimiento.\r\n";
			break;
		case AccountEmailLocale::German:
			outSubject = "Bestätigung der Nutzungsbedingungen (" + versionLabel + ")";
			outBody    = "Wir bestätigen die Speicherung Ihrer Annahme der Nutzungsbedingungen, Version "
			             + versionLabel + ", zum Serverzeitpunkt.\r\n\r\n"
			             "Bewahren Sie diese E-Mail als Nachweis Ihrer Zustimmung auf.\r\n";
			break;
		case AccountEmailLocale::Portuguese:
			outSubject = "Confirmação de aceitação dos termos (" + versionLabel + ")";
			outBody    = "Confirmamos o registo da sua aceitação dos termos e condições, versão "
			             + versionLabel + ", na data e hora do servidor.\r\n\r\n"
			             "Guarde este e-mail como comprovativo do seu consentimento.\r\n";
			break;
		case AccountEmailLocale::Italian:
			outSubject = "Conferma accettazione dei termini (" + versionLabel + ")";
			outBody    = "Confermiamo la registrazione dell'accettazione dei termini e condizioni, versione "
			             + versionLabel + ", alla data e ora del server.\r\n\r\n"
			             "Conservi questa e-mail come prova del suo consenso.\r\n";
			break;
		case AccountEmailLocale::English:
		default:
			outSubject = "Terms of service acceptance (" + versionLabel + ")";
			outBody    = "We confirm that your acceptance of the terms of service, version "
			             + versionLabel + ", has been recorded at the server date and time.\r\n\r\n"
			             "Please keep this email as proof of your consent.\r\n";
			break;
		}
	}
}
