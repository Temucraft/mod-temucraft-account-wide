-- mod-temucraft-account-wide: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-account-wide';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-account-wide', 1, '|cff66ccff[Account]|r From your other characters: {} mounts, {} pets, {} recipes, {} flight paths, {} titles.'),
('mod-temucraft-account-wide', 2, '|cff66ccff[Account]|r {}'),
('mod-temucraft-account-wide', 3, '|cff66ccff[Account]|r Shared by your account: {} mounts, {} companion pets, {} recipes, {} flight paths, {} titles, {} trainer unlocks, {} vanity items.'),
('mod-temucraft-account-wide', 4, '|cff66ccff[Account]|r That account name and password don''t match.'),
('mod-temucraft-account-wide', 5, '|cff66ccff[Account]|r Linked: your settings, key bindings and macros are now shared with {}.'),
('mod-temucraft-account-wide', 6, '|cff66ccff[Account]|r This account no longer shares its settings by link (accounts with the same e-mail still do, if the server allows it).'),
('mod-temucraft-account-wide', 7, '|cff66ccff[Account]|r Shares its settings with: {}'),
('mod-temucraft-account-wide', 8, 'no other account'),
('mod-temucraft-account-wide', 9, '|cff66ccff[Account]|r Vanity items are turned off on this server.'),
('mod-temucraft-account-wide', 10, '|cff66ccff[Account]|r {} is now unlocked for your whole account: summon it again any time from the Vanity window (/vanity).'),
('mod-temucraft-account-wide', 11, '|cff66ccff[Account]|r Vanity items: {} of {} unlocked.'),
('mod-temucraft-account-wide', 12, '  {}'),
('mod-temucraft-account-wide', 13, ' (you have it)'),
('mod-temucraft-account-wide', 14, '|cff66ccff[Account]|r No unlocked vanity item matches "{}".'),
('mod-temucraft-account-wide', 15, '|cff66ccff[Account]|r Your account hasn''t unlocked {} yet.'),
('mod-temucraft-account-wide', 16, '|cff66ccff[Account]|r You already have {} (bags or bank).'),
('mod-temucraft-account-wide', 17, '|cff66ccff[Account]|r {} is not for this character.'),
('mod-temucraft-account-wide', 18, '|cff66ccff[Account]|r That isn''t a vanity item.'),
('mod-temucraft-account-wide', 19, '|cff66ccff[Account]|r Summoned {}.'),
('mod-temucraft-account-wide', 20, '|cff66ccff[Account]|r Summon one with .collection summon <name>, or use the Vanity window (/vanity).');
