-- mod-temucraft-account-wide: vanity items. One bought or found by any character is unlocked for its whole account
-- (characters table mod_temucraft_account_collection, kind 7), and every character that may use it can summon it again
-- (.collection summon, or the Vanity window of the Temucraft client). Other modules add their items here (for example
-- mod-temucraft-travel: the Stones and Scrolls of Homecoming); they make this table too if it is missing.
-- category is the heading in the Vanity window, sort the order inside it.
CREATE TABLE IF NOT EXISTS `mod_temucraft_vanity_item` (
  `item` int unsigned NOT NULL COMMENT 'item_template.entry',
  `category` varchar(32) NOT NULL DEFAULT '' COMMENT 'heading in the Vanity window',
  `sort` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'order inside its heading',
  PRIMARY KEY (`item`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Temucraft: vanity items (account collections)';
