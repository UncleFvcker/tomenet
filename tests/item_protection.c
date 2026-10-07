/* Real recharge, flags and inventory/ground damage paths; isolate client I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

static object_kind kinds[5];
static object_type inventory[INVEN_TOTAL], floor_objects[2];
static int random_result = -1, floor_deletions;
static cptr inscriptions[] = {""};
static bool native_cleanup;

void test_random_result(int result) { random_result = result; }
void test_inventory_cleanup(bool native) { native_cleanup = native; }

s32b __real_Rand_div(s32b m);
s32b __wrap_Rand_div(s32b m) {
	return random_result < 0 ? __real_Rand_div(m) : (m > 1 ? random_result : 0);
}
void __wrap_object_desc(int Ind, char *buf, object_type *o, int pref, int mode) { strcpy(buf, "test item"); }
void __wrap_inven_item_describe(int Ind, int item) {}
bool __real_inven_item_optimize(int Ind, int item);
bool __wrap_inven_item_optimize(int Ind, int item) {
	return native_cleanup ? __real_inven_item_optimize(Ind, item) : TRUE;
}
void __wrap_delete_object_idx(int idx, bool unfound_art, bool log) {
	floor_deletions++;
	memset(&o_list[idx], 0, sizeof(o_list[idx]));
}

void test_floor_item(int idx);
int test_inventory_fire(void);
void test_floor_damage(worldpos *wpos, int typ);

void check_item_protection(void) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p));
	p->inventory = inventory;
	p->rp_ptr = &race_info[0]; p->cp_ptr = &class_info[0];
	p->using_up_item = p->item_newest = -1;
	p->wpos = (worldpos){10, 10, -1};
	k_info = kinds; max_k_idx = 5;
	quark__str = inscriptions; quark__num = 1;
	o_list = floor_objects; o_max = 2;
	kinds[1].tval = TV_STAFF; kinds[1].sval = SV_STAFF_TELEPORTATION; kinds[1].level = 10;
	kinds[2].tval = TV_WAND; kinds[2].sval = SV_WAND_MAGIC_MISSILE; kinds[2].level = 10;
	kinds[3].tval = TV_MSTAFF;
	random_result = 0; /* Force both the failure and the old destruction branch. */
	for (int count = 1; count <= 10; count++) {
		invcopy(&inventory[0], 1);
		inventory[0].number = count; inventory[0].pval = 20;
		assert(recharge_aux(1, 0, 60));
		assert(inventory[0].k_idx == 1 && inventory[0].number == count);
		assert(inventory[0].pval == 0 && (inventory[0].ident & ID_EMPTY));
	}
	/* Ground staves use the same recharge path. */
	floor_objects[1] = inventory[0]; floor_objects[1].pval = 20;
	assert(recharge_aux(1, -1, 60));
	assert(floor_objects[1].number == 10 && floor_objects[1].pval == 0);
#ifdef MSTAFF_MDEV_COMBO
	invcopy(&inventory[0], 3); inventory[0].number = 1;
	inventory[0].xtra1 = SV_STAFF_TELEPORTATION + 1; inventory[0].pval = 20;
	assert(recharge_aux(1, 0, 60));
	assert(inventory[0].number == 1 && inventory[0].xtra1 == SV_STAFF_TELEPORTATION + 1);
#endif
	/* Wands retain their original failure penalty. */
	invcopy(&inventory[0], 2); inventory[0].number = 10; inventory[0].pval = 20;
	assert(recharge_aux(1, 0, 60));
	assert(inventory[0].number == 7);
	/* Successful staff recharge still adds charges. */
	invcopy(&inventory[0], 1); inventory[0].number = 1; inventory[0].ident = ID_EMPTY;
	random_result = 1;
	assert(recharge_aux(1, 0, 140));
	assert(inventory[0].pval > 0 && inventory[0].number == 1 && !(inventory[0].ident & ID_EMPTY));
	random_result = 0;
	int damage[] = {GF_ACID, GF_ELEC, GF_FIRE, GF_COLD, GF_WATER, GF_PLASMA, GF_ICE,
	                GF_SHARDS, GF_FORCE, GF_SOUND, GF_MANA, GF_METEOR, GF_DISENCHANT,
	                GF_ANNIHILATION, GF_DISINTEGRATE, GF_HAVOC, GF_INFERNO, GF_DETONATION, GF_ROCKET};
	for (int tval = TV_SHOT; tval <= TV_BOLT; tval++) {
		memset(inventory, 0, sizeof(inventory));
		kinds[4].tval = tval; kinds[4].sval = SV_AMMO_MAGIC;
		invcopy(&inventory[0], 4); inventory[0].number = 10;
		inventory[INVEN_AMMO] = inventory[0];
		u32b f1, f2, f3, f4, f5, f6, esp;
		object_flags(&inventory[0], &f1, &f2, &f3, &f4, &f5, &f6, &esp);
		u32b base = TR3_IGNORE_ACID | TR3_IGNORE_ELEC | TR3_IGNORE_FIRE | TR3_IGNORE_COLD;
		u32b extra = TR5_IGNORE_WATER | TR5_IGNORE_MANA | TR5_IGNORE_DISEN;
		assert((f3 & base) == base && (f5 & extra) == extra);
		assert(test_inventory_fire() == 0);
		assert(inven_damage(1, set_all_destroy, 100) == 0);
		assert(inventory[0].number == 10 && inventory[INVEN_AMMO].number == 10);
		for (unsigned i = 0; i < sizeof(damage) / sizeof(*damage); i++) {
			floor_objects[1] = inventory[0]; floor_objects[1].wpos = p->wpos;
			floor_objects[1].to_h = floor_objects[1].to_d = 5;
			test_floor_item(1);
			test_floor_damage(&p->wpos, damage[i]);
			assert(floor_deletions == 0 && floor_objects[1].k_idx == 4 && floor_objects[1].number == 10);
			assert(floor_objects[1].to_h == 5 && floor_objects[1].to_d == 5);
		}
	}
	/* All book variants, including single-spell scrolls and personalized codices. */
	int books[] = {0, SV_TOME_CHAOS, SV_BOOK_COMBO, SV_SPELLBOOK,
	               SV_CUSTOM_TOME_1, SV_CUSTOM_TOME_2, SV_CUSTOM_TOME_3};
	for (unsigned b = 0; b < sizeof(books) / sizeof(*books); b++) {
		memset(inventory, 0, sizeof(inventory));
		kinds[4].tval = TV_BOOK; kinds[4].sval = books[b];
		invcopy(&inventory[0], 4); inventory[0].number = 10;
		inventory[0].xtra1 = 12; inventory[0].xtra2 = 34;
		inventory[INVEN_WIELD] = inventory[0];
		assert(!test_inventory_fire() && !inven_damage(1, set_all_destroy, 100));
		assert(inventory[0].number == 10 && inventory[INVEN_WIELD].number == 10);
		for (unsigned i = 0; i < sizeof(damage) / sizeof(*damage); i++) {
			floor_objects[1] = inventory[0]; floor_objects[1].wpos = p->wpos;
			test_floor_item(1); test_floor_damage(&p->wpos, damage[i]);
			assert(!floor_deletions && floor_objects[1].number == 10);
			assert(floor_objects[1].xtra1 == 12 && floor_objects[1].xtra2 == 34);
		}
	}
	/* Ordinary arrows still burn, so protection is specific to magic ammunition. */
	memset(inventory, 0, sizeof(inventory));
	kinds[4].tval = TV_ARROW; kinds[4].sval = 0;
	invcopy(&inventory[0], 4); inventory[0].number = 10;
	floor_objects[1] = inventory[0]; floor_objects[1].wpos = p->wpos;
	assert(test_inventory_fire() == 10 && inventory[0].number == 0);
	test_floor_damage(&p->wpos, GF_FIRE);
	assert(floor_deletions == 1 && floor_objects[1].k_idx == 0);
	test_floor_item(0); random_result = -1;
	puts("Staff recharge and magic ammunition protection checks passed.");
}
