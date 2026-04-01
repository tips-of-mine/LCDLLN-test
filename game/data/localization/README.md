Localization catalogs for `STAB.14`.

Rules:
- Put one flat JSON file per locale in `localization/text/<locale>.json`.
- Keep keys stable across all locales.
- Prefer short locale tags such as `en`, `fr`, `es`.
- Missing keys fall back to `en`, then to the key name itself.
