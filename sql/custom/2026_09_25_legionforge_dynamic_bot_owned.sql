-- Run against the characters database before enabling LegionForge.DynamicBots.Enable.
-- This ledger is the only authority for automatic character deletion.
CREATE TABLE IF NOT EXISTS `lf_dynamic_bot_owned` (
  `account` int unsigned NOT NULL,
  `guid` int unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`account`),
  KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
