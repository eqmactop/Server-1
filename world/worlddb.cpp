/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2006 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "worlddb.h"
//#include "../common/item.h"
#include "../common/string_util.h"
#include "../common/eq_packet_structs.h"
#include "../common/item.h"
#include "../common/dbasync.h"
#include "../common/rulesys.h"
#include <iostream>
#include <cstdlib>
#include <vector>
#include "sof_char_create_data.h"

WorldDatabase database;
extern std::vector<RaceClassAllocation> character_create_allocations;
extern std::vector<RaceClassCombos> character_create_race_class_combos;


// solar: the current stuff is at the bottom of this function
void WorldDatabase::GetCharSelectInfo(uint32 account_id, CharacterSelect_Struct* cs) {

	Inventory *inv;

	for (int i=0; i<10; i++) {
		strcpy(cs->name[i], "<none>");
		cs->zone[i] = 0;
		cs->level[i] = 0;
			cs->tutorial[i] = 0;
		cs->gohome[i] = 0;
	}

	// Populate character info
	std::string query = StringFormat("SELECT name, profile, zonename, class, level "
                                    "FROM character_ WHERE account_id = %i "
                                    "ORDER BY name LIMIT 10", account_id);
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        std::cerr << "Error in GetCharSelectInfo query '" << query << "' " << results.ErrorMessage() << std::endl;
		return;
    }

    int char_num = 0;
    for (auto row = results.begin(); (row != results.end()) || (char_num < 10); ++row, ++char_num) {
        if (results.LengthOfColumn(1) != sizeof(PlayerProfile_Struct)) {
            std::cout << "Got a bogus character (" << row[0] << ") Ignoring!!!" << std::endl;
            std::cout << "PP length = " << results.LengthOfColumn(1) << " but PP should be " << sizeof(PlayerProfile_Struct) << std::endl;
            continue;
        }

        strcpy(cs->name[char_num], row[0]);
        PlayerProfile_Struct* pp = (PlayerProfile_Struct*)row[1];
        uint8 clas = atoi(row[3]);
        uint8 lvl = atoi(row[4]);

        // Character information
        if(lvl == 0)
            cs->level[char_num]	= pp->level;	//no level in DB, trust PP
        else
            cs->level[char_num]	= lvl;

        if(clas == 0)
            cs->class_[char_num] = pp->class_;	//no class in DB, trust PP
        else
            cs->class_[char_num] = clas;

        cs->race[char_num] = pp->race;
        cs->gender[char_num] = pp->gender;
        cs->deity[char_num]	= pp->deity;
        cs->zone[char_num] = GetZoneID(row[2]);
        cs->face[char_num] = pp->face;
        cs->haircolor[char_num]	= pp->haircolor;
		cs->beardcolor[char_num] = pp->beardcolor;
		cs->eyecolor2[char_num]	= pp->eyecolor2;
		cs->eyecolor1[char_num]	= pp->eyecolor1;
		cs->hairstyle[char_num]	= pp->hairstyle;
		cs->beard[char_num]	= pp->beard;
		cs->drakkin_heritage[char_num] = pp->drakkin_heritage;
		cs->drakkin_tattoo[char_num] = pp->drakkin_tattoo;
		cs->drakkin_details[char_num] = pp->drakkin_details;

        if(RuleB(World, EnableTutorialButton) && (lvl <= RuleI(World, MaxLevelForTutorial)))
            cs->tutorial[char_num] = 1;

        if(RuleB(World, EnableReturnHomeButton)) {
            int now = time(nullptr);
            if((now - pp->lastlogin) >= RuleI(World, MinOfflineTimeToReturnHome))
                cs->gohome[char_num] = 1;
        }

        inv = new Inventory;
        if(!GetInventory(account_id, cs->name[char_num], inv)) {
            printf("Error loading inventory for %s\n", cs->name[char_num]);
            safe_delete(inv);
            continue;
        }

        for (uint8 material = 0; material <= 8; material++) {
            uint32 color;
            ItemInst *item = inv->GetItem(Inventory::CalcSlotFromMaterial(material));
            if(item == 0)
                continue;

            cs->equip[char_num][material] = item->GetItem()->Material;

            if(pp->item_tint[material].rgb.use_tint)	// they have a tint (LoY dye)
                color = pp->item_tint[material].color;
            else	// no tint, use regular item color
                color = item->GetItem()->Color;

            cs->cs_colors[char_num][material].color = color;

            // the weapons are kept elsewhere
            if (material != MaterialPrimary && material != MaterialSecondary)
                continue;


            if(strlen(item->GetItem()->IDFile) <= 2)
                continue;

            uint32 idfile = atoi(&item->GetItem()->IDFile[2]);
            if (material == MaterialPrimary)
                cs->primary[char_num] = idfile;
            else
                cs->secondary[char_num] = idfile;

        }
        safe_delete(inv);
    }

}

int WorldDatabase::MoveCharacterToBind(int CharID, uint8 bindnum) {
	// if an invalid bind point is specified, use the primary bind
	if (bindnum > 4)
		bindnum = 0;

	PlayerProfile_Struct pp;

    std::string query = StringFormat("SELECT profile FROM character_ WHERE id = '%i'", CharID);
    auto results = QueryDatabase(query);
    if (!results.Success())
        return 0;

    auto row = results.begin();
    if (results.LengthOfColumn(0) != sizeof(PlayerProfile_Struct))
        return 0;

    memcpy(&pp, row[0], sizeof(PlayerProfile_Struct));

	const char *BindZoneName = StaticGetZoneName(pp.binds[bindnum].zoneId);

	if(!strcmp(BindZoneName, "UNKNWN"))
        return pp.zone_id;

    query = StringFormat("UPDATE character_ SET zonename = '%s', zoneid = %i, x = %f, y = %f, z = %f, instanceid = 0 "
                        "WHERE id = '%i'",
                        BindZoneName, pp.binds[bindnum].zoneId, pp.binds[bindnum].x, pp.binds[bindnum].y,
                        pp.binds[bindnum].z, CharID);
    results = QueryDatabase(query);
	if (!results.Success())
		return pp.zone_id;

	return pp.binds[bindnum].zoneId;
}

bool WorldDatabase::GetStartZone(PlayerProfile_Struct* in_pp, CharCreate_Struct* in_cc)
{
	if(!in_pp || !in_cc)
		return false;

	in_pp->x = in_pp->y = in_pp->z = in_pp->heading = in_pp->zone_id = 0;
	in_pp->binds[0].x = in_pp->binds[0].y = in_pp->binds[0].z = in_pp->binds[0].zoneId = 0;

    std::string query = StringFormat("SELECT x, y, z, heading, zone_id, bind_id "
                                    "FROM start_zones WHERE player_choice = % i "
                                    "AND player_class = %i AND player_deity = %i "
                                    "AND player_race = %i",
                                    in_cc->start_zone, in_cc->class_, in_cc->deity,
                                    in_cc->race);
    auto results = QueryDatabase(query);
	if(!results.Success()) {
		LogFile->write(EQEMuLog::Error, "Start zone query failed: %s : %s\n", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}

	LogFile->write(EQEMuLog::Status, "Start zone query: %s\n", query.c_str());

    if (results.RowCount() == 0) {
        printf("No start_zones entry in database, using defaults\n");
		switch(in_cc->start_zone)
		{
			case 0:
			{
				in_pp->zone_id = 24;	// erudnext
				in_pp->binds[0].zoneId = 38;	// tox
				break;
			}
			case 1:
			{
				in_pp->zone_id =2;	// qeynos2
				in_pp->binds[0].zoneId = 2;	// qeynos2
				break;
			}
			case 2:
			{
				in_pp->zone_id =29;	// halas
				in_pp->binds[0].zoneId = 30;	// everfrost
				break;
			}
			case 3:
			{
				in_pp->zone_id =19;	// rivervale
				in_pp->binds[0].zoneId = 20;	// kithicor
				break;
			}
			case 4:
			{
				in_pp->zone_id =9;	// freportw
				in_pp->binds[0].zoneId = 9;	// freportw
				break;
			}
			case 5:
			{
				in_pp->zone_id =40;	// neriaka
				in_pp->binds[0].zoneId = 25;	// nektulos
				break;
			}
			case 6:
			{
				in_pp->zone_id =52;	// gukta
				in_pp->binds[0].zoneId = 46;	// innothule
				break;
			}
			case 7:
			{
				in_pp->zone_id =49;	// oggok
				in_pp->binds[0].zoneId = 47;	// feerrott
				break;
			}
			case 8:
			{
				in_pp->zone_id =60;	// kaladima
				in_pp->binds[0].zoneId = 68;	// butcher
				break;
			}
			case 9:
			{
				in_pp->zone_id =54;	// gfaydark
				in_pp->binds[0].zoneId = 54;	// gfaydark
				break;
			}
			case 10:
			{
				in_pp->zone_id =61;	// felwithea
				in_pp->binds[0].zoneId = 54;	// gfaydark
				break;
			}
			case 11:
			{
				in_pp->zone_id =55;	// akanon
				in_pp->binds[0].zoneId = 56;	// steamfont
				break;
			}
			case 12:
			{
				in_pp->zone_id =82;	// cabwest
				in_pp->binds[0].zoneId = 78;	// fieldofbone
				break;
			}
			case 13:
			{
				in_pp->zone_id =155;	// sharvahl
				in_pp->binds[0].zoneId = 155;	// sharvahl
				break;
			}
		}
    }
    else {
		LogFile->write(EQEMuLog::Status, "Found starting location in start_zones");
		auto row = results.begin();
		in_pp->x = atof(row[0]);
		in_pp->y = atof(row[1]);
		in_pp->z = atof(row[2]);
		in_pp->heading = atof(row[3]);
		in_pp->zone_id = atoi(row[4]);
		in_pp->binds[0].zoneId = atoi(row[5]);
	}

	if(in_pp->x == 0 && in_pp->y == 0 && in_pp->z == 0)
		database.GetSafePoints(in_pp->zone_id, 0, &in_pp->x, &in_pp->y, &in_pp->z);

	if(in_pp->binds[0].x == 0 && in_pp->binds[0].y == 0 && in_pp->binds[0].z == 0)
		database.GetSafePoints(in_pp->binds[0].zoneId, 0, &in_pp->binds[0].x, &in_pp->binds[0].y, &in_pp->binds[0].z);

	return true;
}

bool WorldDatabase::GetStartZoneSoF(PlayerProfile_Struct* in_pp, CharCreate_Struct* in_cc)
{
	// SoF doesn't send the player_choice field in character creation, it now sends the real zoneID instead.
	//
	// For SoF, search for an entry in start_zones with a matching zone_id, class, race and deity.
	//
	// For now, if no row matching row is found, send them to Crescent Reach, as that is probably the most likely
	// reason for no match being found.
	//
	if(!in_pp || !in_cc)
		return false;

	in_pp->x = in_pp->y = in_pp->z = in_pp->heading = in_pp->zone_id = 0;
	in_pp->binds[0].x = in_pp->binds[0].y = in_pp->binds[0].z = in_pp->binds[0].zoneId = 0;

    std::string query = StringFormat("SELECT x, y, z, heading, bind_id FROM start_zones WHERE zone_id = %i "
                                    "AND player_class = %i AND player_deity = %i AND player_race = %i",
                                    in_cc->start_zone, in_cc->class_, in_cc->deity, in_cc->race);
    auto results = QueryDatabase(query);
	if(!results.Success()) {
		LogFile->write(EQEMuLog::Status, "SoF Start zone query failed: %s : %s\n", query.c_str(), results.ErrorMessage().c_str());
		return false;
	}

	LogFile->write(EQEMuLog::Status, "SoF Start zone query: %s\n", query.c_str());

    if (results.RowCount() == 0) {
        printf("No start_zones entry in database, using defaults\n");

		if(in_cc->start_zone == RuleI(World, TutorialZoneID))
			in_pp->zone_id = in_cc->start_zone;
		else {
			in_pp->x = in_pp->binds[0].x = -51;
			in_pp->y = in_pp->binds[0].y = -20;
			in_pp->z = in_pp->binds[0].z = 0.79;
			in_pp->zone_id = in_pp->binds[0].zoneId = 394; // Crescent Reach.
		}
    }
    else {
		LogFile->write(EQEMuLog::Status, "Found starting location in start_zones");
		auto row = results.begin();
		in_pp->x = atof(row[0]);
		in_pp->y = atof(row[1]);
		in_pp->z = atof(row[2]);
		in_pp->heading = atof(row[3]);
		in_pp->zone_id = in_cc->start_zone;
		in_pp->binds[0].zoneId = atoi(row[4]);
	}

	if(in_pp->x == 0 && in_pp->y == 0 && in_pp->z == 0)
		database.GetSafePoints(in_pp->zone_id, 0, &in_pp->x, &in_pp->y, &in_pp->z);

	if(in_pp->binds[0].x == 0 && in_pp->binds[0].y == 0 && in_pp->binds[0].z == 0)
		database.GetSafePoints(in_pp->binds[0].zoneId, 0, &in_pp->binds[0].x, &in_pp->binds[0].y, &in_pp->binds[0].z);

	return true;
}

void WorldDatabase::GetLauncherList(std::vector<std::string> &rl) {
	rl.clear();

    const std::string query = "SELECT name FROM launcher";
    auto results = QueryDatabase(query);
    if (!results.Success()) {
        LogFile->write(EQEMuLog::Error, "WorldDatabase::GetLauncherList: %s", results.ErrorMessage().c_str());
        return;
    }

    for (auto row = results.begin(); row != results.end(); ++row)
        rl.push_back(row[0]);

}

void WorldDatabase::SetMailKey(int CharID, int IPAddress, int MailKey) {

	char MailKeyString[17];

	if(RuleB(Chat, EnableMailKeyIPVerification) == true)
		sprintf(MailKeyString, "%08X%08X", IPAddress, MailKey);
	else
		sprintf(MailKeyString, "%08X", MailKey);

    std::string query = StringFormat("UPDATE character_ SET mailkey = '%s' WHERE id = '%i'",
                                    MailKeyString, CharID);
    auto results = QueryDatabase(query);
	if (!results.Success())
		LogFile->write(EQEMuLog::Error, "WorldDatabase::SetMailKey(%i, %s) : %s", CharID, MailKeyString, results.ErrorMessage().c_str());

}

bool WorldDatabase::GetCharacterLevel(const char *name, int &level)
{
	std::string query = StringFormat("SELECT level FROM character_ WHERE name = '%s'", name);
	auto results = QueryDatabase(query);
	if (!results.Success()) {
        LogFile->write(EQEMuLog::Error, "WorldDatabase::GetCharacterLevel: %s", results.ErrorMessage().c_str());
        return false;
	}

	if (results.RowCount() == 0)
        return false;

    auto row = results.begin();
    level = atoi(row[0]);

    return true;
}

bool WorldDatabase::LoadCharacterCreateAllocations() {
	character_create_allocations.clear();

	std::string query = "SELECT * FROM char_create_point_allocations ORDER BY id";
	auto results = QueryDatabase(query);
	if (!results.Success())
        return false;

    for (auto row = results.begin(); row != results.end(); ++row) {
        RaceClassAllocation allocate;
		allocate.Index = atoi(row[0]);
		allocate.BaseStats[0] = atoi(row[1]);
		allocate.BaseStats[3] = atoi(row[2]);
		allocate.BaseStats[1] = atoi(row[3]);
		allocate.BaseStats[2] = atoi(row[4]);
		allocate.BaseStats[4] = atoi(row[5]);
		allocate.BaseStats[5] = atoi(row[6]);
		allocate.BaseStats[6] = atoi(row[7]);
		allocate.DefaultPointAllocation[0] = atoi(row[8]);
		allocate.DefaultPointAllocation[3] = atoi(row[9]);
		allocate.DefaultPointAllocation[1] = atoi(row[10]);
		allocate.DefaultPointAllocation[2] = atoi(row[11]);
		allocate.DefaultPointAllocation[4] = atoi(row[12]);
		allocate.DefaultPointAllocation[5] = atoi(row[13]);
		allocate.DefaultPointAllocation[6] = atoi(row[14]);

		character_create_allocations.push_back(allocate);
    }

	return true;
}

bool WorldDatabase::LoadCharacterCreateCombos() {
	character_create_race_class_combos.clear();

	char errbuf[MYSQL_ERRMSG_SIZE];
	char* query = 0;
	MYSQL_RES *result;
	MYSQL_ROW row;
	if(RunQuery(query, MakeAnyLenString(&query, "select * from char_create_combinations order by race, class, deity, start_zone"), errbuf, &result)) {
		safe_delete_array(query);
		while(row = mysql_fetch_row(result)) {
			RaceClassCombos combo;
			int r = 0;
			combo.AllocationIndex = atoi(row[r++]);
			combo.Race = atoi(row[r++]);
			combo.Class = atoi(row[r++]);
			combo.Deity = atoi(row[r++]);
			combo.Zone = atoi(row[r++]);
			combo.ExpansionRequired = atoi(row[r++]);
			character_create_race_class_combos.push_back(combo);
		}
		mysql_free_result(result);
	} else {
		safe_delete_array(query);
		return false;
	}

	return true;
}

