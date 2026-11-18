/*
 * mod-temucraft-account-wide: account-wide collections and unlocks.
 *
 * What one character of an account has, every character of it gets (when it logs in, and new ones as they log in):
 *   mounts and companion pets      the spell, if the character's race and class may have it
 *   recipes and patterns           if the character has the profession, enough skill for the recipe and any
 *                                  specialization it needs (Gnomish/Goblin engineering, armorsmith...)
 *   flight paths                   the ones the character's faction can use
 *   titles
 * Locked trainers (world table mod_temucraft_locked_trainer): a trainer stays closed until any character of the
 * account has done the quest that unlocks it; then it is open to every character.
 *
 * .collection shows what the account has shared. Each part has a switch in the config.
 */

#include "Chat.h"
#include "TemucraftModuleText.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

namespace
{
    enum Kind : uint8 { KIND_MOUNT = 1, KIND_PET = 2, KIND_RECIPE = 3, KIND_TAXI = 4, KIND_TITLE = 5, KIND_QUEST = 6 };
    constexpr uint32 SKILL_MOUNTS = 777, SKILL_PETS = 778;
    std::unordered_set<uint32> const PROFESSION_SKILLS = { 164, 165, 171, 197, 202, 333, 755, 773, 185, 129 };

    struct CollectionSettings
    {
        bool mounts = true, pets = true, recipes = true, taxi = true, titles = true, trainers = true, announce = true;
    } acfg;

    struct LockedTrainer { uint32 quest; std::string hint; };
    std::unordered_map<uint32, LockedTrainer> lockedTrainers;     // creature entry
    std::unordered_set<uint32> unlockQuests;
    std::unordered_map<uint32, std::set<std::pair<uint8, uint32>>> accounts;   // account -> (kind, id)

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

    void Store(uint32 account, uint8 kind, uint32 id)
    {
        if (!Enabled(kind) || !accounts[account].insert({ kind, id }).second)
            return;
        CharacterDatabase.Execute("INSERT IGNORE INTO `mod_temucraft_account_collection` (`account`, `kind`, `id`) VALUES ({}, {}, {})", account, kind, id);
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
        uint32 counts[7] = {};
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
    }
};

class TemucraftAccountCollections_Commands : public CommandScript
{
public:
    TemucraftAccountCollections_Commands() : CommandScript("TemucraftAccountCollections_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "collection", HandleCollection, SEC_PLAYER, Console::No },
        };
        return commandTable;
    }

    static bool HandleCollection(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        uint32 counts[7] = {};
        for (auto const& [kind, id] : accounts[player->GetSession()->GetAccountId()])
            if (kind < 7)
                ++counts[kind];
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-account-wide", handler, 3, "|cff66ccff[Account]|r Shared by your account: {} mounts, {} companion pets, {} recipes, {} flight paths, "
                                 "{} titles, {} trainer unlocks."), counts[1], counts[2], counts[3], counts[4], counts[5], counts[6]);
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
