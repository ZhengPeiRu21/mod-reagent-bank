
CREATE TABLE IF NOT EXISTS `custom_reagent_bank` (
    `character_id` int(11) NOT NULL,
    `item_entry` int(11) NOT NULL,
    `item_subclass` int(11) NOT NULL,
    `amount` int(11) NOT NULL,
    PRIMARY KEY (`character_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
