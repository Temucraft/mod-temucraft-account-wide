/*
 * mod-temucraft-account-wide: reputation shared by every character of an account.
 *
 * Progress on a faction is kept per account. When a character logs in it gets the account's progress for every
 * faction it can have, and what it earns is saved back for the next character. Whether a character can have a
 * faction comes from Faction.dbc, the same way the core gives base reputation: the faction row that matches the
 * character's race and class decides, and a row marked hidden or "forced invisible" (how the client hides the other
 * side's cities from your reputation pane) means that faction is not shared to that character. Factions with no
 * row for the race (the neutral ones) are shared with everybody.
 *
 * What is shared is the progress (the saved standing); each character keeps its own racial starting bonus on top.
 * Pairs where earning one costs the other (Aldor / Scryers, Frenzyheart / Oracles) are left per character by
 * default: Temucraft.AccountWide.Reputation.Exclude.
 */
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "WorldScript.h"
#include <unordered_map>
#include <unordered_set>

namespace
{
    bool sEnable = true;
    std::unordered_set<uint32> sExclude;
    std::unordered_set<ObjectGuid::LowType> sApplying;   // characters being synced (ignore their rep hooks)

    void LoadConfig()
    {
        sEnable = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Reputation.Enable", true);
        sExclude.clear();
        std::string list = sConfigMgr->GetOption<std::string>("Temucraft.AccountWide.Reputation.Exclude", "932,934,1104,1105");
        for (std::string_view id : Acore::Tokenize(list, ',', false))
            if (Optional<uint32> v = Acore::StringTo<uint32>(id))
                sExclude.insert(*v);
    }

    // Can this character have this faction, according to Faction.dbc?
    bool Shareable(Player* player, FactionEntry const* faction)
    {
        if (!faction || !faction->CanHaveReputation() || sExclude.count(faction->ID))
            return false;
        uint32 raceMask = player->getRaceMask();
        uint32 classMask = player->getClassMask();
        for (uint8 i = 0; i < 4; ++i)
        {
            bool raceOk = (faction->BaseRepRaceMask[i] & raceMask) || (faction->BaseRepRaceMask[i] == 0 && faction->BaseRepClassMask[i] != 0);
            bool classOk = faction->BaseRepClassMask[i] == 0 || (faction->BaseRepClassMask[i] & classMask);
            if (raceOk && classOk)
                return !(faction->ReputationFlags[i] & (FACTION_FLAG_HIDDEN | FACTION_FLAG_INVISIBLE_FORCED));
        }
        return true;   // no row for this race: a neutral faction
    }

    int32 Progress(Player* player, FactionEntry const* faction)
    {
        ReputationMgr& rep = player->GetReputationMgr();
        return rep.GetReputation(faction) - rep.GetBaseReputation(faction);
    }

    void SaveAccount(Player* player)
    {
        if (!sEnable)
            return;
        uint32 account = player->GetSession()->GetAccountId();
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (auto const& [listId, state] : player->GetReputationMgr().GetStateList())
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(state.ID);
            if (!Shareable(player, faction))
                continue;
            trans->Append("REPLACE INTO `mod_temucraft_account_wide_reputation` (`account`, `faction`, `standing`) VALUES ({}, {}, {})",
                account, faction->ID, state.Standing);
        }
        CharacterDatabase.CommitTransaction(trans);
    }

    void ApplyAccount(Player* player)
    {
        if (!sEnable)
            return;
        uint32 account = player->GetSession()->GetAccountId();

        std::unordered_map<uint32, int32> shared;
        QueryResult result = CharacterDatabase.Query("SELECT `faction`, `standing` FROM `mod_temucraft_account_wide_reputation` WHERE `account` = {}", account);
        if (!result)
        {
            // first time for this account: start from the best progress any of its characters has
            result = CharacterDatabase.Query("SELECT r.`faction`, MAX(r.`standing`) FROM `character_reputation` r JOIN `characters` c ON c.`guid` = r.`guid` "
                "WHERE c.`account` = {} GROUP BY r.`faction`", account);
            if (result)
            {
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                do
                {
                    Field* f = result->Fetch();
                    uint32 faction = f[0].Get<uint32>();
                    int32 standing = f[1].Get<int32>();
                    if (sExclude.count(faction))
                        continue;
                    shared[faction] = standing;
                    trans->Append("REPLACE INTO `mod_temucraft_account_wide_reputation` (`account`, `faction`, `standing`) VALUES ({}, {}, {})", account, faction, standing);
                } while (result->NextRow());
                CharacterDatabase.CommitTransaction(trans);
            }
        }
        else
            do
            {
                Field* f = result->Fetch();
                shared[f[0].Get<uint32>()] = f[1].Get<int32>();
            } while (result->NextRow());

        ReputationMgr& rep = player->GetReputationMgr();
        uint32 changed = 0;
        sApplying.insert(player->GetGUID().GetCounter());
        for (auto const& [factionId, standing] : shared)
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(factionId);
            if (!Shareable(player, faction) || Progress(player, faction) == standing)
                continue;
            rep.SetOneFactionReputation(faction, float(rep.GetBaseReputation(faction) + standing), false);
            if (standing != 0)
                rep.SetVisible(faction);
            ++changed;
        }
        sApplying.erase(player->GetGUID().GetCounter());
        if (changed)
            rep.SendStates();

        // what this character has that the account didn't know yet
        SaveAccount(player);
    }
}

class TemucraftAccountWide_Player : public PlayerScript
{
public:
    TemucraftAccountWide_Player() : PlayerScript("TemucraftAccountWide_Player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_SAVE }) { }

    void OnPlayerLogin(Player* player) override { ApplyAccount(player); }
    void OnPlayerLogout(Player* player) override { SaveAccount(player); }
    void OnPlayerSave(Player* player) override
    {
        if (!sApplying.count(player->GetGUID().GetCounter()))
            SaveAccount(player);
    }
};

class TemucraftAccountWide_World : public WorldScript
{
public:
    TemucraftAccountWide_World() : WorldScript("TemucraftAccountWide_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }
    void OnAfterConfigLoad(bool /*reload*/) override { LoadConfig(); }
};

void AddTemucraftAccountWideScripts()
{
    new TemucraftAccountWide_Player();
    new TemucraftAccountWide_World();
}
