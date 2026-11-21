/*
 * mod-temucraft-account-wide: account-wide collections and unlocks.
 *
 * What one character of an account has, every character of it gets (when it logs in, and new ones as they log in):
 *   mounts and companion pets      the spell, if the character's race and class may have it
 *   recipes and patterns           if the character has the profession, enough skill for the recipe and any
 *                                  specialization it needs (Gnomish/Goblin engineering, armorsmith...)
 *   flight paths                   the ones the character's faction can use
 *   titles
 *   vanity items                   (world table mod_temucraft_vanity_item, e.g. the Stones and Scrolls of Homecoming)
 *                                  one bought or found by any character is unlocked for the account; each character of
 *                                  a faction that may use it can summon it again (.collection summon, or the Vanity
 *                                  window of the Temucraft client, addon prefix TVA). Never twice, and its cooldown
 *                                  stays with the character, so summoning it again doesn't reset it.
 * Locked trainers (world table mod_temucraft_locked_trainer): a trainer stays closed until any character of the
 * account has done the quest that unlocks it; then it is open to every character.
 *
 * .collection shows what the account has shared, .collection vanity lists the vanity items. Each part has a switch in
 * the config.
 */

#include "Chat.h"
#include "TemucraftModuleText.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Creature.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "TemucraftAddonMessage.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

namespace
{
    enum Kind : uint8 { KIND_MOUNT = 1, KIND_PET = 2, KIND_RECIPE = 3, KIND_TAXI = 4, KIND_TITLE = 5, KIND_QUEST = 6, KIND_VANITY = 7 };
    constexpr uint8 KIND_COUNT = 8;
    constexpr char const* PREFIX = "TVA";
    constexpr uint32 SKILL_MOUNTS = 777, SKILL_PETS = 778;
    std::unordered_set<uint32> const PROFESSION_SKILLS = { 164, 165, 171, 197, 202, 333, 755, 773, 185, 129 };

    struct CollectionSettings
    {
        bool mounts = true, pets = true, recipes = true, taxi = true, titles = true, trainers = true, vanity = true, announce = true;
    } acfg;

    struct LockedTrainer { uint32 quest; std::string hint; };
    std::unordered_map<uint32, LockedTrainer> lockedTrainers;     // creature entry
    std::unordered_set<uint32> unlockQuests;
    std::unordered_map<uint32, std::set<std::pair<uint8, uint32>>> accounts;   // account -> (kind, id)

    struct VanityItem { uint32 item; std::string category; uint32 sort; };
    std::vector<VanityItem> vanityItems;                                       // in window order
    std::unordered_set<uint32> vanitySet;

    bool Enabled(uint8 kind)
    {
        switch (kind)
        {
            case KIND_MOUNT: return acfg.mounts;
            case KIND_PET: return acfg.pets;
            case KIND_RECIPE: return acfg.recipes;
            case KIND_TAXI: return acfg.taxi;
            case KIND_TITLE: return acfg.titles;
            case KIND_QUEST: return acfg.trainers;
            case KIND_VANITY: return acfg.vanity;
        }
        return false;
    }

    // The collection kind of a spell and its skill line entry (mounts, companions, profession recipes)
    uint8 SpellKind(uint32 spellId, SkillLineAbilityEntry const** abilityOut = nullptr)
    {
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            uint8 kind = ability->SkillLine == SKILL_MOUNTS ? KIND_MOUNT : ability->SkillLine == SKILL_PETS ? KIND_PET :
                         PROFESSION_SKILLS.count(ability->SkillLine) && ability->AcquireMethod == 0 ? KIND_RECIPE : 0;
            if (!kind)
                continue;
            if (kind == KIND_RECIPE)
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
                    if (info->HasEffect(SPELL_EFFECT_SKILL) || info->HasEffect(SPELL_EFFECT_SKILL_STEP))
                        return 0;   // the profession ranks themselves
            if (abilityOut)
                *abilityOut = ability;
            return kind;
        }
        return 0;
    }

    // true if the account didn't have it yet
    bool Store(uint32 account, uint8 kind, uint32 id)
    {
        if (!Enabled(kind) || !accounts[account].insert({ kind, id }).second)
            return false;
        CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_account_collection` (`account`, `kind`, `id`) VALUES ({}, {}, {})", account, kind, id);
        return true;
    }

    void LoadAccount(uint32 account)
    {
        auto& set = accounts[account];
        set.clear();
        if (QueryResult r = CharacterDatabase.Query("SELECT `kind`, `id` FROM `mod_temucraft_account_collection` WHERE `account` = {}", account))
            do { set.insert({ r->Fetch()[0].Get<uint8>(), r->Fetch()[1].Get<uint32>() }); } while (r->NextRow());
    }

    // Everything this character has that the account should keep
    void Gather(Player* player)
    {
        uint32 account = player->GetSession()->GetAccountId();
        for (auto const& [spellId, spell] : player->GetSpellMap())
            if (spell->State != PLAYERSPELL_REMOVED)
                if (uint8 kind = SpellKind(spellId))
                    Store(account, kind, spellId);
        for (uint32 i = 0; i < sTaxiNodesStore.GetNumRows(); ++i)
            if (sTaxiNodesStore.LookupEntry(i) && player->m_taxi.IsTaximaskNodeKnown(i))
                Store(account, KIND_TAXI, i);
        for (uint32 i = 0; i < sCharTitlesStore.GetNumRows(); ++i)
            if (CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(i))
                if (player->HasTitle(title))
                    Store(account, KIND_TITLE, i);
        for (uint32 quest : unlockQuests)
            if (player->IsQuestRewarded(quest))
                Store(account, KIND_QUEST, quest);
        for (VanityItem const& v : vanityItems)
            if (player->HasItemCount(v.item, 1, true))
                Store(account, KIND_VANITY, v.item);
    }

    bool CanHaveSpell(Player* player, uint32 spellId, uint8 kind, SkillLineAbilityEntry const* ability)
    {
        if (ability->RaceMask && !(ability->RaceMask & player->getRaceMask()))
            return false;
        if (ability->ClassMask && !(ability->ClassMask & player->getClassMask()))
            return false;
        if (kind == KIND_RECIPE)
        {
            if (!player->HasSkill(ability->SkillLine) || player->GetSkillValue(ability->SkillLine) < ability->MinSkillLineRank)
                return false;
            for (auto const& [spell, required] : sSpellMgr->GetSpellsRequiredForSpellBounds(spellId))
                if (!player->HasSpell(required))
                    return false;
        }
        return true;
    }

    // Gives this character what its account has
    void Apply(Player* player)
    {
        uint32 account = player->GetSession()->GetAccountId();
        uint32 counts[KIND_COUNT] = {};
        TeamId team = player->GetTeamId();
        for (auto const& [kind, id] : accounts[account])
        {
            if (!Enabled(kind))
                continue;
            switch (kind)
            {
                case KIND_MOUNT: case KIND_PET: case KIND_RECIPE:
                {
                    SkillLineAbilityEntry const* ability = nullptr;
                    if (player->HasSpell(id) || SpellKind(id, &ability) != kind || !ability || !CanHaveSpell(player, id, kind, ability))
                        break;
                    player->learnSpell(id);
                    ++counts[kind];
                    break;
                }
                case KIND_TAXI:
                {
                    TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(id);
                    if (!node || player->m_taxi.IsTaximaskNodeKnown(id) || !node->MountCreatureID[team == TEAM_ALLIANCE ? 1 : 0])
                        break;
                    player->m_taxi.SetTaximaskNode(id);
                    ++counts[kind];
                    break;
                }
                case KIND_TITLE:
                    if (CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(id))
                        if (!player->HasTitle(title))
                        {
                            player->SetTitle(title);
                            ++counts[kind];
                        }
                    break;
            }
        }
        if (acfg.announce && (counts[1] || counts[2] || counts[3] || counts[4] || counts[5]))
            ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player->GetSession(), 1, "|cff66ccff[Account]|r From your other characters: {} mounts, {} pets, "
                "{} recipes, {} flight paths, {} titles."), counts[1], counts[2], counts[3], counts[4], counts[5]);
    }

    bool AccountHasQuest(Player* player, uint32 quest)
    {
        return accounts[player->GetSession()->GetAccountId()].count({ KIND_QUEST, quest }) > 0;
    }

    // ---- vanity items ----------------------------------------------------------------------------------------------

    bool AccountHasVanity(Player* player, uint32 item)
    {
        return accounts[player->GetSession()->GetAccountId()].count({ KIND_VANITY, item }) > 0;
    }

    // May this character have it at all (faction, race, class; its level is checked when it is used)
    bool VanityAllowed(Player* player, ItemTemplate const* proto)
    {
        if (!proto)
            return false;
        TeamId team = player->GetTeamId(true);
        if (proto->HasFlag2(ITEM_FLAG2_FACTION_HORDE) && team != TEAM_HORDE)
            return false;
        if (proto->HasFlag2(ITEM_FLAG2_FACTION_ALLIANCE) && team != TEAM_ALLIANCE)
            return false;
        return (proto->AllowableRace & player->getRaceMask()) && (proto->AllowableClass & player->getClassMask());
    }

    std::string ItemLink(ItemTemplate const* proto)
    {
        static char const* const colors[] = { "ff9d9d9d", "ffffffff", "ff1eff00", "ff0070dd", "ffa335ee", "ffff8000", "ffe6cc80", "ffe6cc80" };
        return Acore::StringFormat("|c{}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r", colors[proto->Quality < 8 ? proto->Quality : 1], proto->ItemId, proto->Name1);
    }

    uint32 CooldownSeconds(Player* player, ItemTemplate const* proto)
    {
        uint32 spell = proto->Spells[0].SpellId;
        return spell ? (player->GetSpellCooldownDelay(spell) + 999) / 1000 : 0;
    }

    // The Vanity window's list: every vanity item this character may have, unlocked or not
    void SendVanity(Player* player)
    {
        if (!acfg.vanity)
            return;
        std::vector<std::string> lines;
        uint32 owned = 0;
        for (VanityItem const& v : vanityItems)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(v.item);
            if (!VanityAllowed(player, proto))
                continue;
            bool unlocked = AccountHasVanity(player, v.item);
            owned += unlocked;
            // ITEM~entry~unlocked~in bags~cooldown seconds~required level~category~name
            lines.push_back(Acore::StringFormat("ITEM~{}~{}~{}~{}~{}~{}~{}", v.item, unlocked ? 1 : 0, player->HasItemCount(v.item, 1, true) ? 1 : 0,
                CooldownSeconds(player, proto), proto->RequiredLevel, TemucraftAddonClean(v.category), TemucraftAddonClean(proto->Name1)));
        }
        TemucraftAddonSend(player, PREFIX, Acore::StringFormat("BEGIN~{}~{}", owned, lines.size()));
        for (std::string const& line : lines)
            TemucraftAddonSend(player, PREFIX, line);
        TemucraftAddonSend(player, PREFIX, "END");
    }

    // Puts an unlocked vanity item back in the bags (never a second one)
    bool SummonVanity(Player* player, uint32 item, ChatHandler& chat)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
        if (!acfg.vanity || !proto || !vanitySet.count(item))
        {
            chat.PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 18, "|cff66ccff[Account]|r That isn't a vanity item."));
            return false;
        }
        if (!AccountHasVanity(player, item))
        {
            chat.PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 15, "|cff66ccff[Account]|r Your account hasn't unlocked {} yet."), ItemLink(proto));
            return false;
        }
        if (!VanityAllowed(player, proto))
        {
            chat.PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 17, "|cff66ccff[Account]|r {} is not for this character."), ItemLink(proto));
            return false;
        }
        if (player->HasItemCount(item, 1, true))
        {
            chat.PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 16, "|cff66ccff[Account]|r You already have {} (bags or bank)."), ItemLink(proto));
            return false;
        }
        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, 1);
        if (msg != EQUIP_ERR_OK)
        {
            player->SendEquipError(msg, nullptr, nullptr, item);
            return false;
        }
        if (Item* added = player->StoreNewItem(dest, item, true))
            player->SendNewItem(added, 1, true, false);
        chat.PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 19, "|cff66ccff[Account]|r Summoned {}."), ItemLink(proto));
        return true;
    }

    void OnVanityObtained(Player* player, uint32 item)
    {
        if (!vanitySet.count(item) || !Store(player->GetSession()->GetAccountId(), KIND_VANITY, item))
            return;
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item))
            ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player, 10,
                "|cff66ccff[Account]|r {} is now unlocked for your whole account: summon it again any time from the Vanity window (/vanity)."), ItemLink(proto));
        TemucraftAddonSend(player, PREFIX, Acore::StringFormat("NEW~{}", item));
    }
}

class TemucraftAccountCollections_Player : public PlayerScript
{
public:
    TemucraftAccountCollections_Player() : PlayerScript("TemucraftAccountCollections_Player") { }

    void OnPlayerLogin(Player* player) override
    {
        LoadAccount(player->GetSession()->GetAccountId());
        Gather(player);
        Apply(player);
    }

    void OnPlayerSave(Player* player) override { Gather(player); }

    // the logout save has run by now (it gathers first); the account is read again at the next login
    void OnPlayerLogout(Player* player) override { accounts.erase(player->GetSession()->GetAccountId()); }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (uint8 kind = SpellKind(spellId))
            Store(player->GetSession()->GetAccountId(), kind, spellId);
    }

    void OnPlayerLearnTaxiNode(Player const* player, uint32 nodeId) override
    {
        Store(player->GetSession()->GetAccountId(), KIND_TAXI, nodeId);
    }

    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
    {
        if (quest && unlockQuests.count(quest->GetQuestId()))
            Store(player->GetSession()->GetAccountId(), KIND_QUEST, quest->GetQuestId());
    }

    // Every new item in the bags: bought, looted, made, a quest reward or summoned
    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/) override
    {
        if (acfg.vanity && item)
            OnVanityObtained(player, item->GetEntry());
    }

    // The Vanity window: LIST, SUMMON~item
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        std::string body;
        if (!TemucraftAddonParse(player, type, language, msg, receiver, PREFIX, body))
            return true;
        std::vector<std::string> f = TemucraftAddonFields(body);
        if (f.empty() || !acfg.vanity)
            return false;
        if (f[0] == "LIST")
            SendVanity(player);
        else if (f[0] == "SUMMON" && f.size() > 1)
        {
            ChatHandler chat(player->GetSession());
            SummonVanity(player, Acore::StringTo<uint32>(f[1]).value_or(0), chat);
            SendVanity(player);
        }
        return false;
    }
};

class TemucraftAccountCollections_Creature : public AllCreatureScript
{
public:
    TemucraftAccountCollections_Creature() : AllCreatureScript("TemucraftAccountCollections_Creature") { }

    // A locked trainer talks to nobody until the account has done its quest
    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!acfg.trainers || player->IsGameMaster())
            return false;
        auto itr = lockedTrainers.find(creature->GetEntry());
        if (itr == lockedTrainers.end() || AccountHasQuest(player, itr->second.quest))
            return false;
        CloseGossipMenuFor(player);
        ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", player->GetSession(), 2, "|cff66ccff[Account]|r {}"), itr->second.hint.empty()
            ? "This trainer will not teach you yet." : itr->second.hint);
        return true;
    }
};

class TemucraftAccountCollections_World : public WorldScript
{
public:
    TemucraftAccountCollections_World() : WorldScript("TemucraftAccountCollections_World") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        acfg.mounts = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Mounts", true);
        acfg.pets = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Pets", true);
        acfg.recipes = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Recipes", true);
        acfg.taxi = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.FlightPaths", true);
        acfg.titles = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Titles", true);
        acfg.trainers = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.LockedTrainers", true);
        acfg.vanity = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Vanity", true);
        acfg.announce = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Announce", true);
    }

    void OnStartup() override
    {
        lockedTrainers.clear();
        unlockQuests.clear();
        if (QueryResult r = WorldDatabase.Query("SELECT `entry`, `quest`, `hint` FROM `mod_temucraft_locked_trainer`"))
            do
            {
                Field* f = r->Fetch();
                lockedTrainers[f[0].Get<uint32>()] = { f[1].Get<uint32>(), f[2].Get<std::string>() };
                unlockQuests.insert(f[1].Get<uint32>());
            } while (r->NextRow());

        vanityItems.clear();
        vanitySet.clear();
        if (QueryResult r = WorldDatabase.Query("SELECT `item`, `category`, `sort` FROM `mod_temucraft_vanity_item` ORDER BY `category`, `sort`, `item`"))
            do
            {
                Field* f = r->Fetch();
                uint32 item = f[0].Get<uint32>();
                if (!sObjectMgr->GetItemTemplate(item))
                    continue;
                vanityItems.push_back({ item, f[1].Get<std::string>(), f[2].Get<uint32>() });
                vanitySet.insert(item);
            } while (r->NextRow());
    }
};

class TemucraftAccountCollections_Commands : public CommandScript
{
public:
    TemucraftAccountCollections_Commands() : CommandScript("TemucraftAccountCollections_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable collectionTable =
        {
            { "",       HandleCollection, SEC_PLAYER, Console::No },
            { "vanity", HandleVanity,     SEC_PLAYER, Console::No },
            { "summon", HandleSummon,     SEC_PLAYER, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "collection", collectionTable },
        };
        return commandTable;
    }

    static bool HandleCollection(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        uint32 counts[KIND_COUNT] = {};
        for (auto const& [kind, id] : accounts[player->GetSession()->GetAccountId()])
            if (kind < KIND_COUNT)
                ++counts[kind];
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 3, "|cff66ccff[Account]|r Shared by your account: {} mounts, {} companion pets, {} recipes, {} flight paths, "
                                 "{} titles, {} trainer unlocks, {} vanity items."), counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7]);
        return true;
    }

    // Lists the vanity items in chat and opens the Vanity window
    static bool HandleVanity(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!acfg.vanity)
        {
            handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 9, "|cff66ccff[Account]|r Vanity items are turned off on this server."));
            return true;
        }
        uint32 owned = 0, total = 0;
        std::vector<std::string> lines;
        for (VanityItem const& v : vanityItems)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(v.item);
            if (!VanityAllowed(player, proto))
                continue;
            ++total;
            if (!AccountHasVanity(player, v.item))
                continue;
            ++owned;
            lines.push_back(Acore::StringFormat("{}{}", ItemLink(proto), player->HasItemCount(v.item, 1, true)
                ? TemucraftModuleText("mod-temucraft-account-wide", handler, 13, " (you have it)") : ""));
        }
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 11, "|cff66ccff[Account]|r Vanity items: {} of {} unlocked."), owned, total);
        for (std::string const& line : lines)
            handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 12, "  {}"), line);
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 20, "|cff66ccff[Account]|r Summon one with .collection summon <name>, or use the Vanity window (/vanity)."));
        TemucraftAddonSend(player, PREFIX, "OPEN");
        return true;
    }

    // .collection summon <item id, link or part of the name>
    static bool HandleSummon(ChatHandler* handler, Tail text)
    {
        Player* player = handler->GetPlayer();
        std::string want(text);
        uint32 item = 0;
        // an item link: |Hitem:123:...
        if (size_t at = want.find("Hitem:"); at != std::string::npos)
            item = Acore::StringTo<uint32>(want.substr(at + 6, want.find(':', at + 6) - at - 6)).value_or(0);
        else if (!want.empty() && std::all_of(want.begin(), want.end(), [](unsigned char c) { return std::isdigit(c); }))
            item = Acore::StringTo<uint32>(want).value_or(0);
        else if (!want.empty())
        {
            auto lower = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
            std::string needle = lower(want);
            for (VanityItem const& v : vanityItems)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(v.item);
                if (!VanityAllowed(player, proto) || !AccountHasVanity(player, v.item))
                    continue;
                std::string name = lower(proto->Name1);
                if (name == needle) { item = v.item; break; }
                if (!item && name.find(needle) != std::string::npos)
                    item = v.item;
            }
        }
        if (!item)
        {
            handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 14, "|cff66ccff[Account]|r No unlocked vanity item matches \"{}\"."), want);
            return true;
        }
        SummonVanity(player, item, *handler);
        SendVanity(player);   // refreshes the Vanity window, if it is there
        return true;
    }
};

void AddTemucraftAccountCollectionsScripts()
{
    new TemucraftAccountCollections_Player();
    new TemucraftAccountCollections_Creature();
    new TemucraftAccountCollections_World();
    new TemucraftAccountCollections_Commands();
}
