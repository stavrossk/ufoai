/**
 * @file cp_ufopedia.c
 * @brief UFOpaedia script interpreter.
 * @todo Split the mail code into cl_mailclient.c/h
 * @todo Remove direct access to nodes
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../client.h"
#include "../cl_game.h"
#include "../menu/m_main.h"
#include "../menu/m_data.h"
#include "../menu/m_icon.h"
#include "../menu/node/m_node_text.h"
#include "../../shared/parse.h"
#include "cp_campaign.h"
#include "cp_mapfightequip.h"
#include "cp_time.h"

static cvar_t *mn_uppretext;
static cvar_t *mn_uppreavailable;

static pediaChapter_t *upChaptersDisplayList[MAX_PEDIACHAPTERS];
static int numChaptersDisplayList;

static technology_t	*upCurrentTech;
static pediaChapter_t *currentChapter;

#define MAX_UPTEXT 4096
static char upBuffer[MAX_UPTEXT];

#define MAIL_LENGTH 256
#define MAIL_BUFFER_SIZE 0x4000
static char mailBuffer[MAIL_BUFFER_SIZE];
#define CHECK_MAIL_EOL if (tempBuf[MAIL_LENGTH-3] != '\n') tempBuf[MAIL_LENGTH-2] = '\n';
#define MAIL_CLIENT_LINES 15

/**
 * @note don't change the order or you have to change the if statements about mn_updisplay cvar
 * in menu_ufopedia.ufo, too
 */
enum {
	UFOPEDIA_CHAPTERS,
	UFOPEDIA_INDEX,
	UFOPEDIA_ARTICLE,

	UFOPEDIA_DISPLAYEND
};
static int upDisplay = UFOPEDIA_CHAPTERS;

/**
 * @brief Checks If a technology/UFOpaedia-entry will be displayed in the UFOpaedia (-list).
 * @note This does not check for different display modes (only pre-research text, what statistics, etc...). The
 * content is mostly checked in @c UP_Article
 * @return qtrue if the tech gets displayed at all, otherwise qfalse.
 * @sa UP_Article
 */
static qboolean UP_TechGetsDisplayed (const technology_t *tech)
{
	return (RS_IsResearched_ptr(tech)	/* Is already researched OR ... */
	 || RS_Collected_(tech)	/* ... has collected items OR ... */
	 || (tech->statusResearchable && tech->preDescription.numDescriptions > 0))
	 && tech->type != RS_LOGIC	/* Is no logic block. */
	 && !tech->redirect;		/* Another technology will get displayed instead of this one. */
}

/**
 * @brief Modify the global display var
 * @sa UP_SetMailHeader
 */
static void UP_ChangeDisplay (int newDisplay)
{
	if (newDisplay < UFOPEDIA_DISPLAYEND && newDisplay >= 0)
		upDisplay = newDisplay;
	else
		Com_Printf("Error in UP_ChangeDisplay (%i)\n", newDisplay);

	Cvar_SetValue("mn_uppreavailable", 0);

	/* make sure, that we leave the mail header space */
	MN_ResetData(TEXT_UFOPEDIA_MAILHEADER);
	MN_ResetData(TEXT_UFOPEDIA_MAIL);
	MN_ResetData(TEXT_UFOPEDIA_REQUIREMENT);
	MN_ResetData(TEXT_UFOPEDIA_METADATA);
	MN_ResetData(TEXT_UFOPEDIA);

	switch (upDisplay) {
	case UFOPEDIA_CHAPTERS:
		currentChapter = NULL;
		upCurrentTech = NULL;
		Cvar_Set("mn_upmodel_top", "");
		Cvar_Set("mn_upmodel_bottom", "");
		Cvar_Set("mn_upimage_top", "base/empty");
		MN_ExecuteConfunc("mn_up_empty");
		Cvar_Set("mn_uptitle", _("UFOpaedia"));
		break;
	case UFOPEDIA_INDEX:
		Cvar_Set("mn_upmodel_top", "");
		Cvar_Set("mn_upmodel_bottom", "");
		Cvar_Set("mn_upimage_top", "base/empty");
		/* no break here */
	case UFOPEDIA_ARTICLE:
		MN_ExecuteConfunc("mn_up_article");
		break;
	}
	Cvar_SetValue("mn_updisplay", upDisplay);
}

/**
 * @brief Translate a aircraft statistic integer to a translated string
 * @sa aircraftParams_t
 * @sa CL_AircraftMenuStatsValues
 */
static const char* CL_AircraftStatToName (int stat)
{
	switch (stat) {
	case AIR_STATS_SPEED:
		return _("Cruising speed");
	case AIR_STATS_MAXSPEED:
		return _("Maximum speed");
	case AIR_STATS_SHIELD:
		return _("Armour");
	case AIR_STATS_ECM:
		return _("ECM");
	case AIR_STATS_DAMAGE:
		return _("Aircraft damage");
	case AIR_STATS_ACCURACY:
		return _("Accuracy");
	case AIR_STATS_FUELSIZE:
		return _("Fuel size");
	case AIR_STATS_WRANGE:
		return _("Weapon range");
	default:
		return _("Unknown weapon skill");
	}
}

/**
 * @brief Translate a aircraft size integer to a translated string
 * @sa aircraftSize_t
 */
static const char* CL_AircraftSizeToName (int aircraftSize)
{
	switch (aircraftSize) {
	case AIRCRAFT_SMALL:
		return _("Small");
	case AIRCRAFT_LARGE:
		return _("Large");
	default:
		return _("Unknown aircraft size");
	}
}

/**
 * @brief Displays the tech tree dependencies in the UFOpaedia
 * @sa UP_Article
 * @todo Add support for "requireAND"
 * @todo re-iterate trough logic blocks (i.e. append the tech-names it references recursively)
 */
static void UP_DisplayTechTree (const technology_t* t)
{
	int i;
	linkedList_t *upTechtree;
	const requirements_t *required;

	required = &t->requireAND;
	upTechtree = NULL;

	if (required->numLinks <= 0)
		LIST_AddString(&upTechtree, _("No requirements"));
	else
		for (i = 0; i < required->numLinks; i++) {
			const requirement_t *req = &required->links[i];
			if (req->type == RS_LINK_TECH) {
				technology_t *techRequired = req->link;
				if (!techRequired)
					Com_Error(ERR_DROP, "Could not find the tech for '%s'", req->id);

				/** Only display tech if it is ok to do so.
				 * @todo If it is one (a logic tech) we may want to re-iterate from its requirements? */
				if (!UP_TechGetsDisplayed(techRequired))
					continue;

				LIST_AddString(&upTechtree, _(techRequired->name));
			}
		}

	/* and now register the buffer */
	Cvar_Set("mn_uprequirement", "1");
	MN_RegisterLinkedListText(TEXT_UFOPEDIA_REQUIREMENT, upTechtree);
}

/**
 * @brief Prints the UFOpaedia description for armour
 * @sa UP_Article
 * @todo This is not ufopedia only - no? We could use it in the equip menu, too (object info tab)
 */
static void UP_ArmourDescription (const technology_t* t)
{
	int i;
	const objDef_t *od = INVSH_GetItemByID(t->provides);
	if (!od)
		Com_Error(ERR_DROP, "Could not find armour definition '%s' which should have been provided by '%s'", t->provides, t->id);

	/* select item */
	Cvar_Set("mn_upmodel_top", "");
	Cvar_Set("mn_upmodel_bottom", "");
	Cvar_Set("mn_upimage_top", t->image);

	upBuffer[0] = '\0';
	Com_sprintf(upBuffer, sizeof(upBuffer), _("Size:\t%i\n"), od->size);
	Q_strcat(upBuffer, "\n", sizeof(upBuffer));

	Q_strcat(upBuffer, _("^BDamage type\tProtection\n"), sizeof(upBuffer));
	for (i = 0; i < csi.numDTs; i++) {
		if (!csi.dts[i].showInMenu)
			continue;
		Q_strcat(upBuffer, va(_("%s\t%i\n"), _(csi.dts[i].id), od->ratings[i]), sizeof(upBuffer));
	}

	Cvar_Set("mn_upmetadata", "1");
	MN_RegisterText(TEXT_UFOPEDIA_METADATA, upBuffer);
	UP_DisplayTechTree(t);
}

/**
 * @brief Prints the UFOpaedia description for buildings
 * @sa UP_Article
 */
static void UP_BuildingDescription (const technology_t* t)
{
	const building_t* b = B_GetBuildingTemplate(t->provides);

	if (!b) {
		Com_sprintf(upBuffer, sizeof(upBuffer), _("Error - could not find building"));
	} else {
		Com_sprintf(upBuffer, sizeof(upBuffer), _("Needs:\t%s\n"), b->dependsBuilding ? _(b->dependsBuilding->name) : _("None"));
		Q_strcat(upBuffer, va(ngettext("Construction time:\t%i day\n", "Construction time:\t%i days\n", b->buildTime), b->buildTime), sizeof(upBuffer));
		Q_strcat(upBuffer, va(_("Cost:\t%i c\n"), b->fixCosts), sizeof(upBuffer));
		Q_strcat(upBuffer, va(_("Running costs:\t%i c\n"), b->varCosts), sizeof(upBuffer));
	}

	Cvar_Set("mn_upmetadata", "1");
	MN_RegisterText(TEXT_UFOPEDIA_METADATA, upBuffer);
	UP_DisplayTechTree(t);
}

/**
 * @brief Prints the (UFOpaedia and other) description for aircraft items
 * @param item The object definition of the item
 * @sa UP_Article
 * Not only called from UFOpaedia but also from other places to display
 * @todo Don't display things like speed for base defence items - a missile
 * facility isn't getting slower or faster due a special weapon or ammunition
 */
void UP_AircraftItemDescription (const objDef_t *item)
{
	static char itemText[MAX_SMALLMENUTEXTLEN];
	int i;

	/* Set menu text node content to null. */
	INV_ItemDescription(NULL);

	/* no valid item id given */
	if (!item) {
		Cvar_Set("mn_item", "");
		Cvar_Set("mn_itemname", "");
		Cvar_Set("mn_upmodel_top", "");
		MN_ResetData(TEXT_UFOPEDIA_METADATA);
		return;
	}

	/* select item */
	assert(item->craftitem.type >= 0);
	assert(item->tech);
	/** @todo Is this actually _used_ in the ufopedia?
	 * No, but in buy and production - and they are using these functions, too, no? (mattn) */
	Cvar_Set("mn_item", item->id);
	Cvar_Set("mn_itemname", _(item->tech->name));
	if (item->tech->mdl)
		Cvar_Set("mn_upmodel_top", item->tech->mdl);
	else
		Cvar_Set("mn_upmodel_top", "");

	/* set description text */
	if (RS_IsResearched_ptr(item->tech)) {
		*itemText = '\0';

		if (item->craftitem.type == AC_ITEM_WEAPON)
			Q_strcat(itemText, va(_("Weight:\t%s\n"), AII_WeightToName(AII_GetItemWeightBySize(item))), sizeof(itemText));
		else if (item->craftitem.type == AC_ITEM_AMMO) {
			/* We display the characteristics of this ammo */
			if (!item->craftitem.unlimitedAmmo)
				Q_strcat(itemText, va(_("Ammo:\t%i\n"), item->ammo), sizeof(itemText));
			if (!equal(item->craftitem.weaponDamage, 0))
				Q_strcat(itemText, va(_("Damage:\t%i\n"), (int) item->craftitem.weaponDamage), sizeof(itemText));
			Q_strcat(itemText, va(_("Reloading time:\t%i\n"),  (int) item->craftitem.weaponDelay), sizeof(itemText));
		}
		/* We write the range of the weapon */
		if (!equal(item->craftitem.stats[AIR_STATS_WRANGE], 0))
			Q_strcat(itemText, va("%s:\t%i\n", CL_AircraftStatToName(AIR_STATS_WRANGE),
				CL_AircraftMenuStatsValues(item->craftitem.stats[AIR_STATS_WRANGE], AIR_STATS_WRANGE)), sizeof(itemText));

		/* we scan all stats except weapon range */
		for (i = 0; i < AIR_STATS_MAX; i++) {
			const char *statsName = CL_AircraftStatToName(i);
			if (i == AIR_STATS_WRANGE)
				continue;
			if (item->craftitem.stats[i] > 2.0f)
				Q_strcat(itemText, va("%s:\t+%i\n", statsName, CL_AircraftMenuStatsValues(item->craftitem.stats[i], i)), sizeof(itemText));
			else if (item->craftitem.stats[i] < -2.0f)
				Q_strcat(itemText, va("%s:\t%i\n", statsName, CL_AircraftMenuStatsValues(item->craftitem.stats[i], i)), sizeof(itemText));
			else if (item->craftitem.stats[i] > 1.0f)
				Q_strcat(itemText, va(_("%s:\t+%i %%\n"), statsName, (int)(item->craftitem.stats[i] * 100) - 100), sizeof(itemText));
			else if (!equal(item->craftitem.stats[i], 0))
				Q_strcat(itemText, va(_("%s:\t%i %%\n"), statsName, (int)(item->craftitem.stats[i] * 100) - 100), sizeof(itemText));
		}
	}

	Cvar_Set("mn_upmetadata", "1");
	MN_RegisterText(TEXT_UFOPEDIA_METADATA, itemText);
}

/**
 * @brief Prints the UFOpaedia description for aircraft
 * @note Also checks whether the aircraft tech is already researched or collected
 * @sa BS_MarketAircraftDescription
 * @sa UP_Article
 */
void UP_AircraftDescription (const technology_t* tech)
{
	INV_ItemDescription(NULL);

	/* ensure that the buffer is emptied in every case */
	upBuffer[0] = '\0';

	if (RS_IsResearched_ptr(tech)) {
		const aircraft_t* aircraft = AIR_GetAircraft(tech->provides);
		if (!aircraft) {
			Com_sprintf(upBuffer, sizeof(upBuffer), _("Error - could not find aircraft"));
		} else {
			int i;
			for (i = 0; i < AIR_STATS_MAX; i++) {
				switch (i) {
				case AIR_STATS_SPEED:
					/* speed may be converted to km/h : multiply by pi / 180 * earth_radius */
					Q_strcat(upBuffer, va(_("%s:\t%i km/h\n"), CL_AircraftStatToName(i),
						CL_AircraftMenuStatsValues(aircraft->stats[i], i)), sizeof(upBuffer));
					break;
				case AIR_STATS_MAXSPEED:
					/* speed may be converted to km/h : multiply by pi / 180 * earth_radius */
					Q_strcat(upBuffer, va(_("%s:\t%i km/h\n"), CL_AircraftStatToName(i),
						CL_AircraftMenuStatsValues(aircraft->stats[i], i)), sizeof(upBuffer));
					break;
				case AIR_STATS_FUELSIZE:
					Q_strcat(upBuffer, va(_("Operational range:\t%i km\n"),
						CL_AircraftMenuStatsValues(aircraft->stats[AIR_STATS_FUELSIZE] * aircraft->stats[AIR_STATS_SPEED], AIR_STATS_OP_RANGE)), sizeof(upBuffer));
				case AIR_STATS_ACCURACY:
					Q_strcat(upBuffer, va(_("%s:\t%i\n"), CL_AircraftStatToName(i),
						CL_AircraftMenuStatsValues(aircraft->stats[i], i)), sizeof(upBuffer));
					break;
				default:
					break;
				}
			}
			Q_strcat(upBuffer, va(_("Aircraft size:\t%s\n"), CL_AircraftSizeToName(aircraft->size)), sizeof(upBuffer));
			/* @note: while MAX_ACTIVETEAM limits the number of soldiers on a craft
			 * there is no use to show this in case of an UFO (would be misleading): */
			if (!AIR_IsUFO(aircraft))
				Q_strcat(upBuffer, va(_("Max. soldiers:\t%i\n"), aircraft->maxTeamSize), sizeof(upBuffer));
		}
	} else if (RS_Collected_(tech)) {
		/** @todo Display crippled info and pre-research text here */
		Com_sprintf(upBuffer, sizeof(upBuffer), _("Unknown - need to research this"));
	} else {
		Com_sprintf(upBuffer, sizeof(upBuffer), _("Unknown - need to research this"));
	}

	Cvar_Set("mn_upmetadata", "1");
	MN_RegisterText(TEXT_UFOPEDIA_METADATA, upBuffer);
	UP_DisplayTechTree(tech);
}

/**
 * @brief Prints the description for robots/ugvs.
 * @param[in] ugvType What type of robot/ugv to print the description for.
 * @sa BS_MarketClick_f
 * @sa UP_Article
 */
void UP_UGVDescription (const ugv_t *ugvType)
{
	static char itemText[MAX_SMALLMENUTEXTLEN];
	const technology_t *tech;

	assert(ugvType);

	tech = RS_GetTechByProvided(ugvType->id);
	assert(tech);

	INV_ItemDescription(NULL);

	/* Set name of ugv/robot */
	Cvar_Set("mn_itemname", _(tech->name));

	Cvar_Set("mn_upmetadata", "1");
	if (RS_IsResearched_ptr(tech)) {
		/** @todo make me shiny */
		Com_sprintf(itemText, sizeof(itemText), _("%s\n%s"), _(tech->name), ugvType->weapon);
		MN_RegisterText(TEXT_UFOPEDIA_METADATA, itemText);
	} else if (RS_Collected_(tech)) {
		/** @todo Display crippled info and pre-research text here */
		Com_sprintf(itemText, sizeof(itemText), _("Unknown - need to research this"));
		MN_RegisterText(TEXT_UFOPEDIA_METADATA, itemText);
	} else {
		Com_sprintf(itemText, sizeof(itemText), _("Unknown - need to research this"));
		MN_RegisterText(TEXT_UFOPEDIA_METADATA, itemText);
	}
}

/**
 * @brief Sets the amount of unread/new mails
 * @note This is called every campaign frame - to update ccs.numUnreadMails
 * just set it to -1 before calling this function
 * @sa CL_CampaignRun
 */
int UP_GetUnreadMails (void)
{
	const message_t *m = cp_messageStack;

	if (ccs.numUnreadMails != -1)
		return ccs.numUnreadMails;

	ccs.numUnreadMails = 0;

	while (m) {
		switch (m->type) {
		case MSG_RESEARCH_PROPOSAL:
			assert(m->pedia);
			if (m->pedia->mail[TECHMAIL_PRE].from && !m->pedia->mail[TECHMAIL_PRE].read)
				ccs.numUnreadMails++;
			break;
		case MSG_RESEARCH_FINISHED:
			assert(m->pedia);
			if (m->pedia->mail[TECHMAIL_RESEARCHED].from && RS_IsResearched_ptr(m->pedia) && !m->pedia->mail[TECHMAIL_RESEARCHED].read)
				ccs.numUnreadMails++;
			break;
		case MSG_NEWS:
			assert(m->pedia);
			if (m->pedia->mail[TECHMAIL_PRE].from && !m->pedia->mail[TECHMAIL_PRE].read)
				ccs.numUnreadMails++;
			if (m->pedia->mail[TECHMAIL_RESEARCHED].from && !m->pedia->mail[TECHMAIL_RESEARCHED].read)
				ccs.numUnreadMails++;
			break;
		case MSG_EVENT:
			assert(m->eventMail);
			if (!m->eventMail->read)
				ccs.numUnreadMails++;
			break;
		default:
			break;
		}
		m = m->next;
	}

	/* use strings here */
	Cvar_Set("mn_upunreadmail", va("%i", ccs.numUnreadMails));
	return ccs.numUnreadMails;
}

/**
 * @brief Binds the mail header (if needed) to the mn.menuText array.
 * @note if there is a mail header.
 * @param[in] tech The tech to generate a header for.
 * @param[in] type The type of mail (research proposal or finished research)
 * @sa UP_ChangeDisplay
 * @sa CL_EventAddMail_f
 * @sa CL_GetEventMail
 */
static void UP_SetMailHeader (technology_t* tech, techMailType_t type, eventMail_t* mail)
{
	static char mailHeader[8 * MAX_VAR] = ""; /* bigger as techMail_t (utf8) */
	char dateBuf[MAX_VAR] = "";
	const char *subjectType = "";
	const char *from, *to, *subject, *model;
	dateLong_t date;

	if (mail) {
		from = mail->from;
		to = mail->to;
		model = mail->model;
		subject = mail->subject;
		Q_strncpyz(dateBuf, _(mail->date), sizeof(dateBuf));
		mail->read = qtrue;
		/* reread the unread mails in UP_GetUnreadMails */
		ccs.numUnreadMails = -1;
	} else {
		assert(tech);
		assert(type < TECHMAIL_MAX);

		from = tech->mail[type].from;
		to = tech->mail[type].to;
		subject = tech->mail[type].subject;
		model = tech->mail[type].model;

		if (tech->mail[type].date) {
			Q_strncpyz(dateBuf, _(tech->mail[type].date), sizeof(dateBuf));
		} else {
			switch (type) {
			case TECHMAIL_PRE:
				CL_DateConvertLong(&tech->preResearchedDate, &date);
				Com_sprintf(dateBuf, sizeof(dateBuf), _("%i %s %02i"),
					date.year, Date_GetMonthName(date.month - 1), date.day);
				break;
			case TECHMAIL_RESEARCHED:
				CL_DateConvertLong(&tech->researchedDate, &date);
				Com_sprintf(dateBuf, sizeof(dateBuf), _("%i %s %02i"),
					date.year, Date_GetMonthName(date.month - 1), date.day);
				break;
			default:
				Com_Error(ERR_DROP, "UP_SetMailHeader: unhandled techMailType_t %i for date.", type);
			}
		}
		if (from != NULL) {
			if (!tech->mail[type].read) {
				tech->mail[type].read = qtrue;
				/* reread the unread mails in UP_GetUnreadMails */
				ccs.numUnreadMails = -1;
			}
			/* only if mail and mail_pre are available */
			if (tech->numTechMails == TECHMAIL_MAX) {
				switch (type) {
				case TECHMAIL_PRE:
					subjectType = _("Proposal: ");
					break;
				case TECHMAIL_RESEARCHED:
					subjectType = _("Re: ");
					break;
				default:
					Com_Error(ERR_DROP, "UP_SetMailHeader: unhandled techMailType_t %i for subject.", type);
				}
			}
		} else {
			MN_ResetData(TEXT_UFOPEDIA_MAILHEADER);
			return;
		}
	}
	Com_sprintf(mailHeader, sizeof(mailHeader), _("FROM: %s\nTO: %s\nDATE: %s"),
		_(from), _(to), dateBuf);
	Cvar_Set("mn_mail_sender_head", model ? model : "");
	Cvar_Set("mn_mail_from", _(from));
	Cvar_Set("mn_mail_subject", va("%s%s", subjectType, _(subject)));
	Cvar_Set("mn_mail_to", _(to));
	Cvar_Set("mn_mail_date", dateBuf);
	MN_RegisterText(TEXT_UFOPEDIA_MAILHEADER, mailHeader);
}

/**
 * @brief Set the ammo model to display to selected ammo (only for a reloadable weapon)
 * @param tech technology_t pointer for the weapon's tech
 * @sa UP_Article
 */
static void UP_DrawAssociatedAmmo (const technology_t* tech)
{
	const objDef_t *od = INVSH_GetItemByID(tech->provides);
	/* If this is a weapon, we display the model of the associated ammunition in the lower right */
	if (od->numAmmos > 0) {
		const technology_t *associated = od->ammos[0]->tech;
		Cvar_Set("mn_upmodel_bottom", associated->mdl);
	}
}

/**
 * @brief Display only the TEXT_UFOPEDIA part for a given technology
 * @param[in] tech The technology_t pointer to print the UFOpaedia article for
 * @sa UP_Article
 */
static void UP_Article (technology_t* tech, eventMail_t *mail)
{
	int i;

	UP_ChangeDisplay(UFOPEDIA_ARTICLE);

	if (tech) {
		if (tech->mdl)
			Cvar_Set("mn_upmodel_top", tech->mdl);
		else
			Cvar_Set("mn_upmodel_top", "");

		if (tech->image)
			Cvar_Set("mn_upimage_top", tech->image);
		else
			Cvar_Set("mn_upimage_top", "");

		Cvar_Set("mn_upmodel_bottom", "");

		if (tech->type == RS_WEAPON)
			UP_DrawAssociatedAmmo(tech);
		Cvar_Set("mn_uprequirement", "");
		Cvar_Set("mn_upmetadata", "");
	}

	MN_ResetData(TEXT_UFOPEDIA);
	MN_ResetData(TEXT_UFOPEDIA_REQUIREMENT);

	if (mail) {
		/* event mail */
		Cvar_SetValue("mn_uppreavailable", 0);
		Cvar_SetValue("mn_updisplay", UFOPEDIA_CHAPTERS);
		UP_SetMailHeader(NULL, 0, mail);
		MN_RegisterText(TEXT_UFOPEDIA, _(mail->body));
		/* This allows us to use the index button in the UFOpaedia,
		 * eventMails don't have any chapter to go back to. */
		upDisplay = UFOPEDIA_INDEX;
	} else if (tech) {
		currentChapter = tech->upChapter;
		upCurrentTech = tech;

		if (RS_IsResearched_ptr(tech)) {
			Cvar_Set("mn_uptitle", va("%s: %s %s", _("UFOpaedia"), _(tech->name), _("(complete)")));
			/* If researched -> display research text */
			MN_RegisterText(TEXT_UFOPEDIA, _(RS_GetDescription(&tech->description)));
			if (tech->preDescription.numDescriptions > 0) {
				/* Display pre-research text and the buttons if a pre-research text is available. */
				if (mn_uppretext->integer) {
					MN_RegisterText(TEXT_UFOPEDIA, _(RS_GetDescription(&tech->preDescription)));
					UP_SetMailHeader(tech, TECHMAIL_PRE, NULL);
				} else {
					UP_SetMailHeader(tech, TECHMAIL_RESEARCHED, NULL);
				}
				Cvar_SetValue("mn_uppreavailable", 1);
			} else {
				/* Do not display the pre-research-text button if none is available (no need to even bother clicking there). */
				Cvar_SetValue("mn_uppreavailable", 0);
				Cvar_SetValue("mn_updisplay", UFOPEDIA_CHAPTERS);
				UP_SetMailHeader(tech, TECHMAIL_RESEARCHED, NULL);
			}

			switch (tech->type) {
			case RS_ARMOUR:
				UP_ArmourDescription(tech);
				break;
			case RS_WEAPON:
				for (i = 0; i < csi.numODs; i++) {
					if (!strcmp(tech->provides, csi.ods[i].id)) {
						const char *text;
						INV_ItemDescription(&csi.ods[i]);
						UP_DisplayTechTree(tech);
						/* translate info */
						text = MN_GetText(TEXT_STANDARD);
						MN_RegisterText(TEXT_UFOPEDIA_METADATA, text);
						if (text != NULL)
							Cvar_Set("mn_upmetadata", "1");
						break;
					}
				}
				break;
			case RS_TECH:
				UP_DisplayTechTree(tech);
				break;
			case RS_CRAFT:
				UP_AircraftDescription(tech);
				break;
			case RS_CRAFTITEM:
				UP_AircraftItemDescription(AII_GetAircraftItemByID(tech->provides));
				break;
			case RS_BUILDING:
				UP_BuildingDescription(tech);
				break;
			default:
				break;
			}
		/* see also UP_TechGetsDisplayed */
		} else if (RS_Collected_(tech) || (tech->statusResearchable && tech->preDescription.numDescriptions > 0)) {
			/* This tech has something collected or has a research proposal. (i.e. pre-research text) */
			Cvar_Set("mn_uptitle", va("%s: %s", _("UFOpaedia"), _(tech->name)));
			/* Not researched but some items collected -> display pre-research text if available. */
			if (tech->preDescription.numDescriptions > 0) {
				MN_RegisterText(TEXT_UFOPEDIA, _(RS_GetDescription(&tech->preDescription)));
				UP_SetMailHeader(tech, TECHMAIL_PRE, NULL);
			} else {
				MN_RegisterText(TEXT_UFOPEDIA, _("No pre-research description available."));
			}
		} else {
			Cvar_Set("mn_uptitle", va("%s: %s", _("UFOpaedia"), _(tech->name)));
			MN_ResetData(TEXT_UFOPEDIA);
		}
	} else {
		Com_Error(ERR_DROP, "UP_Article: No mail or tech given");
	}
}

/**
 * @sa CL_EventAddMail_f
 */
void UP_OpenEventMail (const char *eventMailID)
{
	eventMail_t* mail;
	mail = CL_GetEventMail(eventMailID, qfalse);
	if (!mail)
		return;

	MN_PushMenu("mail", NULL);
	UP_Article(NULL, mail);
}

/**
 * @brief Opens the mail view from everywhere with the entry given through name
 * @param name mail entry id (technology script id)
 * @sa UP_FindEntry_f
 */
static void UP_OpenMailWith (const char *techID)
{
	if (!techID)
		return;

	MN_PushMenu("mail", NULL);
	Cbuf_AddText(va("ufopedia %s\n", techID));
}

/**
 * @brief Opens the UFOpaedia from everywhere with the entry given through name
 * @param name UFOpaedia entry id (technology script id)
 * @sa UP_FindEntry_f
 */
void UP_OpenWith (const char *techID)
{
	if (!techID)
		return;

	MN_PushMenu("ufopedia", NULL);
	Cbuf_AddText(va("ufopedia %s\n", techID));
}

/**
 * @brief Opens the UFOpaedia with the entry given through name, not deleting copies
 * @param name UFOpaedia entry id (technology script id)
 * @sa UP_FindEntry_f
 */
void UP_OpenCopyWith (const char *techID)
{
	Cmd_ExecuteString("mn_push ufopedia");
	Cbuf_AddText(va("ufopedia %s\n", techID));
}


/**
 * @brief Search and open the UFOpaedia with given id
 */
static void UP_FindEntry_f (void)
{
	const char *id;
	technology_t *tech;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <id>\n", Cmd_Argv(0));
		return;
	}

	/* what are we searching for? */
	id = Cmd_Argv(1);

	/* maybe we get a call like 'ufopedia ""' */
	if (id[0] == '\0') {
		Com_Printf("UP_FindEntry_f: No UFOpaedia entry given as parameter\n");
		return;
	}

	tech = RS_GetTechByID(id);
	if (!tech) {
		Com_DPrintf(DEBUG_CLIENT, "UP_FindEntry_f: No UFOpaedia entry found for %s\n", id);
		return;
	}

	if (tech->redirect)
		tech = tech->redirect;

	UP_Article(tech, NULL);
}

static menuOption_t *upChapters;

/**
 * @brief Alloc an array of option for all chapters and articles we can have
 * @note only alloc one time the memory, else return the last allocated memory
 * @return An array of options
 * @sa upChapters
 */
static menuOption_t * UP_AllocOptions()
{
	int count = 0;
	int i;

	if (upChapters)
		return upChapters;

	/* one option per chapters */
	count += ccs.numChapters;

	/* one option per used tech */
	for (i = 0; i < ccs.numChapters; i++) {
		technology_t *tech = ccs.upChapters[i].first;
		while (tech) {
			count++;
			/* @todo is it really need? */
			if (tech == tech->upNext)
				break;
			tech = tech->upNext;
		}
	}

	upChapters = (menuOption_t *) Mem_PoolAlloc(sizeof(*upChapters) * count, cp_campaignPool, 0);
	return upChapters;
}

/**
 * @brief Generate a list of options for all allowed articles of a chapter
 * @param[in] parentChapter requested chapter
 * @param[in] array first element of the array of option we can update
 * @param[out] count number of generated options/articles
 * @return The first option of the list, else NULL if no articles
 */
static menuOption_t* UP_GenerateArticlesSummary (pediaChapter_t *parentChapter, menuOption_t* array, int *count)
{
	technology_t *tech = parentChapter->first;
	int articleCount = 0;
	menuOption_t* option = array;
	menuOption_t *last = NULL;

	while (tech) {
		if (UP_TechGetsDisplayed(tech)) {
			const char* id = va("@%i", tech->idx);
			MN_InitOption(option, id, tech->name, id);
			articleCount++;
			if (last)
				last->next = option;
			last = option;
			option++;
		}
		tech = tech->upNext;
	}

	*count = articleCount;
	if (articleCount == 0)
		return NULL;

	MN_SortOptions(&array);

	return array;
}

/**
 * @brief Generate a tree of option for all allowed chapters and articles
 * @note it update OPTION_UFOPEDIA
 */
static void UP_GenerateSummary (void)
{
	int i;
	menuOption_t *chapters;
	menuOption_t *chapter;
	menuOption_t *articleOptions;
	menuOption_t *last = NULL;
	int num = 0;

	/* alloc options only one time */
	chapters = UP_AllocOptions();
	chapter = chapters;
	numChaptersDisplayList = 0;

	articleOptions = chapters + ccs.numChapters;

	for (i = 0; i < ccs.numChapters; i++) {
		/* Check if there are any researched or collected items in this chapter ... */
		qboolean researchedEntries = qfalse;
		upCurrentTech = ccs.upChapters[i].first;
		do {
			if (UP_TechGetsDisplayed(upCurrentTech)) {
				researchedEntries = qtrue;
				break;
			}
			upCurrentTech = upCurrentTech->upNext;
		} while (upCurrentTech);

		/* .. and if so add them to the displaylist of chapters. */
		if (researchedEntries) {
			int articleCount = 0;
			if (numChaptersDisplayList >= MAX_PEDIACHAPTERS)
				Com_Error(ERR_DROP, "MAX_PEDIACHAPTERS hit");
			upChaptersDisplayList[numChaptersDisplayList++] = &ccs.upChapters[i];

			/* chapter section*/
			MN_InitOption(chapter, ccs.upChapters[i].id, ccs.upChapters[i].name, va("%i", num));
			chapter->icon = MN_GetIconByName(va("ufopedia_%s", ccs.upChapters[i].id));

			/* articles */
			chapter->firstChild = UP_GenerateArticlesSummary(&ccs.upChapters[i], articleOptions, &articleCount);
			articleOptions += articleCount;

			/* link chapters together */
			if (last)
				last->next = chapter;
			last = chapter;
			chapter++;
			num++;
		}
	}

	MN_RegisterOption(OPTION_UFOPEDIA, chapters);
	Cvar_Set("mn_uptitle", _("UFOpaedia"));
}

/**
 * @brief Displays the chapters in the UFOpaedia
 */
static void UP_Content_f (void)
{
	UP_GenerateSummary();
	UP_ChangeDisplay(UFOPEDIA_CHAPTERS);
}

/**
 * @brief Callback when we click on the ufopedia summary
 * @note when we click on a chapter, param=chapterId,
 * when we click on an article, param='@'+techId
 */
static void UP_Click_f (void)
{
	/* article index start with a @ */
	if (Cmd_Argv(1)[0] == '@') {
		const int techId = atoi(Cmd_Argv(1) + 1);
		technology_t* tech;
		assert(techId >= 0);
		assert(techId < ccs.numTechnologies);
		tech = &ccs.technologies[techId];
		if (tech)
			UP_Article(tech, NULL);
		return;
	}

	/* it clean up the display */
	UP_ChangeDisplay(UFOPEDIA_CHAPTERS);
}

/**
 * @todo The "num" value and the link-index will most probably not match.
 */
static void UP_TechTreeClick_f (void)
{
	int num;
	int i;
	requirements_t *required_AND;
	technology_t *techRequired;

	if (Cmd_Argc() < 2)
		return;
	num = atoi(Cmd_Argv(1));

	if (!upCurrentTech)
		return;

	required_AND = &upCurrentTech->requireAND;
	if (num < 0 || num >= required_AND->numLinks)
		return;

	/* skip every tech which have not been displayed in techtree */
	for (i = 0; i <= num; i++) {
		if (required_AND->links[i].type != RS_LINK_TECH
		 && required_AND->links[i].type != RS_LINK_TECH_NOT)
			num++;
	}

	techRequired = required_AND->links[num].link;
	if (!techRequired)
		Com_Error(ERR_DROP, "Could not find the tech for '%s'", required_AND->links[num].id);

	/* maybe there is no UFOpaedia chapter assigned - this tech should not be opened at all */
	if (!techRequired->upChapter)
		return;

	UP_OpenWith(techRequired->id);
}

/**
 * @brief Redraw the UFOpaedia article
 */
static void UP_Update_f (void)
{
	if (upCurrentTech)
		UP_Article(upCurrentTech, NULL);
}

/**
 * @brief Mailclient click function callback
 * @sa UP_OpenMail_f
 */
static void UP_MailClientClick_f (void)
{
	message_t *m = cp_messageStack;
	int num;
	int cnt = -1;

	if (Cmd_Argc() < 2)
		return;

	num = atoi(Cmd_Argv(1));

	while (m) {
		switch (m->type) {
		case MSG_RESEARCH_PROPOSAL:
			if (!m->pedia->mail[TECHMAIL_PRE].from)
				break;
			cnt++;
			if (cnt == num) {
				Cvar_SetValue("mn_uppretext", 1);
				UP_OpenMailWith(m->pedia->id);
				return;
			}
			break;
		case MSG_RESEARCH_FINISHED:
			if (!m->pedia->mail[TECHMAIL_RESEARCHED].from)
				break;
			cnt++;
			if (cnt == num) {
				Cvar_SetValue("mn_uppretext", 0);
				UP_OpenMailWith(m->pedia->id);
				return;
			}
			break;
		case MSG_NEWS:
			if (m->pedia->mail[TECHMAIL_PRE].from || m->pedia->mail[TECHMAIL_RESEARCHED].from) {
				cnt++;
				if (cnt >= num) {
					UP_OpenMailWith(m->pedia->id);
					return;
				}
			}
			break;
		case MSG_EVENT:
			cnt++;
			if (cnt >= num) {
				UP_OpenEventMail(m->eventMail->id);
				return;
			}
			break;
		default:
			break;
		}
		m = m->next;
	}
}

/**
 * @brief Change UFOpaedia article when clicking on the name of associated ammo or weapon
 */
static void UP_ResearchedLinkClick_f (void)
{
	objDef_t *od;

	if (!upCurrentTech) /* if called from console */
		return;

	od = INVSH_GetItemByID(upCurrentTech->provides);
	assert(od);

	if (!strcmp(od->type, "ammo")) {
		const technology_t *t = od->weapons[0]->tech;
		if (UP_TechGetsDisplayed(t))
			UP_OpenWith(t->id);
	} else if (od->weapon && od->reload) {
		const technology_t *t = od->ammos[0]->tech;
		if (UP_TechGetsDisplayed(t))
			UP_OpenWith(t->id);
	}
}

/**
 * @brief Set up mail icons
 * @sa UP_OpenMail_f
 * @note @c UP_OpenMail_f must be called before using this function - we need the
 * @c mailClientListNode pointer here
 */
static void UP_SetMailButtons_f (void)
{
	int i = 0, num;
	const message_t *m = cp_messageStack;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <pos>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));

	while (m && (i < MAIL_CLIENT_LINES)) {
		switch (m->type) {
		case MSG_RESEARCH_PROPOSAL:
			if (!m->pedia->mail[TECHMAIL_PRE].from)
				break;
			if (num) {
				num--;
			} else {
				Cvar_Set(va("mn_mail_read%i", i), m->pedia->mail[TECHMAIL_PRE].read ? "1": "0");
				Cvar_Set(va("mn_mail_icon%i", i++), m->pedia->mail[TECHMAIL_PRE].icon);
			}
			break;
		case MSG_RESEARCH_FINISHED:
			if (!m->pedia->mail[TECHMAIL_RESEARCHED].from)
				break;
			if (num) {
				num--;
			} else {
				Cvar_Set(va("mn_mail_read%i", i), m->pedia->mail[TECHMAIL_RESEARCHED].read ? "1": "0");
				Cvar_Set(va("mn_mail_icon%i", i++), m->pedia->mail[TECHMAIL_RESEARCHED].icon);
			}
			break;
		case MSG_NEWS:
			if (m->pedia->mail[TECHMAIL_PRE].from) {
				if (num) {
					num--;
				} else {
					Cvar_Set(va("mn_mail_read%i", i), m->pedia->mail[TECHMAIL_PRE].read ? "1": "0");
					Cvar_Set(va("mn_mail_icon%i", i++), m->pedia->mail[TECHMAIL_PRE].icon);
				}
			} else if (m->pedia->mail[TECHMAIL_RESEARCHED].from) {
				if (num) {
					num--;
				} else {
					Cvar_Set(va("mn_mail_read%i", i), m->pedia->mail[TECHMAIL_RESEARCHED].read ? "1": "0");
					Cvar_Set(va("mn_mail_icon%i", i++), m->pedia->mail[TECHMAIL_RESEARCHED].icon);
				}
			}
			break;
		case MSG_EVENT:
			if (m->eventMail->from) {
				if (num) {
					num--;
				} else {
					Cvar_Set(va("mn_mail_read%i", i), m->eventMail->read ? "1": "0");
					Cvar_Set(va("mn_mail_icon%i", i++), m->eventMail->icon ? m->eventMail->icon : "");
				}
			}
			break;
		default:
			break;
		}
		m = m->next;
	}
	while (i < MAIL_CLIENT_LINES) {
		Cvar_Set(va("mn_mail_read%i", i), "-1");
		Cvar_Set(va("mn_mail_icon%i", i++), "");
	}
}

/**
 * @brief Start the mailclient
 * @sa UP_MailClientClick_f
 * @sa CP_GetUnreadMails
 * @sa CL_EventAddMail_f
 * @sa MS_AddNewMessage
 */
static void UP_OpenMail_f (void)
{
	char tempBuf[MAIL_LENGTH] = "";
	const message_t *m = cp_messageStack;
	dateLong_t date;

	mailBuffer[0] = '\0';

	while (m) {
		switch (m->type) {
		case MSG_RESEARCH_PROPOSAL:
			if (!m->pedia->mail[TECHMAIL_PRE].from)
				break;
			CL_DateConvertLong(&m->pedia->preResearchedDate, &date);
			if (!m->pedia->mail[TECHMAIL_PRE].read)
				Com_sprintf(tempBuf, sizeof(tempBuf), _("^BProposal: %s\t%i %s %02i\n"),
					_(m->pedia->mail[TECHMAIL_PRE].subject),
					date.year, Date_GetMonthName(date.month - 1), date.day);
			else
				Com_sprintf(tempBuf, sizeof(tempBuf), _("Proposal: %s\t%i %s %02i\n"),
					_(m->pedia->mail[TECHMAIL_PRE].subject),
					date.year, Date_GetMonthName(date.month - 1), date.day);
			CHECK_MAIL_EOL
			Q_strcat(mailBuffer, tempBuf, sizeof(mailBuffer));
			break;
		case MSG_RESEARCH_FINISHED:
			if (!m->pedia->mail[TECHMAIL_RESEARCHED].from)
				break;
			CL_DateConvertLong(&m->pedia->researchedDate, &date);
			if (!m->pedia->mail[TECHMAIL_RESEARCHED].read)
				Com_sprintf(tempBuf, sizeof(tempBuf), _("^BRe: %s\t%i %s %02i\n"),
					_(m->pedia->mail[TECHMAIL_RESEARCHED].subject),
					date.year, Date_GetMonthName(date.month - 1), date.day);
			else
				Com_sprintf(tempBuf, sizeof(tempBuf), _("Re: %s\t%i %s %02i\n"),
					_(m->pedia->mail[TECHMAIL_RESEARCHED].subject),
					date.year, Date_GetMonthName(date.month - 1), date.day);
			CHECK_MAIL_EOL
			Q_strcat(mailBuffer, tempBuf, sizeof(mailBuffer));
			break;
		case MSG_NEWS:
			if (m->pedia->mail[TECHMAIL_PRE].from) {
				CL_DateConvertLong(&m->pedia->preResearchedDate, &date);
				if (!m->pedia->mail[TECHMAIL_PRE].read)
					Com_sprintf(tempBuf, sizeof(tempBuf), _("^B%s\t%i %s %02i\n"),
						_(m->pedia->mail[TECHMAIL_PRE].subject),
						date.year, Date_GetMonthName(date.month - 1), date.day);
				else
					Com_sprintf(tempBuf, sizeof(tempBuf), _("%s\t%i %s %02i\n"),
						_(m->pedia->mail[TECHMAIL_PRE].subject),
						date.year, Date_GetMonthName(date.month - 1), date.day);
				CHECK_MAIL_EOL
				Q_strcat(mailBuffer, tempBuf, sizeof(mailBuffer));
			} else if (m->pedia->mail[TECHMAIL_RESEARCHED].from) {
				CL_DateConvertLong(&m->pedia->researchedDate, &date);
				if (!m->pedia->mail[TECHMAIL_RESEARCHED].read)
					Com_sprintf(tempBuf, sizeof(tempBuf), _("^B%s\t%i %s %02i\n"),
						_(m->pedia->mail[TECHMAIL_RESEARCHED].subject),
						date.year, Date_GetMonthName(date.month - 1), date.day);
				else
					Com_sprintf(tempBuf, sizeof(tempBuf), _("%s\t%i %s %02i\n"),
						_(m->pedia->mail[TECHMAIL_RESEARCHED].subject),
						date.year, Date_GetMonthName(date.month - 1), date.day);
				CHECK_MAIL_EOL
				Q_strcat(mailBuffer, tempBuf, sizeof(mailBuffer));
			}
			break;
		case MSG_EVENT:
			assert(m->eventMail);
			if (!m->eventMail->from)
				break;
			if (!m->eventMail->read)
				Com_sprintf(tempBuf, sizeof(tempBuf), _("^B%s\t%s\n"),
					_(m->eventMail->subject), _(m->eventMail->date));
			else
				Com_sprintf(tempBuf, sizeof(tempBuf), _("%s\t%s\n"),
					_(m->eventMail->subject), _(m->eventMail->date));
			CHECK_MAIL_EOL
			Q_strcat(mailBuffer, tempBuf, sizeof(mailBuffer));
			break;
		default:
			break;
		}
		m = m->next;
	}
	MN_RegisterText(TEXT_UFOPEDIA_MAIL, mailBuffer);

	UP_SetMailButtons_f();
}

/**
 * @brief Marks all mails read in mailclient
 */
static void UP_SetAllMailsRead_f (void)
{
	const message_t *m = cp_messageStack;

	while (m) {
		switch (m->type) {
		case MSG_RESEARCH_PROPOSAL:
			assert(m->pedia);
			m->pedia->mail[TECHMAIL_PRE].read = qtrue;
			break;
		case MSG_RESEARCH_FINISHED:
			assert(m->pedia);
			m->pedia->mail[TECHMAIL_RESEARCHED].read = qtrue;
			break;
		case MSG_NEWS:
			assert(m->pedia);
			m->pedia->mail[TECHMAIL_PRE].read = qtrue;
			m->pedia->mail[TECHMAIL_RESEARCHED].read = qtrue;
			break;
		case MSG_EVENT:
			assert(m->eventMail);
			m->eventMail->read = qtrue;
			break;
		default:
			break;
		}
		m = m->next;
	}

	ccs.numUnreadMails = 0;
	Cvar_Set("mn_upunreadmail", va("%i", ccs.numUnreadMails));
	UP_OpenMail_f();
}

/**
 * @sa MN_InitStartup
 */
void UP_InitStartup (void)
{
	/* add commands and cvars */
	Cmd_AddCommand("mn_upcontent", UP_Content_f, "Shows the UFOpaedia chapters");
	Cmd_AddCommand("mn_upupdate", UP_Update_f, "Redraw the current UFOpaedia article");
	Cmd_AddCommand("ufopedia", UP_FindEntry_f, "Open the UFOpaedia with the given article");
	Cmd_AddCommand("ufopedia_click", UP_Click_f, NULL);
	Cmd_AddCommand("mailclient_click", UP_MailClientClick_f, NULL);
	Cmd_AddCommand("mn_mail_readall", UP_SetAllMailsRead_f, "Mark all mails read");
	Cmd_AddCommand("ufopedia_openmail", UP_OpenMail_f, "Start the mailclient");
	Cmd_AddCommand("ufopedia_scrollmail", UP_SetMailButtons_f, NULL);
	Cmd_AddCommand("techtree_click", UP_TechTreeClick_f, NULL);
	Cmd_AddCommand("mn_upgotoresearchedlink", UP_ResearchedLinkClick_f, NULL);

	mn_uppretext = Cvar_Get("mn_uppretext", "0", 0, "Show the pre-research text in the UFOpaedia");
	mn_uppreavailable = Cvar_Get("mn_uppreavailable", "0", 0, "True if there is a pre-research text available");
	Cvar_Set("mn_uprequirement", "");
	Cvar_Set("mn_upmetadata", "");
}

/**
 * @sa MN_InitStartup
 */
void UP_Shutdown (void)
{
	/* add commands and cvars */
	Cmd_RemoveCommand("mn_upcontent");
	Cmd_RemoveCommand("mn_upupdate");
	Cmd_RemoveCommand("ufopedia");
	Cmd_RemoveCommand("ufopedia_click");
	Cmd_RemoveCommand("mailclient_click");
	Cmd_RemoveCommand("mn_mail_readall");
	Cmd_RemoveCommand("ufopedia_openmail");
	Cmd_RemoveCommand("ufopedia_scrollmail");
	Cmd_RemoveCommand("techtree_click");
	Cmd_RemoveCommand("mn_upgotoresearchedlink");

	Cvar_Delete("mn_uppretext");
	Cvar_Delete("mn_uppreavailable");
	Cvar_Delete("mn_uprequirement");
	Cvar_Delete("mn_upmetadata");

	if (upChapters) {
		Mem_Free(upChapters);
		upChapters = NULL;
	}

}

/**
 * @brief Parse the UFOpaedia chapters from scripts
 * @param[in] name Chapter ID
 * @param[in] text Text for chapter ID
 * @sa CL_ParseFirstScript
 */
void UP_ParseChapters (const char *name, const char **text)
{
	const char *errhead = "UP_ParseChapters: unexpected end of file (names ";
	const char *token;

	/* get name list body body */
	token = Com_Parse(text);

	if (!*text || *token !='{') {
		Com_Printf("UP_ParseChapters: chapter def \"%s\" without body ignored\n", name);
		return;
	}

	do {
		/* get the id */
		token = Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* add chapter */
		if (ccs.numChapters >= MAX_PEDIACHAPTERS) {
			Com_Printf("UP_ParseChapters: too many chapter defs\n");
			return;
		}
		memset(&ccs.upChapters[ccs.numChapters], 0, sizeof(ccs.upChapters[ccs.numChapters]));
		ccs.upChapters[ccs.numChapters].id = Mem_PoolStrDup(token, cp_campaignPool, 0);
		ccs.upChapters[ccs.numChapters].idx = ccs.numChapters;	/* set self-link */

		/* get the name */
		token = Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		if (*token == '_')
			token++;
		if (!*token)
			continue;
		ccs.upChapters[ccs.numChapters].name = Mem_PoolStrDup(token, cp_campaignPool, 0);

		ccs.numChapters++;
	} while (*text);
}
