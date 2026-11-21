-- mod-temucraft-account-wide: its commands (all under .temucraft) as RBAC permissions 101850-101899. Every command is admin only by
-- default: linked to the administrator roles 192 and 1014. To give one to players, link it to 195 and 1010 here (or
-- grant it per account with .rbac account grant <account> <id>). "Temucraft: use ..." permissions are not
-- commands: they stay linked to the player roles so the items keep working.
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
(192, 101850),
(1014, 101850),
(192, 101851),
(1014, 101851),
(192, 101852),
(1014, 101852),
(192, 101853),
(1014, 101853),
(192, 101854),
(1014, 101854),
(192, 101855),
(1014, 101855);
