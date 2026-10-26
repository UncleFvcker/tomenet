/* Native generation, legacy golden data and item serialization. */
#define SERVER
#include "angband.h"
#include <assert.h>
#include <stdint.h>

extern u32b fake_name_size, fake_text_size;
void test_write_cursed_item(FILE *file, object_type *item);
bool test_read_cursed_item(FILE *file, object_type *item);
bool test_read_legacy_item(FILE *file, object_type *item);
void test_item_metadata(bool enabled);
void test_attack_monster(int Ind);
void test_attack_player(int Ind);
void test_monster_cave(cave_type **cave);
bool __real_mon_take_hit(int Ind, int idx, int damage, bool *fear, cptr note);
void __real_take_hit(int Ind, int damage, cptr reason, int attacker);

static bool testing_attack;
static int captured_damage;
static object_type item_for(int kind, u32b seed, int version);
static int new_traits(artifact_type *a, object_kind *k);
bool __wrap_mon_take_hit(int Ind, int idx, int damage, bool *fear, cptr note) {
    if (!testing_attack) return __real_mon_take_hit(Ind, idx, damage, fear, note);
    captured_damage = damage; *fear = FALSE; return FALSE;
}
void __wrap_take_hit(int Ind, int damage, cptr reason, int attacker) {
    if (!testing_attack) { __real_take_hit(Ind, damage, reason, attacker); return; }
    captured_damage = damage;
}

static int attack_damage(bool pvp, int raw_blows, bool admin) {
    static player_type attacker, victim;
    static object_type gear[INVEN_TOTAL], other_gear[INVEN_TOTAL];
    static cave_type cells[2], *rows[] = {cells};
    static monster_type monsters[2];
    static monster_race races[MAX_R_IDX];
    static player_type *players[3] = {NULL, &attacker, &victim};
    memset(&attacker, 0, sizeof(attacker)); memset(&victim, 0, sizeof(victim));
    memset(gear, 0, sizeof(gear)); memset(other_gear, 0, sizeof(other_gear));
    memset(monsters, 0, sizeof(monsters)); memset(races, 0, sizeof(races));
    memset(cells, 0, sizeof(cells));
    Players = players; NumPlayers = 2; m_list = monsters; r_info = races; r_name = "\0Target";
    attacker.inventory = gear; victim.inventory = other_gear;
    attacker.id = 123; victim.id = 124;
    attacker.screen_hgt = victim.screen_hgt = 24; attacker.screen_wid = victim.screen_wid = 80;
    attacker.cur_hgt = victim.cur_hgt = 24; attacker.cur_wid = victim.cur_wid = 80;
    attacker.cp_ptr = victim.cp_ptr = &class_info[CLASS_WARRIOR];
    attacker.rp_ptr = victim.rp_ptr = &race_info[RACE_HUMAN];
    attacker.lev = victim.lev = 50; attacker.mhp = attacker.chp = victim.mhp = victim.chp = 30000;
    attacker.num_blow = 20; attacker.melee_blows = raw_blows; attacker.piercing = 1000;
    attacker.admin_dm = admin; attacker.skill_thn = 100; attacker.mon_vis[1] = TRUE;
    strcpy(attacker.name, "Tester"); strcpy(victim.name, "Other");
    int kind = 0;
    for (int i = 1; i < max_k_idx; i++) if (k_info[i].tval == TV_SWORD && k_info[i].cost > 0 && !k_info[i].flags1) { kind = i; break; }
    assert(kind);
    gear[INVEN_WIELD] = (object_type){.k_idx = kind, .tval = TV_SWORD, .sval = k_info[kind].sval,
        .number = 1, .dd = 200, .ds = 1, .weight = 10};
    races[1].name = 1; races[1].level = 1; races[1].d_char = 'p'; monsters[1].r_idx = 1;
    monsters[1].hp = monsters[1].maxhp = 100000; monsters[1].fx = 1;
    cells[1].m_idx = pvp ? -2 : 1; cells[1].feat = FEAT_FLOOR;
    test_monster_cave(rows); captured_damage = 0; testing_attack = TRUE;
    Rand_quick = FALSE; Rand_state_init(98765);
    if (pvp) test_attack_player(1); else test_attack_monster(1);
    testing_attack = FALSE; test_monster_cave(NULL);
    return captured_damage;
}

static void check_equipped_ca(void) {
    int kind = 0;
    for (int i = 1; i < max_k_idx; i++) if (k_info[i].tval == TV_BOW && k_info[i].cost > 0) { kind = i; break; }
    assert(kind);
    object_kind original = k_info[kind];
    k_info[kind].flags1 |= TR1_SPEED | TR1_BLOWS | TR1_STR | TR1_BRAND_FIRE;
    k_info[kind].flags5 |= TR5_CRIT | TR5_VORPAL | TR5_CHAOTIC;
    object_type item; artifact_type a; bool found = FALSE;
    for (u32b seed = 1; seed < 100000 && !found; seed++) {
        item = item_for(kind, seed * 2654435761U, RANDART_CHAOS);
        a = *randart_make(&item);
        found = a.pval == 20 && new_traits(&a, &k_info[kind]) <= 3 &&
            !(a.flags3 & (TR3_WRAITH | TR3_CURSED | TR3_HEAVY_CURSE | TR3_PERMA_CURSE | TR3_AGGRAVATE | TR3_NO_MAGIC)) &&
            !(a.flags4 & (TR4_NEVER_BLOW | TR4_BLACK_BREATH | TR4_DG_CURSE));
    }
    assert(found);
    static char *names[] = {"Test"}; randart_names = names; num_randart_names = 1;
    static cave_type cells[2], *rows[] = {cells};
    test_monster_cave(rows);
    player_type *p = Players[1];
    for (int i = 0; i < C_ATTRIBUTES; i++) p->stat_cur[i] = p->stat_max[i] = 18;
    calc_boni(1);
    int base_speed = p->pspeed;
    item.pval = a.pval; item.to_h = a.to_h; item.to_d = a.to_d; item.to_a = a.to_a;
    item.ac = a.ac; item.dd = a.dd; item.ds = a.ds; item.weight = a.weight;
    p->inventory[INVEN_BOW] = item;
    calc_boni(1);
    assert(p->pspeed >= base_speed + 20 && p->xtra_crit >= 20);
    assert(p->num_blow == 20 && p->melee_blows > 20);
    assert((p->melee_flags5 & (TR5_VORPAL | TR5_CHAOTIC)) == (TR5_VORPAL | TR5_CHAOTIC));
    assert(p->slay_equip & TR1_BRAND_FIRE);
    p->inventory[INVEN_BOW] = (object_type){0};
    calc_boni(1);
    assert(p->num_blow < 20 && p->melee_blows == p->num_blow && !p->melee_flags5);
    k_info[kind] = original;
    test_monster_cave(NULL);
}

static object_type item_for(int kind, u32b seed, int version) {
    object_kind *k = &k_info[kind];
    return (object_type){.k_idx = kind, .tval = k->tval, .sval = k->sval, .number = 1,
        .name1 = ART_RANDART, .name3 = seed, .randart_version = version, .pval = k->pval,
        .to_h = k->to_h, .to_d = k->to_d, .to_a = k->to_a};
}

static int new_traits(artifact_type *a, object_kind *k) {
    return count_bits(a->flags1 & ~k->flags1) + count_bits(a->flags2 & ~k->flags2) +
        count_bits(a->flags3 & ~(k->flags3 | TR3_HIDE_TYPE | TR3_IGNORE_ACID | TR3_IGNORE_ELEC | TR3_IGNORE_FIRE | TR3_IGNORE_COLD)) +
        count_bits(a->flags4 & ~k->flags4) + count_bits(a->flags5 & ~(k->flags5 | TR5_IGNORE_WATER)) +
        count_bits(a->flags6 & ~k->flags6) + count_bits(a->esp & ~k->esp);
}

static void check_creation(void) {
    int boots = lookup_kind(TV_BOOTS, SV_PAIR_OF_SOFT_LEATHER_BOOTS);
    int arrows = lookup_kind(TV_ARROW, SV_AMMO_NORMAL);
    int armour = lookup_kind(TV_DRAG_ARMOR, SV_DRAGON_POWER);
    int chaos = 0; bool negative_ammo = FALSE;
    worldpos pos = {0};
    assert(boots && arrows && armour);
    Rand_state_init(6789);
    for (int i = 0; i < 2000; i++) {
        object_type item;
        invcopy(&item, i % 3 == 0 ? boots : i % 3 == 1 ? arrows : armour);
        apply_magic(&pos, &item, 100, TRUE, TRUE, TRUE, FALSE, RESF_FORCERANDART | RESF_NOTRUEART | RESF_LIFE);
        assert(item.name1 == ART_RANDART && item.randart_version != RANDART_LEGACY);
        artifact_type a = *randart_make(&item);
        if (!chaos_artifact_p(&item)) {
            assert(item.randart_version == RANDART_NORMAL);
            assert(!(a.flags3 & TR3_AGGRAVATE) || (a.flags3 & TR3_CURSED) || (k_info[item.k_idx].flags3 & TR3_AGGRAVATE));
            continue;
        }
        chaos++;
        assert(item.pval == a.pval && item.to_h == a.to_h && item.to_d == a.to_d && item.to_a == a.to_a);
        u32b seed = item.name3;
        apply_magic(&pos, &item, 100, TRUE, TRUE, TRUE, FALSE, RESF_FORCERANDART | RESF_NOTRUEART | RESF_LIFE);
        assert(chaos_artifact_p(&item) && item.name3 == seed);
        assert(item.pval == a.pval && item.to_h == a.to_h && item.to_d == a.to_d && item.to_a == a.to_a);
        if (is_ammo(item.tval) && item.pval < 0) {
            negative_ammo = TRUE;
            assert(!ammo_explosive_p(&item));
            char powers[POW_INSCR_LEN] = "";
            power_inscribe(&item, FALSE, powers);
            do_arrow_explode(1, &item, NULL, 0, 0, 1);
            object_type loaded; FILE *saved = tmpfile(); assert(saved);
            test_write_cursed_item(saved, &item); rewind(saved);
            assert(test_read_cursed_item(saved, &loaded)); fclose(saved);
            assert(loaded.pval == item.pval && chaos_artifact_p(&loaded) && !ammo_explosive_p(&loaded));
        }
    }
    assert(chaos > 150 && chaos < 250 && negative_ammo);
    object_type normal = {.tval = TV_ARROW, .pval = GF_FIRE};
    assert(ammo_explosive_p(&normal));
    int activations = 0;
    for (int i = 1; i < max_k_idx; i++) {
        if (!(k_info[i].flags3 & TR3_ACTIVATE)) continue;
        object_type base, ca;
        invcopy(&base, i); ca = item_for(i, 12345, RANDART_CHAOS);
        if (!randart_make(&ca)) continue;
        assert(activation_requires_direction(&base) == activation_requires_direction(&ca));
        cptr plain = item_activation(&base), chaotic = item_activation(&ca);
#ifdef MSTAFF_MDEV_COMBO
        if (base.tval == TV_MSTAFF) { assert(!chaotic); continue; } /* Absorption cannot transform artifacts. */
#endif
        assert((!plain && !chaotic) || (plain && chaotic && !strcmp(plain, chaotic)));
        activations++;
    }
    assert(activations > 10);
    puts("Native apply_magic: fresh classification, fixed reapplication, CA ammo PVAL and persistence passed.");
}

static void check_legacy(void) {
    FILE *file = fopen("../tests/fixtures/randart-legacy.txt", "r");
    char line[512]; int cases = 0, removed = 0;
    assert(file);
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') continue;
        int tv, sv, forced; u32b seed; artifact_type e = {0};
        assert(sscanf(line, "%d %d %u %hd %hd %hd %hd %hd %hhu %hhu %hd %d %hhu %u %u %u %u %u %u %u %d",
            &tv, &sv, &seed, &e.pval, &e.to_h, &e.to_d, &e.to_a, &e.ac, &e.dd, &e.ds,
            &e.weight, &e.cost, &e.level, &e.flags1, &e.flags2, &e.flags3, &e.flags4, &e.flags5, &e.flags6, &e.esp, &forced) == 21);
        int kind = lookup_kind(tv, sv); assert(kind);
        object_type item = item_for(kind, seed, RANDART_LEGACY);
        artifact_type a = *randart_make(&item);
        e.tval = tv; e.sval = sv;
        assert(artifact_power(&a) == artifact_power(&e) && a.legacy_aggravation == !!forced);
        if (forced) { e.flags3 &= ~TR3_AGGRAVATE; removed++; }
        assert(a.pval == e.pval && a.to_h == e.to_h && a.to_d == e.to_d && a.to_a == e.to_a);
        assert(a.ac == e.ac && a.dd == e.dd && a.ds == e.ds && a.weight == e.weight);
        assert(a.cost == e.cost && a.level == e.level);
        assert(a.flags1 == e.flags1 && a.flags2 == e.flags2 && a.flags3 == e.flags3);
        assert(a.flags4 == e.flags4 && a.flags5 == e.flags5 && a.flags6 == e.flags6 && a.esp == e.esp);
        cases++;
    }
    fclose(file); assert(cases == 1184 && removed == 75);
    file = fopen("../tests/fixtures/item-4.9.26.bin", "rb"); assert(file);
    object_type loaded; assert(test_read_legacy_item(file, &loaded)); fclose(file);
    assert(loaded.randart_version == RANDART_LEGACY && loaded.name1 == ART_RANDART && loaded.name3 == 0x89abcdefU);
    artifact_type a = *randart_make(&loaded);
    assert(loaded.pval == a.pval && loaded.to_h == a.to_h && loaded.to_d == a.to_d && loaded.to_a == a.to_a);
    assert(loaded.owner == 123 && loaded.mode == MODE_EVERLASTING && loaded.level == 50);
    assert(loaded.sigil == 2 && loaded.sseed == 42 && !strcmp(loaded.f_name, "Legacy fixture"));
    assert(loaded.slain_monsters == 12345 && loaded.done_damage == 9876543210ULL && loaded.dummy1 == 0xdeadbeefU);
    puts("Legacy RA: 1184 exact field comparisons passed; only 75 forced aggravations removed.");
}

void check_chaos_artifacts(void) {
    test_item_metadata(FALSE);
    k_head = calloc(1, sizeof(*k_head));
    k_head->v_major = VERSION_MAJOR; k_head->v_minor = VERSION_MINOR; k_head->v_patch = VERSION_PATCH;
    k_head->info_num = MAX_K_IDX; k_head->info_len = sizeof(object_kind);
    k_info = calloc(MAX_K_IDX, sizeof(*k_info));
    fake_name_size = 256 * 1024; fake_text_size = 1024 * 1024;
    k_name = calloc(fake_name_size, 1); k_text = calloc(fake_text_size, 1);
    FILE *file = fopen("../lib/game/k_info.txt", "r"); char buffer[1024];
    assert(file && !init_k_info_txt(file, buffer)); fclose(file);
    check_legacy();

    int kind = lookup_kind(TV_BOOTS, SV_PAIR_OF_SOFT_LEATHER_BOOTS); assert(kind);
    object_kind *k = &k_info[kind];
    int pvals[31] = {0}, bonuses[3][61] = {{0}}, counts[31] = {0};
    bool zero = FALSE, negative = FALSE, overlapping = FALSE, strong = FALSE;
    for (u32b i = 1; i <= 100000; i++) {
        object_type item = item_for(kind, i * 2654435761U, RANDART_CHAOS);
        artifact_type a = *randart_make(&item), again = *randart_make(&item);
        assert(!memcmp(&a, &again, sizeof(a)));
        assert(a.pval >= -10 && a.pval <= 20);
        assert(a.to_h >= -10 && a.to_h <= 50 && a.to_d >= -10 && a.to_d <= 50 && a.to_a >= -10 && a.to_a <= 50);
        assert(a.ac == k->ac && a.dd == k->dd && a.ds == k->ds && a.weight == k->weight);
        assert((a.flags1 & k->flags1) == k->flags1 && (a.flags2 & k->flags2) == k->flags2);
        assert((a.flags3 & TR3_ACTIVATE) == (k->flags3 & TR3_ACTIVATE));
        int n = new_traits(&a, k); assert(n >= 1 && n <= 30); counts[n]++;
        pvals[a.pval + 10]++; bonuses[0][a.to_h + 10]++; bonuses[1][a.to_d + 10]++; bonuses[2][a.to_a + 10]++;
        zero |= a.pval == 0; negative |= a.pval == -10;
        overlapping |= (a.flags2 & (TR2_RES_FIRE | TR2_IM_FIRE)) == (TR2_RES_FIRE | TR2_IM_FIRE);
        strong |= a.pval == 20 && (a.flags1 & (TR1_BLOWS | TR1_LIFE)) == (TR1_BLOWS | TR1_LIFE);
        if (i <= 100) {
            item.pval = a.pval; item.to_h = a.to_h; item.to_d = a.to_d; item.to_a = a.to_a;
            item.ac = a.ac; item.dd = a.dd; item.ds = a.ds; item.weight = a.weight;
            object_type loaded; FILE *saved = tmpfile(); assert(saved);
            test_write_cursed_item(saved, &item); rewind(saved);
            assert(test_read_cursed_item(saved, &loaded)); fclose(saved);
            assert(loaded.randart_version == RANDART_CHAOS && loaded.name3 == item.name3);
            if (loaded.pval != item.pval || loaded.to_h != item.to_h || loaded.to_d != item.to_d || loaded.to_a != item.to_a)
                fprintf(stderr, "CA load seed=%u before=%d/%d/%d/%d after=%d/%d/%d/%d\n", item.name3,
                    item.pval, item.to_h, item.to_d, item.to_a, loaded.pval, loaded.to_h, loaded.to_d, loaded.to_a);
            assert(loaded.pval == item.pval && loaded.to_h == item.to_h && loaded.to_d == item.to_d && loaded.to_a == item.to_a);
            artifact_type restored = *randart_make(&loaded); assert(!memcmp(&a, &restored, sizeof(a)));
        }
    }
    assert(zero && negative && overlapping && strong);
    for (int i = 0; i < 31; i++) assert(pvals[i] > 2700 && pvals[i] < 3800);
    for (int i = 1; i <= 30; i++) assert(counts[i] > 2800 && counts[i] < 3900);
    for (int j = 0; j < 3; j++) for (int i = 0; i < 61; i++) assert(bonuses[j][i] > 1300 && bonuses[j][i] < 2000);
    Rand_state_init(12345); int chaos = 0;
    for (int i = 0; i < 20000; i++) { object_type item = {0}; randart_new(&item); chaos += chaos_artifact_p(&item); }
    assert(chaos > 1800 && chaos < 2200);
    player_type p = {0};
    for (int n = 0; n <= 40; n++) {
        p.melee_blows = n;
        assert(melee_overflow_damage(&p, 100) == (n <= 20 ? 100 : n * 5));
    }
    p.admin_dm = TRUE; p.melee_blows = 40; assert(melee_overflow_damage(&p, 100) == 100);
    p.admin_dm = FALSE; assert(melee_overflow_damage(&p, 0) == 0);
    assert(melee_overflow_damage(&p, 2000000000) == 2147483647);
    int normal = attack_damage(FALSE, 20, FALSE);
    assert(normal > 0);
    assert(attack_damage(FALSE, 25, FALSE) == normal * 25 / 20);
    assert(attack_damage(FALSE, 40, FALSE) == normal * 2);
    assert(attack_damage(FALSE, 40, TRUE) == normal);
    int duel = attack_damage(TRUE, 20, FALSE), duel_boost = attack_damage(TRUE, 40, FALSE);
    assert(duel > 0 && duel_boost >= duel * 2 - 1 && duel_boost <= duel * 2);
    check_equipped_ca();
    check_creation();
    puts("Chaos RA: 100000 generations, uniform rolls, fixed base, overlaps, zero/negative PVAL, item persistence and overflow damage passed.");
}
