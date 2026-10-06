/* Exercise the actual sigil activation and flag pool; isolate examination UI. */
#define SERVER
#include "angband.h"
#include <assert.h>

static bool active;
static object_kind kinds[2];
static artifact_type artifacts[2];
static object_type inventory[INVEN_TOTAL];

bool __real_identify_fully_item(int Ind, int item);
bool __wrap_identify_fully_item(int Ind, int item) {
	return active ? TRUE : __real_identify_fully_item(Ind, item);
}

void check_physical_runes(void) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p)); p->inventory = inventory;
	p->using_up_item = p->item_newest = -1;
	k_info = kinds; max_k_idx = 2; a_info = artifacts;
	active = TRUE;
	/* Every equipment slot accepts normal, fixed-artifact and random-artifact items. */
	for (int slot = INVEN_WIELD; slot < INVEN_TOTAL; slot++) {
		for (int art = 0; art < 3; art++) {
			memset(inventory, 0, sizeof(inventory));
			inventory[0].k_idx = 1; inventory[0].tval = TV_RUNE;
			inventory[0].sval = SV_R_TIME; inventory[0].number = 2;
			inventory[slot].k_idx = 1; inventory[slot].number = 1;
			inventory[slot].name1 = art == 2 ? ART_RANDART : art;
			inventory[slot].pval = 10;
			assert(rune_enchant(1, slot));
			assert(inventory[slot].sigil == SV_R_TIME && inventory[slot].pval == 10);
			assert(inventory[slot].name1 == (art == 2 ? ART_RANDART : art));
			assert(inventory[0].number == 1);
			assert(!rune_enchant(1, 0) && !rune_enchant(1, INVEN_TOTAL));
			int other = slot == INVEN_TOOL ? INVEN_LEFT : INVEN_TOOL;
			inventory[other].k_idx = 1; inventory[other].number = 1;
			assert(!rune_enchant(1, other) && inventory[0].number == 1);
			/* Replacing a sigil on its own slot is still allowed. */
			assert(rune_enchant(1, slot) && !inventory[0].number);
			memset(&inventory[slot], 0, sizeof(inventory[slot]));
			assert(!rune_enchant(1, slot));
		}
	}
	active = FALSE;
	/* High PVAL and conflicting high-value combinations no longer suppress candidates. */
	const struct { int tval, sigil, category; u32b flag; } cases[] = {
		{TV_SWORD, SV_R_TIME, 1, TR1_SPEED}, {TV_BOOTS, SV_R_TIME, 1, TR1_SPEED},
		{TV_SWORD, SV_R_TIME, 1, TR1_BLOWS}, {TV_GLOVES, SV_R_TIME, 1, TR1_BLOWS},
		{TV_CROWN, SV_R_MANA, 1, TR1_MANA}, {TV_GLOVES, SV_R_MANA, 1, TR1_MANA},
		{TV_GLOVES, SV_R_SHAR, 5, TR5_CRIT}, {TV_RING, SV_R_CONF, 1, TR1_INT},
		{TV_RING, SV_R_CONF, 1, TR1_WIS}, {TV_RING, SV_R_ELEC, 1, TR1_DEX},
		{TV_RING, SV_R_FIRE, 1, TR1_STR}, {TV_RING, SV_R_ACID, 1, TR1_CHR},
		{TV_RING, SV_R_POIS, 1, TR1_CON}, {TV_CLOAK, SV_R_DARK, 1, TR1_STEALTH}
	};
	for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(kinds, 0, sizeof(kinds));
		kinds[1].flags1 = TR1_LIFE | TR1_MANA | TR1_SPEED;
		kinds[1].flags5 = TR5_CRIT;
		if (cases[i].category == 1) kinds[1].flags1 &= ~cases[i].flag;
		else kinds[1].flags5 &= ~cases[i].flag;
		artifacts[1].flags2 = TR2_RES_CHAOS;
		object_type item = {0}; item.k_idx = 1; item.tval = cases[i].tval;
		item.name1 = 1; item.pval = 10; item.sigil = cases[i].sigil;
		bool found = FALSE;
		for (int seed = 1; seed <= 1000 && !found; seed++) {
			u32b f[7]; item.sseed = seed;
			object_flags(&item, &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[0]);
			assert(item.pval == 10 && (f[2] & TR2_RES_CHAOS));
			found = !!(f[cases[i].category] & cases[i].flag);
		}
		assert(found);
	}
	puts("Physical runes: all slots, artifacts, consumption, uniqueness and +10 flag pools passed.");
}
