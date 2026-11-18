-- mod-temucraft-account-wide: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-account-wide';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-account-wide', 1, '|cff66ccff[Account]|r From your other characters: {} mounts, {} pets, {} recipes, {} flight paths, {} titles.'),
('mod-temucraft-account-wide', 2, '|cff66ccff[Account]|r {}'),
('mod-temucraft-account-wide', 3, '|cff66ccff[Account]|r Shared by your account: {} mounts, {} companion pets, {} recipes, {} flight paths, {} titles, {} trainer unlocks.');
