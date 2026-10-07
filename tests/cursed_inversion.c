/* Real equip/uncurse/remove, shape-change and item save paths; no map/network I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

static object_kind kinds[2];
static artifact_type artifacts[2];
static ego_item_type egos[1];
static monster_race races[MAX_R_IDX];
static object_type inventory[INVEN_TOTAL];
static cptr inscriptions[32] = {""};
static const u32b bad3 = TR3_AUTO_CURSE | TR3_NO_TELE | TR3_NO_MAGIC | TR3_TY_CURSE |
    TR3_DRAIN_EXP | TR3_TELEPORT | TR3_AGGRAVATE;
static const u32b bad4 = TR4_BLACK_BREATH | TR4_DG_CURSE | TR4_CLONE | TR4_CURSE_NO_DROP;
static const u32b bad5 = TR5_DRAIN_MANA | TR5_DRAIN_HP;

void test_inventory_cleanup(bool native);
bool __real_do_mimic_change(int Ind, int race, bool force);
void test_write_cursed_item(FILE *file, object_type *item);
bool test_read_cursed_item(FILE *file, object_type *item);

static void flags(object_type *item, bool flipped) {
	u32b f[7];
	object_flags(item, &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[0]);
	assert((f[3] & bad3) == (flipped ? 0 : bad3));
	assert((f[4] & bad4) == (flipped ? 0 : bad4));
	assert((f[5] & bad5) == (flipped ? 0 : bad5));
	assert(f[1] & TR1_STR); assert(f[2] & TR2_RES_FIRE);
	assert((f[3] & (TR3_CURSED | TR3_HEAVY_CURSE)) == (TR3_CURSED | TR3_HEAVY_CURSE));
	assert(f[4] & TR4_ANTIMAGIC_10); assert(f[5] & TR5_WHITE_LIGHT);
	char powers[POW_INSCR_LEN] = "";
	power_inscribe(item, FALSE, powers);
	assert(!!strstr(powers, "Aggr") == !flipped && !!strstr(powers, "Drx") == !flipped);
}

static void fixture(int race, int class, int form, int art) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p)); memset(inventory, 0, sizeof(inventory));
	p->inventory = inventory; p->id = 123; p->lev = p->max_plv = 50;
	p->mode = MODE_EVERLASTING; p->prace = race; p->pclass = class; p->body_monster = form;
	p->rp_ptr = &race_info[race]; p->cp_ptr = &class_info[class]; p->wpos.wz = -1;
	p->using_up_item = p->item_newest = p->store_num = -1; p->inven_cnt = 1;
	inventory[0] = (object_type){.k_idx = 1, .tval = TV_BOOTS, .sval = 1, .number = 1,
	    .owner = 123, .mode = MODE_EVERLASTING, .name1 = art, .pval = -5, .to_a = -10};
}

void check_cursed_inversion(void) {
	k_info = kinds; max_k_idx = 2; a_info = artifacts; e_info = egos; r_info = races;
	quark__str = inscriptions; quark__num = 1;
	k_name = a_name = e_name = r_name = "Test";
	kinds[1] = (object_kind){.tval = TV_BOOTS, .sval = 1, .cost = 100, .pval = -5,
	    .flags1 = TR1_STR, .flags2 = TR2_RES_FIRE,
	    .flags3 = bad3 | TR3_CURSED | TR3_HEAVY_CURSE,
	    .flags4 = bad4 | TR4_ANTIMAGIC_10, .flags5 = bad5 | TR5_WHITE_LIGHT};
	artifacts[1].pval = -5; artifacts[1].cost = 100;
	races[RI_BLOODTHIRSTER].body_parts[BODY_LEGS] = 2;
	test_inventory_cleanup(TRUE);
	const int users[][3] = {
		{RACE_HUMAN, CLASS_HELLKNIGHT, 0}, {RACE_VAMPIRE, CLASS_WARRIOR, 0},
		{RACE_HUMAN, CLASS_CPRIEST, RI_BLOODTHIRSTER},
		{RACE_HUMAN, CLASS_WARRIOR, 0}, {RACE_HUMAN, CLASS_CPRIEST, 0}
	};
	for (int art = 0; art <= 1; art++) for (int i = 0; i < 5; i++) {
		fixture(users[i][0], users[i][1], users[i][2], art);
		bool eligible = i < 3;
		assert(do_cmd_wield(1, 0, 0) == INVEN_FEET);
		object_type *item = &inventory[INVEN_FEET];
		assert(cursed_p(item) && !!item->pval2 == eligible);
		flags(item, eligible);
		assert(eligible ? item->pval > 0 && item->to_a == 10 : item->pval == -5 && item->to_a == -10);
		/* Existing binding remains; this must not silently grant free removal. */
		if (i != 2) { do_cmd_takeoff(1, INVEN_FEET, 1); assert(inventory[INVEN_FEET].k_idx); }
		assert(remove_all_curse(1));
		assert(!cursed_p(item) && !item->pval2); flags(item, FALSE);
		do_cmd_takeoff(1, INVEN_FEET, 1); assert(!inventory[INVEN_FEET].k_idx);
		assert(inventory[0].k_idx && !inventory[0].pval2);
		/* The native wield path auto-curses BEFORE flipping again. */
		assert(do_cmd_wield(1, 0, 0) == INVEN_FEET);
		assert(cursed_p(item) && !!item->pval2 == eligible); flags(item, eligible);
		FILE *file = tmpfile(); object_type loaded;
		assert(file); test_write_cursed_item(file, item); rewind(file);
		assert(test_read_cursed_item(file, &loaded)); fclose(file);
		assert(loaded.pval2 == item->pval2 && loaded.pval == item->pval);
		flags(&loaded, eligible);
	}
	/* Native randart rerolling remains reversible; failures retain their raw flags. */
	object_kind old_kind = kinds[1];
	kinds[1] = (object_kind){.tval = TV_BOOTS, .sval = 1, .cost = 100, .ac = 1};
	bool rerolled = FALSE;
	for (int seed = 1; seed <= 1000 && !rerolled; seed++) {
		object_type random = {.k_idx = 1, .tval = TV_BOOTS, .sval = 1, .number = 1,
		    .name1 = ART_RANDART, .name3 = seed, .level = 10, .owner = 123};
		artifact_type original = *randart_make(&random);
		if (!(original.flags3 & TR3_HEAVY_CURSE)) continue;
		random.ident = ID_CURSED;
		inverse_cursed(&random);
		u32b f[7];
		object_flags(&random, &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[0]);
		if (random.pval2 < 0) {
			assert((f[3] & bad3) == (original.flags3 & bad3));
			assert((f[4] & bad4) == (original.flags4 & bad4));
			assert((f[5] & bad5) == (original.flags5 & bad5));
			continue;
		}
		assert(random.pval2 > 0 && !cursed_p(&random));
		assert(!(f[3] & bad3) && !(f[4] & bad4) && !(f[5] & bad5));
		reverse_cursed(&random);
		assert(!random.pval2 && random.name3 == seed && cursed_p(&random));
		object_flags(&random, &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[0]);
		assert(f[3] == original.flags3 && f[4] == original.flags4 && f[5] == original.flags5);
		rerolled = TRUE;
	}
	assert(rerolled); kinds[1] = old_kind;
	/* Leaving Blood Sacrifice restores side effects; re-entering suppresses them. */
	fixture(RACE_HUMAN, CLASS_CPRIEST, 0, 0);
	assert(do_cmd_wield(1, 0, 0) == INVEN_FEET);
	assert(__real_do_mimic_change(1, RI_BLOODTHIRSTER, TRUE)); flags(&inventory[INVEN_FEET], TRUE);
	assert(__real_do_mimic_change(1, 0, TRUE)); flags(&inventory[INVEN_FEET], FALSE);
	assert(__real_do_mimic_change(1, RI_BLOODTHIRSTER, TRUE)); flags(&inventory[INVEN_FEET], TRUE);
	/* Ordinary curses, excluded items and recorded failures receive no protection. */
	object_type item = inventory[INVEN_FEET]; reverse_cursed(&item);
	item.pval2 = -1; flags(&item, FALSE);
	item.pval2 = 0; item.tval = TV_RING; item.sval = SV_RING_SPECIAL;
	inverse_cursed(&item); assert(!item.pval2); flags(&item, FALSE);
	item.tval = TV_SWORD; kinds[1].flags4 |= TR4_NEVER_BLOW;
	inverse_cursed(&item); assert(!item.pval2); flags(&item, FALSE);
	kinds[1].flags4 &= ~TR4_NEVER_BLOW; kinds[1].flags3 &= ~TR3_HEAVY_CURSE;
	inverse_cursed(&item); assert(!item.pval2);
	test_inventory_cleanup(FALSE);
	puts("Curse inversion: eligibility, side effects, uncurse/re-equip, form changes and item save/load passed.");
}
