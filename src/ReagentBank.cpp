/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ReagentBank.h"

// Add player scripts
class npc_reagent_banker : public CreatureScript
{
private:
    std::string GetItemLink(uint32 entry, WorldSession* session) const
    {
        int loc_idx = session->GetSessionDbLocaleIndex();
        const ItemTemplate *temp = sObjectMgr->GetItemTemplate(entry);
        std::string name = temp->Name1;
        if (ItemLocale const* il = sObjectMgr->GetItemLocale(temp->ItemId))
            ObjectMgr::GetLocaleString(il->Name, loc_idx, name);

        std::ostringstream oss;
        oss << "|c" << std::hex << ItemQualityColors[temp->Quality] << std::dec <<
            "|Hitem:" << temp->ItemId << ":" <<
            (uint32)0 << "|h[" << name << "]|h|r";

        return oss.str();
    }

    std::string GetItemIcon(uint32 entry, uint32 width, uint32 height, int x, int y) const
    {
        std::ostringstream ss;
        ss << "|TInterface";
        const ItemTemplate *temp = sObjectMgr->GetItemTemplate(entry);
        const ItemDisplayInfoEntry *dispInfo = NULL;
        if (temp)
        {
            dispInfo = sItemDisplayInfoStore.LookupEntry(temp->DisplayInfoID);
            if (dispInfo)
                ss << "/ICONS/" << dispInfo->inventoryIcon;
        }
        if (!dispInfo)
            ss << "/InventoryItems/WoWUnknownItem01";
        ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";
        return ss.str();
    }

    void WithdrawItem(Player* player, uint32 entry)
    {
        // This query can be changed to async to improve performance, but there will be some visual bugs because the query will not be done executing when the menu refreshes
        std::string query = "SELECT amount FROM custom_reagent_bank WHERE character_id = " + std::to_string(player->GetGUID().GetCounter()) + " AND item_entry = " + std::to_string(entry);
        QueryResult result = CharacterDatabase.Query("SELECT amount FROM custom_reagent_bank WHERE character_id = " + std::to_string(player->GetGUID().GetCounter()) + " AND item_entry = " + std::to_string(entry));
        if (result)
        {
            uint32 storedAmount = (*result)[0].Get<uint32>();
            const ItemTemplate *temp = sObjectMgr->GetItemTemplate(entry);
            uint32 stackSize = temp->GetMaxStackSize();
            if (storedAmount <= stackSize)
            {
                // Give the player all of the item and remove it from the DB
                ItemPosCountVec dest;
                InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, storedAmount);
                if (msg == EQUIP_ERR_OK)
                {
                    CharacterDatabase.Execute("DELETE FROM custom_reagent_bank WHERE character_id = {} AND item_entry = {}", player->GetGUID().GetCounter(), entry);
                    Item* item = player->StoreNewItem(dest, entry, true);
                    player->SendNewItem(item, storedAmount, true, false);
                }
                else
                {
                    player->SendEquipError(msg, nullptr, nullptr, entry);
                    return;
                }
            }
            else
            {
                // Give the player a single stack
                ItemPosCountVec dest;
                InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, stackSize);
                if (msg == EQUIP_ERR_OK)
                {
                    CharacterDatabase.Execute("UPDATE custom_reagent_bank SET amount = {} WHERE character_id = {} AND item_entry = {}", storedAmount - stackSize, player->GetGUID().GetCounter(), entry);
                    Item* item = player->StoreNewItem(dest, entry, true);
                    player->SendNewItem(item, stackSize, true, false);
                }
                else
                {
                    player->SendEquipError(msg, nullptr, nullptr, entry);
                    return;
                }
            }
        }
    }

    void UpdateItemCount(std::map<uint32, uint32> &entryToAmountMap, std::map<uint32, uint32> &entryToSubclassMap, Item* pItem, Player* player, uint32 bagSlot, uint32 itemSlot)
    {
        uint32 count = pItem->GetCount();
        ItemTemplate const *itemTemplate = pItem->GetTemplate();
        
        if (!(itemTemplate->Class == ITEM_CLASS_TRADE_GOODS || itemTemplate->Class == ITEM_CLASS_GEM) || itemTemplate->GetMaxStackSize() == 1)
            return;
        uint32 itemEntry = itemTemplate->ItemId;
        uint32 itemSubclass = itemTemplate->SubClass;
        
        // Put gems to ITEM_SUBCLASS_JEWELCRAFTING section
        if (itemTemplate->Class == ITEM_CLASS_GEM)
        {
            itemSubclass = ITEM_SUBCLASS_JEWELCRAFTING;
        }

        if (!entryToAmountMap.count(itemEntry))
        {
            // Item does not exist yet in storage
            entryToAmountMap[itemEntry] = count;
            entryToSubclassMap[itemEntry] = itemSubclass;
        }
        else
        {
            entryToAmountMap[itemEntry] = entryToAmountMap.find(itemEntry)->second + count;
        }
        // The item counts have been updated, remove the original items from the player
        player->DestroyItem(bagSlot, itemSlot, true);
    }

    void DepositAllReagents(Player* player) {
        WorldSession *session = player->GetSession();
        std::string query = "SELECT item_entry, item_subclass, amount FROM custom_reagent_bank WHERE character_id = " + std::to_string(player->GetGUID().GetCounter());
        session->GetQueryProcessor().AddCallback( CharacterDatabase.AsyncQuery(query).WithCallback([=, this](QueryResult result) {
            std::map<uint32, uint32> entryToAmountMap;
            std::map<uint32, uint32> entryToSubclassMap;
            if (result)
            {
                do {
                    uint32 itemEntry = (*result)[0].Get<uint32>();
                    uint32 itemSubclass = (*result)[1].Get<uint32>();
                    uint32 itemAmount = (*result)[2].Get<uint32>();
                    entryToAmountMap[itemEntry] = itemAmount;
                    entryToSubclassMap[itemEntry] = itemSubclass;
                } while (result->NextRow());
            }
            // Inventory Items
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            {
                if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    UpdateItemCount(entryToAmountMap, entryToSubclassMap, pItem, player, INVENTORY_SLOT_BAG_0, i);
                }

            }
            // Bag Items
            for (uint32 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
            {
                Bag* bag = player->GetBagByPos(i);
                if (!bag)
                    continue;
                for (uint32 j = 0; j < bag->GetBagSize(); j++) {
                    if (Item * pItem = player->GetItemByPos(i, j))
                    {
                        UpdateItemCount(entryToAmountMap, entryToSubclassMap, pItem, player, i, j);
                    }
                }
            }
            if (entryToAmountMap.size() != 0)
            {
                auto trans = CharacterDatabase.BeginTransaction();
                for (std::pair<uint32, uint32> mapEntry : entryToAmountMap)
                {
                    uint32 itemEntry = mapEntry.first;
                    uint32 itemAmount = mapEntry.second;
                    uint32 itemSubclass = entryToSubclassMap.find(itemEntry)->second;
                    trans->Append("REPLACE INTO custom_reagent_bank (character_id, item_entry, item_subclass, amount) VALUES ({}, {}, {}, {})", player->GetGUID().GetCounter(), itemEntry, itemSubclass, itemAmount);
                }
                CharacterDatabase.CommitTransaction(trans);
            }
        }));
        ChatHandler(player->GetSession()).PSendSysMessage("All reagents deposited successfully.");
        CloseGossipMenuFor(player);
    }

public:
    npc_reagent_banker() : CreatureScript("npc_reagent_banker") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(4359, 30, 30, -18, 0) + "Parts", ITEM_SUBCLASS_PARTS, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(4358, 30, 30, -18, 0) + "Explosives", ITEM_SUBCLASS_EXPLOSIVES, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(4388, 30, 30, -18, 0) + "Devices", ITEM_SUBCLASS_DEVICES, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(1206, 30, 30, -18, 0) + "Jewelcrafting", ITEM_SUBCLASS_JEWELCRAFTING, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(2589, 30, 30, -18, 0) + "Cloth", ITEM_SUBCLASS_CLOTH, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(2318, 30, 30, -18, 0) + "Leather", ITEM_SUBCLASS_LEATHER, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(2772, 30, 30, -18, 0) + "Metal & Stone", ITEM_SUBCLASS_METAL_STONE, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(12208, 30, 30, -18, 0) + "Meat", ITEM_SUBCLASS_MEAT, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(2453, 30, 30, -18, 0) + "Herb", ITEM_SUBCLASS_HERB, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(7068, 30, 30, -18, 0) + "Elemental", ITEM_SUBCLASS_ELEMENTAL, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(10940, 30, 30, -18, 0) + "Enchanting", ITEM_SUBCLASS_ENCHANTING, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(23572, 30, 30, -18, 0) + "Nether Material", ITEM_SUBCLASS_MATERIAL, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(2604, 30, 30, -18, 0) + "Other Trade Goods", ITEM_SUBCLASS_TRADE_GOODS_OTHER, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(38682, 30, 30, -18, 0) + "Armor Vellum", ITEM_SUBCLASS_ARMOR_ENCHANTMENT, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(39349, 30, 30, -18, 0) + "Weapon Vellum", ITEM_SUBCLASS_WEAPON_ENCHANTMENT, 0);
        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Deposit All Reagents", DEPOSIT_ALL_REAGENTS, 0);
        SendGossipMenuFor(player, NPC_TEXT_ID, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 item_subclass, uint32 gossipPageNumber) override
    {
        player->PlayerTalkClass->ClearMenus();
        if (item_subclass > MAX_PAGE_NUMBER)
        {
            // item_subclass is actually an item ID to withdraw
            // Get the actual item subclass from the template
            const ItemTemplate *temp = sObjectMgr->GetItemTemplate(item_subclass);
            WithdrawItem(player, item_subclass);
            if (temp->Class == ITEM_CLASS_GEM)
            {
                // Get back to ITEM_SUBCLASS_JEWELCRAFTING section when withdrawing gems
                ShowReagentItems(player, creature, ITEM_SUBCLASS_JEWELCRAFTING, gossipPageNumber);
            }
            else
            {
                ShowReagentItems(player, creature, temp->SubClass, gossipPageNumber);
            }
            return true;
        }
        if (item_subclass == DEPOSIT_ALL_REAGENTS)
        {
            DepositAllReagents(player);
            return true;
        }
        else if (item_subclass == MAIN_MENU)
        {
            OnGossipHello(player, creature);
            return true;
        }
        else
        {
            ShowReagentItems(player, creature, item_subclass, gossipPageNumber);
            return true;
        }
    }

    void ShowReagentItems(Player* player, Creature* creature, uint32 item_subclass, uint16 gossipPageNumber)
    {
        WorldSession* session = player->GetSession();
        std::string query = "SELECT item_entry, amount FROM custom_reagent_bank WHERE character_id = " + std::to_string(player->GetGUID().GetCounter()) + " AND item_subclass = " +
                std::to_string(item_subclass) + " ORDER BY item_entry";
        session->GetQueryProcessor().AddCallback(CharacterDatabase.AsyncQuery(query).WithCallback([=, this](QueryResult result)
        {
            uint32 startValue = (gossipPageNumber * (MAX_OPTIONS));
            uint32 endValue = (gossipPageNumber + 1) * (MAX_OPTIONS) - 1;
            std::map<uint32, uint32> entryToAmountMap;
            std::vector<uint32> itemEntries;
            if (result) {
                do {
                    uint32 itemEntry = (*result)[0].Get<uint32>();
                    uint32 itemAmount = (*result)[1].Get<uint32>();
                    entryToAmountMap[itemEntry] = itemAmount;
                    itemEntries.push_back(itemEntry);
                } while (result->NextRow());
            }
            for (uint32 i = startValue; i <= endValue; i++)
            {
                if (itemEntries.empty() || i > itemEntries.size() - 1)
                {
                    break;
                }
                uint32 itemEntry = itemEntries.at(i);
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, GetItemIcon(itemEntry, 30, 30, -18, 0) + GetItemLink(itemEntry, session) + " (" + std::to_string(entryToAmountMap.find(itemEntry)->second) + ")", itemEntry, gossipPageNumber);
            }
            if (gossipPageNumber > 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Previous Page", item_subclass, gossipPageNumber - 1);
            }
            if (endValue < entryToAmountMap.size())
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Next Page", item_subclass, gossipPageNumber + 1);
            }
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "|TInterface/ICONS/Ability_Spy:30:30:-18:0|tBack...", MAIN_MENU, 0);
            SendGossipMenuFor(player, NPC_TEXT_ID, creature->GetGUID());
        }));
    }
};

// Add all scripts in one
void AddSC_mod_reagent_bank()
{
    new npc_reagent_banker();
}
