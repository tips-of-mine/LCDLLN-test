// Round-trip tests pour les payloads AdminCommand (195/196) + edge cases
// + reject-short / reject-extra. Pas de framework, asserts simples.
// Le binaire est enregistre dans CMakeLists.txt comme cible CTest
// `admin_command_payloads_tests`.

#include "src/shared/network/AdminCommandPayloads.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace engine::network::admin;

namespace
{
	/// Round-trip d'une Request standard "/sky moon 7".
	void TestRequestRoundTripSkyMoon()
	{
		AdminCommandRequest src;
		src.command = "/sky moon";
		src.args.push_back("7");

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);

		AdminCommandRequest dst;
		assert(ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		assert(dst.command == src.command);
		assert(dst.args.size() == 1);
		assert(dst.args[0] == "7");
		std::puts("[OK] TestRequestRoundTripSkyMoon");
	}

	/// Round-trip d'une Request sans arguments (commande nue).
	void TestRequestRoundTripEmptyArgs()
	{
		AdminCommandRequest src;
		src.command = "/who";

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);

		AdminCommandRequest dst;
		assert(ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		assert(dst.command == "/who");
		assert(dst.args.empty());
		std::puts("[OK] TestRequestRoundTripEmptyArgs");
	}

	/// Round-trip d'une Request avec plusieurs arguments.
	void TestRequestRoundTripMultiArgs()
	{
		AdminCommandRequest src;
		src.command = "/ban";
		src.args.push_back("hacker_42");
		src.args.push_back("griefing");
		src.args.push_back("permanent");

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);

		AdminCommandRequest dst;
		assert(ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		assert(dst.command == "/ban");
		assert(dst.args.size() == 3);
		assert(dst.args[0] == "hacker_42");
		assert(dst.args[1] == "griefing");
		assert(dst.args[2] == "permanent");
		std::puts("[OK] TestRequestRoundTripMultiArgs");
	}

	/// Round-trip d'une Request avec une commande tres longue (proche du cap).
	void TestRequestRoundTripLongCommand()
	{
		AdminCommandRequest src;
		src.command = std::string("/long ") + std::string(500, 'x');
		src.args.push_back(std::string(1024, 'y'));

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);

		AdminCommandRequest dst;
		assert(ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		assert(dst.command == src.command);
		assert(dst.args.size() == 1);
		assert(dst.args[0] == src.args[0]);
		std::puts("[OK] TestRequestRoundTripLongCommand");
	}

	/// Round-trip d'une Request avec command vide (cas degenere mais valide).
	void TestRequestRoundTripEmptyCommand()
	{
		AdminCommandRequest src;
		src.command = "";

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);
		// Format minimal : 2 octets command_len(0) + 2 octets args_count(0) = 4 octets.
		assert(buf.size() == 4);

		AdminCommandRequest dst;
		assert(ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		assert(dst.command.empty());
		assert(dst.args.empty());
		std::puts("[OK] TestRequestRoundTripEmptyCommand");
	}

	/// Reject-short : Request tronquee avant la fin doit etre rejetee.
	void TestRequestRejectShort()
	{
		AdminCommandRequest src;
		src.command = "/sky moon";
		src.args.push_back("7");

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);

		// Tronque les 3 derniers octets (mange "7" de l'argument).
		buf.resize(buf.size() - 3);

		AdminCommandRequest dst;
		assert(!ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestRequestRejectShort");
	}

	/// Reject-extra : un octet en trop a la fin doit etre rejete.
	void TestRequestRejectExtra()
	{
		AdminCommandRequest src;
		src.command = "/sky moon";
		src.args.push_back("7");

		std::vector<uint8_t> buf;
		BuildAdminCommandRequestPayload(src, buf);
		buf.push_back(0xAAu);

		AdminCommandRequest dst;
		assert(!ParseAdminCommandRequestPayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestRequestRejectExtra");
	}

	/// Round-trip d'une Response Ok avec result key=value et message.
	void TestResponseRoundTripOk()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Ok;
		src.command = "/sky moon";
		src.result.push_back("phase=7");
		src.result.push_back("illumination=1.000");
		src.message = "OK";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == AdminCommandStatus::Ok);
		assert(dst.command == "/sky moon");
		assert(dst.result.size() == 2);
		assert(dst.result[0] == "phase=7");
		assert(dst.result[1] == "illumination=1.000");
		assert(dst.message == "OK");
		std::puts("[OK] TestResponseRoundTripOk");
	}

	/// Round-trip d'une Response Denied avec message d'erreur localise.
	void TestResponseRoundTripDenied()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Denied;
		src.command = "/sky moon";
		src.message = "Permission refusee : role administrator requis";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == AdminCommandStatus::Denied);
		assert(dst.command == "/sky moon");
		assert(dst.result.empty());
		assert(dst.message == "Permission refusee : role administrator requis");
		std::puts("[OK] TestResponseRoundTripDenied");
	}

	/// Round-trip pour CHAQUE valeur de status enum (couverture exhaustive).
	void TestResponseAllStatusValues()
	{
		const AdminCommandStatus statuses[] = {
			AdminCommandStatus::Ok,
			AdminCommandStatus::Unauthorized,
			AdminCommandStatus::Denied,
			AdminCommandStatus::UnknownCommand,
			AdminCommandStatus::InvalidArgs,
			AdminCommandStatus::ServerError,
		};

		for (auto s : statuses)
		{
			AdminCommandResponse src;
			src.status = s;
			src.command = "/cmd";
			src.message = "msg";

			std::vector<uint8_t> buf;
			BuildAdminCommandResponsePayload(src, buf);

			AdminCommandResponse dst;
			assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
			assert(dst.status == s);
			assert(dst.command == "/cmd");
			assert(dst.message == "msg");
		}
		std::puts("[OK] TestResponseAllStatusValues");
	}

	/// Round-trip d'une Response Unauthorized vide (cas le plus frequent
	/// quand la session est invalide -> on echo juste le command).
	void TestResponseRoundTripUnauthorized()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Unauthorized;
		src.command = "/sky moon";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == AdminCommandStatus::Unauthorized);
		assert(dst.command == "/sky moon");
		assert(dst.message.empty());
		std::puts("[OK] TestResponseRoundTripUnauthorized");
	}

	/// Round-trip d'une Response UnknownCommand.
	void TestResponseRoundTripUnknownCommand()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::UnknownCommand;
		src.command = "/inexistant";
		src.message = "Commande inconnue";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == AdminCommandStatus::UnknownCommand);
		assert(dst.command == "/inexistant");
		assert(dst.message == "Commande inconnue");
		std::puts("[OK] TestResponseRoundTripUnknownCommand");
	}

	/// Round-trip d'une Response InvalidArgs.
	void TestResponseRoundTripInvalidArgs()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::InvalidArgs;
		src.command = "/sky moon";
		src.message = "phase doit etre dans 0..15";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == AdminCommandStatus::InvalidArgs);
		std::puts("[OK] TestResponseRoundTripInvalidArgs");
	}

	/// Reject-short : Response tronquee avant la fin doit etre rejetee.
	void TestResponseRejectShort()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Ok;
		src.command = "/cmd";
		src.message = "msg";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);
		buf.resize(buf.size() - 1);

		AdminCommandResponse dst;
		assert(!ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestResponseRejectShort");
	}

	/// Reject-extra : un octet en trop a la fin doit etre rejete.
	void TestResponseRejectExtra()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Ok;
		src.command = "/cmd";
		src.message = "msg";

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);
		buf.push_back(0xAA);

		AdminCommandResponse dst;
		assert(!ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestResponseRejectExtra");
	}

	/// Reject-short : payload completement vide -> doit etre rejete (manque le u8 status).
	void TestResponseRejectEmpty()
	{
		std::vector<uint8_t> buf;
		AdminCommandResponse dst;
		assert(!ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestResponseRejectEmpty");
	}

	/// Edge case : Response avec result list contenant des chaines vides.
	void TestResponseEmptyStringInList()
	{
		AdminCommandResponse src;
		src.status = AdminCommandStatus::Ok;
		src.command = "/cmd";
		src.result.push_back("");
		src.result.push_back("non_empty");
		src.result.push_back("");

		std::vector<uint8_t> buf;
		BuildAdminCommandResponsePayload(src, buf);

		AdminCommandResponse dst;
		assert(ParseAdminCommandResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.result.size() == 3);
		assert(dst.result[0].empty());
		assert(dst.result[1] == "non_empty");
		assert(dst.result[2].empty());
		std::puts("[OK] TestResponseEmptyStringInList");
	}
}

int main()
{
	TestRequestRoundTripSkyMoon();
	TestRequestRoundTripEmptyArgs();
	TestRequestRoundTripMultiArgs();
	TestRequestRoundTripLongCommand();
	TestRequestRoundTripEmptyCommand();
	TestRequestRejectShort();
	TestRequestRejectExtra();
	TestResponseRoundTripOk();
	TestResponseRoundTripDenied();
	TestResponseAllStatusValues();
	TestResponseRoundTripUnauthorized();
	TestResponseRoundTripUnknownCommand();
	TestResponseRoundTripInvalidArgs();
	TestResponseRejectShort();
	TestResponseRejectExtra();
	TestResponseRejectEmpty();
	TestResponseEmptyStringInList();
	std::puts("[ALL OK] AdminCommandPayloadsTests");
	return 0;
}
