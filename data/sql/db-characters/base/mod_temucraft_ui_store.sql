-- mod-temucraft-account-wide: what the Temucraft interface keeps between sessions (favourite mounts and pets,
-- talent loadouts), per account (guid 0) or per character (addon prefix TUS)
CREATE TABLE IF NOT EXISTS `mod_temucraft_ui_store` (
  `account` int unsigned NOT NULL,
  `guid` int unsigned NOT NULL DEFAULT '0',
  `key` varchar(48) NOT NULL,
  `value` text NOT NULL,
  PRIMARY KEY (`account`, `guid`, `key`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-temucraft-account-wide: Temucraft interface store';
