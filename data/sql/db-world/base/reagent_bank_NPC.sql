
SET
@Entry = 290011,
@Name = "Ling";

DELETE FROM `creature_template` WHERE `entry` = @Entry;
DELETE FROM `creature_template_model` WHERE `CreatureID` = @Entry;

INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `rank`, `dmgschool`, `baseattacktime`, `rangeattacktime`, `unit_class`, `unit_flags`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`, `AIName`, `MovementType`, `HoverHeight`, `RacialLeader`, `movementId`, `RegenHealth`, `mechanic_immune_mask`, `flags_extra`, `ScriptName`) VALUES
(@Entry, @Name, 'Reagent Banker', NULL, 0, 6, 6, 0, 35, 1, 0, 0, 2000, 0, 1, 0, 7, 138412032, 0, 0, 0, '', 0, 1, 0, 0, 1, 0, 2, 'npc_reagent_banker');
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@Entry, 0, 15965, 1.0, 1.0, NULL);
