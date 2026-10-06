/* Real item eligibility, town-drop and floor-pickup paths; isolate inventory/map I/O. */
#define SERVER
#include "angband.h"
#include <assert.h>

static bool active;
static int carried, dropped;
static object_kind kinds[2];
static object_type inventory[INVEN_TOTAL], floor_objects[2];
static feature_type features[MAX_F_IDX];
static artifact_type artifacts[2];

void test_floor_item(int idx);
cptr __real_lookup_accountname(int id);
cptr __wrap_lookup_accountname(int id) {
	return active ? "test account" : __real_lookup_accountname(id);
}
void __real_object_desc_store(int Ind, char *buf, object_type *o, int pref, int mode);
void __wrap_object_desc_store(int Ind, char *buf, object_type *o, int pref, int mode) {
	if (active) strcpy(buf, "test item");
	else __real_object_desc_store(Ind, buf, o, pref, mode);
}
s16b __real_inven_carry(int Ind, object_type *o);
s16b __wrap_inven_carry(int Ind, object_type *o) {
	if (!active) return __real_inven_carry(Ind, o);
	inventory[0] = *o; carried++; return 0;
}
int __real_drop_near(bool handle_d, int Ind, object_type *o, int chance, worldpos *pos, int y, int x);
int __wrap_drop_near(bool handle_d, int Ind, object_type *o, int chance, worldpos *pos, int y, int x) {
	if (!active) return __real_drop_near(handle_d, Ind, o, chance, pos, y, x);
	floor_objects[1] = *o; dropped++; return 1;
}
void __real_whats_under_your_feet(int Ind, bool describe);
void __wrap_whats_under_your_feet(int Ind, bool describe) {
	if (!active) __real_whats_under_your_feet(Ind, describe);
}

void check_item_sharing(void) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p));
	p->id = 123; p->lev = p->max_plv = 1; p->mode = MODE_EVERLASTING;
	p->max_exp = 1; p->inventory = inventory; p->wpos = (worldpos){1, 1, 0};
	p->rp_ptr = &race_info[0]; p->cp_ptr = &class_info[0];
	k_info = kinds; max_k_idx = 2; o_list = floor_objects; o_max = 2;
	f_info = features; a_info = artifacts;
	kinds[1].tval = TV_SWORD; kinds[1].sval = 1; p->obj_aware[1] = TRUE;
	object_type item = {0};
	item.k_idx = 1; item.tval = TV_SWORD; item.sval = 1; item.number = 1;
	item.wpos = p->wpos;
	item.owner = 456; item.mode = MODE_EVERLASTING | MODE_STARTER_ITEM;
	item.ident = ID_KNOWN; item.level = 0;
	assert(!is_admin(p));
	assert(can_use_admin(1, &item) && item.owner == 456); /* Preview must not take ownership. */
	assert(can_use(1, &item) && item.owner == p->id && !item.level);
	item.owner = 456;
	assert(can_use_verbose(1, &item) && item.owner == p->id && !item.level);
	assert(item.mode & MODE_STARTER_ITEM); /* Sharing does not clear the sale restriction. */
	item.owner = 456; item.name1 = 1;
	assert(can_use(1, &item) && artifacts[1].carrier == p->id && !item.level);
	item.name1 = 0; item.owner = 456; item.level = 2;
	assert(!can_use(1, &item) && !can_use_admin(1, &item) && !can_use_verbose(1, &item));
	assert(item.owner == 456); /* Above-level items still need their original level. */
	item.level = 0; item.mode = MODE_PVP;
	assert(!can_use(1, &item) && !can_use_admin(1, &item) && !can_use_verbose(1, &item));
	assert(item.owner == 456); /* Cross-mode restrictions remain. */
	item.mode = MODE_EVERLASTING | MODE_STARTER_ITEM;
	active = TRUE; cfg.anti_cheeze_pickup = cfg.anti_arts_pickup = TRUE;
	wild_info[1][1].type = WILD_TOWN;
	int before = dropped; inventory[0] = item;
	do_cmd_drop(1, 0, 1);
	assert(dropped == before + 1 && !inventory[0].level);
	/* Actual pickup guard and ownership transfer, with anti-cheese enabled. */
	memset(inventory, 0, sizeof(inventory));
	floor_objects[1] = item; test_floor_item(1); before = carried;
	carry(1, 2, 0, FALSE);
	assert(carried == before + 1 && inventory[0].owner == p->id && !inventory[0].level);
	assert(inventory[0].mode & MODE_STARTER_ITEM);
	/* Solo and higher-level pickup still fail before inventory I/O. */
	p->mode |= MODE_SOLO; floor_objects[1] = item;
	carry(1, 2, 0, FALSE); assert(carried == before + 1);
	p->mode &= ~MODE_SOLO; item.level = 2; floor_objects[1] = item;
	carry(1, 2, 0, FALSE); assert(carried == before + 1);
	active = FALSE; test_floor_item(0); wild_info[1][1].type = 0;
	puts("Level-zero sharing, ownership transfer, town drop and floor pickup checks passed.");
}
