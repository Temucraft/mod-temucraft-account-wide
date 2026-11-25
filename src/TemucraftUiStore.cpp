/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * mod-temucraft-account-wide: a small store for the Temucraft interface (addon prefix TUS).
 *
 * The interface is built into the client's FrameXML, which has no SavedVariables, so what it keeps between sessions
 * (favourite mounts and pets of the Collections window, talent loadouts) is kept here, per account ("A") or per
 * character ("C"), in mod_temucraft_ui_store (characters DB).
 *
 *   client: ALL                         server: B~scope~key, D~scope~key~text..., E~scope~key per value, then READY
 *   client: SB~scope~key, SD~scope~key~text..., SE~scope~key   (set: begin, append, commit; empty value deletes)
 *
 * Keys are [A-Za-z0-9._-], at most 48 characters; values at most 16 KB.
 */
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemucraftAddonMessage.h"
#include "WorldScript.h"
#include <cctype>
#include <map>
#include <string>
#include <utility>

namespace
{
    constexpr char const* PREFIX = "TUS";
    constexpr size_t MAX_VALUE = 16384;
    constexpr size_t CHUNK = 200;
    bool sEnable = true;

    // values being sent by a client: (character guid, scope~key) -> text so far
    std::map<std::pair<uint32, std::string>, std::string> sIncoming;

    bool ValidKey(std::string const& key)
    {
        if (key.empty() || key.size() > 48)
            return false;
        for (char c : key)
            if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
                return false;
        return true;
    }

    bool ValidScope(std::string const& scope) { return scope == "A" || scope == "C"; }

    uint32 Owner(Player* player, std::string const& scope)
    {
        return scope == "C" ? player->GetGUID().GetCounter() : 0;
    }

    void SendValue(Player* player, std::string const& scope, std::string const& key, std::string const& value)
    {
        std::string head = scope + "~" + key;
        TemucraftAddonSend(player, PREFIX, "B~" + head);
        for (size_t at = 0; at < value.size(); at += CHUNK)
            TemucraftAddonSend(player, PREFIX, "D~" + head + "~" + value.substr(at, CHUNK));
        TemucraftAddonSend(player, PREFIX, "E~" + head);
    }

    void SendAll(Player* player)
    {
        uint32 account = player->GetSession()->GetAccountId();
        uint32 guid = player->GetGUID().GetCounter();
        if (QueryResult r = CharacterDatabase.Query("SELECT `guid`, `key`, `value` FROM `mod_temucraft_ui_store` WHERE `account` = {} AND `guid` IN (0, {})", account, guid))
        {
            do
            {
                Field* f = r->Fetch();
                SendValue(player, f[0].Get<uint32>() ? "C" : "A", f[1].Get<std::string>(), f[2].Get<std::string>());
            } while (r->NextRow());
        }
        TemucraftAddonSend(player, PREFIX, "READY");
    }

    void Save(Player* player, std::string const& scope, std::string key, std::string value)
    {
        uint32 account = player->GetSession()->GetAccountId();
        uint32 owner = Owner(player, scope);
        CharacterDatabase.EscapeString(key);
        if (value.empty())
        {
            CharacterDatabase.Execute("DELETE FROM `mod_temucraft_ui_store` WHERE `account` = {} AND `guid` = {} AND `key` = '{}'", account, owner, key);
            return;
        }
        CharacterDatabase.EscapeString(value);
        CharacterDatabase.Execute("REPLACE INTO `mod_temucraft_ui_store` (`account`, `guid`, `key`, `value`) VALUES ({}, {}, '{}', '{}')", account, owner, key, value);
    }
}

class TemucraftUiStore_Player : public PlayerScript
{
public:
    TemucraftUiStore_Player() : PlayerScript("TemucraftUiStore_Player") { }

    void OnPlayerLogout(Player* player) override
    {
        uint32 guid = player->GetGUID().GetCounter();
        for (auto itr = sIncoming.begin(); itr != sIncoming.end();)
            itr = itr->first.first == guid ? sIncoming.erase(itr) : std::next(itr);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        std::string body;
        if (!TemucraftAddonParse(player, type, language, msg, receiver, PREFIX, body))
            return true;
        if (!sEnable)
            return false;
        std::vector<std::string> f = TemucraftAddonFields(body);
        if (f.empty())
            return false;
        if (f[0] == "ALL")
        {
            SendAll(player);
            return false;
        }
        if (f.size() < 3 || !ValidScope(f[1]) || !ValidKey(f[2]))
            return false;
        auto id = std::make_pair(player->GetGUID().GetCounter(), f[1] + "~" + f[2]);
        if (f[0] == "SB")
            sIncoming[id].clear();
        else if (f[0] == "SD" && f.size() > 3)
        {
            auto itr = sIncoming.find(id);
            if (itr == sIncoming.end())
                return false;
            // the text may itself hold '~' only if the client sent it raw; keep every field after the key
            std::string text = f[3];
            for (size_t i = 4; i < f.size(); ++i)
                text += "~" + f[i];
            if (itr->second.size() + text.size() > MAX_VALUE)
                sIncoming.erase(itr);
            else
                itr->second += text;
        }
        else if (f[0] == "SE")
        {
            auto itr = sIncoming.find(id);
            if (itr == sIncoming.end())
                return false;
            Save(player, f[1], f[2], itr->second);
            sIncoming.erase(itr);
        }
        return false;
    }
};

class TemucraftUiStore_World : public WorldScript
{
public:
    TemucraftUiStore_World() : WorldScript("TemucraftUiStore_World") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sEnable = sConfigMgr->GetOption<bool>("Temucraft.AccountWide.UiStore", true);
    }
};

void AddTemucraftUiStoreScripts()
{
    new TemucraftUiStore_Player();
    new TemucraftUiStore_World();
}
