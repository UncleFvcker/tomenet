/* Exercise the real death/ghost-revival code with map and network I/O isolated. */
#define SERVER
#include "angband.h"
#include <assert.h>
#include <setjmp.h>
#include <sys/time.h>

static player_type player;
static object_type inventory[INVEN_TOTAL], saved_inventory[INVEN_TOTAL];
static dungeon_type test_dungeon;
static dun_level floor_info;
static int recalls, legacy_path;
static bool real_recall;
static bool disconnect_recall;
static cave_type cave_cell, *cave_rows[] = {&cave_cell};
static jmp_buf legacy_jump;

dungeon_type *__wrap_getdungeon(worldpos *wpos) { return &test_dungeon; }
dun_level *__wrap_getfloor(worldpos *wpos) { return &floor_info; }
cave_type **__wrap_getcave(worldpos *wpos) { return real_recall ? cave_rows : NULL; }
int __wrap_getlevel(worldpos *wpos) { return ABS(wpos->wz); }
cptr __wrap_get_ptitle(player_type *p, bool short_form) { return "Tester"; }
int __wrap_get_esp_link(int Ind, u32b flags, player_type **p) { return 0; }
s16b __real_get_skill(player_type *p, int skill);
s16b __wrap_get_skill(player_type *p, int skill) { return __real_get_skill(p, skill); }
int __wrap_s_printf(const char *fmt, ...) {
	if (legacy_path && !strncmp(fmt, "CHARACTER_TERMINATION:", 22)) longjmp(legacy_jump, 1);
	return 0;
}
int __wrap_l_printf(char *fmt, ...) { return 0; }
void __wrap_plog(cptr msg) {}
void __wrap_msg_print(int Ind, cptr msg) {}
void __wrap_msg_broadcast(int Ind, cptr msg) {}
void __wrap_msg_broadcast_format(int Ind, cptr fmt, ...) {}
void __wrap_msg_format_near(int Ind, cptr fmt, ...) {}
void __wrap_rd_print(int Ind, char *date, char *msg, int format) {}
void __wrap_Handle_clear_buffer(int Ind) {}
void __wrap_break_cloaking(int Ind, int discovered) {}
void __wrap_break_shadow_running(int Ind) {}
void __wrap_stop_precision(int Ind) {}
void __wrap_stop_shooting_till_kill(int Ind) {}
void __wrap_disturb(int Ind, int stop_search, int keep_resting) {}
void __wrap_update_stuff(int Ind) {}
void __wrap_handle_stuff(int Ind) {}
void __wrap_handle_music(int Ind) {}
void __wrap_everyone_lite_spot(worldpos *wpos, int y, int x) {}
void __wrap_forget_lite(int Ind) {}
void __wrap_forget_view(int Ind) {}
void __wrap_new_players_on_depth(worldpos *wpos, int value, bool inc) {}
bool __wrap_set_invuln_short(int Ind, int value) { return TRUE; }
int __wrap_Send_playerlist(int Ind, int i, int mode) { return 0; }
int __wrap_Send_chardump(int Ind, cptr tag) { return 0; }
#ifdef USE_SOUND_2010
void __wrap_sound(int Ind, cptr name, cptr alternative, int type, bool nearby) {}
#else
void __wrap_sound(int Ind, int num) {}
#endif
void __real_recall_player(int Ind, char *message);
void __wrap_recall_player(int Ind, char *message) {
	if (disconnect_recall) {
		recalls++;
		return;
	}
	assert(player.new_level_method == LEVEL_TO_TEMPLE);
	assert(player.recall_pos.wx == player.town_x && player.recall_pos.wy == player.town_y);
	assert(player.recall_pos.wz == 0);
	if (real_recall) __real_recall_player(Ind, message);
	else player.wpos = player.recall_pos;
	recalls++;
}

static void setup(void) {
	memset(&player, 0, sizeof(player));
	memset(inventory, 0, sizeof(inventory));
	memset(&test_dungeon, 0, sizeof(test_dungeon));
	memset(&floor_info, 0, sizeof(floor_info));
	gettimeofday(&Conn[0]->last_keepalive_recv, NULL);
	player.inventory = inventory;
	player.rp_ptr = &race_info[0];
	player.cp_ptr = &class_info[0];
	player.mode = MODE_EVERLASTING;
	player.lev = player.max_lev = player.max_plv = 50;
	for (int i = 0; i <= PY_MAX_LEVEL; i++) player_exp[i] = i < 49 ? 1 : 1000000;
	player.expfact = 100;
	player.exp = 123456;
	player.max_exp = 234567;
	player.au = 98765;
	player.balance = 76543;
	player.total_winner = TRUE;
	player.death = TRUE;
	player.chp = player.csane = -1;
	player.mhp = 100;
	player.msane = 80;
	player.town_x = 2;
	player.town_y = 3;
	player.wpos.wx = 10;
	player.wpos.wy = 10;
	player.wpos.wz = -100;
	player.store_num = -1;
	player.food = PY_FOOD_FULL - 1;
	player.lives = 5;
	player.inven_cnt = INVEN_PACK;
	player.equip_cnt = INVEN_EQ;
	player.skill_points = 7;
	strcpy(player.name, "Tester");
	strcpy(player.died_from, "Morgoth, Lord of Darkness");
	strcpy(player.really_died_from, player.died_from);
	/* Full inventory, including winner artifacts, a rescue amulet and a bag. */
	for (int i = 0; i < INVEN_TOTAL; i++) {
		inventory[i].k_idx = 1;
		inventory[i].number = 2;
	}
	inventory[INVEN_HEAD].name1 = ART_MORGOTH;
	inventory[INVEN_WIELD].name1 = ART_GROND;
	inventory[INVEN_NECK].sval = SV_AMULET_LIFE_SAVING;
	inventory[0].tval = TV_JUNK;
	inventory[0].sval = SV_GLASS_SHARD;
#ifdef ENABLE_SUBINVEN
	inventory[1].tval = TV_SUBINVEN;
	player.subinventory[1][0] = inventory[INVEN_WIELD];
#endif
	memcpy(saved_inventory, inventory, sizeof(inventory));
	recalls = legacy_path = 0;
	ge_special_sector = FALSE;
	sector000separation = FALSE;
	NumPlayers = 1;
	m_top = 0;
}

static void check_death(void) {
	s32b gold = player.au, bank = player.balance;
	player_death(1);
	assert(recalls == 1 && player.wpos.wz == 0);
	assert(!player.death && !player.ghost);
	assert(player.chp == player.mhp && player.csane == player.msane);
	assert(player.exp == 123456 && player.max_exp == 234567 && player.lev == 50);
	assert(player.au == gold && player.balance == bank && player.total_winner);
	assert(player.lives == 5 && player.deaths + player.soft_deaths == 1);
	assert(player.inven_cnt == INVEN_PACK && player.equip_cnt == INVEN_EQ && player.skill_points == 7);
	assert(!memcmp(saved_inventory, inventory, sizeof(inventory)));
#ifdef ENABLE_SUBINVEN
	assert(!memcmp(&player.subinventory[1][0], &inventory[INVEN_WIELD], sizeof(object_type)));
#endif
}

void check_skill_potion(void);
void check_skill_respec(void);

int main(void) {
	player_type *players[2] = {NULL, &player};
	connection_t connection = {0}, *connections[1] = {&connection};
	Players = players;
	Conn = connections;
	check_skill_potion();
	check_skill_respec();
	/* Preserve ordinary disconnect protection; Everlasting still revives fully. */
	setup(); Conn[0]->last_keepalive_recv.tv_sec -= 3; check_death();
	setup(); player.mode = 0; Conn[0]->last_keepalive_recv.tv_sec -= 3;
	disconnect_recall = TRUE;
	player_death(1);
	assert(!player.death && player.chp == player.mhp && recalls == 1 && player.deaths == 0);
	assert(player.exp == 123456 && player.au == 98765);
	assert(!memcmp(saved_inventory, inventory, sizeof(inventory)));
	disconnect_recall = FALSE;
	for (int enabled = 0; enabled <= 1; enabled++) {
		setup(); player.insta_res = enabled; check_death();
		setup(); player.au = player.balance = 0; player.insta_res = enabled; check_death();
		setup(); floor_info.flags1 = LF1_NO_GHOST; player.insta_res = enabled; check_death();
		setup(); test_dungeon.flags2 = DF2_HELL | DF2_IRON; check_death();
		setup(); cfg.no_ghost = TRUE; check_death(); cfg.no_ghost = FALSE;
		setup(); player.ghost = 1; check_death();
		setup(); strcpy(player.died_from, "insanity"); check_death();
		setup(); strcpy(player.died_from, "indecisiveness"); check_death();
		setup(); strcpy(player.died_from, "indetermination"); check_death();
		setup(); strcpy(player.died_from, "divine wrath"); check_death();
		setup(); test_dungeon.flags2 = DF2_NO_DEATH; check_death();
		setup(); test_dungeon.type = DI_DEATH_FATE; check_death();
	}
	setup();
	netherrealm_wpos_x = player.wpos.wx; netherrealm_wpos_y = player.wpos.wy; netherrealm_wpos_z = -1;
	assert(in_netherrealm(&player.wpos)); check_death();
	setup();
	player.poisoned = player.diseased = player.cut = player.stun = 1;
	player.image = player.blind = player.paralyzed = player.confused = 1;
	player.black_breath = TRUE;
	check_death();
	assert(!player.poisoned && !player.diseased && !player.cut && !player.stun);
	assert(!player.image && !player.blind && !player.paralyzed && !player.confused && !player.black_breath);
	setup(); sector000separation = TRUE;
	player.wpos.wx = WPOS_SECTOR000_X; player.wpos.wy = WPOS_SECTOR000_Y;
	player.wpos.wz = WPOS_SECTOR000_Z_DUN;
	player.global_event_temp = PEVF_SAFEDUN_00;
	check_death();
	setup(); player.global_event_temp = PEVF_NOGHOST_00; check_death();
	/* Real recall routing must not count death as beating an ironman dungeon. */
	setup(); real_recall = TRUE;
	player.wpos.wx = WPOS_IRONDEEPDIVE_X; player.wpos.wy = WPOS_IRONDEEPDIVE_Y;
	player.wpos.wz = WPOS_IRONDEEPDIVE_Z * 100; player.IDDC_flags = 1;
	deep_dive_level[0] = 137;
	check_death();
	assert(!player.iron_winner && !player.IDDC_flags && deep_dive_level[0] == 137);
	setup();
	hallsofmandos_wpos_x = player.wpos.wx; hallsofmandos_wpos_y = player.wpos.wy; hallsofmandos_wpos_z = -1;
	check_death();
	real_recall = FALSE;
	/* Repeated deaths cannot charge gold, grant gold or duplicate items. */
	setup();
	for (int i = 0; i < 10; i++) {
		player.death = TRUE;
		player_death(1);
	}
	assert(player.deaths == 10 && recalls == 10 && player.au == 98765 && player.balance == 76543);
	assert(!memcmp(saved_inventory, inventory, sizeof(inventory)));
	/* Old ghosts: default, reduced, invalid and full loss factors are all free. */
	int factors[] = {0, 1, 35, 100, -1, 101};
	for (unsigned i = 0; i < sizeof(factors) / sizeof(*factors); i++) {
		setup(); player.ghost = 1; player.death = FALSE;
		resurrect_player(1, factors[i]);
		assert(!player.ghost && player.exp == 123456 && player.max_exp == 234567 && player.lives == 5);
	}
	/* Other modes retain the default 35% resurrection loss and life count. */
	setup(); player.mode = 0; player.ghost = 1; player.exp = player.max_exp = 10000;
	resurrect_player(1, 0);
#ifdef ENABLE_INSTANT_RES
	assert(player.exp == 6500 && player.max_exp == 6500 && player.lives == 4);
#else
	assert(player.exp == 6000 && player.max_exp == 6000 && player.lives == 4);
#endif
	/* Stop at the legacy termination log before character/save-file deletion. */
	for (int suicide = 0; suicide <= 1; suicide++) {
		setup(); memset(inventory, 0, sizeof(inventory)); player.au = 0;
		player.mode = suicide ? MODE_EVERLASTING : 0;
		player.suicided = suicide; legacy_path = 1;
		if (suicide) Conn[0]->last_keepalive_recv.tv_sec -= 3;
		if (!setjmp(legacy_jump)) { player_death(1); assert(!"legacy death path was bypassed"); }
		assert(recalls == 0);
	}
	puts("Everlasting death and resurrection checks passed.");
	return 0;
}
