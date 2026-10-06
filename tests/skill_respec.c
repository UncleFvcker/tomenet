/* Real command, allocation, potion and save/load paths; isolate client I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

int __wrap_Send_skill_info(int Ind, int skill, bool keep) { return 0; }
void __wrap_calc_techniques(int Ind) {}
bool __wrap_do_mimic_change(int Ind, int race, bool force) { return TRUE; }
int __wrap_exec_lua(int Ind, char *code) { return 0; }

static skill_type skills[MAX_SKILLS];
static monster_race monsters[MAX_R_IDX];
static object_type inventory[INVEN_TOTAL];
static player_class test_class;
static player_race test_race;

static void reset_command(void) {
	char message[MSG_LEN] = "/respec", uncensored[MSG_LEN] = "/respec";
	do_slash_cmd(1, message, uncensored);
}

static void fixture(int race, int class) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p));
	memset(skills, 0, sizeof(skills));
	s_info = skills;
	r_info = monsters;
	test_class = class_info[class];
	test_race = race_info[race];
	/* Startup normally fills these skill tables from s_info.txt. */
	for (int i = 0; i < MAX_SKILLS; i++) {
		test_class.skills[i].skill = test_race.skills[i].skill = i;
		test_class.skills[i].value = i == SKILL_COMBAT ? 1000 : 0;
		test_class.skills[i].mod = (i == SKILL_COMBAT || i == SKILL_ARCHERY || i == SKILL_OSPIRIT) ? 1000 : 0;
		test_class.skills[i].vmod = test_class.skills[i].mmod = '+';
		test_race.skills[i].value = test_race.skills[i].mod = 0;
		test_race.skills[i].vmod = test_race.skills[i].mmod = '+';
	}
	p->inventory = inventory;
	p->prace = race;
	p->pclass = class;
	p->rp_ptr = &test_race;
	p->cp_ptr = &test_class;
	p->lev = p->max_lev = p->max_plv = 50;
	p->inval = TRUE; /* Ordinary, unvalidated account, including TEST_SERVER builds. */
	p->wpos.wz = -100;
	p->au = 1234;
	strcpy(p->name, "RespecTester");
}

static void reload_save(void) {
	player_type *p = Players[1];
	FILE *file = fopen(p->savefile, "rb");
	assert(file);
	sf_major = fgetc(file); sf_minor = fgetc(file);
	sf_patch = fgetc(file); sf_extra = fgetc(file);
	fclose(file);
	assert(rd_savefile_new(1) == 0);
	p->rp_ptr = &test_race;
	p->cp_ptr = &test_class;
}

void check_skill_respec(void) {
	player_type *p = Players[1];
	fixture(RACE_HUMAN, CLASS_WARRIOR);
	reset_command();
	assert(p->skill_points == 245 && p->skill_points_bonus == 0);
	assert(p->au == 1234 && p->wpos.wz == -100);
	s32b combat_base = p->s_info[SKILL_COMBAT].value;
	u16b archery_mod = p->s_info[SKILL_ARCHERY].mod;
	assert(archery_mod && p->s_info[SKILL_COMBAT].mod);
	/* Reset restores the modifier of a skill disabled by an exclusive skill. */
	skills[SKILL_COMBAT].action[SKILL_ARCHERY] = SKILL_EXCLUSIVE;
	increase_skill(1, SKILL_COMBAT, TRUE);
	assert(p->skill_points == 244 && p->s_info[SKILL_ARCHERY].mod == 0);
	assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
	assert(p->skill_points_bonus == 1 && p->skill_points == 245);
	p->aura[AURA_FEAR] = p->aura[AURA_SHIVER] = p->aura[AURA_DEATH] = TRUE;
	p->spell_project = 1;
	reset_command();
	assert(p->skill_points == 246 && p->skill_points_old == 246);
	assert(p->s_info[SKILL_COMBAT].value == combat_base);
	assert(p->s_info[SKILL_ARCHERY].mod == archery_mod);
	assert(!p->spell_project && !p->aura[AURA_FEAR] && !p->aura[AURA_SHIVER] && !p->aura[AURA_DEATH]);
	for (int i = 0; i < 10; i++) {
		increase_skill(1, SKILL_COMBAT, TRUE);
		assert(p->skill_points == 245);
		reset_command();
		assert(p->skill_points == 246 && p->skill_points_bonus == 1);
	}
	/* Skill caps and truncated modifiers must not swallow invested points. */
	p->s_info[SKILL_COMBAT].value = SKILL_MAX - 1;
	increase_skill(1, SKILL_COMBAT, TRUE);
	assert(p->skill_points == 245 && p->s_info[SKILL_COMBAT].value == SKILL_MAX);
	reset_command();
	assert(p->skill_points == 246);
	/* Persist the bonus after spending it, then refund it after reloading. */
	increase_skill(1, SKILL_COMBAT, TRUE);
	snprintf(p->savefile, sizeof(p->savefile), "%s/respec-test", getenv("TOMENET_TEST_DIR"));
	assert(save_player(1));
	p->skill_points_bonus = 0; p->skill_points = 0;
	reload_save();
	assert(p->skill_points_bonus == 1 && p->skill_points == 245);
	reset_command();
	assert(p->skill_points == 246);
	/* The old reserved word was zero: legacy saves use the normal level budget. */
	p->skill_points_bonus = 0; p->skill_points = 7;
	assert(save_player(1));
	p->skill_points_bonus = 99;
	reload_save();
	assert(p->skill_points_bonus == 0);
	reset_command();
	assert(p->skill_points == 245);
	/* An unsupported total must leave allocated skills and points intact. */
	p->skill_points_bonus = MAX_SHORT;
	p->s_info[SKILL_COMBAT].value = 12345;
	reset_command();
	assert(p->s_info[SKILL_COMBAT].value == 12345 && p->skill_points == 245);
	fixture(RACE_HUMAN, CLASS_RANGER); reset_command();
	assert(p->s_info[SKILL_ARCHERY].flags1 & SKF1_MAX_10);
	fixture(RACE_HUMAN, CLASS_DRUID); reset_command();
	assert(p->s_info[SKILL_MIMIC].flags1 & SKF1_MAX_1);
	fixture(RACE_VAMPIRE, CLASS_MAGE); reset_command();
	assert(p->s_info[SKILL_MIMIC].flags1 & SKF1_MAX_1);
#ifdef VAMP_ISTAR_SHADOW
	assert(p->s_info[SKILL_OSHADOW].value == 1000 && p->s_info[SKILL_OSHADOW].mod == 1700);
#endif
	fixture(RACE_MAIA, CLASS_MAGE); p->ptrait = TRAIT_ENLIGHTENED;
	reset_command();
	u16b spirit_mod = p->s_info[SKILL_OSPIRIT].mod;
	assert(spirit_mod && !p->s_info[SKILL_OSHADOW].mod);
	reset_command();
	assert(p->s_info[SKILL_OSPIRIT].mod == spirit_mod && p->skill_points == 245);
	fixture(RACE_DRACONIAN, CLASS_WARRIOR); p->ptrait = TRAIT_RED; reset_command();
	assert(p->s_info[SKILL_PICK_BREATH].value == 0);
	fixture(RACE_HUMAN, CLASS_WARRIOR); p->fruit_bat = 1; reset_command();
	assert(!p->s_info[SKILL_SWORD].mod && !p->s_info[SKILL_ARCHERY].mod);
	puts("Skill respec, repeated refunds and save/load checks passed.");
}
