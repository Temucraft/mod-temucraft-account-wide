-- mod-temucraft-account-wide: trainers that stay closed until any character of the account has done a quest.
-- Empty by default: add rows, e.g. (entry of the trainer, the quest, 'Prove yourself to the Argent Dawn first.').
CREATE TABLE IF NOT EXISTS `mod_temucraft_locked_trainer` (
  `entry` int unsigned NOT NULL COMMENT 'creature_template.entry of the trainer',
  `quest` int unsigned NOT NULL COMMENT 'quest any character of the account must have done',
  `hint` varchar(255) NOT NULL DEFAULT '' COMMENT 'said to players it turns away',
  PRIMARY KEY (`entry`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-temucraft-account-wide: locked trainers';
