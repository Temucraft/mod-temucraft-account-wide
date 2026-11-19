-- mod-temucraft-account-wide: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-account-wide';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-account-wide', 1, '|cff66ccff[Account]|r From your other characters: {} mounts, {} pets, {} recipes, {} flight paths, {} titles.'),
('mod-temucraft-account-wide', 2, '|cff66ccff[Account]|r {}'),
('mod-temucraft-account-wide', 3, '|cff66ccff[Account]|r Shared by your account: {} mounts, {} companion pets, {} recipes, {} flight paths, {} titles, {} trainer unlocks.'),
('mod-temucraft-account-wide', 4, '|cff66ccff[Account]|r That account name and password don''t match.'),
('mod-temucraft-account-wide', 5, '|cff66ccff[Account]|r Linked: your settings, key bindings and macros are now shared with {}.'),
('mod-temucraft-account-wide', 6, '|cff66ccff[Account]|r This account no longer shares its settings by link (accounts with the same e-mail still do, if the server allows it).'),
('mod-temucraft-account-wide', 7, '|cff66ccff[Account]|r Shares its settings with: {}'),
('mod-temucraft-account-wide', 8, 'no other account');
