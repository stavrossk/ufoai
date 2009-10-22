/**
 * @file e_event_results.c
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

#include "../../../../client.h"
#include "../../../../cl_game.h"
#include "e_event_results.h"

/**
 * @brief Reads mission result data from server
 * @sa EV_RESULTS
 * @sa G_MatchSendResults
 * @sa GAME_CP_Results_f
 */
void CL_ParseResults (const eventRegister_t *self, struct dbuffer *msg)
{
	int winner;
	int i, j, num;
	int num_spawned[MAX_TEAMS];
	int num_alive[MAX_TEAMS];
	int num_kills[MAX_TEAMS][MAX_TEAMS];
	int num_stuns[MAX_TEAMS][MAX_TEAMS];

	memset(num_spawned, 0, sizeof(num_spawned));
	memset(num_alive, 0, sizeof(num_alive));
	memset(num_kills, 0, sizeof(num_kills));
	memset(num_stuns, 0, sizeof(num_stuns));

	/* get number of teams */
	num = NET_ReadByte(msg);
	if (num > MAX_TEAMS)
		Com_Error(ERR_DROP, "Too many teams in result message");

	Com_DPrintf(DEBUG_CLIENT, "Receiving results with %i teams.\n", num);

	/* get winning team */
	winner = NET_ReadByte(msg);

	if (cls.team > num)
		Com_Error(ERR_DROP, "Team number %d too high (only %d teams)", cls.team, num);

	/* get spawn and alive count */
	for (i = 0; i < num; i++) {
		num_spawned[i] = NET_ReadByte(msg);
		num_alive[i] = NET_ReadByte(msg);
	}

	/* get kills */
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++)
			num_kills[i][j] = NET_ReadByte(msg);

	/* get stuns */
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++)
			num_stuns[i][j] = NET_ReadByte(msg);

	GAME_HandleResults(msg, winner, num_spawned, num_alive, num_kills, num_stuns);
}
