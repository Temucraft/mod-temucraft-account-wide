-- mod-temucraft-account-wide: what an account's characters have collected, shared with all of them
-- kind: 1 mount, 2 companion pet, 3 recipe (spell ids), 4 flight path (TaxiNodes id), 5 title (CharTitles id),
--       6 quest done that unlocks a trainer, 7 vanity item (item_template entry, see mod_temucraft_vanity_item)
CREATE TABLE IF NOT EXISTS `mod_temucraft_account_collection` (
  `account` int unsigned NOT NULL, `kind` tinyint unsigned NOT NULL, `id` int unsigned NOT NULL,
  PRIMARY KEY (`account`, `kind`, `id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-temucraft-account-wide: account collections';
