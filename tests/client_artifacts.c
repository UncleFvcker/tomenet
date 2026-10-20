/* Native text parsing, terminal repaint and animated-color conversion without a Windows UI. */
#include "angband.h"
#include <assert.h>

static unsigned gold, purple;
static int missing_repaints;
extern byte term2attr(byte attr);
void test_client_buffers(void);
bool test_vault_request(bool toggle);
void test_catalog_packets(void);
void test_store_key(int key);

static errr text(int x, int y, int n, byte attr, cptr str) {
    byte color = term2attr(attr);
    if (y == 0) gold |= 1u << color;
    if (y == 1) purple |= 1u << color;
    if (y == 2) missing_repaints++;
    return 0;
}

int main(void) {
    term screen;
    memset(&screen, 0, sizeof(screen));
    term_init(&screen, 80, 24, 256); screen.text_hook = text;
    ang_term[0] = &screen; Term_activate(&screen);
    server_version = (version_type){.major = 4, .minor = 9, .patch = 6, .build = 2};
    c_cfg.no_flicker = FALSE; Rand_quick = TRUE; Rand_value = 12345;
    Term_putstr(0, 0, -1, TERM_WHITE, "\377:[TA] Gold");
    Term_putstr(0, 1, -1, TERM_WHITE, "\377;[RA] Purple");
    Term_putstr(0, 2, -1, TERM_SLATE, "[Missing]");
    assert(screen.scr->a[0][0] == TERM_TRUE_ARTIFACT);
    assert(screen.scr->a[1][0] == TERM_RANDOM_ARTIFACT);
    for (int i = 0; i < 40; i++) flicker();
    assert(gold == ((1u << TERM_YELLOW) | (1u << TERM_L_UMBER)));
    assert(purple == ((1u << TERM_VIOLET) | (1u << TERM_L_WHITE)));
    assert(!missing_repaints);
    c_cfg.no_flicker = TRUE;
    assert(term2attr(TERM_TRUE_ARTIFACT) == TERM_YELLOW);
    assert(term2attr(TERM_RANDOM_ARTIFACT) == TERM_VIOLET);
    assert(color_char_to_attr(color_attr_to_char(TERM_TRUE_ARTIFACT)) == TERM_TRUE_ARTIFACT);
    assert(color_char_to_attr(color_attr_to_char(TERM_RANDOM_ARTIFACT)) == TERM_RANDOM_ARTIFACT);
    test_client_buffers();
    Term_keypress('v'); cmd_party();
    assert(test_vault_request(FALSE));
    C_MAKE(store.stock, STORE_INVEN_MAX, object_type);
    shopping = TRUE; store_num = STORE_HOME;
    strcpy(c_store.owner_name, "Shared Vault"); strcpy(c_store.store_name, "Items");
    test_store_key('A'); assert(test_vault_request(TRUE));
    test_catalog_packets(); do_redraw_store();
    assert(store.stock_num == 289 && store.stock[288].number == 1);
    assert(store.stock[287].attr == TERM_SLATE && store.stock[288].attr == TERM_TRUE_ARTIFACT);
    assert(strstr(c_store.store_name, "1/288"));
    c_cfg.auto_inscr_off = FALSE;
    strcpy(auto_inscription_match[0], "$^The Boots");
    strcpy(auto_inscription_tag[0], "!G7");
    char artifact_name[] = "[TA] \377:The Boots 'Test'\377. {1}";
    char random_name[] = "[RA] \377;The Boots 'Test'\377. {1}";
    assert(scan_auto_inscriptions_for_limit(artifact_name) == 7);
    assert(scan_auto_inscriptions_for_limit(random_name) == 7);
    inventory[0].tval = TV_BOOTS; strcpy(inventory_name[0], artifact_name);
    assert(apply_auto_inscriptions_aux(0, 0, TRUE));
    FREE(store.stock, object_type);
    term_nuke(&screen);
    puts("Client: P/v entry, A page command, 16-bit catalog packets, terminal rendering, idle animation and no-flicker mapping passed.");
    return 0;
}
