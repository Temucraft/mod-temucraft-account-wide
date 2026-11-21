-- mod-temucraft-account-wide: accounts that share their game settings, key bindings and macros
-- (linked with .temucraft shared link; accounts with the same e-mail share without a row here when LinkByEmail is on)
CREATE TABLE IF NOT EXISTS `mod_temucraft_settings_link` (
  `account_id` int unsigned NOT NULL,
  `group_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`), KEY `group_id` (`group_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-temucraft-account-wide: linked accounts';
