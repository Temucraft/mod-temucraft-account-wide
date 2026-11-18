# mod-temucraft-account-wide

**Account-wide reputation.** Progress on a faction is kept per account: every character logs in with the account's
progress, and what it earns is saved for the others.

- **Only valid factions.** Whether a character can have a faction is read from `Faction.dbc`, the same rows the core
  uses for base reputation. If the row for the character's race and class is hidden or forced invisible (how the
  game hides the other side's cities), that faction is not shared to it. Neutral factions (no race rows) are shared
  with everyone. So a Horde character never gets Stormwind, and the Alliance never gets Orgrimmar.
- **Progress, not base.** What is shared is the earned standing; each character keeps its race's starting bonus.
- **First login** of an account starts from the best progress any of its characters already has.
- **Kept per character:** `Temucraft.AccountWide.Reputation.Exclude` (default Aldor/Scryers and Frenzyheart/Oracles,
  where earning one lowers the other).

**Account-wide collections.** What one character has, every character of the account gets when it logs in:
mounts and companion pets (if its race and class may have them), recipes and patterns (if it has the profession,
enough skill and any specialization the recipe needs), flight paths of its faction, and titles. `.collection` shows
what the account has shared.

**Locked trainers.** Trainers listed in `mod_temucraft_locked_trainer` (world) stay closed until any character of the
account has done their quest; then they are open to all of them.

SQL: `data/sql/db-characters/base`, `data/sql/db-world/base`. Settings: `conf/mod-temucraft-account-wide.conf.dist`. No client changes.

## Texts and translations

The 3 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-account-wide`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.
