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

**Shared game settings.** A player's accounts keep the same game settings (auto loot and the rest of the
interface options), key bindings and macros, on any PC: when one account saves a copy, the server gives the same copy,
with the same time, to the player's other accounts, and the client takes it at their next login. Accounts are linked
by the same e-mail address (`Temucraft.AccountWide.Settings.LinkByEmail`) or by the player with
`.shared link <account> <password>` (`.shared list`, `.shared unlink`). Each character's own key bindings and macros can
be shared too (`Temucraft.AccountWide.Settings.Characters`, off by default). Needs the client's "synchronize settings"
(on by default). The Temucraft launcher can do the same on one PC without the server.

**Vanity items.** Items listed in `mod_temucraft_vanity_item` (world), such as the Stones and Scrolls of Homecoming of
mod-temucraft-travel, are unlocked for the whole account as soon as any character buys, loots or gets one. Every
character that may use it (its faction, race and class) can then summon it again into its bags, never twice:
`.collection vanity` lists them (and opens the Vanity window), `.collection summon <name>` brings one back. The Temucraft
client's Vanity window (`/vanity`, or the Vanity button of the Wardrobe) shows each one with its icon, name and
cooldown, and a Summon button; it talks to the server with addon messages (prefix `TVA`). A cooldown stays with the
character, so summoning an item again doesn't reset it. More vanity items: add rows (`item`, `category` heading,
`sort`). `Temucraft.AccountWide.Vanity` turns it off.

**Locked trainers.** Trainers listed in `mod_temucraft_locked_trainer` (world) stay closed until any character of the
account has done their quest; then they are open to all of them.

SQL: `data/sql/db-characters/base`, `data/sql/db-world/base`. Settings: `conf/mod-temucraft-account-wide.conf.dist`. No client
changes needed; the Vanity window is in the Temucraft client (mod-temucraft-client-version).

## Texts and translations

The 20 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-account-wide`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.
