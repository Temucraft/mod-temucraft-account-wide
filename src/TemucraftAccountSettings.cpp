/*
 * mod-temucraft-account-wide: the game's own settings shared between a player's accounts.
 *
 * The 3.3.5a client keeps its settings on the server when "synchronize settings" is on (the default): per account in
 * account_data (type 0 settings like auto loot, 2 key bindings, 4 general macros) and per character in
 * character_account_data (3 the character's own key bindings, 5 its macros, ...). The client sends a new copy with
 * CMSG_UPDATE_ACCOUNT_DATA whenever one changes, and at login takes the server's copy when it is newer than its own.
 *
 * Accounts are linked by the same e-mail address (Temucraft.AccountWide.Settings.LinkByEmail) or with
 * ".shared link <account> <password>". When one linked account saves a copy, the server writes the same copy, with the
 * same time, to the other accounts (and, with Temucraft.AccountWide.Settings.Characters, to all their characters),
 * so the next login on any of them, on any PC, picks it up.
 */
#include "AccountMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ServerScript.h"
#include "StringConvert.h"
#include "TemucraftModuleText.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    char const* const Module = "mod-temucraft-account-wide";

    bool sEnable = true;
    bool sLinkByEmail = true;
    std::set<uint8> sAccountTypes;     // account_data types shared between linked accounts
    std::set<uint8> sCharacterTypes;   // character_account_data types shared between all their characters

    // Saves the client sent (seen on the network thread), handled a moment later on the world thread, after the
    // core has stored them: account, type, when it was seen (ms).
    struct Pending { uint32 account; uint8 type; uint32 seen; };
    std::mutex sPendingLock;
    std::vector<Pending> sPending;
    // the last copy shared per account and type, so the same copy isn't shared twice
    std::map<std::pair<uint32, uint8>, uint32> sShared;

    std::set<uint8> Types(std::string const& option, std::string const& fallback)
    {
        std::set<uint8> types;
        std::string list = sConfigMgr->GetOption<std::string>(option, fallback);
        for (std::string_view part : Acore::Tokenize(list, ',', false))
            if (Optional<uint32> t = Acore::StringTo<uint32>(part); t && *t < NUM_ACCOUNT_DATA_TYPES)
                types.insert(uint8(*t));
        return types;
    }

    void LoadConfig()
    {
        sEnable = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Settings.Enable", true);
        sLinkByEmail = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Settings.LinkByEmail", true);
        sAccountTypes.clear();
        for (uint8 t : Types("Temucraft.AccountWide.Settings.AccountTypes", "0,2,4"))
            if ((1 << t) & GLOBAL_CACHE_MASK)
                sAccountTypes.insert(t);
        sCharacterTypes.clear();
        if (sConfigMgr->GetOption<bool>("Temucraft.AccountWide.Settings.Characters", false))
            for (uint8 t : Types("Temucraft.AccountWide.Settings.CharacterTypes", "3,5"))
                if (!((1 << t) & GLOBAL_CACHE_MASK))
                    sCharacterTypes.insert(t);
    }

    // The accounts linked to this one (not counting itself): the same e-mail, and the ones linked by command.
    std::set<uint32> LinkedAccounts(uint32 account)
    {
        std::set<uint32> linked;
        if (sLinkByEmail)
            if (QueryResult result = LoginDatabase.Query("SELECT b.id FROM account a JOIN account b ON b.email = a.email AND b.id <> a.id "
                                                         "WHERE a.id = {} AND a.email <> ''", account))
                do linked.insert((*result)[0].Get<uint32>()); while (result->NextRow());
        if (QueryResult result = CharacterDatabase.Query("SELECT b.account_id FROM mod_temucraft_settings_link a JOIN mod_temucraft_settings_link b "
                                                         "ON b.group_id = a.group_id AND b.account_id <> a.account_id WHERE a.account_id = {}", account))
            do linked.insert((*result)[0].Get<uint32>()); while (result->NextRow());
        linked.erase(account);
        return linked;
    }

    void ShareAccountCopy(uint32 from, uint8 type, AccountData const& copy)
    {
        for (uint32 account : LinkedAccounts(from))
        {
            if (WorldSession* other = sWorldSessionMgr->FindSession(account))
            {
                other->SetAccountData(AccountDataType(type), copy.Time, copy.Data);   // saves it too
                continue;
            }
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_ACCOUNT_DATA);
            stmt->SetData(0, account);
            stmt->SetData(1, type);
            stmt->SetData(2, uint32(copy.Time));
            stmt->SetData(3, copy.Data);
            CharacterDatabase.Execute(stmt);
        }
    }

    void ShareCharacterCopy(WorldSession* session, uint8 type, AccountData const& copy)
    {
        Player* player = session->GetPlayer();
        if (!player)
            return;
        ObjectGuid::LowType source = player->GetGUID().GetCounter();
        std::set<uint32> accounts = LinkedAccounts(session->GetAccountId());
        accounts.insert(session->GetAccountId());
        std::string list;
        for (uint32 a : accounts)
            list += (list.empty() ? "" : ",") + std::to_string(a);
        QueryResult result = CharacterDatabase.Query("SELECT guid, account FROM characters WHERE account IN ({}) AND deleteDate IS NULL", list);
        if (!result)
            return;
        do
        {
            ObjectGuid::LowType guid = (*result)[0].Get<uint32>();
            uint32 account = (*result)[1].Get<uint32>();
            if (guid == source)
                continue;
            // a character that is playing right now: through its session, so its copy in memory matches
            if (WorldSession* other = sWorldSessionMgr->FindSession(account))
                if (Player* p = other->GetPlayer(); p && p->GetGUID().GetCounter() == guid)
                {
                    other->SetAccountData(AccountDataType(type), copy.Time, copy.Data);
                    continue;
                }
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_PLAYER_ACCOUNT_DATA);
            stmt->SetData(0, guid);
            stmt->SetData(1, type);
            stmt->SetData(2, uint32(copy.Time));
            stmt->SetData(3, copy.Data);
            CharacterDatabase.Execute(stmt);
        } while (result->NextRow());
    }

    void HandlePending()
    {
        std::vector<Pending> ready;
        uint32 now = GameTime::GetGameTimeMS().count();
        {
            std::lock_guard<std::mutex> guard(sPendingLock);
            // give the core a moment to store the copy before it is read back
            auto split = std::partition(sPending.begin(), sPending.end(), [now](Pending const& p) { return now - p.seen < 2000; });
            ready.assign(split, sPending.end());
            sPending.erase(split, sPending.end());
        }
        for (Pending const& p : ready)
        {
            WorldSession* session = sWorldSessionMgr->FindSession(p.account);
            if (!session)
                continue;
            AccountData const* copy = session->GetAccountData(AccountDataType(p.type));
            if (!copy || copy->Time == 0)
                continue;
            uint32& last = sShared[{ p.account, p.type }];
            if (last == uint32(copy->Time))
                continue;
            last = uint32(copy->Time);
            if (sAccountTypes.count(p.type))
                ShareAccountCopy(p.account, p.type, *copy);
            else if (sCharacterTypes.count(p.type))
                ShareCharacterCopy(session, p.type, *copy);
        }
    }
}

class TemucraftAccountSettings_Server : public ServerScript
{
public:
    TemucraftAccountSettings_Server() : ServerScript("TemucraftAccountSettings_Server", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (!sEnable || !session || packet.GetOpcode() != CMSG_UPDATE_ACCOUNT_DATA || packet.size() < 4)
            return true;
        WorldPacket copy(packet);
        copy.rpos(0);
        uint32 type = 0;
        copy >> type;
        if (type < NUM_ACCOUNT_DATA_TYPES && (sAccountTypes.count(uint8(type)) || sCharacterTypes.count(uint8(type))))
        {
            std::lock_guard<std::mutex> guard(sPendingLock);
            sPending.push_back({ session->GetAccountId(), uint8(type), uint32(GameTime::GetGameTimeMS().count()) });
        }
        return true;   // the core still handles the packet as usual
    }
};

class TemucraftAccountSettings_World : public WorldScript
{
public:
    TemucraftAccountSettings_World() : WorldScript("TemucraftAccountSettings_World") { }

    void OnAfterConfigLoad(bool /*reload*/) override { LoadConfig(); }

    void OnUpdate(uint32 diff) override
    {
        _timer += diff;
        if (_timer < 1000)
            return;
        _timer = 0;
        if (sEnable)
            HandlePending();
    }

private:
    uint32 _timer = 0;
};

class TemucraftAccountSettings_Commands : public CommandScript
{
public:
    TemucraftAccountSettings_Commands() : CommandScript("TemucraftAccountSettings_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable shared =
        {
            { "link",   HandleLink,   SEC_PLAYER, Console::No },
            { "unlink", HandleUnlink, SEC_PLAYER, Console::No },
            { "list",   HandleList,   SEC_PLAYER, Console::No },
        };
        static ChatCommandTable commandTable = { { "shared", shared } };
        return commandTable;
    }

    // .shared link <account> <password>: proves the other account is yours, then both share their settings
    static bool HandleLink(ChatHandler* handler, std::string account, std::string password)
    {
        uint32 self = handler->GetSession()->GetAccountId();
        uint32 other = AccountMgr::GetId(account);
        if (!other || other == self || !AccountMgr::CheckPassword(other, password))
        {
            handler->SendSysMessage(TemucraftModuleText(Module, handler, 4, "|cff66ccff[Account]|r That account name and password don't match."));
            return true;
        }
        // both join the group of whichever is already in one (or a new group named after this account)
        uint32 group = self;
        if (QueryResult r = CharacterDatabase.Query("SELECT group_id FROM mod_temucraft_settings_link WHERE account_id IN ({}, {}) LIMIT 1", self, other))
            group = (*r)[0].Get<uint32>();
        CharacterDatabase.DirectExecute("REPLACE INTO mod_temucraft_settings_link (account_id, group_id) VALUES ({}, {}), ({}, {})", self, group, other, group);
        handler->PSendSysMessage(TemucraftModuleText(Module, handler, 5, "|cff66ccff[Account]|r Linked: your settings, key bindings and macros are now shared with {}."), account);
        return true;
    }

    static bool HandleUnlink(ChatHandler* handler)
    {
        CharacterDatabase.DirectExecute("DELETE FROM mod_temucraft_settings_link WHERE account_id = {}", handler->GetSession()->GetAccountId());
        handler->SendSysMessage(TemucraftModuleText(Module, handler, 6, "|cff66ccff[Account]|r This account no longer shares its settings by link (accounts with the same e-mail still do, if the server allows it)."));
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        std::set<uint32> linked = LinkedAccounts(handler->GetSession()->GetAccountId());
        std::string names;
        for (uint32 id : linked)
        {
            std::string name;
            if (AccountMgr::GetName(id, name))
                names += (names.empty() ? "" : ", ") + name;
        }
        handler->PSendSysMessage(TemucraftModuleText(Module, handler, 7, "|cff66ccff[Account]|r Shares its settings with: {}"),
                                 names.empty() ? TemucraftModuleText(Module, handler, 8, "no other account") : names);
        return true;
    }
};

void AddTemucraftAccountSettingsScripts()
{
    new TemucraftAccountSettings_Server();
    new TemucraftAccountSettings_World();
    new TemucraftAccountSettings_Commands();
}
