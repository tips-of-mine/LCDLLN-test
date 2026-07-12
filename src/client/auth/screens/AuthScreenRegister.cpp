// AUTH-UI.2 — Écran d'inscription : saisie des informations personnelles, vérification du nom d'utilisateur et envoi du formulaire.
//
// Couche modèle : BuildModel_* peuple RenderModel, Update_* gère les entrées clavier hors ImGui, ImGui* reçoit les actions du renderer.
#include "src/client/auth/AuthUi.h"
#include "src/client/render/AuthUiRenderer.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#	include "src/shared/account/AccountValidation.h"
#	include "src/shared/auth/Argon2Hash.h"
#	include "src/shared/network/AuthRegisterPayloads.h"
#	include "src/shared/network/ErrorPacket.h"
#	include "src/shared/network/ProtocolV1Constants.h"
#	include "src/shared/network/RequestResponseDispatcher.h"
#endif

namespace engine::client
{
#if defined(_WIN32)
namespace
{
	static constexpr std::array<std::string_view, 50> kCountryCodes = {
		"AF", "AL", "AR", "AT", "AU", "BE", "BR", "CA", "CH", "CL", "CN", "CO", "CZ", "DE", "DK", "DZ", "EG", "ES", "FI", "FR", "GB",
		"GR", "HR", "HU", "ID", "IE", "IL", "IN", "IT", "JP", "KR", "LU", "MA", "MX", "NL", "NO", "NZ", "PE", "PL", "PT", "RO", "RU",
		"SA", "SE", "TN", "TR", "UA", "US", "VE", "ZA" };

	bool IsAsciiDigits(std::string_view text)
	{
		for (unsigned char c : text)
		{
			if (c < '0' || c > '9')
			{
				return false;
			}
		}
		return !text.empty();
	}

	std::string BirthCycleDisplayForRegister(std::string_view raw, int defaultV, int minV, int maxV)
	{
		int v = defaultV;
		if (!raw.empty() && IsAsciiDigits(raw))
		{
			v = std::stoi(std::string(raw));
			v = std::clamp(v, minV, maxV);
		}
		return std::string("< ") + std::to_string(v) + " >";
	}

	int CountryIndexOf(std::string_view code)
	{
		for (int i = 0; i < static_cast<int>(kCountryCodes.size()); ++i)
		{
			if (kCountryCodes[static_cast<size_t>(i)] == code)
			{
				return i;
			}
		}
		return 0;
	}

	std::string_view CountryCodeAt(int idx)
	{
		const int n = static_cast<int>(kCountryCodes.size());
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127) // n derive d une table de taille compile-time (garde defensive)
#endif
		if (n == 0)
		{
			return "FR";
		}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
		return kCountryCodes[static_cast<size_t>(((idx % n) + n) % n)];
	}

	void AdjustCountryCycleStr(std::string& code, int delta)
	{
		int idx = CountryIndexOf(code);
		const int n = static_cast<int>(kCountryCodes.size());
		idx = (((idx + delta) % n) + n) % n;
		code = std::string(CountryCodeAt(idx));
	}

	void AdjustBirthCycleStr(std::string& s, int delta, int minV, int maxV)
	{
		int v = minV;
		if (!s.empty() && IsAsciiDigits(s))
		{
			v = std::stoi(s);
			v = std::clamp(v, minV, maxV);
		}
		v += delta;
		v = std::clamp(v, minV, maxV);
		s = std::to_string(v);
	}

	std::string Pad2Int(int v)
	{
		v = std::clamp(v, 0, 99);
		char b[8]{};
		std::snprintf(b, sizeof(b), "%02d", v);
		return std::string(b);
	}

	std::string Pad4YearInt(int y)
	{
		y = std::clamp(y, 1900, 2100);
		char b[8]{};
		std::snprintf(b, sizeof(b), "%04d", y);
		return std::string(b);
	}

	bool WaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			auto events = c->PollEvents();
			for (const auto& ev : events)
			{
				if (ev.type == engine::network::NetClientEventType::Connected)
				{
					return true;
				}
				if (ev.type == engine::network::NetClientEventType::Disconnected)
				{
					return false;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		return false;
	}

	void ApplyMasterTlsConfig(engine::network::NetClient& client, const std::string& fingerprintHex, bool allowInsecure)
	{
		if (!fingerprintHex.empty())
		{
			client.SetExpectedServerFingerprint(fingerprintHex);
		}
		client.SetAllowInsecureDev(allowInsecure);
	}

	const char* NetErrorLabel(engine::network::NetErrorCode c)
	{
		using engine::network::NetErrorCode;
		switch (c)
		{
		case NetErrorCode::OK: return "OK";
		case NetErrorCode::BAD_REQUEST: return "Bad request";
		case NetErrorCode::INVALID_CREDENTIALS: return "Invalid credentials";
		case NetErrorCode::ACCOUNT_NOT_FOUND: return "Account not found";
		case NetErrorCode::ACCOUNT_LOCKED: return "Account locked";
		case NetErrorCode::ALREADY_LOGGED_IN: return "Already logged in";
		case NetErrorCode::LOGIN_ALREADY_TAKEN: return "Login already taken";
		case NetErrorCode::INVALID_EMAIL: return "Invalid email";
		case NetErrorCode::WEAK_PASSWORD: return "Weak password";
		case NetErrorCode::INVALID_LOGIN: return "Invalid login";
		case NetErrorCode::EMAIL_VERIFICATION_REQUIRED: return "Email verification required";
		case NetErrorCode::EMAIL_ALREADY_VERIFIED: return "Email already verified";
		case NetErrorCode::VERIFICATION_CODE_INVALID: return "Verification code invalid";
		case NetErrorCode::REGISTRATION_DISABLED: return "Registration disabled";
		case NetErrorCode::REGISTRATION_INVALID: return "Registration invalid";
		case NetErrorCode::INTERNAL_ERROR: return "Server error";
		case NetErrorCode::TIMEOUT: return "Timeout";
		default: return "Network error";
		}
	}
} // namespace

	/// Peuple le RenderModel avec les champs du formulaire d'inscription (identité, date de naissance, pays, mots de passe).
	void AuthUiPresenter::BuildModel_Register(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.panel.register");
		auto maskedPassword = [this]() -> std::string {
			std::string out;
			AppendPasswordStars(out, m_password.size());
			return out;
		};
		auto maskedConfirm = [this]() -> std::string {
			std::string out;
			AppendPasswordStars(out, m_passwordConfirm.size());
			return out;
		};

		const bool pwdMatch = !m_passwordConfirm.empty() && (m_password == m_passwordConfirm);
		const bool pwdMismatch = !m_passwordConfirm.empty() && (m_password != m_passwordConfirm);

		auto addGridField = [&](std::string label, std::string value, bool active, bool secret, bool cyclePicker, std::string tooltipText,
			int32_t col, int32_t span, int32_t pwdMatchState = 0, std::string inputPlaceholder = {}) {
			RenderField f{};
			f.label = std::move(label);
			f.value = std::move(value);
			f.active = active;
			f.hovered = static_cast<int32_t>(model.fields.size()) == m_hoveredFieldIndex;
			f.secret = secret;
			f.cyclePicker = cyclePicker;
			f.tooltipText = std::move(tooltipText);
			f.gridColumn = col;
			f.gridSpan = span;
			f.passwordMatchState = pwdMatchState;
			f.inputPlaceholder = std::move(inputPlaceholder);
			model.fields.push_back(std::move(f));
		};

		auto monthDisplay = [this](std::string_view raw) -> std::string {
			int v = 1;
			if (!raw.empty())
			{
				try
				{
					v = std::clamp(std::stoi(std::string(raw)), 1, 12);
				}
				catch (...)
				{
					v = 1;
				}
			}
			return std::string("< ") + Tr("month." + std::to_string(v)) + " >";
		};

		auto countryDisplay = [this](std::string_view code) -> std::string {
			if (code.empty())
			{
				return std::string("< ") + Tr("country.FR") + " >";
			}
			return std::string("< ") + Tr("country." + std::string(code)) + " >";
		};

		static const int kDefaultYear = []() -> int {
			const std::time_t t = std::time(nullptr);
			struct std::tm tm{};
#if defined(_WIN32)
			localtime_s(&tm, &t);
#else
			localtime_r(&t, &tm);
#endif
			return 1900 + tm.tm_year - 25;
		}();

		addGridField(Tr("auth.label.login"), m_login, m_activeField == 0, false, false, Tr("auth.tooltip.login"), 0, 1, 0,
			Tr("auth.placeholder.register_login"));
		model.fields.back().usernameCheckState = [this]() -> int32_t {
			switch (m_usernameCheckState)
			{
			case UsernameCheckState::Available: return 2;
			case UsernameCheckState::Taken: return 3;
			case UsernameCheckState::Pending: return 1;
			default: return 0;
			}
		}();
		addGridField(Tr("auth.label.country"), countryDisplay(m_country), m_activeField == 9, false, true, Tr("auth.tooltip.country"), 2, 1);
		addGridField(Tr("auth.label.last_name"), m_lastName, m_activeField == 5, false, false, Tr("auth.tooltip.last_name"), 0, 1);
		addGridField(Tr("auth.label.first_name"), m_firstName, m_activeField == 4, false, false, Tr("auth.tooltip.first_name"), 1, 1);
		addGridField(Tr("common.email"), m_email, m_activeField == 3, false, false, Tr("auth.tooltip.email"), 0, 3, 0,
			Tr("auth.placeholder.register_email"));
		model.fields.back().emailFormatState = m_email.empty()
			? 0
			: (engine::server::ValidateEmail(engine::server::NormaliseEmail(m_email))
				== engine::network::NetErrorCode::OK ? 1 : -1);
		addGridField(Tr("auth.label.birth_day"), BirthCycleDisplayForRegister(m_birthDay, 1, 1, 31), m_activeField == 6, false, true,
			Tr("auth.tooltip.birth_day"), 0, 1);
		addGridField(Tr("auth.label.birth_month"), monthDisplay(m_birthMonth), m_activeField == 7, false, true, Tr("auth.tooltip.birth_month"),
			1, 1);
		addGridField(Tr("auth.label.birth_year"), BirthCycleDisplayForRegister(m_birthYear, kDefaultYear, 1900, 2100), m_activeField == 8,
			false, true, Tr("auth.tooltip.birth_year"), 2, 1);
		addGridField(Tr("auth.label.password"), maskedPassword(), m_activeField == 1, true, false, Tr("auth.tooltip.password"), 0, 3, 0,
			Tr("auth.placeholder.register_password"));
		{
			RenderField& pwField = model.fields.back();
			if (m_password.empty())
			{
				pwField.pwdRuleLength = -1;
				pwField.pwdRuleLetter = -1;
				pwField.pwdRuleDigit  = -1;
			}
			else
			{
				const engine::server::PasswordRuleStatus rules =
					engine::server::EvaluatePasswordRules(m_password);
				pwField.pwdRuleLength = rules.lengthOk ? 1 : 0;
				pwField.pwdRuleLetter = rules.hasLetter ? 1 : 0;
				pwField.pwdRuleDigit  = rules.hasDigit ? 1 : 0;
			}
		}
		addGridField(Tr("auth.label.password_confirm"), maskedConfirm(), m_activeField == 2, true, false, Tr("auth.tooltip.password_confirm"),
			0, 3, pwdMatch ? 1 : (pwdMismatch ? -1 : 0), Tr("auth.placeholder.register_password_confirm"));

		{
			auto makeIntOptions = [](int lo, int hi) -> std::vector<DropdownOption> {
				std::vector<DropdownOption> opts;
				opts.reserve(static_cast<size_t>(hi - lo + 1));
				for (int i = lo; i <= hi; ++i)
				{
					char buf[8]{};
					std::snprintf(buf, sizeof(buf), "%d", i);
					opts.push_back({ std::string(buf), std::string(buf) });
				}
				return opts;
			};

			{
				RenderDropdown dd;
				dd.label = Tr("auth.label.birth_day");
				dd.options = makeIntOptions(1, 31);
				dd.selectedIndex = std::clamp(m_birthDayIndex, 0, 30);
				dd.isOpen = (m_openDropdownIndex == 0);
				model.dropdowns.push_back(dd);
			}
			{
				RenderDropdown dd;
				dd.label = Tr("auth.label.birth_month");
				for (int m = 1; m <= 12; ++m)
				{
					char key[16]{};
					std::snprintf(key, sizeof(key), "month.%d", m);
					dd.options.push_back({ Tr(std::string_view(key)), std::to_string(m) });
				}
				dd.selectedIndex = std::clamp(m_birthMonthIndex, 0, 11);
				dd.isOpen = (m_openDropdownIndex == 1);
				model.dropdowns.push_back(dd);
			}
			{
				RenderDropdown dd;
				dd.label = Tr("auth.label.birth_year");
				dd.options = makeIntOptions(1900, 2010);
				dd.selectedIndex = std::clamp(m_birthYearIndex, 0, 110);
				dd.isOpen = (m_openDropdownIndex == 2);
				model.dropdowns.push_back(dd);
			}
		}

		model.authRegisterPanelBadge = Tr("auth.register.panel_badge");
		model.authRegisterPanelSubtitle = Tr("auth.register.panel_subtitle");
		model.authRegisterEmailHint = Tr("auth.register.email_hint");
		model.authRegisterCrumbLabels.clear();
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.lang"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.account"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.email"));
		model.authRegisterCrumbLabels.push_back(Tr("auth.register.crumb.world"));
		model.authRegisterCrumbCurrent = 1;
		model.authRegisterCountryPick.clear();
		for (std::string_view codeView : kCountryCodes)
		{
			const std::string code(codeView);
			model.authRegisterCountryPick.push_back({ code, Tr(std::string("country.") + code) });
		}
		model.authRegisterFooterChips.clear();
		model.authRegisterFooterChips.push_back({ Tr("auth.footer.chip.enter.key"), Tr("auth.register.footer.validate") });
		model.authRegisterFooterChips.push_back({ Tr("auth.footer.chip.esc.key"), Tr("auth.register.footer.back") });
		model.authRegisterShowErrorsLabel = Tr("auth.register.show_errors");

		{
			RenderAction a{};
			a.labelKey = "auth.register.submit_create";
			a.primary = true;
			a.active = true;
			a.emphasized = true;
			a.hovered = m_hoveredActionIndex == 0;
			model.actions.push_back(std::move(a));
		}
		{
			RenderAction a{};
			a.labelKey = "auth.hint.return_login";
			a.primary = false;
			a.active = true;
			a.emphasized = false;
			a.hovered = m_hoveredActionIndex == 1;
			model.actions.push_back(std::move(a));
		}
	}

	/// Gère la navigation au clavier dans les champs cycliques (date de naissance, pays) du formulaire d'inscription.
	void AuthUiPresenter::Update_Register(engine::platform::Input& input, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool authUiImguiMode)
	{
		if (usingNativeAuth || authUiImguiMode || m_phase != Phase::Register)
		{
			return;
		}
		if ((m_activeField >= 6u && m_activeField <= 8u) || m_activeField == 9u)
		{
			const auto stepCycle = [this](int delta) {
				if (m_activeField == 6)
				{
					AdjustBirthCycleStr(m_birthDay, delta, 1, 31);
				}
				else if (m_activeField == 7)
				{
					AdjustBirthCycleStr(m_birthMonth, delta, 1, 12);
				}
				else if (m_activeField == 8)
				{
					AdjustBirthCycleStr(m_birthYear, delta, 1900, 2100);
				}
				else if (m_activeField == 9)
				{
					AdjustCountryCycleStr(m_country, delta);
				}
			};
			if (input.WasPressed(engine::platform::Key::Up) || input.WasPressed(engine::platform::Key::Right))
			{
				stepCycle(1);
			}
			if (input.WasPressed(engine::platform::Key::Down) || input.WasPressed(engine::platform::Key::Left))
			{
				stepCycle(-1);
			}
		}
	}

	/// Lance le worker asynchrone qui envoie la requête d'inscription au serveur maître.
	void AuthUiPresenter::StartRegisterWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.hash_password_failed"));
			LOG_ERROR(Core, "[AuthUiPresenter] Register aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string locale = CurrentLocale();
		const std::string login = m_login;
		const std::string email = m_email;
		const std::string firstName = m_firstName;
		const std::string lastName = m_lastName;
		const std::string country = m_country;
		int birthY = 0;
		int birthM = 0;
		int birthD = 0;
		try
		{
			birthY = std::stoi(m_birthYear);
			birthM = std::stoi(m_birthMonth);
			birthD = std::stoi(m_birthDay);
		}
		catch (const std::exception& ex)
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.invalid_birth_date"));
			LOG_ERROR(Core, "[AUTH-REG] StartRegisterWorker aborted: birth date parse exception: {}", ex.what());
			return;
		}
		catch (...)
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.invalid_birth_date"));
			LOG_ERROR(Core, "[AUTH-REG] StartRegisterWorker aborted: birth date parse unknown exception");
			return;
		}
		const std::string birthDate = Pad4YearInt(birthY) + "-" + Pad2Int(birthM) + "-" + Pad2Int(birthD);

		LOG_INFO(Core,
			"[AUTH-REG] StartRegisterWorker: host={} port={} timeout_ms={} allow_insecure={} locale_len={} birthDate_iso_len={}", host, port,
			timeoutMs, allowInsecure ? 1 : 0, locale.size(), birthDate.size());

		m_pendingAsyncKind = AsyncKind::Register;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, login, email, firstName, lastName, birthDate, hash, allowInsecure, serverFingerprint,
								 locale, country]() {
			LOG_INFO(Net, "[AUTH-REG] worker thread started (login_len={} email_len={})", login.size(), email.size());
			try
			{
				AsyncResult local{};
				engine::network::NetClient client;
				ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
				LOG_INFO(Net, "[AuthUiPresenter] Register worker: connecting {}:{} (tls_fp_len={})", host, port, serverFingerprint.size());
				client.Connect(host, port);
				if (!WaitConnected(&client, timeoutMs + 2000u))
				{
					local.ready = true;
					local.success = false;
					local.message = "Master connect failed or timeout.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					LOG_ERROR(Net, "[AuthUiPresenter] Register worker: connect FAILED");
					return;
				}
				engine::network::RequestResponseDispatcher disp(&client);
				std::vector<uint8_t> payload =
					engine::network::BuildRegisterRequestPayload(login, email, hash, firstName, lastName, birthDate, {}, locale, country);
				if (payload.empty())
				{
					local.ready = true;
					local.success = false;
					local.message = "REGISTER payload build failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					LOG_ERROR(Net, "[AuthUiPresenter] Register worker: BuildRegisterRequestPayload FAILED");
					client.Disconnect("payload");
					return;
				}

				bool done = false;
				bool ok = false;
				std::string errMsg;
				if (!disp.SendRequest(engine::network::kOpcodeRegisterRequest, payload,
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							done = true;
							if (timeout)
							{
								errMsg = "REGISTER timeout.";
								return;
							}
							auto reg = engine::network::ParseRegisterResponsePayload(pl.data(), pl.size());
							if (reg && reg->success != 0)
							{
								local.accountId = reg->account_id;
								if (!reg->tag_id.empty())
								{
									local.tagId = reg->tag_id;
								}
								ok = true;
								return;
							}
							if (reg && reg->success == 0)
							{
								errMsg = NetErrorLabel(reg->error_code);
								return;
							}
							auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
							if (er)
							{
								errMsg = std::string(NetErrorLabel(er->errorCode))
									+ (er->message.empty() ? "" : (": " + er->message));
							}
							else
							{
								errMsg = "REGISTER response parse failed.";
							}
						},
						timeoutMs))
				{
					local.ready = true;
					local.success = false;
					local.message = "Send REGISTER failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					client.Disconnect("send");
					LOG_ERROR(Net, "[AuthUiPresenter] Register worker: SendRequest FAILED");
					return;
				}

				auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!done && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!done)
				{
					errMsg = "REGISTER timeout.";
				}
				client.Disconnect("register_done");

				local.ready = true;
				local.success = ok;
				local.message = ok ? std::string("Registration OK. Check your email for the verification code.")
								   : (errMsg.empty() ? "REGISTER failed." : errMsg);
				{
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
				}
				LOG_INFO(Net, "[AUTH-REG] Register worker finished (success={} err_nonempty={})", (int)ok, errMsg.empty() ? 0 : 1);
			}
			catch (const std::exception& ex)
			{
				AsyncResult local{};
				local.ready = true;
				local.success = false;
				local.message = std::string("Register worker exception: ") + ex.what();
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				LOG_ERROR(Net, "[AUTH-REG] Register worker std::exception: {}", ex.what());
			}
			catch (...)
			{
				AsyncResult local{};
				local.ready = true;
				local.success = false;
				local.message = "Register worker unknown exception.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				LOG_ERROR(Net, "[AUTH-REG] Register worker non-std exception");
			}
		});
	}

	/// Lance le worker asynchrone de vérification de disponibilité du nom d'utilisateur (debounce côté modèle).
	void AuthUiPresenter::StartUsernameCheckWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(
			std::clamp<int64_t>(cfg.GetInt("client.auth_ui.username_check_timeout_ms", 2000), 500, 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string login = m_usernameLastChecked;
		const uint32_t seq = m_usernameCheckSeq;

		m_pendingAsyncKind = AsyncKind::UsernameCheck;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, login, seq]() {
			AsyncResult local{};
			local.usernameCheckSeq = seq;
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 1000u))
			{
				local.ready = true;
				local.success = false;
				local.message = "UsernameCheck: connect failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				LOG_WARN(Net, "[UsernameCheck] connect FAILED host={} port={}", host, port);
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			const std::vector<uint8_t> payload = engine::network::BuildUsernameAvailableRequestPayload(login, seq);
			bool done = false;
			uint8_t available = 0;
			if (!disp.SendRequest(engine::network::kOpcodeUsernameAvailableRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = !timeout;
						if (!timeout)
						{
							auto resp = engine::network::ParseUsernameAvailableResponsePayload(pl.data(), pl.size());
							if (resp)
							{
								available = resp->available;
							}
						}
					},
					timeoutMs))
			{
				local.ready = true;
				local.success = false;
				local.message = "UsernameCheck: SendRequest failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				client.Disconnect("send_fail");
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			client.Disconnect("username_check_done");
			local.ready = true;
			local.success = done;
			local.usernameAvailable = done ? available : 0;
			local.usernameCheckSeq = seq;
			local.message = done ? "" : "UsernameCheck: timeout.";
			{
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[UsernameCheck] done seq={} available={}", seq, (int)available);
		});
	}

	/// Construit un instantané des champs d'inscription pour les exposer au renderer ImGui.
	AuthUiPresenter::RegisterFieldsMirrorForImGui AuthUiPresenter::BuildRegisterFieldsMirrorForImGui() const
	{
		RegisterFieldsMirrorForImGui s{};
		s.login = m_login;
		s.email = m_email;
		s.firstName = m_firstName;
		s.lastName = m_lastName;
		s.countryIso2 = m_country;
		s.birthDayIndex = m_birthDayIndex;
		s.birthMonthIndex = m_birthMonthIndex;
		s.birthYearIndex = m_birthYearIndex;
		return s;
	}

	/// Reçoit le formulaire complet depuis ImGui et déclenche la soumission de l'inscription.
	void AuthUiPresenter::ImGuiSubmitRegister(const engine::core::Config& cfg, const RegisterImGuiSubmit& form)
	{
		if (m_phase != Phase::Register)
		{
			return;
		}
		auto pull = [](std::string& dst, const char* p) { dst = p ? std::string(p) : std::string(); };
		pull(m_login, form.login);
		pull(m_email, form.email);
		pull(m_password, form.password);
		pull(m_passwordConfirm, form.passwordConfirm);
		pull(m_firstName, form.firstName);
		pull(m_lastName, form.lastName);
		pull(m_birthDay, form.birthDay);
		pull(m_birthMonth, form.birthMonth);
		pull(m_birthYear, form.birthYear);
		pull(m_country, form.countryIso2);
		if (m_country.size() >= 2u)
		{
			m_country[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(m_country[0])));
			m_country[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(m_country[1])));
		}
		if (m_country.size() > 2u)
		{
			m_country.resize(2u);
		}
		SubmitCurrentPhase(cfg);
	}

	/// Force l'affichage de l'écran d'erreur de validation lorsque des champs sont incomplets ou invalides.
	void AuthUiPresenter::ImGuiRegisterPreviewValidationErrors(const engine::core::Config& cfg)
	{
		(void)cfg;
		if (m_phase != Phase::Register)
		{
			return;
		}
		EnterAuthErrorPhase(Phase::Register, Tr("auth.error.enter_register_fields"));
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::BuildModel_Register(RenderModel&) const {}

	void AuthUiPresenter::Update_Register(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool) {}

#endif
} // namespace engine::client
