/* Native vault commands and save records; only screen/network I/O is isolated. */
#define SERVER
#include "angband.h"
#include <assert.h>

static player_type visitors[2];
static object_type inventory[2][INVEN_TOTAL];
static object_kind kinds[2];
static artifact_type artifacts[MAX_A_IDX];
static ego_item_type egos[1];
static feature_type features[MAX_F_IDX];
static object_type floor_items[4];
static cave_type cell, *rows[] = {&cell};
static struct town_type towns[1];
static store_type shops[MAX_ST_IDX];
static byte colors[3][MAX_A_IDX];
static int quantities[3][MAX_A_IDX], updates[3];
static cptr inscriptions[32] = {""};
static bool active;

void home_sell(int Ind, int item, int amt);
void home_purchase(int Ind, int item, int amt);
void do_cmd_fixed_house(int Ind);
void test_inventory_cleanup(bool native);
void test_item_metadata(bool enabled);
void test_monster_cave(cave_type **cave);
void test_process_artifacts(void);
void test_write_vault_house(FILE *file, house_type *house);
bool test_read_vault_house(FILE *file, int slot);
void __real_object_desc(int Ind, char *buf, object_type *item, int pref, int mode);

int __wrap_Send_store(int Ind, s16b pos, byte attr, int weight, int number, int price,
    cptr name, byte tval, byte sval, s32b pval, char *powers) {
    if (active && pos < MAX_A_IDX) { colors[Ind][pos] = attr; quantities[Ind][pos] = number; }
    return 0;
}
int __wrap_Send_store_info(int Ind, int num, cptr store, cptr owner, int items, int purse, byte attr, char c) {
    if (active) updates[Ind]++;
    return 0;
}
void __wrap_show_building(int Ind, store_type *store) {}
int __wrap_Send_gold(int Ind, s32b gold, s32b balance) { return 0; }

static void give(int a_idx) {
    inventory[0][0] = (object_type){.k_idx = 1, .tval = TV_BOOTS, .sval = 1, .number = 1,
        .name1 = a_idx, .owner = 123, .mode = MODE_EVERLASTING, .level = 1,
        .ident = ID_KNOWN | ID_MENTAL, .pval = 10, .to_a = 17, .sigil = 6, .sseed = 12345};
    visitors[0].inven_cnt = 1;
    visitors[0].total_weight = inventory[0][0].weight;
}

static byte floor_color(void) {
    byte attr, back_attr; char32_t glyph, back_glyph;
#ifdef GRAPHICS_BG_MASK
    map_info(1, 0, 0, &attr, &glyph, &back_attr, &back_glyph, FALSE);
#else
    map_info(1, 0, 0, &attr, &glyph, FALSE);
#endif
    assert(glyph == ']');
    return attr;
}

void check_artifact_vault(void) {
	bool old_color = use_color;
    use_color = TRUE;
    player_type **old_players = Players; int old_count = NumPlayers;
    house_type *old_houses = houses, *old_bak = houses_bak;
    u32b old_alloc = house_alloc; int old_hcount = num_houses;
    player_type *players[] = {NULL, &visitors[0], &visitors[1]};
    Players = players; NumPlayers = 2; active = TRUE; test_inventory_cleanup(TRUE);
    test_item_metadata(TRUE);
    C_MAKE(houses, 64, house_type); C_MAKE(houses_bak, 64, house_type);
    house_alloc = 64; num_houses = 0;
    k_info = kinds; max_k_idx = 2; a_info = artifacts; max_a_idx = 289; e_info = egos;
    k_name = "\0Boots~"; a_name = "\0'Collection Test'"; e_name = "";
    quark__str = inscriptions; quark__num = 1;
    kinds[1] = (object_kind){.name = 1, .tval = TV_BOOTS, .sval = 1, .cost = 100};
    for (int i = 1; i < max_a_idx; i++)
        artifacts[i] = (artifact_type){.name = 1, .tval = TV_BOOTS, .sval = 1, .cost = 100, .cur_num = 1, .timeout = 42};
    town = towns; towns[0].townstore = shops;
    assert(!cfg.kings_etiquette && !cfg.fallenkings_etiquette);
    cfg.anti_arts_house = TRUE; cfg.persistent_artifacts = FALSE; cfg.fps = 60;
    for (int i = 0; i < 2; i++) {
        player_type *p = players[i + 1];
        p->inventory = inventory[i]; p->id = 123 + i; p->lev = p->max_plv = 50; p->max_exp = 1;
        p->rp_ptr = &race_info[0]; p->cp_ptr = &class_info[0];
        p->obj_aware[1] = TRUE; p->mode = MODE_EVERLASTING;
        p->using_up_item = p->item_newest = p->store_num = -1;
        p->version.major = 4; p->version.minor = 9; p->version.patch = 6; p->version.build = 2;
        do_cmd_fixed_house(i + 1);
        assert(is_fixed_house_session(p));
        store_exec_command(i + 1, STORE_ACTION_VAULT_TOGGLE, 0, 0, 0, 0);
    }
    assert(num_houses == 2 && quantities[1][288] == 0 && colors[1][288] == TERM_SLATE);
    give(288); object_type original = inventory[0][0];
    home_sell(1, 0, 2); assert(inventory[0][0].k_idx && !artifact_in_vault(288));
    home_sell(1, 0, 1);
    assert(!inventory[0][0].k_idx && artifact_in_vault(288));
    assert(quantities[1][288] == 1 && quantities[2][288] == 1);
    assert(colors[1][288] == TERM_TRUE_ARTIFACT && artifacts[288].cur_num == 1);
    assert(houses[1].stock_num == 1 && houses[1].stock[0].sigil == original.sigil);
    assert(houses[1].stock[0].sseed == original.sseed && houses[1].stock[0].pval == 10);
    turn = 288; test_process_artifacts(); assert(artifacts[288].timeout == 42);
    FILE *file = tmpfile(); assert(file);
    test_write_vault_house(file, &houses[1]); rewind(file);
    assert(test_read_vault_house(file, 2)); fclose(file);
    assert(!strcmp(houses[2].tag, "artifact_vault") && houses[2].stock_num == 1);
    assert(houses[2].stock[0].name1 == 288 && houses[2].stock[0].sigil == 6);
    for (int i = 0; i < INVEN_PACK; i++) inventory[0][i] = (object_type){.k_idx = 1, .tval = TV_BOOTS, .number = 1};
    visitors[0].inven_cnt = INVEN_PACK;
    home_purchase(1, 288, 1); assert(artifact_in_vault(288));
    memset(inventory[0], 0, sizeof(inventory[0])); visitors[0].inven_cnt = 0;
    visitors[0].total_winner = TRUE;
    home_purchase(1, 288, 1);
    assert(inventory[0][0].name1 == 288 && inventory[0][0].pval == 10 && !artifact_in_vault(288));
    assert(artifacts[288].carrier == 123);
    home_purchase(2, 288, 1); assert(!inventory[1][0].k_idx);
    assert(!quantities[1][288] && !quantities[2][288] && colors[1][288] == TERM_SLATE);
    test_process_artifacts(); assert(artifacts[288].timeout == 41);
    assert(get_attr_from_tval(&inventory[0][0]) == TERM_TRUE_ARTIFACT);
    inventory[0][0].name1 = ART_RANDART;
    assert(get_attr_from_tval(&inventory[0][0]) == TERM_RANDOM_ARTIFACT);
    inventory[0][0].name1 = 288;
    char name[ONAME_LEN]; object_type description_item = inventory[0][0];
    description_item.sigil = 0; description_item.owner = 0;
    __real_object_desc(1, name, &description_item, TRUE, 3);
    assert(strstr(name, "[TA]") && strstr(name, "\377!"));
    __real_object_desc(0, name, &inventory[0][0], TRUE, 256);
    assert(!strstr(name, "[TA]") && !strchr(name, '\377'));
    /* Visible artifacts buried in a pile retain their color even in water. */
    cave_type **old_surface = wild_info[0][0].surface.cave;
    wild_info[0][0].surface.cave = rows;
    f_info = features; o_list = floor_items; o_max = 4; test_monster_cave(rows);
    kinds[1].d_char = ']';
    features[FEAT_SHAL_WATER].flags1 = FF1_FLOOR | FF1_LOS;
    cell.feat = FEAT_SHAL_WATER; cell.o_idx = 1;
    visitors[0].py = visitors[0].px = 1; visitors[0].ascii_items = TRUE;
    for (int i = 1; i <= 3; i++) {
        floor_items[i] = (object_type){.k_idx = 1, .tval = TV_BOOTS, .number = 1,
            .next_o_idx = i < 3 ? i + 1 : 0, .name1 = i == 2 ? ART_RANDART : i == 3 ? 288 : 0};
        visitors[0].obj_vis[i] = i < 3;
    }
    assert(floor_color() == TERM_RANDOM_ARTIFACT);
    visitors[0].obj_vis[3] = TRUE; assert(floor_color() == TERM_TRUE_ARTIFACT);
    visitors[0].version.build = 0; assert(floor_color() == TERM_YELLOW);
    visitors[0].version.build = 2; test_monster_cave(NULL);
    wild_info[0][0].surface.cave = old_surface;
    /* Existing vault items move intact when the vault next opens. */
    houses[0].stock[0] = original; houses[0].stock_num = 1;
    do_cmd_fixed_house(1); assert(!houses[0].stock_num && artifact_in_vault(288));
    visitors[0].wpos.wz = -1; int previous = visitors[0].store_action;
    store_exec_command(1, STORE_ACTION_VAULT_TOGGLE, 0, 0, 0, 0);
    assert(visitors[0].store_action == previous);
    assert(erase_or_locate_artifact(288, FALSE) && artifact_in_vault(288));
    assert(erase_or_locate_artifact(288, TRUE) && !artifact_in_vault(288));
    assert(!artifacts[288].cur_num);
    /* Legitimate duplicate artifacts remain separate physical items. */
    visitors[0].wpos.wz = 0;
    give(ART_GROND); home_sell(1, 0, 1);
    give(ART_GROND); inventory[0][0].to_a = 18; home_sell(1, 0, 1);
    assert(houses[1].stock_num == 2);
    store_exec_command(2, STORE_ACTION_VAULT_TOGGLE, 0, 0, 0, 0);
    store_exec_command(2, STORE_ACTION_VAULT_TOGGLE, 0, 0, 0, 0);
    home_purchase(2, ART_GROND, 1); assert(artifact_in_vault(ART_GROND));
    assert(inventory[1][0].name1 == ART_GROND && inventory[1][0].owner == 124);
    for (int i = 0; i < 3; i++) { FREE(houses[i].stock, object_type); FREE(houses[i].dna, struct dna_type); }
    FREE(houses, house_type); FREE(houses_bak, house_type);
    houses = old_houses; houses_bak = old_bak; house_alloc = old_alloc; num_houses = old_hcount;
    Players = old_players; NumPlayers = old_count; active = FALSE; use_color = old_color; test_inventory_cleanup(FALSE);
    test_item_metadata(FALSE);
    puts("Artifact vault: stable catalog IDs, shared transfers, capacity, legacy migration, item/house persistence and paused lifetime passed.");
}
