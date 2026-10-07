/* Real slash commands and stock replacement; isolate placement, generation and screen I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

static bool active;
static int placement_result, spawned, displays[4], maintenance;
static player_type visitors[3];
static monster_type monsters[3];
static monster_race races[MAX_R_IDX];
static dungeon_info_type dungeons[MAX_D_IDX];
static struct town_type towns[2];
static store_type shops[2][MAX_ST_IDX];
static store_info_type templates[MAX_ST_IDX];
static store_action_type actions[2];
static object_type stock[8];

void test_dungeon_config(int type, int bottom);
void test_floor_item(int idx);
bool refresh_store(int Ind);
int __real_alloc_monster_specific(worldpos *wpos, int race, int distance, int asleep);
int __wrap_alloc_monster_specific(worldpos *wpos, int race, int distance, int asleep) {
	if (!active) return __real_alloc_monster_specific(wpos, race, distance, asleep);
	assert(!Players[1]->r_killed[race]);
	assert(summon_override_checks == (SO_BOSS_MONSTERS | SO_FORCE_DEPTH));
	assert(!asleep && distance == 20); spawned++;
	return placement_result;
}
/* These names replace the native definitions in the test translation unit only. */
void display_store(int Ind) { assert(active); displays[Ind]++; }
void store_maint(store_type *shop) {
	assert(active); maintenance++;
	if (shop->stock_num) assert(shop->stock[0].k_idx == 2); /* All old stacks were removed. */
	shop->stock_num = 1; shop->stock[0] = (object_type){.k_idx = 2, .number = 1};
}
static void command(cptr text) {
	char msg[MSG_LEN] = {0}, raw[MSG_LEN] = {0};
	strcpy(msg, text); strcpy(raw, text); do_slash_cmd(1, msg, raw);
}

void check_boss_store(void) {
	player_type **old_players = Players; int old_count = NumPlayers;
	player_type *players[4] = {NULL, &visitors[0], &visitors[1], &visitors[2]};
	Players = players; NumPlayers = 3; active = TRUE;
	for (int i = 1; i <= 3; i++) {
		players[i]->id = i; players[i]->au = 1234; players[i]->balance = 5678;
		players[i]->wpos = (worldpos){0, 0, -10}; players[i]->r_killed[1] = 1;
	}
	m_list = monsters; m_max = 3; r_info = races; d_info = dungeons;
	races[1].flags1 = RF1_UNIQUE; test_dungeon_config(1, 10);
	dungeons[1].final_guardian = 1; test_floor_item(1);
	summon_override_checks = SO_SURFACE;
	command("/resetboss");
	assert(spawned == 1 && !players[1]->r_killed[1] && players[2]->r_killed[1] == 1);
	assert(summon_override_checks == SO_SURFACE);
	players[1]->r_killed[1] = 1; placement_result = 47;
	command("/resetboss"); assert(spawned == 2 && players[1]->r_killed[1] == 1);
	monsters[1].r_idx = 1; monsters[1].wpos = players[1]->wpos;
	monsters[1].hp = 7; command("/resetboss");
	assert(spawned == 2 && !players[1]->r_killed[1] && monsters[1].hp == 7);
	players[1]->r_killed[1] = 1; monsters[1].wpos.wz = -11;
	command("/resetboss"); assert(spawned == 2 && players[1]->r_killed[1] == 1);
	monsters[1].r_idx = 0; players[1]->wpos.wz = -9;
	command("/resetboss"); assert(spawned == 2 && players[1]->r_killed[1] == 1);
	players[1]->wpos.wz = 0; command("/resetboss"); assert(spawned == 2);
	/* Morgoth rematches preserve winner state; Sauron's prerequisite remains. */
	races[RI_MORGOTH].level = 100; test_dungeon_config(DUNGEON_ANGBAND, 127);
	players[1]->wpos.wz = -100; players[1]->total_winner = TRUE;
	players[1]->r_killed[RI_MORGOTH] = 1;
	command("/resetboss"); assert(spawned == 2 && players[1]->r_killed[RI_MORGOTH] == 1);
	players[1]->r_killed[RI_SAURON] = 1; placement_result = 0;
	command("/resetboss"); assert(spawned == 3 && !players[1]->r_killed[RI_MORGOTH]);
	assert(players[1]->total_winner && players[1]->r_killed[RI_SAURON] == 1);
	/* Town shop refresh: free, shared by current visitors, and isolated from other towns. */
	town = towns; numtowns = 2; max_st_idx = MAX_ST_IDX; st_info = templates; ba_info = actions;
	for (int i = 0; i < 2; i++) { towns[i].townstore = shops[i]; towns[i].x = i; }
	actions[1].action = BACT_BUY; templates[1].actions[0] = 1;
	shops[0][1].st_idx = 1; shops[0][1].stock = stock; shops[0][1].stock_size = 8;
	shops[0][1].stock_num = 3;
	for (int i = 0; i < 3; i++) stock[i] = (object_type){.k_idx = 1, .number = 5};
	for (int i = 1; i <= 3; i++) { players[i]->store_num = 1; players[i]->wpos.wz = 0; }
	players[3]->wpos.wx = 1;
	command("/refreshstore");
	assert(maintenance == 10 && shops[0][1].stock_num == 1 && stock[0].k_idx == 2);
	assert(displays[1] == 1 && displays[2] == 1 && !displays[3]);
	assert(players[1]->au == 1234 && players[1]->balance == 5678 && !shops[0][1].owner);
	int denied[] = {-1, -2, STORE_HOME, STORE_HOME_DUN, MAX_ST_IDX};
	for (int i = 0; i < sizeof(denied) / sizeof(*denied); i++) {
		players[1]->store_num = denied[i]; assert(!refresh_store(1));
	}
	players[1]->store_num = 1;
	templates[1].flags2 = SF2_MUSEUM; assert(!refresh_store(1)); templates[1].flags2 = 0;
	templates[1].flags1 = SF1_SPECIAL; assert(!refresh_store(1)); templates[1].flags1 = 0;
	templates[1].actions[0] = 0; assert(!refresh_store(1)); templates[1].actions[0] = 1;
	assert(maintenance == 10 && stock[0].k_idx == 2);
	players[1]->wpos.wz = players[2]->wpos.wz = -1;
	assert(refresh_store(1) && maintenance == 2 * 10);
	assert(displays[1] == 2 && displays[2] == 2 && !displays[3]);
	active = FALSE; test_floor_item(0); summon_override_checks = SO_NONE;
	Players = old_players; NumPlayers = old_count;
	puts("Boss rematch rollback/uniqueness and free NPC restock isolation checks passed.");
}
