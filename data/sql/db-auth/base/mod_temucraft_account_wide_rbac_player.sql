-- mod-temucraft-account-wide: its player commands (all under .temucraft) as RBAC permissions 101850-101899. Linked to the stock player
-- role 195 and the Temucraft player role 1010, so every account has them by default. To take one away, remove its
-- link here (or deny it per account with .rbac account deny <account> <id>); to give it to staff only, link it to
-- 1011-1014 instead.
DELETE FROM `rbac_linked_permissions` WHERE `linkedId` BETWEEN 101850 AND 101899;
DELETE FROM `rbac_permissions` WHERE `id` BETWEEN 101850 AND 101899;
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(101850, 'Command: temucraft collection'),
(101851, 'Command: temucraft collection vanity'),
(101852, 'Command: temucraft collection summon'),
(101853, 'Command: temucraft shared link'),
(101854, 'Command: temucraft shared unlink'),
(101855, 'Command: temucraft shared list');
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(195, 101850),
(1010, 101850),
(195, 101851),
(1010, 101851),
(195, 101852),
(1010, 101852),
(195, 101853),
(1010, 101853),
(195, 101854),
(1010, 101854),
(195, 101855),
(1010, 101855);
