/* Exercise the actual potion effect, including the skill undo snapshot. */
#define SERVER
#include "angband.h"
#include <assert.h>

void __wrap_msg_format(int Ind, cptr fmt, ...) {}

void check_skill_potion(void) {
	player_type *p = Players[1];
	memset(p, 0, sizeof(*p));
	p->skill_points = 7;
	p->skill_points_old = 10;
	p->reskill_possible = RESKILL_F_UNDO;
	for (int i = 0; i < 10; i++) {
		assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
		assert(p->skill_points == 8 + i && p->skill_points_old == 11 + i);
		assert(p->update & PU_SKILL_MOD);
		assert(p->redraw & PR_STUDY);
		assert(!bypass_invuln);
	}
	/* Spending then undoing preserves every potion point. */
	p->skill_points--;
	assert(p->skill_points_old - p->skill_points == 4);
	p->skill_points = p->skill_points_old;
	assert(p->skill_points == 20);
	/* No undo snapshot, and a current client. */
	p->reskill_possible = 0;
	p->version.major = 5;
	p->redraw = p->update = 0;
	assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
	assert(p->skill_points == 21 && p->skill_points_old == 20);
	assert(p->update & PU_SKILL_MOD);
	assert(!(p->redraw & PR_STUDY));
	/* Never wrap the signed skill-point counters into negative values. */
	p->skill_points = MAX_SHORT - 1;
	assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
	assert(p->skill_points == MAX_SHORT);
	assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
	assert(p->skill_points == MAX_SHORT);
	p->skill_points = 7;
	p->skill_points_old = MAX_SHORT;
	p->reskill_possible = RESKILL_F_UNDO;
	assert(quaff_potion(1, TV_POTION2, SV_POTION2_SKILL, 0));
	assert(p->skill_points == 7 && p->skill_points_old == MAX_SHORT);
	puts("Skill potion checks passed.");
}
