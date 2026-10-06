/* Actual quota, deletion, compaction, AI and record read/write; isolate placement and client I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

static monster_type monsters[MAX_M_IDX], loaded[2];
static monster_race races[MAX_R_IDX];
static monster_ego egos[MAX_RE_IDX];
static cave_type cells[24][24], *rows[24];
static feature_type features[MAX_F_IDX];
static bool active, fail_placement;
static int spawn_x = 11, spawn_y = 10, astar_result, teleports;

void test_monster_cave(cave_type **cave);
extern u32b fake_name_size, fake_text_size;
void test_random_result(int result);
void test_cooldown_start(monster_type *m, int chance);
void test_cooldown_charge(monster_type *m, int energy);
void test_cooldown_end(monster_type *m);
bool test_monster_moves(int m_idx);
void test_write_monsters(FILE *file, monster_type *m, int count, bool legacy);
bool test_read_monsters(FILE *file, monster_type *m, int capacity, bool legacy);

void __real_scatter(worldpos *wpos, int *yp, int *xp, int y, int x, int d, int m);
void __wrap_scatter(worldpos *wpos, int *yp, int *xp, int y, int x, int d, int m) {
	if (!active) { __real_scatter(wpos, yp, xp, y, x, d, m); return; }
	*yp = spawn_y; *xp = spawn_x;
}
int __wrap_pick_ego_monster(int race, int level) { return 0; }
int __wrap_place_monster_one(worldpos *wpos, int y, int x, int race, int ego, int randuni, bool slp, int clone, int summons) {
	assert(active);
	if (fail_placement) return 1;
	int idx = m_pop();
	assert(idx);
	monsters[idx] = (monster_type){0};
	monsters[idx].r_idx = race; monsters[idx].wpos = *wpos;
	monsters[idx].fy = y; monsters[idx].fx = x;
	monsters[idx].clone = clone; monsters[idx].clone_summoning = summons;
	monsters[idx].astar_idx = -1;
	cells[y][x].m_idx = idx;
	races[race].cur_num++;
	if (races[race].flags7 & RF7_MULTIPLY) num_repro++;
	return 0;
}
void __wrap_monster_desc(int Ind, char *buf, int idx, int mode) { strcpy(buf, "test monster"); }
void __wrap_update_health(int idx) {}
bool __real_projectable_wall(worldpos *wpos, int y1, int x1, int y2, int x2, int range);
bool __wrap_projectable_wall(worldpos *wpos, int y1, int x1, int y2, int x2, int range) {
	return active ? TRUE : __real_projectable_wall(wpos, y1, x1, y2, x2, range);
}
bool __wrap_teleport_away(int idx, int range) { teleports++; return TRUE; }
int __wrap_get_moves_astar(int Ind, int idx, int *yp, int *xp) { return astar_result; }

static int root(int x) {
	worldpos pos = {10, 10, -1};
	assert(!__wrap_place_monster_one(&pos, 10, x, 1, 0, 0, FALSE, 0, 0));
	return cells[10][x].m_idx;
}

static void parse_intervals(void) {
	static header race_header, ego_header;
	static char names[2048], text[2048], ego_names[2048];
	char buf[1024];
	r_head = &race_header; re_head = &ego_header;
	r_head->info_num = MAX_R_IDX; re_head->info_num = MAX_RE_IDX;
	r_name = names; r_text = text; re_name = ego_names;
	fake_name_size = fake_text_size = 2048;
	int intervals[] = {1, 3, 5, 15};
	for (unsigned i = 0; i < sizeof(intervals) / sizeof(*intervals); i++) {
		FILE *file = tmpfile(); assert(file);
		fprintf(file, "V:4.9.2\nN:1:test\nS:1_IN_%d | HEAL\n", intervals[i]); rewind(file);
		assert(!init_r_info_txt(file, buf)); fclose(file);
		assert(races[1].spell_interval == intervals[i] && races[1].freq_spell == 100 / intervals[i]);
		file = tmpfile(); assert(file);
		fprintf(file, "V:4.0.0\nN:1:test\nS:1_IN_%d | HEAL\n", intervals[i]); rewind(file);
		assert(!init_re_info_txt(file, buf)); fclose(file);
		assert(egos[1].spell_interval == intervals[i]);
	}
}

void check_monster_rules(void) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p));
	p->id = 123; p->rp_ptr = &race_info[0]; p->cp_ptr = &class_info[0];
	p->wpos = (worldpos){10, 10, -1}; p->py = 10; p->px = 13;
	m_list = monsters; memset(m_fast, 0, sizeof(m_fast)); m_max = m_nxt = 1; m_top = num_repro = NumPlayers = 0;
	r_info = races; re_info = egos; f_info = features;
	for (int y = 0; y < 24; y++) {
		rows[y] = cells[y];
		for (int x = 0; x < 24; x++) cells[y][x].feat = FEAT_FLOOR;
	}
	features[FEAT_FLOOR].flags1 = FF1_FLOOR | FF1_LOS;
	active = TRUE; test_monster_cave(rows); test_random_result(0); monster_families_reset();
	parse_intervals();
	races[1].level = 20; races[1].rarity = 1; races[1].flags7 = RF7_MULTIPLY;
	races[1].flags2 = RF2_STUPID; races[1].d_char = 'p';
	int mother = root(10), other = root(15);
	assert(multiply_monster(mother));
	int child = cells[10][11].m_idx;
	u16b family = monsters[mother].repro_family;
	spawn_x = 12;
	assert(multiply_monster(child));
	assert(monsters[cells[10][12].m_idx].repro_family == family && monster_family_births(&monsters[mother]) == 2);
	delete_monster_idx(cells[10][12].m_idx, TRUE);
	fail_placement = TRUE;
	assert(!multiply_monster(mother) && monster_family_births(&monsters[mother]) == 2);
	fail_placement = FALSE;
	cells[10][12].m_idx = mother; /* All 18 location attempts are blocked. */
	assert(!multiply_monster(mother) && monster_family_births(&monsters[mother]) == 2);
	cells[10][12].m_idx = 0;
	for (int count = 3; count <= 100; count++) {
		assert(multiply_monster(mother));
		assert(monster_family_births(&monsters[mother]) == count);
		delete_monster_idx(cells[10][12].m_idx, TRUE);
	}
	assert(!multiply_monster(mother) && !multiply_monster(child));
	delete_monster_idx(mother, TRUE);
	assert(monster_family_births(&monsters[child]) == 100 && !multiply_monster(child));
	assert(multiply_monster(other)); /* A second family has its own quota. */
	int other_child = cells[10][12].m_idx;
	monsters[child].spell_cooldown = 4; monsters[child].spell_cooldown_energy = 7;
	assert(replace_monster(child, 1));
	child = cells[10][11].m_idx;
	assert(monsters[child].repro_family == family && monster_family_births(&monsters[child]) == 100);
	assert(monsters[child].spell_cooldown == 4 && monsters[child].spell_cooldown_energy == 7);
	compact_monsters(0, FALSE);
	child = cells[10][11].m_idx; other_child = cells[10][12].m_idx;
	assert(monsters[child].repro_family == family && !multiply_monster(child));
	/* Round-trip actual monster records, including a family whose mother is dead. */
	monster_type records[2] = {monsters[child], monsters[other_child]};
	monster_race special_race = races[1]; special_race.spell_interval = 15;
	records[1].special = TRUE; records[1].r_ptr = &special_race;
	FILE *file = tmpfile(); assert(file);
	test_write_monsters(file, records, 2, FALSE); rewind(file); monster_families_reset();
	assert(test_read_monsters(file, loaded, 2, FALSE)); fclose(file);
	assert(monster_family_births(&loaded[0]) == 100 && monster_family_births(&loaded[1]) == 1);
	assert(loaded[0].spell_cooldown == 4 && loaded[0].spell_cooldown_energy == 7);
	assert(loaded[1].r_ptr->spell_interval == 15); FREE(loaded[1].r_ptr, monster_race);
	records[1].special = FALSE; records[1].r_ptr = NULL;
	file = tmpfile(); assert(file);
	test_write_monsters(file, records, 2, TRUE); rewind(file); monster_families_reset();
	assert(test_read_monsters(file, loaded, 2, TRUE)); fclose(file);
	assert(!loaded[0].repro_family && !loaded[0].spell_cooldown && !loaded[1].repro_family);
	/* Actual cast selection: heal once, then block the next five own actions. */
	monster_type *m = &monsters[child];
	m->repro_family = 0; m->spell_cooldown = m->spell_cooldown_energy = 0;
	m->hp = 10; m->maxhp = 100; m->cdis = 3;
	races[1].freq_innate = races[1].freq_spell = 20; races[1].spell_interval = 5;
	races[1].flags6 = RF6_HEAL;
	assert(make_attack_spell(1, child) && m->hp > 10 && m->spell_cooldown == 5);
	int action_energy = level_speed(&m->wpos);
	for (int turn = 0; turn < 5; turn++) {
		assert(!make_attack_spell(1, child));
		/* Repeated stuck retries cannot reduce cooldown without earning an action's energy. */
		for (int e = 1; e < action_energy; e++) {
			test_cooldown_charge(m, 1); test_cooldown_end(m);
			assert(m->spell_cooldown == 5 - turn);
		}
		test_cooldown_charge(m, 1);
		assert(!make_attack_spell(1, child)); /* Fifth action remains blocked, before settlement. */
		test_cooldown_end(m); assert(m->spell_cooldown == 4 - turn);
	}
	m->hp = 10; assert(make_attack_spell(1, child) && m->spell_cooldown == 5);
	races[1].flags6 = RF6_BLINK; assert(!make_attack_spell(1, child));
	m->spell_cooldown = 0; cells[10][11].info |= CAVE_STCK;
	assert(make_attack_spell(1, child) && m->spell_cooldown == 5); /* Blocked teleport consumed its cast. */
	cells[10][11].info &= ~CAVE_STCK;
	m->spell_cooldown = 0; races[1].flags6 = 0;
	assert(!make_attack_spell(1, child) && !m->spell_cooldown);
	races[1].flags6 = RF6_HEAL; test_random_result(99);
	assert(!make_attack_spell(1, child) && !m->spell_cooldown); test_random_result(0);
	/* Both A* escape paths share the same cooldown, including failed movement spells. */
	races[1].flags7 = RF7_ASTAR; races[1].flags6 = RF6_BLINK; m->astar_idx = 0;
	for (int code = 0; code <= 2; code += 2) {
		astar_result = code; m->spell_cooldown = 5; int before = teleports;
		assert(!test_monster_moves(child) && teleports == before);
		m->spell_cooldown = 0;
		assert(test_monster_moves(child) && teleports == before + 1 && m->spell_cooldown == 5);
	}
	int intervals[] = {1, 3, 5, 15};
	for (unsigned i = 0; i < sizeof(intervals) / sizeof(*intervals); i++) {
		races[1].spell_interval = intervals[i]; test_cooldown_start(m, 100 / intervals[i]);
		assert(m->spell_cooldown == intervals[i]);
	}
	test_cooldown_start(m, 50); assert(m->spell_cooldown == 2); /* Dynamic AI frequency. */
	m->csleep = 1; test_cooldown_charge(m, action_energy * 10); test_cooldown_end(m);
	assert(m->spell_cooldown == 2 && !m->spell_cooldown_energy);
	m->csleep = 0; test_cooldown_charge(m, action_energy * 10);
	assert(m->spell_cooldown == 2); test_cooldown_end(m); assert(m->spell_cooldown == 1);
	test_cooldown_end(m); assert(m->spell_cooldown == 1); /* No banked extra turns. */
	/* Run the actual energy scheduler: inactive/sleeping monsters cannot tick down;
	   blocked movement retries cannot turn server frames into action turns. */
	m_fast[0] = child; m_top = 1; m->astar_idx = -1; m->energy = 0;
	races[1].flags7 = 0; races[1].flags2 = RF2_STUPID | RF2_NEVER_MOVE | RF2_NEVER_BLOW;
	p->cur_hgt = p->cur_wid = 24; m->maxhp = m->hp = 100;
	int durations[2];
	for (int speed = 0; speed < 2; speed++) {
		m->mspeed = speed ? 120 : 110; m->energy = 0;
		test_cooldown_start(m, 20); /* The denominator currently differs, so dynamic N=5. */
		NumPlayers = 0;
		for (int frame = 0; frame < 200; frame++) { turn++; process_monsters(); }
		assert(m->spell_cooldown == 5);
		NumPlayers = 1; m->csleep = 10000;
		for (int frame = 0; frame < 200; frame++) { turn++; process_monsters(); }
		assert(m->spell_cooldown == 5);
		m->csleep = 0; m->spell_cooldown_energy = 0; m->energy = 0;
		int frame, previous_change = -10000;
		for (frame = 0; frame < 10000 && m->spell_cooldown; frame++) {
			byte before = m->spell_cooldown; turn++; process_monsters();
			if (m->spell_cooldown != before) {
				assert(m->spell_cooldown + 1 == before);
				assert(frame - previous_change >= MONSTER_TURNS);
				previous_change = frame;
			}
		}
		assert(!m->spell_cooldown && frame < 10000); durations[speed] = frame;
	}
	assert(durations[1] < durations[0]);
	monster_type invalid = {0}; invalid.repro_family = MAX_M_IDX;
	assert(!monster_family_restore(&invalid, 1)); invalid.repro_family = 0;
	assert(!monster_family_restore(&invalid, 1));
	active = FALSE; test_monster_cave(NULL); test_random_result(-1);
	m_top = 0; NumPlayers = 1;
	puts("Family quota, spell cooldown, AI escape and monster save compatibility checks passed.");
}
