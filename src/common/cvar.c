/**
 * @file cvar.c
 * @brief Manage cvars
 *
 * cvar_t variables are used to hold scalar or string variables that can be changed or displayed at the console or prog code as well as accessed directly
 * in C code.
 * Cvars are restricted from having the same names as commands to keep this
 * interface from being ambiguous.
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "common.h"
#include "../shared/infostring.h"

#define CVAR_HASH_SIZE          64

static cvar_t *cvarVarsHash[CVAR_HASH_SIZE];

/**
 * @brief This is set each time a CVAR_USERINFO variable is changed
 * so that the client knows to send it to the server
 */
qboolean userinfoModified;

/**
 * @brief Cvar list
 */
static cvar_t *cvarVars;

static qboolean Cvar_InfoValidate (const char *s)
{
	if (strstr(s, "\\"))
		return qfalse;
	if (strstr(s, "\""))
		return qfalse;
	if (strstr(s, ";"))
		return qfalse;
	return qtrue;
}

/**
 * @brief Searches for a cvar given by parameter
 * @param varName The cvar name as string
 * @return Pointer to cvar_t struct
 * @sa Cvar_GetString
 * @sa Cvar_SetValue
 */
cvar_t *Cvar_FindVar (const char *varName)
{
	cvar_t *var;
	const unsigned hash = Com_HashKey(varName, CVAR_HASH_SIZE);

	for (var = cvarVarsHash[hash]; var; var = var->hash_next)
		if (!strcmp(varName, var->name))
			return var;

	return NULL;
}

/**
 * @brief Returns the float value of a cvar
 * @sa Cvar_GetString
 * @sa Cvar_FindVar
 * @sa Cvar_GetInteger
 * @return 0 if not defined
 */
float Cvar_GetValue (const char *varName)
{
	cvar_t *var;

	var = Cvar_FindVar(varName);
	if (!var)
		return 0.0;
	return atof(var->string);
}


/**
 * @brief Set a checker function for cvar values
 * @sa Cvar_FindVar
 * @return true if set
 */
qboolean Cvar_SetCheckFunction (const char *varName, qboolean (*check) (cvar_t* cvar))
{
	cvar_t *var;

	var = Cvar_FindVar(varName);
	if (!var) {
		Com_Printf("Could not set check function for cvar '%s'\n", varName);
		return qfalse;
	}
	var->check = check;
	/* execute the check */
	var->check(var);
	return qtrue;
}

/**
 * @brief Checks cvar values
 * @return true if assert
 * @param[in] cvar Cvar to check
 * @param[in] minVal The minimal value the cvar should have
 * @param[in] maxVal The maximal value the cvar should have
 * @param[in] shouldBeIntegral No floats for this cvar please
 * @sa Cvar_AssertString
 */
qboolean Cvar_AssertValue (cvar_t * cvar, float minVal, float maxVal, qboolean shouldBeIntegral)
{
	assert(cvar);

	if (shouldBeIntegral) {
		if ((int)cvar->value != cvar->integer) {
			Com_Printf("WARNING: cvar '%s' must be integral (%f)\n", cvar->name, cvar->value);
			Cvar_Set(cvar->name, va("%d", cvar->integer));
			return qtrue;
		}
	}

	if (cvar->value < minVal) {
		Com_Printf("WARNING: cvar '%s' out of range (%f < %f)\n", cvar->name, cvar->value, minVal);
		Cvar_SetValue(cvar->name, minVal);
		return qtrue;
	} else if (cvar->value > maxVal) {
		Com_Printf("WARNING: cvar '%s' out of range (%f > %f)\n", cvar->name, cvar->value, maxVal);
		Cvar_SetValue(cvar->name, maxVal);
		return qtrue;
	}

	/* no changes */
	return qfalse;
}

/**
 * @brief Checks cvar values
 * @return true if assert
 * @sa Cvar_AssertValue
 * @param[in] cvar Cvar to check
 * @param[in] array Array of valid cvar value strings
 * @param[in] arraySize Number of entries in the string array
 */
qboolean Cvar_AssertString (cvar_t * cvar, char **array, int arraySize)
{
	int i;
	char *string;

	assert(cvar);

	for (i = 0; i < arraySize; i++) {
		string = array[i];
		if (strncmp(cvar->string, string, sizeof(cvar->string))) {
			/* valid value */
			return qfalse;
		}
	}

	Com_Printf("Cvar '%s' has not a valid value\n", cvar->name);

	if (cvar->oldString)
		Cvar_Set(cvar->name, cvar->oldString);

	/* not a valid value */
	return qtrue;
}

/**
 * @brief Returns the int value of a cvar
 * @sa Cvar_GetValue
 * @sa Cvar_GetString
 * @sa Cvar_FindVar
 * @return 0 if not defined
 */
int Cvar_GetInteger (const char *varName)
{
	const cvar_t *var;

	var = Cvar_FindVar(varName);
	if (!var)
		return 0;
	return var->integer;
}

/**
 * @brief Returns the value of cvar as string
 * @sa Cvar_GetValue
 * @sa Cvar_FindVar
 *
 * Even if the cvar does not exist this function will not return a null pointer
 * but an empty string
 */
const char *Cvar_GetString (const char *varName)
{
	const cvar_t *var;

	var = Cvar_FindVar(varName);
	if (!var)
		return "";
	return var->string;
}

/**
 * @brief Returns the old value of cvar as string before we changed it
 * @sa Cvar_GetValue
 * @sa Cvar_FindVar
 *
 * Even if the cvar does not exist this function will not return a null pointer
 * but an empty string
 */
const char *Cvar_VariableStringOld (const char *varName)
{
	cvar_t *var;

	var = Cvar_FindVar(varName);
	if (!var)
		return "";
	if (var->oldString)
		return var->oldString;
	else
		return "";
}

/**
 * @brief Unix like tab completion for console variables
 * @param partial The beginning of the variable we try to complete
 * @sa Cmd_CompleteCommand
 * @sa Key_CompleteCommand
 */
int Cvar_CompleteVariable (const char *partial, const char **match)
{
	int matches = 0;
	const char *localMatch[MAX_COMPLETE];
	cvar_t* cvar;
	size_t len;

	len = strlen(partial);
	if (!len)
		return 0;

	localMatch[matches] = NULL;

	/* check for partial matches */
	for (cvar = cvarVars; cvar; cvar = cvar->next)
		if (!strncmp(partial, cvar->name, len)) {
#ifndef DEBUG
			if (cvar->flags & CVAR_DEVELOPER)
				continue;
#endif
			Com_Printf("[var] %-20s = \"%s\"\n", cvar->name, cvar->string);
			if (cvar->description)
				Com_Printf("%c      %s\n", COLORED_GREEN, cvar->description);
			localMatch[matches++] = cvar->name;
			if (matches >= MAX_COMPLETE)
				break;
		}

	return Cmd_GenericCompleteFunction(len, match, matches, localMatch);
}

/**
 * @brief Function to remove the cvar and free the space
 */
qboolean Cvar_Delete (const char *var_name)
{
	unsigned hash;
	cvar_t *var, *previousVar = NULL;

	hash = Com_HashKey(var_name, CVAR_HASH_SIZE);
	for (var = cvarVarsHash[hash]; var; var = var->hash_next) {
		if (!Q_strcasecmp(var_name, var->name)) {
			if (var->flags & (CVAR_USERINFO | CVAR_SERVERINFO | CVAR_NOSET | CVAR_LATCH)) {
				Com_Printf("Can't delete the cvar '%s' - it's a special cvar\n", var_name);
				return qfalse;
			}
			if (var->prev) {
				assert(var->prev->next == var);
				var->prev->next = var->next;
			} else
				cvarVars = var->next;
			if (var->next) {
				assert(var->next->prev == var);
				var->next->prev = var->prev;
			}
			if (previousVar) {
				assert(previousVar->hash_next == var);
				previousVar->hash_next = var->hash_next;
			} else {
				cvarVarsHash[hash] = var->hash_next;
			}
			Mem_Free(var->name);
			Mem_Free(var->string);
			if (var->oldString)
				Mem_Free(var->oldString);
			if (var->default_string)
				Mem_Free(var->default_string);
			/* latched cvars should not be removable */
			assert(var->latchedString == NULL);
			Mem_Free(var);
			return qtrue;
		}
		previousVar = var;
	}
	Com_Printf("Cvar '%s' wasn't found\n", var_name);
	return qfalse;
}

/**
 * @brief Init or return a cvar
 * @param[in] var_name The cvar name
 * @param[in] var_value The standard cvar value (will be set if the cvar doesn't exist)
 * @param[in] flags CVAR_USERINFO, CVAR_LATCH, CVAR_SERVERINFO, CVAR_ARCHIVE and so on
 * @param[in] desc This is a short description of the cvar (see console command cvarlist)
 * @note CVAR_ARCHIVE: Cvar will be saved to config.cfg when game shuts down - and
 * will be reloaded when game starts up the next time
 * @note CVAR_LATCH: Latched cvars will be updated at the next map load
 * @note CVAR_SERVERINFO: This cvar will be send in the server info response strings (server browser)
 * @note CVAR_NOSET: This cvar can not be set from the commandline
 * @note CVAR_USERINFO: This cvar will be added to the userinfo string when changed (network synced)
 * @note CVAR_DEVELOPER: Only changeable if we are in development mode
 * If the variable already exists, the value will not be set
 * The flags will be or'ed in if the variable exists.
 */
cvar_t *Cvar_Get (const char *var_name, const char *var_value, int flags, const char* desc)
{
	cvar_t *var;
	const unsigned hash = Com_HashKey(var_name, CVAR_HASH_SIZE);

	if (flags & (CVAR_USERINFO | CVAR_SERVERINFO))
		if (!Cvar_InfoValidate(var_name)) {
			Com_Printf("invalid info cvar name\n");
			return NULL;
		}

	var = Cvar_FindVar(var_name);
	if (var) {
		if (!var->default_string && flags & CVAR_CHEAT)
			var->default_string = Mem_PoolStrDup(var_value, com_cvarSysPool, 0);
		var->flags |= flags;
		if (desc)
			var->description = desc;
		return var;
	}

	if (!var_value)
		return NULL;

	if (flags & (CVAR_USERINFO | CVAR_SERVERINFO))
		if (!Cvar_InfoValidate(var_value)) {
			Com_Printf("invalid info cvar value '%s' of cvar '%s'\n", var_value, var_name);
			return NULL;
		}

	var = Mem_PoolAlloc(sizeof(*var), com_cvarSysPool, 0);
	var->name = Mem_PoolStrDup(var_name, com_cvarSysPool, 0);
	var->string = Mem_PoolStrDup(var_value, com_cvarSysPool, 0);
	var->oldString = NULL;
	var->modified = qtrue;
	var->value = atof(var->string);
	var->integer = atoi(var->string);
	var->description = desc;

	/* link the variable in */
	/* cvar_vars_hash should be null on the first run */
	var->hash_next = cvarVarsHash[hash];
	/* set the cvar_vars_hash pointer to the current cvar */
	/* if there were already others in cvar_vars_hash at position hash, they are
	 * now accessible via var->hash_next - loop until var->hash_next is null (the first
	 * cvar at that position)
	 */
	cvarVarsHash[hash] = var;
	var->next = cvarVars;
	cvarVars = var;
	if (var->next)
		var->next->prev = var;

	var->flags = flags;
	if (var->flags & CVAR_CHEAT)
		var->default_string = Mem_PoolStrDup(var_value, com_cvarSysPool, 0);

	return var;
}

/**
 * @brief Sets a cvar values
 * Handles write protection and latched cvars as expected
 * @param[in] varName Which cvar
 * @param[in] value Set the cvar to the value specified by 'value'
 * @param[in] force Force the update of the cvar
 */
static cvar_t *Cvar_Set2 (const char *varName, const char *value, qboolean force)
{
	cvar_t *var;

	if (!value)
		return NULL;

	var = Cvar_FindVar(varName);
	/* create it */
	if (!var)
		return Cvar_Get(varName, value, 0, NULL);

	if (var->flags & (CVAR_USERINFO | CVAR_SERVERINFO))
		if (!Cvar_InfoValidate(value)) {
			Com_Printf("invalid info cvar value '%s' of cvar '%s'\n", value, varName);
			return var;
		}

	if (!force) {
		if (var->flags & CVAR_NOSET) {
			Com_Printf("%s is write protected.\n", varName);
			return var;
		}
#ifndef DEBUG
		if (var->flags & CVAR_DEVELOPER) {
			Com_Printf("%s is a developer cvar.\n", varName);
			return var;
		}
#endif

		if (var->flags & CVAR_LATCH) {
			if (var->latchedString) {
				if (!strcmp(value, var->latchedString))
					return var;
				Mem_Free(var->latchedString);
				var->latchedString = NULL;
			} else {
				if (!strcmp(value, var->string))
					return var;
			}

			/* if we are running a server */
			if (Com_ServerState()) {
				Com_Printf("%s will be changed for next game.\n", varName);
				var->latchedString = Mem_PoolStrDup(value, com_cvarSysPool, 0);
			} else {
				if (var->oldString)
					Mem_Free(var->oldString);
				var->oldString = var->string;
				var->string = Mem_PoolStrDup(value, com_cvarSysPool, 0);
				var->value = atof(var->string);
				var->integer = atoi(var->string);
			}

			if (var->check)
				if (var->check(var))
					Com_Printf("Invalid value for cvar %s\n", varName);

			return var;
		}
	} else {
		if (var->latchedString) {
			Mem_Free(var->latchedString);
			var->latchedString = NULL;
		}
	}

	if (!strcmp(value, var->string))
		return var;				/* not changed */

	if (var->oldString)
		Mem_Free(var->oldString);		/* free the old value string */
	var->oldString = var->string;
	var->modified = qtrue;

	if (var->flags & CVAR_USERINFO)
		userinfoModified = qtrue;	/* transmit at next opportunity */

	var->string = Mem_PoolStrDup(value, com_cvarSysPool, 0);
	var->value = atof(var->string);
	var->integer = atoi(var->string);

	if (var->check)
		if (var->check(var)) {
			Com_Printf("Invalid value for cvar %s\n", varName);
			return var;
		}

	return var;
}

/**
 * @brief Will set the variable even if NOSET or LATCH
 */
cvar_t *Cvar_ForceSet (const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, qtrue);
}

/**
 * @brief Sets a cvar value
 * @param varName Which cvar should be set
 * @param value Which value should the cvar get
 * @note Look after the CVAR_LATCH stuff and check for write protected cvars
 */
cvar_t *Cvar_Set (const char *varName, const char *value)
{
	return Cvar_Set2(varName, value, qfalse);
}

/**
 * @brief Sets a cvar from console with the given flags
 * @note flags are:
 * CVAR_ARCHIVE These cvars will be saved.
 * CVAR_USERINFO Added to userinfo  when changed.
 * CVAR_SERVERINFO Added to serverinfo when changed.
 * CVAR_NOSET Don't allow change from console at all but can be set from the command line.
 * CVAR_LATCH Save changes until server restart.
 *
 * @param varName Which cvar
 * @param value Which value for the cvar
 * @param flags which flags
 * @sa Cvar_Set_f
 */
cvar_t *Cvar_FullSet (const char *varName, const char *value, int flags)
{
	cvar_t *var;

	if (!value)
		return NULL;

	var = Cvar_FindVar(varName);
	/* create it */
	if (!var)
		return Cvar_Get(varName, value, flags, NULL);

	var->modified = qtrue;

	/* transmit at next opportunity */
	if (var->flags & CVAR_USERINFO)
		userinfoModified = qtrue;

	if (var->oldString)
		Mem_Free(var->oldString);		/* free the old value string */
	var->oldString = var->string;

	var->string = Mem_PoolStrDup(value, com_cvarSysPool, 0);
	var->value = atof(var->string);
	var->integer = atoi(var->string);
	var->flags = flags;

	return var;
}

/**
 * @brief Expands value to a string and calls Cvar_Set
 * @note Float values are in the format #.##
 */
void Cvar_SetValue (const char *varName, float value)
{
	char val[32];

	if (value == (int) value)
		Com_sprintf(val, sizeof(val), "%i", (int) value);
	else
		Com_sprintf(val, sizeof(val), "%1.2f", value);
	Cvar_Set(varName, val);
}


/**
 * @brief Any variables with latched values will now be updated
 * @note CVAR_LATCH cvars are not updated during a game (tactical mission)
 */
void Cvar_UpdateLatchedVars (void)
{
	cvar_t *var;

	for (var = cvarVars; var; var = var->next) {
		if (!var->latchedString)
			continue;
		var->oldString = var->string;
		var->string = var->latchedString;
		var->latchedString = NULL;
		var->value = atof(var->string);
		var->integer = atoi(var->string);
	}
}

/**
 * @brief Handles variable inspection and changing from the console
 * @return True if cvar exists - false otherwise
 *
 * You can print the current value or set a new value with this function
 * To set a new value for a cvar from within the console just type the cvar name
 * followed by the value. To print the current cvar's value just type the cvar name
 * and hit enter
 * @sa Cvar_Set_f
 * @sa Cvar_SetValue
 * @sa Cvar_Set
 */
qboolean Cvar_Command (void)
{
	cvar_t *v;

	/* check variables */
	v = Cvar_FindVar(Cmd_Argv(0));
	if (!v)
		return qfalse;

	/* perform a variable print or set */
	if (Cmd_Argc() == 1) {
		Com_Printf("\"%s\" is \"%s\"\n", v->name, v->string);
		return qtrue;
	}

	Cvar_Set(v->name, Cmd_Argv(1));
	return qtrue;
}

/**
 * @brief Allows resetting cvars to old value from console
 */
static void Cvar_SetOld_f (void)
{
	cvar_t *v;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <variable>\n", Cmd_Argv(0));
		return;
	}

	/* check variables */
	v = Cvar_FindVar(Cmd_Argv(1));
	if (!v) {
		Com_Printf("cvar '%s' not found\n", Cmd_Argv(1));
		return;
	}
	if (v->oldString)
		Cvar_Set(Cmd_Argv(1), v->oldString);
}

/**
 * @brief Allows setting and defining of arbitrary cvars from console
 */
static void Cvar_Set_f (void)
{
	const int c = Cmd_Argc();
	if (c != 3 && c != 4) {
		Com_Printf("Usage: %s <variable> <value> [u / s]\n", Cmd_Argv(0));
		return;
	}

	if (c == 4) {
		const char *arg = Cmd_Argv(3);
		int flags = 0;

		while (arg[0] != '\0') {
			switch (arg[0]) {
			case 'u':
				flags |= CVAR_USERINFO;
				break;
			case 's':
				flags |= CVAR_SERVERINFO;
				break;
			case 'a':
				flags |= CVAR_ARCHIVE;
				break;
			default:
				Com_Printf("invalid flags %c given\n", arg[0]);
			}
			arg++;
		}
		Cvar_FullSet(Cmd_Argv(1), Cmd_Argv(2), flags);
	} else
		Cvar_Set(Cmd_Argv(1), Cmd_Argv(2));
}


/**
 * @brief Allows copying variables
 * Available via console command copy
 */
static void Cvar_Copy_f (void)
{
	int c;

	c = Cmd_Argc();
	if (c < 3) {
		Com_Printf("Usage: %s <target> <source>\n", Cmd_Argv(0));
		return;
	}

	Cvar_Set(Cmd_Argv(1), Cvar_GetString(Cmd_Argv(2)));
}


/**
 * @brief appends lines containing "set variable value" for all variables
 * with the archive flag set to true.
 * @note Stores the archive cvars
 */
void Cvar_WriteVariables (qFILE *f)
{
	const cvar_t *var;

	for (var = cvarVars; var; var = var->next)
		if (var->flags & CVAR_ARCHIVE)
			FS_Printf(f, "set %s \"%s\" a\n", var->name, var->string);
}

/**
 * @brief Checks whether there are pending cvars for the given flags
 * @param flags The CVAR_* flags
 * @return true if there are pending cvars, false otherwise
 */
qboolean Cvar_PendingCvars (int flags)
{
	const cvar_t *var;

	for (var = cvarVars; var; var = var->next)
		if ((var->flags & flags) && var->modified)
			return qtrue;

	return qfalse;
}

void Cvar_ClearVars (int flags)
{
	cvar_t *var;

	for (var = cvarVars; var; var = var->next)
		if ((var->flags & flags) && var->modified)
			var->modified = qfalse;
}

/**
 * @brief List all cvars via console command 'cvarlist'
 */
static void Cvar_List_f (void)
{
	cvar_t *var;
	int i, c, l = 0;
	const char *token = NULL;

	c = Cmd_Argc();

	if (c == 2) {
		token = Cmd_Argv(1);
		l = strlen(token);
	}

	i = 0;
	for (var = cvarVars; var; var = var->next, i++) {
		if (c == 2 && strncmp(var->name, token, l)) {
			i--;
			continue;
		}
#ifndef DEBUG
		/* don't show developer cvars in release mode */
		if (var->flags & CVAR_DEVELOPER)
			continue;
#endif

		if (var->flags & CVAR_ARCHIVE)
			Com_Printf("A");
		else
			Com_Printf(" ");
		if (var->flags & CVAR_USERINFO)
			Com_Printf("U");
		else
			Com_Printf(" ");
		if (var->flags & CVAR_SERVERINFO)
			Com_Printf("S");
		else
			Com_Printf(" ");
		if (var->modified)
			Com_Printf("M");
		else
			Com_Printf(" ");
		if (var->flags & CVAR_DEVELOPER)
			Com_Printf("D");
		else
			Com_Printf(" ");
		if (var->flags & CVAR_R_IMAGES)
			Com_Printf("I");
		else
			Com_Printf(" ");
		if (var->flags & CVAR_NOSET)
			Com_Printf("-");
		else if (var->flags & CVAR_LATCH)
			Com_Printf("L");
		else
			Com_Printf(" ");
		Com_Printf(" %-20s \"%s\"\n", var->name, var->string);
		if (var->description)
			Com_Printf("%c        %s\n", COLORED_GREEN, var->description);
	}
	Com_Printf("%i cvars\n", i);
	Com_Printf("legend:\n"
		"S: Serverinfo\n"
		"L: Latched\n"
		"D: Developer\n"
		"U: Userinfo\n"
		"I: Image\n"
		"*: Archive\n"
		"-: Not changeable\n"
	);
}

/**
 * @brief Return a string with all cvars with bitflag given by parameter set
 * @param bit The bitflag we search the global cvar array for
 */
static char *Cvar_BitInfo (int bit)
{
	static char info[MAX_INFO_STRING];
	cvar_t *var;

	info[0] = 0;

	for (var = cvarVars; var; var = var->next)
		if (var->flags & bit)
			Info_SetValueForKey(info, var->name, var->string);
	return info;
}

/**
 * @brief Returns an info string containing all the CVAR_USERINFO cvars
 */
const char *Cvar_Userinfo (void)
{
	return Cvar_BitInfo(CVAR_USERINFO);
}

/**
 * @brief Returns an info string containing all the CVAR_SERVERINFO cvars
 * @sa SV_StatusString
 */
const char *Cvar_Serverinfo (void)
{
	return Cvar_BitInfo(CVAR_SERVERINFO);
}

/**
 * @brief Delete a cvar - set [cvar] "" isn't working from within the scripts
 * @sa Cvar_Set_f
 */
static void Cvar_Del_f (void)
{
	int c;

	c = Cmd_Argc();
	if (c != 2) {
		Com_Printf("Usage: %s <variable>\n", Cmd_Argv(0));
		return;
	}

	Cvar_Delete(Cmd_Argv(1));
}

/**
 * @brief Add a value to a cvar
 */
static void Cvar_Add_f (void)
{
	cvar_t *cvar;
	float value;
	if (Cmd_Argc() != 3) {
		Com_Printf("Usage: %s <variable> <value>\n", Cmd_Argv(0));
		return;
	}

	cvar = Cvar_FindVar(Cmd_Argv(1));
	if (!cvar) {
		Com_Printf("Cvar_Add_f: %s not exists\n", Cmd_Argv(1));
		return;
	}

	value = cvar->value + atof(Cmd_Argv(2));
	Cvar_SetValue(Cmd_Argv(1), value);
}

/**
 * @brief Apply a modulo to a cvar
 */
static void Cvar_Mod_f (void)
{
	cvar_t *cvar;
	int value;
	if (Cmd_Argc() != 3) {
		Com_Printf("Usage: %s <variable> <value>\n", Cmd_Argv(0));
		return;
	}

	cvar = Cvar_FindVar(Cmd_Argv(1));
	if (!cvar) {
		Com_Printf("Cvar_Mod_f: %s not exists\n", Cmd_Argv(1));
		return;
	}

	value = cvar->integer % atoi(Cmd_Argv(2));
	Cvar_SetValue(Cmd_Argv(1), value);
}

/**
 * @brief Reset cheat cvar values to default
 * @sa CL_SendCommand
 */
void Cvar_FixCheatVars (void)
{
	cvar_t *var;

	if (!(Com_ServerState() && !Cvar_GetInteger("sv_cheats")))
		return;

	for (var = cvarVars; var; var = var->next) {
		if (!(var->flags & CVAR_CHEAT))
			continue;

		if (!var->default_string) {
			Com_Printf("Cheat cvars: Cvar %s has no default value\n", var->name);
			continue;
		}
		/* also remove the oldString value here */
		if (var->oldString) {
			Mem_Free(var->oldString);
			var->oldString = NULL;
		}
		Mem_Free(var->string);
		var->string = Mem_PoolStrDup(var->default_string, com_cvarSysPool, 0);
		var->value = atof(var->string);
		var->integer = atoi(var->string);
	}
}

/**
 * @brief Reads in all archived cvars
 * @sa Qcommon_Init
 */
void Cvar_Init (void)
{
	Cmd_AddCommand("setold", Cvar_SetOld_f, "Restore the cvar old value");
	Cmd_AddCommand("del", Cvar_Del_f, "Delete a cvar");
	Cmd_AddCommand("set", Cvar_Set_f, "Set a cvar value");
	Cmd_AddCommand("add", Cvar_Add_f, "Add a value to a cvar");
	Cmd_AddCommand("mod", Cvar_Mod_f, "Apply a modulo on a cvar");
	Cmd_AddCommand("copy", Cvar_Copy_f, "Copy cvar target to source");
	Cmd_AddCommand("cvarlist", Cvar_List_f, "Show all cvars");
}

void Cvar_Shutdown (void)
{
	cvarVars = NULL;
	memset(cvarVarsHash, 0, sizeof(cvarVarsHash));
}
