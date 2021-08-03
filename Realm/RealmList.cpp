/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2021 MaNGOS <https://getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/** \file
    \ingroup realmd
*/

#include "Common.h"
#include "RealmList.h"
#include "Auth/AuthCodes.h"
#include "Util.h"                                           // for Tokens typedef
#include "Policies/Singleton.h"
#include "Database/DatabaseEnv.h"

INSTANTIATE_SINGLETON_1(RealmList);

extern DatabaseType LoginDatabase;

// will only support WoW 1.12.1/1.12.2/1.12.3, WoW:TBC 2.4.3, WoW:WotLK 3.3.5a and official release for WoW:Cataclysm and later, client builds 15595, 10505, 8606, 6005, 5875
// if you need more from old build then add it in cases in realmd sources code
// list sorted from high to low build and first build used as low bound for accepted by default range (any > it will accepted by realmd at least)

static const RealmBuildInfo ExpectedRealmdClientBuilds[] =
{
    // highest supported build, also auto accept all above for simplify future supported builds testing
    { 18414, 5, 4, 8, ' ', { { } }, { { } } },
    { 18273, 5, 4, 8, ' ', { { } }, { { } } },
    { 15595, 4, 3, 4, ' ', { { } }, { { } } },
    { 13930, 3, 3, 5,  'a', { { } }, { { } } }, // 3.3.5a China Mainland build
    { 12340, 3, 3, 5,  'a',
    { { 0xCD, 0xCB, 0xBD, 0x51, 0x88, 0x31, 0x5E, 0x6B, 0x4D, 0x19, 0x44, 0x9D, 0x49, 0x2D, 0xBC, 0xFA, 0xF1, 0x56, 0xA3, 0x47 } },
    { { 0xB7, 0x06, 0xD1, 0x3F, 0xF2, 0xF4, 0x01, 0x88, 0x39, 0x72, 0x94, 0x61, 0xE3, 0xF8, 0xA0, 0xE2, 0xB5, 0xFD, 0xC0, 0x34 } },
    },
    { 11723, 3, 3, 3,  'a', { { } }, { { } } },
    { 11403, 3, 3, 2,  ' ', { { } }, { { } } },
    { 11159, 3, 3, 0,  'a', { { } }, { { } } },
    { 10505, 3, 2, 2,  'a', { { } }, { { } } },
    { 8606,  2, 4, 3,  ' ',
    { { 0x31, 0x9A, 0xFA, 0xA3, 0xF2, 0x55, 0x96, 0x82, 0xF9, 0xFF, 0x65, 0x8B, 0xE0, 0x14, 0x56, 0x25, 0x5F, 0x45, 0x6F, 0xB1 } },
    { { } },
    },
    { 6141,  1, 12, 3, ' ', { { } }, { { } } },
    { 6005,  1, 12, 2, ' ', { { } }, { { } } },
    { 5875,  1, 12, 1, ' ',
    { { } },
    { { 0x8D, 0x17, 0x3C, 0xC3, 0x81, 0x96, 0x1E, 0xEB, 0xAB, 0xF3, 0x36, 0xF5, 0xE6, 0x67, 0x5B, 0x10, 0x1B, 0xB5, 0x13, 0xE5 } },
    },
    { 5464,  1, 11, 2, ' ', { { } }, { { } } },
    { 5302,  1, 10, 2, ' ', { { } }, { { } } },
    { 5086,  1, 9, 4,  ' ', { { } }, { { } } },
    { 0,     0, 0, 0,  ' ', { { } }, { { } } }  // terminator
};

RealmBuildInfo const* FindBuildInfo(uint16 _build)
{
    // first build is low bound of always accepted range
    if (_build >= ExpectedRealmdClientBuilds[0].build)
    {
        return &ExpectedRealmdClientBuilds[0];
    }

    // continue from 1 with explicit equal check
    for (int i = 1; ExpectedRealmdClientBuilds[i].build; ++i)
        if (_build == ExpectedRealmdClientBuilds[i].build)
        {
            return &ExpectedRealmdClientBuilds[i];
        }

    // none appropriate build
    return NULL;
}

RealmList::RealmList() : m_UpdateInterval(0), m_NextUpdateTime(time(NULL))
{
}

RealmList& sRealmList
{
    static RealmList realmlist;
    return realmlist;
}

RealmVersion RealmList::BelongsToVersion(uint32 build) const
{
    RealmBuildVersionMap::const_iterator it;
    if ((it = m_buildToVersion.find(build)) != m_buildToVersion.end())
    {
        return it->second;
    }
    else
    {
        return REALM_VERSION_VANILLA;
    }
}

RealmList::RealmListIterators RealmList::GetIteratorsForBuild(uint32 build) const
{
    RealmVersion version = BelongsToVersion(build);
    if (version >= REALM_VERSION_COUNT)
    {
        return RealmListIterators(
            m_realmsByVersion[0].end(),
            m_realmsByVersion[0].end()
            );
    }
    return RealmListIterators(
        m_realmsByVersion[uint32(version)].begin(),
        m_realmsByVersion[uint32(version)].end()
        );

}

/// Load the realm list from the database
void RealmList::Initialize(uint32 updateInterval)
{
    m_UpdateInterval = updateInterval;

    InitBuildToVersion();

    ///- Get the content of the realmlist table in the database
    UpdateRealms(true);
}

uint32 RealmList::NumRealmsForBuild(uint32 build) const
{
    return m_realmsByVersion[BelongsToVersion(build)].size();
}

void RealmList::AddRealmToBuildList(const Realm& realm)
{
    RealmBuilds builds = realm.realmbuilds;
    int buildNumber = *(builds.begin());
    m_realmsByVersion[BelongsToVersion(buildNumber)].push_back(&realm);
}

void RealmList::InitBuildToVersion()
{
    m_buildToVersion[5875] = REALM_VERSION_VANILLA;
    m_buildToVersion[6005] = REALM_VERSION_VANILLA;
    m_buildToVersion[6141] = REALM_VERSION_VANILLA;

    m_buildToVersion[8606] = REALM_VERSION_TBC;

    m_buildToVersion[12340] = REALM_VERSION_WOTLK;

    m_buildToVersion[15595] = REALM_VERSION_CATA;

    m_buildToVersion[18273] = REALM_VERSION_MOP;
    m_buildToVersion[18414] = REALM_VERSION_MOP;

    m_buildToVersion[21742] = REALM_VERSION_WOD;

    m_buildToVersion[26972] = REALM_VERSION_LEGION;

    m_buildToVersion[35662] = REALM_VERSION_BFA;

    m_buildToVersion[40000] = REALM_VERSION_SHADOWLANDS;
}

void RealmList::UpdateRealm(uint32 ID, const std::string& name, ACE_INET_Addr const& address, ACE_INET_Addr const& localAddr, ACE_INET_Addr const& localSubmask, uint32 port, uint8 icon, RealmFlags realmflags, uint8 timezone, AccountTypes allowedSecurityLevel, float popu, const std::string& builds)
{
    ///- Create new if not exist or update existed
    Realm& realm = m_realms[name];

    realm.m_ID       = ID;
    realm.name       = name;
    realm.icon       = icon;
    realm.realmflags = realmflags;
    realm.timezone   = timezone;
    realm.allowedSecurityLevel = allowedSecurityLevel;
    realm.populationLevel      = popu;

    Tokens tokens = StrSplit(builds, " ");
    Tokens::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); ++iter)
    {
        uint32 build = atol((*iter).c_str());
        realm.realmbuilds.insert(build);
    }

    uint16 first_build = !realm.realmbuilds.empty() ? *realm.realmbuilds.begin() : 0;

    if (first_build)
    {
        AddRealmToBuildList(realm);
    }
    else
    {
        sLog.outError("You don't seem to have added any allowed realmbuilds to the realm: %s"
                      " and therefore it will not be listed to anyone", name.c_str());
    }
    realm.realmBuildInfo.build = first_build;
    realm.realmBuildInfo.major_version = 0;
    realm.realmBuildInfo.minor_version = 0;
    realm.realmBuildInfo.bugfix_version = 0;
    realm.realmBuildInfo.hotfix_version = ' ';

    if (first_build)
        if (RealmBuildInfo const* bInfo = FindBuildInfo(first_build))
            if (bInfo->build == first_build)
            {
                realm.realmBuildInfo = *bInfo;
            }

    ///- Append port to IP address.
    realm.ExternalAddress = address;
    realm.LocalAddress = localAddr;
    realm.LocalSubnetMask = localSubmask;
}

void RealmList::UpdateIfNeed()
{
    // maybe disabled or updated recently
    if (!m_UpdateInterval || m_NextUpdateTime > time(NULL))
    {
        return;
    }

    m_NextUpdateTime = time(NULL) + m_UpdateInterval;

    // Clears Realm list
    m_realms.clear();
    for (int i = 0; i < REALM_VERSION_COUNT; ++i)
    {
        m_realmsByVersion[i].clear();
    }

    // Get the content of the realmlist table in the database
    UpdateRealms(false);
}

void RealmList::UpdateRealms(bool init)
{
    DETAIL_LOG("Updating Realm List...");

    ////                                               0     1       2          3               4                  5       6       7             8           9                       10            11
    QueryResult* result = LoginDatabase.Query("SELECT `id`, `name`, `address`, `localAddress`, `localSubnetMask`, `port`, `icon`, `realmflags`, `timezone`, `allowedSecurityLevel`, `population`, `realmbuilds` FROM `realmlist` WHERE (`realmflags` & 1) = 0 ORDER BY `name`");

    ///- Circle through results and add them to the realm map
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 Id                       = fields[0].GetUInt32();
            std::string name                = fields[1].GetString();
            std::string externalAddress     = fields[2].GetString();
            std::string localAddress        = fields[3].GetString();
            std::string localSubmask        = fields[4].GetString();
            uint32 port                     = fields[5].GetUInt32();
            uint8 icon                      = fields[6].GetUInt8();
            uint8 realmflags                = fields[7].GetUInt8();
            uint8 timezone                  = fields[8].GetUInt8();
            uint8 allowedSecurityLevel      = fields[9].GetUInt8();
            float population                = fields[10].GetFloat();
            std::string realmbuilds         = fields[11].GetString();

            ACE_INET_Addr externalAddr(port, externalAddress.c_str(), AF_INET);
            ACE_INET_Addr localAddr(port, localAddress.c_str(), AF_INET);
            ACE_INET_Addr submask(0, localSubmask.c_str(), AF_INET);

            if (realmflags & ~(REALM_FLAG_OFFLINE | REALM_FLAG_NEW_PLAYERS | REALM_FLAG_RECOMMENDED | REALM_FLAG_SPECIFYBUILD))
            {
                sLog.outError("Realm (id %u, name '%s') can only be flagged as OFFLINE (mask 0x02), NEWPLAYERS (mask 0x20), RECOMMENDED (mask 0x40), or SPECIFICBUILD (mask 0x04) in DB", Id, name.c_str());
                realmflags &= (REALM_FLAG_OFFLINE | REALM_FLAG_NEW_PLAYERS | REALM_FLAG_RECOMMENDED | REALM_FLAG_SPECIFYBUILD);
            }

            UpdateRealm(Id, name, externalAddr, localAddr, submask, port, icon, RealmFlags(realmflags), timezone, (allowedSecurityLevel <= SEC_ADMINISTRATOR ? AccountTypes(allowedSecurityLevel) : SEC_ADMINISTRATOR), population, realmbuilds);

            if (init)
            {
                sLog.outString("Added realm id %u, name '%s'",  Id, name.c_str());
            }
        }
        while (result->NextRow());
        delete result;
    }
}
