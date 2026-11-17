// AuthUiPresenter — mise à jour par frame (Update : saisie, navigation, clics), modèle de rendu (BuildRenderModel, BuildPanelText, GetVisualState), Échap et retour à l'écran de connexion.
//
// Fonctions membres déplacées telles quelles depuis AuthUiPresenterCore.cpp
// (aucun changement de comportement). Documentation : AuthUi.h ; utilitaires
// communs : AuthUiPresenterInternal.h.

#include "src/client/auth/AuthUiPresenterInternal.h"

namespace engine::client
{
	using namespace engine::client::auth_detail;

	void AuthUiPresenter::Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
		const engine::core::Config& cfg)
	{
		LOG_DEBUG(Core, "[DIAG-AUTH] Update enter this={} initialized={} flowComplete={} authEnabled={}",
			(void*)this, m_initialized ? 1 : 0, m_flowComplete ? 1 : 0, m_authEnabled ? 1 : 0);
		m_usingNativeAuthScreen = false;
		LOG_DEBUG(Core, "[DIAG-AUTH] before SetAuthScreenState");
		window.SetAuthScreenState({});
		LOG_DEBUG(Core, "[DIAG-AUTH] after SetAuthScreenState");
		if (!m_initialized || m_flowComplete || !m_authEnabled)
			return;

		LOG_DEBUG(Core, "[DIAG-AUTH] before PollAsyncResult m_asyncMutex={}", (void*)m_asyncMutex.get());
		PollAsyncResult(cfg);
		LOG_DEBUG(Core, "[DIAG-AUTH] after PollAsyncResult");
		if (m_flowComplete)
			return;

		LOG_DEBUG(Core, "[DIAG-AUTH] before statusProbe check phase={}", static_cast<int>(m_phase));
		// Rafraîchissement de la sonde /status : 20 s. Ce timer pilote la fraîcheur
		// du compteur joueurs affiché sur l'écran de connexion et « Choisir un serveur ».
		// 120 s donnait un affichage trop figé ; 20 s reste léger (GET HTTP uniquement
		// pendant le flux d'auth, jamais en jeu) tout en restant « fluide ».
		constexpr float kStatusProbeIntervalSeconds = 20.0f;
		const bool shouldProbeStatus = !m_masterAvailabilityUrl.empty() && !m_flowComplete && m_phase != Phase::Submitting;
		const bool statusProbeInFlight = m_worker.joinable() && m_pendingAsyncKind == AsyncKind::StatusProbe;
		m_authAvailabilityChecking = statusProbeInFlight;
		if (m_authAvailabilityChecking)
			m_authLogoRotationRad += deltaSeconds * 2.8f;

		if (shouldProbeStatus)
		{
			// Démarrage immédiat dès le début de l'écran d'authentification, puis toutes les 2 minutes.
			if (!m_statusProbeInitialized && !m_worker.joinable())
			{
				LOG_INFO(Core, "[StatusProbe] Update: première sonde planifiée (écran auth)");
				StartStatusProbeWorker(cfg);
				m_statusProbeInitialized = true;
				m_statusPollTimer = 0.f;
			}
			else if (m_statusProbeInitialized && !m_worker.joinable())
			{
				m_statusPollTimer += deltaSeconds;
				if (m_statusPollTimer >= kStatusProbeIntervalSeconds)
				{
					LOG_INFO(Core, "[StatusProbe] Update: rafraîchissement périodique (timer ≥ {} s)", kStatusProbeIntervalSeconds);
					StartStatusProbeWorker(cfg);
					m_statusPollTimer = 0.f;
				}
			}
		}
		else if (!statusProbeInFlight)
		{
			m_authAvailabilityPollTimer = 0.f;
			m_authLogoRotationRad = 0.f;
			m_statusProbeInitialized = false;
			m_statusPollTimer = 0.f;
		}

		if (m_phase == Phase::Submitting)
		{
			window.SetAuthScreenState({});
			UpdateWindowTitle(window);
			return;
		}

		// Plan C : tick debounce disponibilité username.
		// Le worker n'est lancé que si aucun worker critique n'est actif (Register/Login/etc.).
		if (m_usernameDebounceTimer > 0.0)
		{
			m_usernameDebounceTimer -= static_cast<double>(deltaSeconds);
			if (m_usernameDebounceTimer <= 0.0)
			{
				m_usernameDebounceTimer = 0.0;
				if (m_phase == Phase::Register && m_login.size() >= 3
					&& !m_worker.joinable() && !m_asyncResult.ready)
				{
					m_usernameCheckState = UsernameCheckState::Pending;
					m_usernameLastChecked = m_login;
					StartUsernameCheckWorker(cfg);
					LOG_DEBUG(Core, "[UsernameCheck] worker lancé pour login='{}' seq={}", m_usernameLastChecked, m_usernameCheckSeq);
				}
			}
		}

		const bool usingNativeAuth = false;
		const bool authUiImguiMode = cfg.GetBool("render.auth_ui.imgui.enabled", false);

		auto currentField = [this]() -> std::string* {
			switch (m_phase)
			{
			case Phase::Login:
				if (m_activeField == 0)
					return &m_login;
				if (m_activeField == 1)
					return &m_password;
				return nullptr;
			case Phase::Register:
				switch (m_activeField)
				{
				case 0: return &m_login;
				case 1: return &m_password;
				case 2: return &m_passwordConfirm;
				case 3: return &m_email;
				case 4: return &m_firstName;
				case 5: return &m_lastName;
				case 6: return &m_birthDay;
				case 7: return &m_birthMonth;
				case 8: return &m_birthYear;
				default: return &m_country;
				}
			case Phase::VerifyEmail:
			case Phase::EmailConfirmationPending:
				return &m_verifyCode;
			case Phase::ForgotPassword:
				return &m_email;
			case Phase::Terms:
				return nullptr;
			case Phase::CharacterCreate:
				return &m_characterName;
			case Phase::LanguageSelectionFirstRun:
			case Phase::LanguageOptions:
			default:
				return nullptr;
			}
		};

		auto applyPrimaryAction = [&]()
		{
			if (m_phase == Phase::LanguageSelectionFirstRun)
			{
				ApplyLocaleSelection(true);
			}
			else if (m_phase == Phase::LanguageOptions)
			{
				CommitLanguageOptionsMenuApply(cfg);
			}
			else if (m_phase == Phase::Terms)
			{
				if (!m_termsScrolledToBottom)
					return;
				if (!m_termsAcknowledgeChecked)
				{
					m_termsAcknowledgeChecked = true;
					return;
				}
				SubmitCurrentPhase(cfg);
			}
			else
			{
				if (m_phase == Phase::Register)
				{
					LOG_INFO(Core, "[AUTH-REG] UI primary action (submit button / Enter) -> SubmitCurrentPhase");
				}
				SubmitCurrentPhase(cfg);
			}
		};

		if (!usingNativeAuth)
		{
			RenderModel model = BuildRenderModel();
			if (model.visible && m_viewportW > 0 && m_viewportH > 0 && !authUiImguiMode)
			{
				const VkExtent2D ext{ m_viewportW, m_viewportH };
				const VisualState vsLayout = GetVisualState();
				const engine::render::AuthUiLayoutMetrics lay = engine::render::BuildAuthUiLayoutMetrics(ext, vsLayout, model);
				const int32_t panelY = lay.panelY;
				const int32_t panelH = lay.panelH;
				const int32_t contentX = lay.contentX;
				const int32_t contentW = lay.contentW;
				const int32_t topOffset = lay.topOffset;
				const int32_t fieldStep = lay.fieldRowStepPx;
				const int32_t bodyScale = engine::render::AuthUiClassicTextScaleFromPanelW(lay.panelW);
				const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
				const bool centeredLanguageSelection = vsLayout.languageSelection || vsLayout.languageOptions;
				const int32_t bitmapBodyLinePitch = centeredLanguageSelection
					? std::max(36, bodyLineStep + 16)
					: std::max(28, bodyLineStep + 10);
				const int32_t approxTtfLh = static_cast<int32_t>(std::lround(static_cast<float>(bodyLineStep) * 1.28f));
				const int32_t bodyLinePitch = std::max(bitmapBodyLinePitch,
					approxTtfLh + (centeredLanguageSelection ? 14 : 10));
				const int32_t afterFieldsGap = centeredLanguageSelection ? 34 : 18;
				const int32_t fieldCount = static_cast<int32_t>(model.fields.size());
				std::vector<int32_t> fieldLogicalRow(static_cast<size_t>(fieldCount), 0);
				if (fieldCount > 0)
				{
					int32_t row = -1;
					int32_t lastCol = engine::render::kAuthUiGridColumns;
					for (int32_t fi = 0; fi < fieldCount; ++fi)
					{
						const auto& f = model.fields[static_cast<size_t>(fi)];
						if (f.gridColumn < 0 || f.gridColumn <= lastCol)
							++row;
						lastCol = (f.gridColumn < 0) ? engine::render::kAuthUiGridColumns : f.gridColumn;
						fieldLogicalRow[static_cast<size_t>(fi)] = row;
					}
				}
				const int32_t logicalRowCount =
					fieldCount > 0 ? fieldLogicalRow[static_cast<size_t>(fieldCount - 1)] + 1 : 0;
				const int32_t bodyStartY = panelY + topOffset + logicalRowCount * fieldStep + afterFieldsGap;
				const int32_t mx = input.MouseX();
				const int32_t my = input.MouseY();
				m_hoveredFieldIndex = -1;
				m_hoveredFieldInfoIndex = -1;
				m_hoveredBodyLineIndex = -1;
				m_hoveredActionIndex = -1;
				m_hoveredLanguageCardIndex = -1;

				auto contains = [](int32_t px, int32_t py, int32_t x, int32_t y, int32_t rw, int32_t rh)
				{
					return px >= x && py >= y && px < (x + rw) && py < (y + rh);
				};

				// --- Calcul géométrie dropdowns date (phase Inscription) ---
				// Les dropdowns jour/mois/année sont à la ligne logique 3 de la grille d'inscription,
				// colonnes 0, 1, 2. On remplit dd.x/y/w/h ici (même logique que AuthGlyphPass).
				if (m_phase == Phase::Register && model.dropdowns.size() == 3)
				{
					// Les champs birthDay/Month/Year ont les indices 5, 6, 7 dans la liste des champs.
					// Leurs colonnes respectives : 0, 1, 2.
					constexpr int kBirthFieldBase = 5;
					constexpr int kDropdownH = engine::render::kAuthUiFieldBoxHeightPx;
					for (int di = 0; di < 3 && (kBirthFieldBase + di) < fieldCount; ++di)
					{
						const int32_t logRow = fieldLogicalRow[static_cast<size_t>(kBirthFieldBase + di)];
						const int32_t ddY = panelY + topOffset + logRow * fieldStep;
						int32_t ddX = contentX;
						int32_t ddW = contentW;
						engine::render::AuthUiGridFieldGeometry(contentX, contentW, di, 1, ddX, ddW);
						auto& dd = model.dropdowns[static_cast<size_t>(di)];
						dd.x = ddX;
						dd.y = ddY;
						dd.w = ddW;
						dd.h = kDropdownH;
					}
				}

				for (size_t i = 0; i < model.fields.size(); ++i)
				{
					const auto& fldHit = model.fields[i];
					const int32_t rowY = panelY + topOffset + fieldLogicalRow[i] * fieldStep;
					int32_t fx = contentX;
					int32_t fw = contentW;
					if (fldHit.gridColumn >= 0)
					{
						engine::render::AuthUiGridFieldGeometry(
						    contentX, contentW, fldHit.gridColumn, fldHit.gridSpan, fx, fw);
					}
					if (contains(mx, my, fx, rowY, fw, engine::render::kAuthUiFieldBoxHeightPx))
					{
						m_hoveredFieldIndex = static_cast<int32_t>(i);
						break;
					}
				}
				const int32_t smallScaleHit = std::max(2, bodyScale - 1);
				const int32_t labelAboveFieldPxHit = smallScaleHit * 11 + 6;
				for (int32_t fi = 0; fi < static_cast<int32_t>(model.fields.size()); ++fi)
				{
					const RenderField& fld = model.fields[static_cast<size_t>(fi)];
					if (fld.tooltipText.empty())
					{
						continue;
					}
					const int32_t rowY = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(fi)] * fieldStep;
					int32_t fx = contentX;
					int32_t fw = contentW;
					if (fld.gridColumn >= 0)
					{
						engine::render::AuthUiGridFieldGeometry(
						    contentX, contentW, fld.gridColumn, fld.gridSpan, fx, fw);
					}
					const int32_t ix = std::max(fx + 10, fx + fw - 36);
					const int32_t iy = rowY - labelAboveFieldPxHit;
					if (contains(mx, my, ix, iy, 18, 18))
					{
						m_hoveredFieldInfoIndex = fi;
						break;
					}
				}

				if (m_phase == Phase::Register && input.MouseScrollDelta() != 0)
				{
					int32_t f = m_hoveredFieldIndex;
					if ((f < 6 || f > 8) && f != 9)
					{
						f = static_cast<int32_t>(m_activeField);
					}
					if ((f >= 6 && f <= 8) || f == 9)
					{
						const int d = input.MouseScrollDelta() > 0 ? 1 : -1;
						if (f == 6)
						{
							AdjustBirthCycle(m_birthDay, d, 1, 31);
						}
						else if (f == 7)
						{
							AdjustBirthCycle(m_birthMonth, d, 1, 12);
						}
						else if (f == 8)
						{
							AdjustBirthCycle(m_birthYear, d, 1900, 2100);
						}
						else if (f == 9)
							AdjustCountryCycle(m_country, d);
					}
				}

				for (int32_t localIdx = 0; localIdx < model.visibleBodyLineCount; ++localIdx)
				{
					const int32_t bodyIndex = model.visibleBodyLineStart + localIdx;
					const int32_t y = bodyStartY + localIdx * bodyLinePitch - 4;
					bool hit = false;
					if (((m_phase == Phase::Login) || (m_phase == Phase::ShardPick))
						&& static_cast<size_t>(bodyIndex) < model.bodyLines.size())
					{
						const RenderBodyLine& bl = model.bodyLines[static_cast<size_t>(bodyIndex)];
						if (bl.checkbox)
						{
							const int32_t cbX = contentX + 2;
							const int32_t cbY = y - 5;
							const int32_t cbS = engine::render::kAuthUiCheckboxOuterPx;
							if (contains(mx, my, cbX, cbY, cbS, cbS)
								|| contains(mx, my, contentX + engine::render::kAuthUiCheckboxLabelOffsetX - 2, y - 8, contentW - engine::render::kAuthUiCheckboxLabelOffsetX, 22))
							{
								hit = true;
							}
						}
						else if (bl.link)
						{
							const int32_t fw = std::min(contentW - 8, static_cast<int32_t>(static_cast<int>(bl.text.size()) * bodyScale * 6));
							if (contains(mx, my, contentX + 2, y - 8, std::max(64, fw), 22))
							{
								hit = true;
							}
						}
						else
						{
							hit = contains(mx, my, contentX - 4, y - 8, contentW, 22);
						}
					}
					else
					{
						hit = contains(mx, my, contentX - 4, y - 8, contentW, 22);
					}
					if (hit)
					{
						m_hoveredBodyLineIndex = bodyIndex;
						break;
					}
				}
				const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
				const int32_t gap = 10;
				engine::render::AuthLoginTwoRowLayout loginTwoRow{};
				if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
				{
					for (int32_t ci = 0; ci < lay.languageCardCount; ++ci)
					{
						if (contains(mx, my, lay.languageCardX[ci], lay.languageCardY[ci], lay.languageCardW[ci], lay.languageCardH[ci]))
						{
							m_hoveredLanguageCardIndex = ci;
							break;
						}
					}
					if (contains(mx, my, lay.languagePanelPrimaryButtonX, lay.languagePanelPrimaryButtonY,
							lay.languagePanelPrimaryButtonW, lay.languagePanelPrimaryButtonH))
					{
						for (int32_t ai = 0; ai < actionCount; ++ai)
						{
							if (model.actions[static_cast<size_t>(ai)].primary)
							{
								m_hoveredActionIndex = ai;
								break;
							}
						}
					}
				}
				else if (m_phase == Phase::Terms)
				{
					const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gap) / actionCount);
					const int32_t termsBtnY = panelY + panelH - 92 + (58 - engine::render::kAuthUiActionButtonHeightPx) / 2;
					for (int32_t i = 0; i < actionCount; ++i)
					{
						const int32_t x = contentX + i * (actionW + gap);
						if (contains(mx, my, x, termsBtnY, actionW, engine::render::kAuthUiActionButtonHeightPx))
						{
							m_hoveredActionIndex = i;
							break;
						}
					}
				}
				else if (m_phase == Phase::Login && lay.loginMaquetteLayout && actionCount == 4)
				{
					const int32_t yBtn = lay.loginPairRowY;
					if (contains(mx, my, lay.loginRegisterBtnX, yBtn, lay.loginRegisterBtnW, engine::render::kAuthUiActionButtonHeightPx))
					{
						m_hoveredActionIndex = 0;
					}
					else if (contains(mx, my, lay.loginSubmitBtnX, yBtn, lay.loginSubmitBtnW, engine::render::kAuthUiActionButtonHeightPx))
					{
						m_hoveredActionIndex = 2;
					}
					else if (contains(mx, my, lay.loginOutLinkOptsX, lay.loginOutLinksY - 4, lay.loginOutLinkOptsW, 26))
					{
						m_hoveredActionIndex = 1;
					}
					else if (contains(mx, my, lay.loginOutLinkQuitX, lay.loginOutLinksY - 4, lay.loginOutLinkQuitW, 26))
					{
						m_hoveredActionIndex = 3;
					}
				}
				else if (engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))
				{
					bool foundAction = false;
					for (int32_t row = 0; row < 2 && !foundAction; ++row)
					{
						const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
						for (int32_t col = 0; col < 2; ++col)
						{
							const int32_t i = row * 2 + col;
							if (i >= actionCount)
							{
								break;
							}
							int32_t btnW = loginTwoRow.buttonHalfWidth;
							int32_t x = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
							if (row == 1)
							{
								btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
								x = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
							}
							if (contains(mx, my, x, rowY, btnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								m_hoveredActionIndex = i;
								foundAction = true;
								break;
							}
						}
					}
				}
				else
				{
					const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
					constexpr int32_t kAuthErrorFooterBarH = 58;
					int32_t buttonY = std::min(panelY + panelH - 86,
						bodyStartY + static_cast<int32_t>(model.visibleBodyLineCount) * bodyLinePitch + buttonPadAfterBody);
					if (m_phase == Phase::Error && lay.authErrorFooterTopFromPanelTopPx > 0)
					{
						buttonY = panelY + lay.authErrorFooterTopFromPanelTopPx
							+ (kAuthErrorFooterBarH - engine::render::kAuthUiActionButtonHeightPx) / 2;
					}
					const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
					for (int32_t i = 0; i < actionCount; ++i)
					{
						const int32_t x = contentX + i * (actionW + gap);
						if (contains(mx, my, x, buttonY, actionW, engine::render::kAuthUiActionButtonHeightPx))
						{
							m_hoveredActionIndex = i;
							break;
						}
					}
				}

				const bool leftClickRaw = input.WasMousePressed(engine::platform::MouseButton::Left);
				const bool leftClick = leftClickRaw
					|| (input.IsMouseDown(engine::platform::MouseButton::Left) && !m_authPrevMouseLeftDown);
				const bool rightClick = input.WasMousePressed(engine::platform::MouseButton::Right);
				if (leftClick || rightClick)
				{
					for (size_t i = 0; i < model.fields.size(); ++i)
					{
						const auto& fldClick = model.fields[i];
						const int32_t rowY = panelY + topOffset + fieldLogicalRow[i] * fieldStep;
						int32_t fx = contentX;
						int32_t fw = contentW;
						if (fldClick.gridColumn >= 0)
						{
							engine::render::AuthUiGridFieldGeometry(
							    contentX, contentW, fldClick.gridColumn, fldClick.gridSpan, fx, fw);
						}
						if (contains(mx, my, fx, rowY, fw, engine::render::kAuthUiFieldBoxHeightPx))
						{
							m_activeField = static_cast<uint32_t>(i);
							break;
						}
					}

					if (m_hoveredBodyLineIndex >= 0)
					{
						if (m_phase == Phase::Login && m_hoveredBodyLineIndex == 1 && leftClick)
						{
							const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
							if (!window.OpenExternalUrl(resetUrl))
							{
								EnterAuthErrorPhase(Phase::Login, Tr("auth.error.open_recovery_portal"));
							}
						}
						else if (m_phase == Phase::Login && m_hoveredBodyLineIndex == 0)
						{
							m_activeField = 2u;
							m_rememberLogin = !m_rememberLogin;
							SaveRememberPreference();
							if (m_rememberLogin)
							{
								PersistRememberedLoginSidecar(m_login);
							}
							else
							{
								ClearRememberedLoginSidecar();
							}
						}
						else if (m_phase == Phase::ShardPick && m_hoveredBodyLineIndex >= 1 && leftClick)
						{
							int32_t remaining = m_hoveredBodyLineIndex;
							for (const auto& e : m_shardPickEntries)
							{
								if (e.status != 1u || e.endpoint.empty())
								{
									continue;
								}
								if (--remaining == 0)
								{
									m_shardPickChoiceShardId = e.shard_id;
									break;
								}
							}
						}
						else if (m_phase == Phase::LanguageSelectionFirstRun)
						{
							const auto& locales = FirstRunLocales();
							if (m_hoveredBodyLineIndex == 1 && !locales.empty())
							{
								m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
								m_selectedLocale = locales[static_cast<size_t>(m_languageSelectionIndex)];
							}
							else if (rightClick && !locales.empty())
							{
								m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
									? static_cast<uint32_t>(locales.size() - 1u)
									: (m_languageSelectionIndex - 1u);
								m_selectedLocale = locales[m_languageSelectionIndex];
							}
						}
						else if (m_phase == Phase::LanguageOptions)
						{
							const auto& locales = m_localization.GetAvailableLocales();
							if (m_optionsSubMenu == OptionsSubMenu::Root)
							{
								const int32_t catLine = m_hoveredBodyLineIndex - 1;
								if (leftClick && catLine >= 0 && catLine < 5)
								{
									m_optionsRootSelection = static_cast<uint32_t>(catLine);
									EnterOptionsSubmenuFromRoot(m_optionsRootSelection);
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Language && !locales.empty()
								&& (m_hoveredBodyLineIndex == 0 || m_hoveredBodyLineIndex == 1)
								&& (leftClick || rightClick))
							{
								if (rightClick)
								{
									m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
										? static_cast<uint32_t>(locales.size() - 1u)
										: (m_languageSelectionIndex - 1u);
								}
								else
								{
									m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
								}
								m_selectedLocale = locales[static_cast<size_t>(m_languageSelectionIndex)];
								if (m_hoveredBodyLineIndex >= 0)
								{
									m_optionsSubSelection = static_cast<uint32_t>(m_hoveredBodyLineIndex);
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Video)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 1 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									if (row == 0)
									{
										m_videoFullscreenPending = !m_videoFullscreenPending;
									}
									else
									{
										m_videoVsyncPending = !m_videoVsyncPending;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Audio)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 3 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									const bool dec = rightClick;
									switch (row)
									{
									case 0:
										m_audioMasterVolumePending = ClampOptionStep(m_audioMasterVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 1:
										m_audioMusicVolumePending = ClampOptionStep(m_audioMusicVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 2:
										m_audioSfxVolumePending = ClampOptionStep(m_audioSfxVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 3:
										m_audioUiVolumePending = ClampOptionStep(m_audioUiVolumePending + (dec ? -0.1f : 0.1f));
										break;
									default:
										break;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Controls)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 2 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									switch (row)
									{
									case 0:
										if (rightClick)
										{
											m_mouseSensitivityPending = std::max(0.001f, m_mouseSensitivityPending - 0.001f);
										}
										else
										{
											m_mouseSensitivityPending = std::min(0.010f, m_mouseSensitivityPending + 0.001f);
										}
										break;
									case 1:
										m_invertYPending = !m_invertYPending;
										break;
									case 2:
										m_useZqsdPending = !m_useZqsdPending;
										break;
									default:
										break;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Game)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 2 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									switch (row)
									{
									case 0:
										m_gameplayUdpEnabledPending = !m_gameplayUdpEnabledPending;
										break;
									case 1:
										m_allowInsecureDevPending = !m_allowInsecureDevPending;
										break;
									case 2:
										if (rightClick)
										{
											m_authTimeoutMsPending = (m_authTimeoutMsPending > 1000u) ? (m_authTimeoutMsPending - 1000u) : 1000u;
										}
										else
										{
											m_authTimeoutMsPending = std::min<uint32_t>(15000u, m_authTimeoutMsPending + 1000u);
										}
										break;
									default:
										break;
									}
								}
							}
						}
						else if (m_phase == Phase::Terms && m_hoveredBodyLineIndex >= 0 && rightClick && m_termsScrolledToBottom)
						{
							m_termsAcknowledgeChecked = !m_termsAcknowledgeChecked;
						}
					}

					if (leftClick)
					{
						// --- Hit-test icône "i" (avant dropdowns et boutons) ---
						bool iconHit = false;
						if (model.infoIconVisible)
						{
							if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
							{
								const bool hitIcon = contains(mx, my, lay.languageInfoIconX, lay.languageInfoIconY, lay.languageInfoIconW,
									lay.languageInfoIconH);
								const bool hitPlate = lay.languageProgressPlatePresent
									&& contains(mx, my, lay.languageProgressPlateX, lay.languageProgressPlateY, lay.languageProgressPlateW,
										lay.languageProgressPlateH);
								if (hitIcon || hitPlate)
								{
									m_infoPopupVisible = !m_infoPopupVisible;
									if (m_infoPopupVisible)
									{
										m_infoPopupText = Tr("language.first_run.info_popup");
									}
									iconHit = true;
								}
							}
							else
							{
								for (int32_t fi = 0; fi < static_cast<int32_t>(model.fields.size()) && !iconHit; ++fi)
								{
									const RenderField& fld = model.fields[static_cast<size_t>(fi)];
									if (fld.tooltipText.empty())
									{
										continue;
									}
									const int32_t rowY = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(fi)] * fieldStep;
									int32_t fx = contentX;
									int32_t fw = contentW;
									if (fld.gridColumn >= 0)
									{
										engine::render::AuthUiGridFieldGeometry(
										    contentX, contentW, fld.gridColumn, fld.gridSpan, fx, fw);
									}
									const int32_t ix = std::max(fx + 10, fx + fw - 36);
									const int32_t iy = rowY - labelAboveFieldPxHit;
									if (contains(mx, my, ix, iy, 18, 18))
									{
										m_infoPopupVisible = !m_infoPopupVisible;
										m_infoPopupText    = fld.tooltipText;
										iconHit = true;
									}
								}
							}
						}
						// Clic hors icône quand le popup est ouvert → fermer le popup.
						if (!iconHit && m_infoPopupVisible)
						{
							m_infoPopupVisible = false;
						}

						// --- Gestion clics dropdowns date de naissance (phase Inscription) ---
						// Traité avant les boutons d'action pour éviter qu'un clic sur un dropdown
						// déclenche accidentellement un bouton.
						bool dropdownHandled = false;
						if (m_phase == Phase::Register && model.dropdowns.size() == 3)
						{
							constexpr int32_t kOptionH = engine::render::kAuthUiFieldBoxHeightPx;
							for (int i = 0; i < 3 && !dropdownHandled; ++i)
							{
								const auto& dd = model.dropdowns[static_cast<size_t>(i)];
								// Clic sur l'en-tête du dropdown (ouverture/fermeture).
								if (contains(mx, my, dd.x, dd.y, dd.w, dd.h))
								{
									m_openDropdownIndex = (m_openDropdownIndex == i) ? -1 : i;
									dropdownHandled = true;
									break;
								}
								// Si ce dropdown est ouvert, tester les options.
								if (m_openDropdownIndex == i && dd.isOpen)
								{
									const int32_t optCount = static_cast<int32_t>(dd.options.size());
									for (int32_t j = 0; j < optCount && !dropdownHandled; ++j)
									{
										const int32_t optY = dd.y + dd.h + j * kOptionH;
										if (contains(mx, my, dd.x, optY, dd.w, kOptionH))
										{
											if (i == 0) m_birthDayIndex   = j;
											else if (i == 1) m_birthMonthIndex = j;
											else if (i == 2) m_birthYearIndex  = j;
											m_openDropdownIndex = -1; // fermer après sélection
											dropdownHandled = true;
										}
									}
								}
							}
							// Clic en dehors de tout dropdown : fermer le dropdown ouvert.
							if (!dropdownHandled && m_openDropdownIndex >= 0)
							{
								m_openDropdownIndex = -1;
							}
						}

						bool actionHit = false;
						if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout
							&& (leftClick || rightClick))
						{
							for (int32_t ci = 0; ci < lay.languageCardCount; ++ci)
							{
								if (contains(mx, my, lay.languageCardX[ci], lay.languageCardY[ci], lay.languageCardW[ci], lay.languageCardH[ci]))
								{
									actionHit = true;
									const auto& localesPick = FirstRunLocales();
									if (!localesPick.empty() && static_cast<size_t>(ci) < model.languageFirstRunCards.size())
									{
										const std::string& tagPick = model.languageFirstRunCards[static_cast<size_t>(ci)].localeTag;
										const auto itPick = std::find(localesPick.begin(), localesPick.end(), tagPick);
										if (itPick != localesPick.end())
										{
											m_languageSelectionIndex = static_cast<uint32_t>(std::distance(localesPick.begin(), itPick));
											m_selectedLocale = *itPick;
										}
									}
									break;
								}
							}
							if (!actionHit && leftClick && contains(mx, my, lay.languagePanelPrimaryButtonX, lay.languagePanelPrimaryButtonY,
									lay.languagePanelPrimaryButtonW, lay.languagePanelPrimaryButtonH))
							{
								actionHit = true;
								applyPrimaryAction();
							}
						}
						else if (m_phase == Phase::Terms)
						{
							const int32_t termsBtnY = panelY + panelH - 92 + (58 - engine::render::kAuthUiActionButtonHeightPx) / 2;
							const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gap) / actionCount);
							for (int32_t i = 0; i < actionCount; ++i)
							{
								const int32_t x = contentX + i * (actionW + gap);
								if (!contains(mx, my, x, termsBtnY, actionW, engine::render::kAuthUiActionButtonHeightPx))
								{
									continue;
								}
								actionHit = true;
								if (i == 0)
								{
									applyPrimaryAction();
								}
								break;
							}
						}
						else if (m_phase == Phase::Login && lay.loginMaquetteLayout && actionCount == 4)
						{
							const int32_t yBtn = lay.loginPairRowY;
							if (contains(mx, my, lay.loginRegisterBtnX, yBtn, lay.loginRegisterBtnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								actionHit = true;
								SetPhase(Phase::Register);
								m_activeField = 0;
								m_userErrorText.clear();
								m_passwordConfirm.clear();
								m_usernameCheckState = UsernameCheckState::Idle;
								m_usernameCheckSeq++;
								m_usernameDebounceTimer = 0.0;
								m_usernameLastChecked.clear();
							}
							else if (contains(mx, my, lay.loginSubmitBtnX, yBtn, lay.loginSubmitBtnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								actionHit = true;
								applyPrimaryAction();
							}
							else if (contains(mx, my, lay.loginOutLinkOptsX, lay.loginOutLinksY - 4, lay.loginOutLinkOptsW, 26))
							{
								actionHit = true;
								OpenLanguageOptions();
							}
							else if (contains(mx, my, lay.loginOutLinkQuitX, lay.loginOutLinksY - 4, lay.loginOutLinkQuitW, 26))
							{
								actionHit = true;
								window.RequestClose();
							}
						}
						else if (engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))
						{
							for (int32_t row = 0; row < 2 && !actionHit; ++row)
							{
								const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
								for (int32_t col = 0; col < 2; ++col)
								{
									const int32_t i = row * 2 + col;
									if (i >= actionCount)
									{
										break;
									}
									int32_t btnW = loginTwoRow.buttonHalfWidth;
									int32_t ax = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
									if (row == 1)
									{
										btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
										ax = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
									}
									if (!contains(mx, my, ax, rowY, btnW, engine::render::kAuthUiActionButtonHeightPx))
									{
										continue;
									}
									actionHit = true;
									switch (m_phase)
									{
									case Phase::Login:
										if (i == 0) {
											SetPhase(Phase::Register);
											m_activeField = 0;
											m_userErrorText.clear();
											m_passwordConfirm.clear();
											// Plan C : réinitialiser état disponibilité username à l'entrée en Register.
											m_usernameCheckState = UsernameCheckState::Idle;
											m_usernameCheckSeq++;
											m_usernameDebounceTimer = 0.0;
											m_usernameLastChecked.clear();
										}
										else if (i == 1) { OpenLanguageOptions(); }
										else if (i == 2) { applyPrimaryAction(); }
										else if (i == 3) { window.RequestClose(); }
										break;
									case Phase::ShardPick:
										if (i == 0)
										{
											applyPrimaryAction();
										}
										else if (i == 1)
										{
											m_shardPickEntries.clear();
											m_shardPickChoiceShardId = 0;
											SetPhase(Phase::Login);
											m_userErrorText.clear();
										}
										break;
									default:
										break;
									}
									break;
								}
							}
						}
						if (!actionHit
							&& !(m_phase == Phase::Login && actionCount == 4
								&& (lay.loginMaquetteLayout
									|| engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))))
						{
							constexpr int32_t kAuthErrorFooterBarH = 58;
							const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
							int32_t buttonY = std::min(panelY + panelH - 86,
								bodyStartY + static_cast<int32_t>(model.visibleBodyLineCount) * bodyLinePitch + buttonPadAfterBody);
							if (m_phase == Phase::Error && lay.authErrorFooterTopFromPanelTopPx > 0)
							{
								buttonY = panelY + lay.authErrorFooterTopFromPanelTopPx
									+ (kAuthErrorFooterBarH - engine::render::kAuthUiActionButtonHeightPx) / 2;
							}
							const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
							for (int32_t i = 0; i < actionCount; ++i)
							{
								const int32_t x = contentX + i * (actionW + gap);
								if (!contains(mx, my, x, buttonY, actionW, engine::render::kAuthUiActionButtonHeightPx))
								{
									continue;
								}

								switch (m_phase)
								{
								case Phase::Login:
									if (i == 0) {
										SetPhase(Phase::Register);
										m_activeField = 0;
										m_userErrorText.clear();
										m_passwordConfirm.clear();
										// Plan C : réinitialiser état disponibilité username à l'entrée en Register.
										m_usernameCheckState = UsernameCheckState::Idle;
										m_usernameCheckSeq++;
										m_usernameDebounceTimer = 0.0;
										m_usernameLastChecked.clear();
									}
									else if (i == 1) { OpenLanguageOptions(); }
									else if (i == 2) { applyPrimaryAction(); }
									else if (i == 3) { window.RequestClose(); }
									break;
								case Phase::Register:
								case Phase::VerifyEmail:
								case Phase::EmailConfirmationPending:
								case Phase::ForgotPassword:
									if (i == 0) applyPrimaryAction();
									else if (i == 1)
									{
										// Plan C : réinitialiser état disponibilité username à la sortie de Register.
										if (m_phase == Phase::Register)
										{
											m_usernameCheckState = UsernameCheckState::Idle;
											m_usernameCheckSeq++;
											m_usernameDebounceTimer = 0.0;
											m_usernameLastChecked.clear();
										}
										if (m_phase == Phase::EmailConfirmationPending)
										{
											m_registeredTagId.clear();
											m_userErrorText.clear();
											m_activeField = 0;
											SetPhase(Phase::Login);
										}
										else
										{
											SetPhase(Phase::Login);
											m_activeField = 0;
											m_userErrorText.clear();
										}
									}
									break;
								case Phase::ShardPick:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									else if (i == 1)
									{
										m_shardPickEntries.clear();
										m_shardPickChoiceShardId = 0;
										SetPhase(Phase::Login);
										m_userErrorText.clear();
									}
									break;
								case Phase::LanguageSelectionFirstRun:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									break;
								case Phase::CharacterCreate:
								case Phase::Submitting:
								case Phase::Error:
									if (i == 0) applyPrimaryAction();
									break;
								case Phase::LanguageOptions:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									else if (i == 1)
									{
										if (m_optionsSubMenu != OptionsSubMenu::Root)
										{
											m_optionsSubMenu = OptionsSubMenu::Root;
											LOG_INFO(Core, "[AuthUiPresenter] Language options: back to root menu");
										}
										else
										{
											m_phase = m_phaseBeforeOptions;
											LOG_INFO(Core, "[AuthUiPresenter] Language options closed via 'return'");
										}
									}
									break;
								default:
									break;
								}
								break;
							}
						}
					}
				}
			}
			m_authPrevMouseLeftDown = input.IsMouseDown(engine::platform::MouseButton::Left);
		}

		std::string text;
		if (!usingNativeAuth && !authUiImguiMode)
		{
			input.ConsumePendingTextUtf8(text);
			if (!text.empty())
			{
				if (std::string* field = currentField())
				{
					const bool registerBirthCombo = m_phase == Phase::Register &&
						((m_activeField >= 6u && m_activeField <= 8u) || m_activeField == 9u);
					if (!registerBirthCombo)
					{
						for (unsigned char c : text)
						{
							// Tab / retours chariot : gérés par Key::Tab et ignorés ici (sinon Tab s’insère comme texte).
							if (c < 32)
								continue;
							const bool digitsOnlyField =
								(m_phase == Phase::Register && (m_activeField == 6 || m_activeField == 7 || m_activeField == 8)) ||
								(m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending);
							if (digitsOnlyField && (c < '0' || c > '9'))
								continue;
							const size_t maxLen =
								(m_phase == Phase::Register && (m_activeField == 6 || m_activeField == 7)) ? 2u :
								(m_phase == Phase::Register && m_activeField == 8) ? 4u :
								(m_phase == Phase::Register && m_activeField == 9) ? 2u :
								(m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending) ? 6u :
								(m_phase == Phase::CharacterCreate) ? 32u : 256u;
							if (field->size() >= maxLen)
								continue;
							field->push_back(static_cast<char>(c));
						}
					}
					if (m_phase == Phase::Login && m_rememberLogin && m_activeField == 0 && !text.empty())
					{
						PersistRememberedLoginSidecar(m_login);
					}
				}
			}
		}

	if (!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Backspace))
		{
			auto popLast = [](std::string& s) {
				while (!s.empty())
				{
					const unsigned char back = static_cast<unsigned char>(s.back());
					s.pop_back();
					if ((back & 0xC0u) != 0x80u)
						break;
				}
			};
			if (std::string* field = currentField())
			{
				if (m_phase == Phase::Register && (
					(m_activeField >= 6u && m_activeField <= 8u) || m_activeField == 9u))
				{
					if (m_activeField == 6)
					{
						AdjustBirthCycle(m_birthDay, -1, 1, 31);
					}
					else if (m_activeField == 7)
					{
						AdjustBirthCycle(m_birthMonth, -1, 1, 12);
					}
					else if (m_activeField == 8)
					{
						AdjustBirthCycle(m_birthYear, -1, 1900, 2100);
					}
					else if (m_activeField == 9)
						AdjustCountryCycle(m_country, -1);
				}
				else
				{
					popLast(*field);
				}
			}
			if (m_phase == Phase::Login && m_rememberLogin && m_activeField == 0)
			{
				PersistRememberedLoginSidecar(m_login);
			}
		}

		// Plan C : réinitialiser debounce disponibilité username après toute frappe sur le champ login en inscription.
		if (m_phase == Phase::Register && m_activeField == 0)
		{
			m_usernameCheckState = UsernameCheckState::Idle;
			m_usernameCheckSeq++;
			m_usernameDebounceTimer = (m_login.size() >= 3) ? 0.8 : 0.0;
		}

	if (!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Tab))
		{
			if (m_phase == Phase::Login)
				m_activeField = (m_activeField + 1u) % 3u;
			else if (m_phase == Phase::Register)
				m_activeField = (m_activeField + 1u) % 10u;
			else if (m_phase == Phase::ForgotPassword)
				m_activeField = 0;
			else
				m_activeField = 0;
			LOG_DEBUG(Core, "[AuthUiPresenter] Focus field={}", m_activeField);
		}

		if (!usingNativeAuth && !authUiImguiMode && m_phase == Phase::Login && input.WasPressed(engine::platform::Key::Space) && m_activeField == 2u)
		{
			m_rememberLogin = !m_rememberLogin;
			SaveRememberPreference();
			if (m_rememberLogin)
			{
				PersistRememberedLoginSidecar(m_login);
			}
			else
			{
				ClearRememberedLoginSidecar();
			}
		}

		Update_Terms(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_CharacterCreate(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_CharacterSelect(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_LanguageSelect(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_ShardPick(input, cfg, window, usingNativeAuth, authUiImguiMode);

		Update_LoginShortcuts(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_Register(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_ForgotPassword(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_VerifyEmail(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_Options(input, cfg, window, usingNativeAuth, authUiImguiMode);

		if ((!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Enter))
			|| (usingNativeAuth && m_phase == Phase::Error))
		{
			if (!usingNativeAuth && !authUiImguiMode && m_phase == Phase::LanguageOptions && m_optionsSubMenu == OptionsSubMenu::Root)
			{
				EnterOptionsSubmenuFromRoot(m_optionsRootSelection);
			}
			else
			{
				applyPrimaryAction();
			}
		}

		UpdateWindowTitle(window);
	}

	void AuthUiPresenter::ResolveActionButtonLabels(RenderModel& model) const
	{
		for (auto& a : model.actions)
		{
			if (a.labelKey.empty())
			{
				continue;
			}
			a.label = Tr(a.labelKey);
			if (a.label.empty() && !a.labelKeyFallback.empty())
			{
				a.label = Tr(a.labelKeyFallback);
			}
			if (a.label.empty())
			{
				a.label = a.labelKey;
			}
		}
	}

	AuthUiPresenter::RenderModel AuthUiPresenter::BuildRenderModel() const
	{
		RenderModel model{};
		model.layoutAuthTitleLine1FromPanelTopPx = m_layoutAuthTitleLine1FromPanelTopPx;
		model.layoutAuthGapTitleToSectionPx = m_layoutAuthGapTitleToSectionPx;
		model.layoutAuthTitleCenterViewportWidth = m_layoutAuthTitleCenterViewportWidth;
		model.layoutAuthFieldRowExtraPx = m_layoutAuthFieldRowExtraPx;
		model.visible = m_initialized && !m_flowComplete && m_authEnabled;
		model.hoveredFieldInfoIndex = m_hoveredFieldInfoIndex;
		model.titleLine1 = Tr("auth.title_line1");
		model.titleLine2 = Tr("auth.title_line2");
		// Phase::Error : on n'affiche pas la bannière (elle serait superposée au cadre d'erreur).
		// SetPhase(Phase::Error) efface m_infoBanner, mais cette garde défensive empêche tout
		// chevauchement résiduel si un chemin de code atypique ne passe pas par SetPhase.
		model.infoBanner = (m_phase == Phase::Error) ? std::string{} : m_infoBanner;
		model.errorText = (m_phase == Phase::Error) ? m_userErrorText : std::string{};
		model.footerHint.clear();

		auto addField = [this, &model](std::string label, std::string value, bool active, bool secret = false, bool cyclePicker = false,
			std::string tooltipKey = {}, std::string tooltipTextDirect = {}, std::string inputPlaceholder = {})
		{
			RenderField field{};
			field.label = std::move(label);
			field.value = std::move(value);
			field.active = active;
			field.hovered = static_cast<int32_t>(model.fields.size()) == m_hoveredFieldIndex;
			field.secret = secret;
			field.cyclePicker = cyclePicker;
			field.tooltipKey = std::move(tooltipKey);
			if (!tooltipTextDirect.empty())
			{
				field.tooltipText = std::move(tooltipTextDirect);
			}
			field.inputPlaceholder = std::move(inputPlaceholder);
			model.fields.push_back(std::move(field));
		};
		auto addActionKeys = [this, &model](std::string_view labelKey, bool primary, bool active = true, bool emphasized = false,
			std::string_view labelKeyFallback = {})
		{
			RenderAction action{};
			action.labelKey = std::string(labelKey);
			if (!labelKeyFallback.empty())
			{
				action.labelKeyFallback = std::string(labelKeyFallback);
			}
			action.primary = primary;
			action.active = active;
			action.emphasized = emphasized;
			action.hovered = static_cast<int32_t>(model.actions.size()) == m_hoveredActionIndex;
			model.actions.push_back(std::move(action));
		};
		auto addBodyLine = [this, &model](std::string text, bool active = false, bool link = false)
		{
			RenderBodyLine line{};
			line.text = std::move(text);
			line.active = active;
			line.link = link;
			line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(line));
		};
		auto addBodyLinesFromNewlines = [&addBodyLine](const std::string& text)
		{
			if (text.empty())
			{
				return;
			}
			size_t start = 0;
			for (;;)
			{
				const size_t nl = text.find('\n', start);
				if (nl == std::string::npos)
				{
					const std::string chunk = text.substr(start);
					if (!chunk.empty())
					{
						addBodyLine(chunk);
					}
					break;
				}
				const std::string chunk = text.substr(start, nl - start);
				if (!chunk.empty())
				{
					addBodyLine(chunk);
				}
				start = nl + 1u;
			}
		};
		auto maskedPassword = [this]() -> std::string
		{
			std::string out;
			AppendPasswordStars(out, m_password.size());
			return out;
		};

		switch (m_phase)
		{
		case Phase::Login:
			BuildModel_Login(model);
			break;
		case Phase::Register:
			BuildModel_Register(model);
			break;
		case Phase::VerifyEmail:
			BuildModel_VerifyEmail(model);
			break;
		case Phase::EmailConfirmationPending:
			BuildModel_EmailConfirmationPending(model);
			break;
		case Phase::ForgotPassword:
			BuildModel_ForgotPassword(model);
			break;
		case Phase::Terms:
			BuildModel_Terms(model);
			break;
		case Phase::CharacterCreate:
			BuildModel_CharacterCreate(model);
			break;
		case Phase::CharacterSelect:
			BuildModel_CharacterSelect(model);
			break;
		case Phase::ShardPick:
			BuildModel_ShardPick(model);
			break;
		case Phase::LanguageSelectionFirstRun:
		{
			BuildModel_LanguageSelect(model);
			addActionKeys("common.continue", true, true, false);
			break;
		}
		case Phase::LanguageOptions:
			BuildModel_Options(model);
			break;
		case Phase::Submitting:
			// Un seul libellé : sectionTitle (évite doublon avec l’ancienne bodyLine dessinée sur les barres).
			model.sectionTitle = Tr("auth.panel.submitting");
			break;
		case Phase::Error:
			BuildModel_Error(model);
			break;
		}

		for (RenderField& f : model.fields)
		{
			if (!f.tooltipKey.empty())
			{
				f.tooltipText = Tr(f.tooltipKey);
			}
		}

		ResolveActionButtonLabels(model);

		if (m_phase == Phase::Register && model.actions.size() >= 2u)
		{
			model.actions[0].actionBadge = Tr("auth.badge.submit_enter");
			model.actions[1].actionBadge = Tr("auth.badge.esc_back");
		}
		if ((m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending) && model.actions.size() >= 2u)
		{
			model.actions[0].actionBadge = Tr("auth.badge.submit_enter");
			model.actions[1].actionBadge = Tr("auth.badge.esc_back");
		}

		if (!model.bodyLines.empty())
		{
			const int32_t total = static_cast<int32_t>(model.bodyLines.size());
			const int32_t maxVisible =
				(m_phase == Phase::LanguageOptions || m_phase == Phase::ShardPick) ? 10 : 6;
			int32_t focusIndex = 0;
			for (int32_t i = 0; i < total; ++i)
			{
				if (model.bodyLines[static_cast<size_t>(i)].hovered || model.bodyLines[static_cast<size_t>(i)].active)
				{
					focusIndex = i;
					break;
				}
			}
			int32_t start = std::max(0, focusIndex - (maxVisible / 2));
			if (start + maxVisible > total)
			{
				start = std::max(0, total - maxVisible);
			}
			model.visibleBodyLineStart = start;
			model.visibleBodyLineCount = std::min(maxVisible, total - start);
		}

		model.authLogoSizePx = m_authLogoSizePx;

		// Popup info — propagé depuis l'état.
		model.infoPopupVisible = m_infoPopupVisible;
		if (!m_infoPopupText.empty())
		{
			model.infoPopupText = m_infoPopupText;
		}
		else
		{
			// Fallback : texte générique selon la phase (utilisé quand le popup n'est pas ouvert sur un champ spécifique).
			if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword)
				model.infoPopupText = Tr("auth.info.login_help");
			else if (m_phase == Phase::Register)
				model.infoPopupText = Tr("auth.info.register_help");
			else if (m_phase == Phase::ShardPick)
				model.infoPopupText = Tr("auth.shard_pick.popup_help");
			else if (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
				model.infoPopupText = Tr("auth.verify.info_popup");
			else if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
				model.infoPopupText = Tr("language.first_run.info_popup");
		}

		// Icône "i" visible sur Login et Register.
		model.infoIconVisible = (m_phase == Phase::Login || m_phase == Phase::Register || m_phase == Phase::ShardPick
			|| m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending
			|| (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout));

		if (model.languageFirstRunLayout && m_viewportW > 0u && m_viewportH > 0u)
		{
			const VkExtent2D extPatch{ m_viewportW, m_viewportH };
			const VisualState vsPatch = GetVisualState();
			const engine::render::AuthUiLayoutMetrics layPatch =
				engine::render::BuildAuthUiLayoutMetrics(extPatch, vsPatch, model);
			model.infoIconX = layPatch.languageInfoIconX;
			model.infoIconY = layPatch.languageInfoIconY;
			model.infoIconW = layPatch.languageInfoIconW;
			model.infoIconH = layPatch.languageInfoIconH;
		}

		return model;
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
		const RenderModel model = BuildRenderModel();
		if (!model.visible)
		{
			return {};
		}

		std::string s;
		s += model.titleLine1;
		s += "\n";
		s += model.titleLine2;
		s += "\n\n";
		if (!model.infoBanner.empty())
		{
			s += Tr("common.information");
			s += "\n";
			s += model.infoBanner;
			s += "\n\n";
		}
		if (!model.sectionTitle.empty())
		{
			s += model.sectionTitle;
			s += "\n\n";
		}
		for (const RenderField& field : model.fields)
		{
			s += field.label;
			s += "\n";
			s += field.value;
			s += field.active ? "|\n" : "\n";
			s += "\n";
		}
		for (int32_t i = 0; i < model.visibleBodyLineCount; ++i)
		{
			const RenderBodyLine& line = model.bodyLines[static_cast<size_t>(model.visibleBodyLineStart + i)];
			if (line.text.empty() && line.valueText.empty())
				continue;
			s += line.text;
			if (!line.valueText.empty())
			{
				s += "\t";
				s += line.valueText;
			}
			s += "\n";
		}
		if (!model.bodyLines.empty())
		{
			s += "\n";
		}
		if (!model.errorText.empty())
		{
			s += "\n\n";
			s += model.errorText;
			s += "\n";
		}
		for (const RenderAction& action : model.actions)
		{
			s += action.primary ? "[*] " : "[ ] ";
			s += action.label;
			s += "\n";
		}
		s += "\n";
		s += model.footerHint;
		s += "\n";
		return s;
	}

	AuthUiPresenter::VisualState AuthUiPresenter::GetVisualState() const
	{
		VisualState state{};
		state.active = m_initialized && !m_flowComplete && m_authEnabled;
		state.login = m_phase == Phase::Login;
		state.registerMode = m_phase == Phase::Register;
		state.verifyEmail = m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending;
		state.forgotPassword = m_phase == Phase::ForgotPassword;
		state.terms = m_phase == Phase::Terms;
		state.characterCreate = m_phase == Phase::CharacterCreate;
		state.characterSelect = m_phase == Phase::CharacterSelect;
		state.languageSelection = m_phase == Phase::LanguageSelectionFirstRun;
		state.languageOptions = m_phase == Phase::LanguageOptions;
		state.options = m_phase == Phase::LanguageOptions;
		state.shardPick = m_phase == Phase::ShardPick;
		state.emailConfirmationPending = m_phase == Phase::EmailConfirmationPending;
		state.submitting = m_phase == Phase::Submitting;
		state.error = m_phase == Phase::Error;
		state.minimalChrome = m_authMinimalChrome;
		state.loginArtColumn = m_authLoginArtColumn;
		state.authLogoSpin = m_authAvailabilityChecking;
		state.authStatusKnown = m_statusProbeCompletedOnce;
		state.authStatusOk = m_statusCache.authOk;
		return state;
	}


	bool AuthUiPresenter::OnEscape()
	{
		if (m_phase == Phase::Submitting)
			return true;
		if (m_phase == Phase::LanguageSelectionFirstRun)
		{
			return true;
		}
		if (m_phase == Phase::LanguageOptions)
		{
			if (m_optionsSubMenu != OptionsSubMenu::Root)
			{
				m_optionsSubMenu = OptionsSubMenu::Root;
				LOG_INFO(Core, "[AuthUiPresenter] Escape: Options -> root menu");
				return true;
			}
			m_phase = m_phaseBeforeOptions;
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Language options closed");
			return true;
		}
		if (m_phase == Phase::Register || m_phase == Phase::ForgotPassword || m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
		{
			if (m_phase == Phase::Register)
			{
				// Plan C : réinitialiser état disponibilité username à la sortie de Register.
				m_usernameCheckState = UsernameCheckState::Idle;
				m_usernameCheckSeq++;
				m_usernameDebounceTimer = 0.0;
				m_usernameLastChecked.clear();
			}
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			m_passwordConfirm.clear();
			m_registeredTagId.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Auth sub-screen -> Login");
			return true;
		}
		if (m_phase == Phase::Terms)
		{
			return true;
		}
		if (m_phase == Phase::CharacterCreate)
		{
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			m_infoBanner = Tr("auth.info.character_cancelled");
			m_chosenShardId = 0;
			m_postRegistrationCharacterCreatePending = false;
			ResetMasterSession();
			return true;
		}
		if (m_phase == Phase::ShardPick)
		{
			m_shardPickEntries.clear();
			m_shardPickChoiceShardId = 0;
			m_chosenShardId = 0;
			m_postRegistrationCharacterCreatePending = false;
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: ShardPick -> Login");
			return true;
		}
		if (m_phase == Phase::Login)
		{
			// Sur la page connexion, Escape ne doit pas fermer le jeu :
			// la sortie se fait uniquement via le bouton "Quitter".
			return true;
		}
		if (m_phase == Phase::Error)
		{
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Error -> Login");
			return true;
		}
		return false;
	}

	void AuthUiPresenter::BypassAuthGateForWorldEditor()
	{
		if (!m_initialized)
		{
			return;
		}
		JoinWorker();
		m_flowComplete = true;
		m_userErrorText.clear();
		m_infoBanner.clear();
		LOG_INFO(Core, "[AuthUiPresenter] World Editor : flux auth marqué complet (pas d’écran login)");
	}

	void AuthUiPresenter::RequestReturnToLogin()
	{
		if (!m_initialized)
		{
			return;
		}
		JoinWorker();
		m_flowComplete = false;
		m_phase = Phase::Login;
		m_errorReturnPhase = Phase::Login;
		m_userErrorText.clear();
		m_infoBanner.clear();
		// Vide les buffers d'identifiant/mot de passe en mémoire pour ne pas re-soumettre
		// par erreur l'auth précédente. Le login persisté sera re-rempli au prochain frame
		// par la lecture de remembered_login.json côté UI.
		m_pendingEnterWorld = {};
		m_postRegistrationCharacterCreatePending = false;
		LOG_INFO(Core, "[AuthUiPresenter] Logout: retour ecran Login (flow_complete=false)");
	}
}
