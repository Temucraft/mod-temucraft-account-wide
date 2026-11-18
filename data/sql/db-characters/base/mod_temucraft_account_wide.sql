-- mod-temucraft-account-wide: reputation progress per account (standing without the racial base, like character_reputation)
CREATE TABLE IF NOT EXISTS `mod_temucraft_account_wide_reputation` (
  `account` int unsigned NOT NULL,
  `faction` smallint unsigned NOT NULL,
  `standing` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`account`, `faction`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
