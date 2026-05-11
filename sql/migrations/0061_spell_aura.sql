-- 0061 — Wave 23 Spell/Aura/Proc : persistance des auras au logout +
-- table proc templates data-driven.
--
-- Idempotente : CREATE TABLE IF NOT EXISTS. Rejouable sans erreur.

-- Persistance des auras actifs sur un character au logout. Au login, le
-- shard re-applique chaque ligne (avec verification expiresAtMs > now pour
-- skip les auras expires entre temps).
CREATE TABLE IF NOT EXISTS character_auras
(
	character_id     BIGINT UNSIGNED NOT NULL,
	spell_id         INT UNSIGNED NOT NULL,
	caster_id        BIGINT UNSIGNED NOT NULL,
	applied_at_ms    BIGINT UNSIGNED NOT NULL,
	expires_at_ms    BIGINT UNSIGNED NOT NULL,
	stack_count      INT UNSIGNED NOT NULL DEFAULT 1,
	PRIMARY KEY (character_id, spell_id, caster_id),
	KEY idx_character_auras_character (character_id),
	KEY idx_character_auras_expires (expires_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Templates des procs data-driven. Charges au boot, immutable runtime.
-- Hot-reload via /reload procs (admin GM+, audit).
CREATE TABLE IF NOT EXISTS spell_proc_template
(
	proc_id              INT UNSIGNED NOT NULL,
	event_type           TINYINT UNSIGNED NOT NULL,  -- ProcEvent enum
	trigger_spell_id     INT UNSIGNED NOT NULL,
	proc_chance          TINYINT UNSIGNED NOT NULL DEFAULT 100,  -- 0-100
	internal_cooldown_ms INT UNSIGNED NOT NULL DEFAULT 0,
	PRIMARY KEY (proc_id),
	KEY idx_spell_proc_template_event (event_type),
	KEY idx_spell_proc_template_trigger (trigger_spell_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
