/* solitaire.c - Klondike solitaire, compact cursor version.
 *
 * Build:  tcc solitaire.c -o solitaire.exe
 *
 * Controls:
 *   Arrows       move cursor (wraps at edges)
 *   Space        select source / select destination / cancel
 *   A            auto-move any legal cards to foundations (cascades)
 *   N            new game (prompts y/n in the bottom banner)
 *   Esc or Q     quit
 *
 * Cursor:  red '<' = selected source, yellow '<' = current position.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tui.h"
#include "fable.h"

/* ---------------- geometry ---------------- */
#define POP_X    15
#define POP_Y     1
#define POP_W     41
#define POP_H     22
#define GOY       (POP_Y + 3)

#define COL_SP     6
#define COL_COUNT  7
#define FDN_COUNT  4
#define MAX_STACK 14


/* ---------------- cards ---------------- */
enum { SUIT_HEART, SUIT_SPADE, SUIT_CLUB, SUIT_DIAMOND, SUIT_COUNT };
enum { RANK_A, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7,
       RANK_8, RANK_9, RANK_10, RANK_J, RANK_Q, RANK_K, RANK_COUNT };

typedef struct card {
    unsigned char suit, rank, revealed;
} card;

enum {
    P_DECK, P_WASTE,
    P_FDN1, P_FDN2, P_FDN3, P_FDN4,
    P_COL1, P_COL2, P_COL3, P_COL4, P_COL5, P_COL6, P_COL7,
    PILE_COUNT
};

typedef struct card_node {
    card *value;
    struct card_node *next;
} card_node;

typedef struct pile {
    card_node *head;
    int num_cards;
    char type;
} pile;

static pile *piles[PILE_COUNT];
static int  score;
/* ---------------- felt themes ---------------- */
typedef struct { int fg, bg, ch, on; const char *name; } felt_variant;

static const felt_variant felt_variants[] = {
    { TUI_BLACK,  TUI_BLACK, ' ',  0, "Classic"     },
    { TUI_GREEN,  TUI_BLACK, 0xB0, 1, "Oval Room"   },
    { TUI_LGREEN, TUI_BLACK, 0xB0, 1, "Pale Powder" },
    { TUI_DGRAY,  TUI_BLACK, 0xB0, 1, "French Gray"     },
    { TUI_CYAN,   TUI_BLUE,  0xB0, 1, "Skylight"      },
    { TUI_BROWN,  TUI_BLACK, 0xB0, 1, "Elephant's Breath"},
    { TUI_MAGENTA, TUI_BLUE, 0xB0, 1, "Magenta Wonderland"  },
};
#define FELT_COUNT ((int)(sizeof(felt_variants) / sizeof(felt_variants[0])))

static int felt_idx = 0;   /* 0 = off, current appearance */

/* ---------------- slots ---------------- */
#define SLOT_STOCK   0
#define SLOT_WASTE   1
#define SLOT_FDN1    2
#define SLOT_COL1    6
#define SLOT_COUNT   13

static int slot_to_pile(int slot)
{
    if (slot == SLOT_STOCK) return P_DECK;
    if (slot == SLOT_WASTE) return P_WASTE;
    if (slot >= SLOT_FDN1 && slot < SLOT_FDN1 + FDN_COUNT)
        return P_FDN1 + (slot - SLOT_FDN1);
    if (slot >= SLOT_COL1 && slot < SLOT_COL1 + COL_COUNT)
        return P_COL1 + (slot - SLOT_COL1);
    return -1;
}

static int slot_x(int slot)
{
    if (slot == SLOT_STOCK) return 0;
    if (slot == SLOT_WASTE) return COL_SP;
    if (slot >= SLOT_FDN1 && slot < SLOT_FDN1 + FDN_COUNT)
        return (3 + (slot - SLOT_FDN1)) * COL_SP;
    if (slot >= SLOT_COL1 && slot < SLOT_COL1 + COL_COUNT)
        return (slot - SLOT_COL1) * COL_SP;
    return 0;
}

/* ---------------- memory ---------------- */
static void *allocz(size_t n) { return calloc(1, n); }

/* ---------------- suit / rank ---------------- */
static const char *suit_ch(int suit)
{
    switch (suit) {
    case SUIT_HEART:   return "H";
    case SUIT_SPADE:   return "S";
    case SUIT_CLUB:    return "C";
    case SUIT_DIAMOND: return "D";
    default:           return "?";
    }
}

static const char *rank_ch(int rank)
{
    static const char *r[] = { "A","2","3","4","5","6","7","8","9","T",
                               "J","Q","K" };
    if (rank < 0 || rank >= RANK_COUNT) return "?";
    return r[rank];
}

static int is_red_card(const card *c)
{
    return c->suit == SUIT_HEART || c->suit == SUIT_DIAMOND;
}
static int is_black_card(const card *c)
{
    return c->suit == SUIT_CLUB || c->suit == SUIT_SPADE;
}
static int is_alternate(const card *a, const card *b)
{
    return is_black_card(a) != is_black_card(b);
}
static int is_in_sequence(const card *lower, const card *higher)
{
    return higher->rank == lower->rank + 1;
}
static int can_place_on_column(const card *parent, const card *child)
{
    return is_alternate(parent, child) && is_in_sequence(child, parent);
}
static int can_place_on_foundation(const card *parent, const card *child)
{
    return parent->suit == child->suit && is_in_sequence(parent, child);
}

/* ---------------- pile primitives ---------------- */
static card_node *make_node(card *c)
{
    card_node *n = (card_node *)allocz(sizeof(card_node));
    if (n) n->value = c;
    return n;
}
static int is_empty(pile *p) { return p->num_cards == 0; }

static void push(pile *p, card *c)
{
    card_node *t = p->head;
    if (!t) p->head = make_node(c);
    else {
        while (t->next) t = t->next;
        t->next = make_node(c);
    }
    p->num_cards++;
}

static card *shift(pile *p)
{
    card_node *h = p->head;
    card *c;
    if (!h) return NULL;
    p->head = h->next;
    c = h->value;
    free(h);
    p->num_cards--;
    return c;
}

static card *peek_at(pile *p, int idx)
{
    card_node *h = p->head;
    int i;
    if (!p || !h) return NULL;
    for (i = 0; i < idx && h; i++) h = h->next;
    return h ? h->value : NULL;
}
static card *peek_last(pile *p)
{
    if (!p || p->num_cards == 0) return NULL;
    return peek_at(p, p->num_cards - 1);
}

static void delete_card(pile *p, card *c)
{
    card_node *prev = NULL, *cur;
    if (is_empty(p)) return;
    for (cur = p->head; cur; prev = cur, cur = cur->next) {
        if (cur->value == c) {
            if (!prev) p->head = cur->next;
            else       prev->next = cur->next;
            p->num_cards--;
            free(cur);
            return;
        }
    }
}

static void insert_at(pile *p, card *c, int idx)
{
    card_node *cur = p->head, *n;
    int i;
    if (idx <= 0 || !cur) {
        n = make_node(c);
        n->next = p->head;
        p->head = n;
        p->num_cards++;
        return;
    }
    for (i = 0; i < idx - 1 && cur->next; i++) cur = cur->next;
    n = make_node(c);
    n->next = cur->next;
    cur->next = n;
    p->num_cards++;
}

static pile *make_pile(char type)
{
    pile *p = (pile *)allocz(sizeof(pile));
    if (p) p->type = type;
    return p;
}

/* ---------------- deck / deal / turn ---------------- */
static void fill_deck(pile *p)
{
    int r, s;
    for (r = 0; r < RANK_COUNT; r++)
        for (s = 0; s < SUIT_COUNT; s++) {
            card *c = (card *)allocz(sizeof(card));
            if (!c) return;
            c->suit = (unsigned char)s;
            c->rank = (unsigned char)r;
            c->revealed = 0;
            push(p, c);
        }
}

static void shuffle_pile(pile *p)
{
    int n = p->num_cards * 10, i;
    for (i = 0; i < n; i++) {
        int idx = (p->num_cards > 1) ? rand() % (p->num_cards - 1) : 0;
        card *c = shift(p);
        insert_at(p, c, idx);
    }
}

static void deal(void)
{
    int i, k;
    for (i = 0; i < COL_COUNT; i++) {
        pile *col = piles[P_COL1 + i];
        for (k = 0; k <= i; k++) {
            card *c = shift(piles[P_DECK]);
            if (c) {
                push(col, c);
                if (k == i) c->revealed = 1;
            }
        }
    }
    {
        card *c = shift(piles[P_DECK]);
        if (c) { c->revealed = 1; push(piles[P_WASTE], c); }
    }
}

static void turn(void)
{
    card *c = shift(piles[P_DECK]);
    if (!c) return;
    c->revealed = 1;
    push(piles[P_WASTE], c);
}

static void redeal(void)
{
    while (!is_empty(piles[P_WASTE])) {
        card *c = shift(piles[P_WASTE]);
        c->revealed = 0;
        push(piles[P_DECK], c);
    }
    score -= 100;
    if (score < 0) score = 0;
}

/* ---------------- scoring ---------------- */
static void add_score(int delta)
{
    score += delta;
    if (score < 0) score = 0;
}

/* ---------------- move logic ---------------- */
static int column_run_start(pile *sp)
{
    int i = sp->num_cards - 1;
    while (i > 0) {
        card *lower = peek_at(sp, i);
        card *higher = peek_at(sp, i - 1);
        if (!lower || !higher) break;
        if (!lower->revealed || !higher->revealed) break;
        if (!is_alternate(lower, higher)) break;
        if (!is_in_sequence(lower, higher)) break;
        i--;
    }
    return i;
}

static int pick_amount(pile *sp, pile *dp, int dst_is_fdn, int forced_index)
{
    if (sp->num_cards == 0) return 0;

    if (forced_index >= 0) {
        card *bottom;
        if (forced_index >= sp->num_cards) return 0;
        bottom = peek_at(sp, forced_index);
        if (!bottom || !bottom->revealed) return 0;
        if (dst_is_fdn) {
            if (forced_index != sp->num_cards - 1) return 0;
            {
                card *top = peek_last(dp);
                if (!top) return (bottom->rank == RANK_A) ? 1 : 0;
                return can_place_on_foundation(top, bottom) ? 1 : 0;
            }
        } else {
            card *dst_top = peek_last(dp);
            if (!dst_top)
                return (bottom->rank == RANK_K)
                     ? sp->num_cards - forced_index : 0;
            return can_place_on_column(dst_top, bottom)
                 ? sp->num_cards - forced_index : 0;
        }
    }

    if (dst_is_fdn) {
        card *top = peek_last(dp);
        card *sc  = peek_last(sp);
        if (!sc || !sc->revealed) return 0;
        if (!top) return (sc->rank == RANK_A) ? 1 : 0;
        return can_place_on_foundation(top, sc) ? 1 : 0;
    }
    {
        int start = column_run_start(sp);
        int run_len = sp->num_cards - start;
        card *run_head = peek_at(sp, start);
        card *dst_top  = peek_last(dp);

        if (run_len > 0 && run_head && run_head->revealed) {
            if (!dst_top) {
                if (run_head->rank == RANK_K) return run_len;
            } else if (can_place_on_column(dst_top, run_head)) {
                return run_len;
            }
        }
        {
            card *top = peek_last(sp);
            if (top && top->revealed) {
                if (!dst_top) {
                    if (top->rank == RANK_K) return 1;
                } else if (can_place_on_column(dst_top, top)) {
                    return 1;
                }
            }
        }
        return 0;
    }
}

static void move_cards(pile *sp, pile *dp, int amount)
{
    int first_idx = sp->num_cards - amount;
    int i;
    for (i = 0; i < amount; i++) {
        card *c = peek_at(sp, first_idx);
        if (!c) break;
        delete_card(sp, c);
        push(dp, c);
    }
    {
        card *next = peek_last(sp);
        if (next && !next->revealed) { next->revealed = 1; add_score(5); }
    }
    if (dp->type == 'f') add_score(10);
    if (sp->type == 'w' && dp->type == 'c') add_score(5);
}

static const char *try_move(int src_slot, int dst_slot, int src_index)
{
    int spi = slot_to_pile(src_slot);
    int dpi = slot_to_pile(dst_slot);
    pile *sp, *dp;
    int amount, dst_is_fdn;

    if (spi < 0 || dpi < 0) return "Invalid slot";

    /* stock is a source-only pile: you draw from it, you never put
     * cards back onto it (except via redeal, which is internal) */
    if (dpi == P_DECK && spi != P_DECK) return "Cannot move to stock";

    sp = piles[spi];
    dp = piles[dpi];

    if (spi == P_DECK) {
        if (is_empty(piles[P_DECK])) {
            if (is_empty(piles[P_WASTE])) return "Cannot redeal";
            redeal();
            return "Redealt";
        }
        turn();
        return "Drew card";
    }

    if (is_empty(sp)) return "Source empty";
    dst_is_fdn = (dp->type == 'f');

    amount = pick_amount(sp, dp, dst_is_fdn, src_index);
    if (amount == 0) return "Invalid move";

    move_cards(sp, dp, amount);
    return "OK";
}

static int auto_foundation(void)
{
    int moved = 0;
    int progress = 1;

    while (progress) {
        int i;
        progress = 0;

        {
            card *c = peek_last(piles[P_WASTE]);
            if (c && c->revealed) {
                for (i = 0; i < FDN_COUNT; i++) {
                    pile *f = piles[P_FDN1 + i];
                    card *top = peek_last(f);
                    int ok = top ? can_place_on_foundation(top, c)
                                 : (c->rank == RANK_A);
                    if (ok) {
                        move_cards(piles[P_WASTE], f, 1);
                        moved++;
                        progress = 1;
                        break;
                    }
                }
            }
        }
        if (progress) continue;

        for (i = 0; i < COL_COUNT; i++) {
            pile *col = piles[P_COL1 + i];
            card *c = peek_last(col);
            int j;
            if (!c || !c->revealed) continue;
            for (j = 0; j < FDN_COUNT; j++) {
                pile *f = piles[P_FDN1 + j];
                card *top = peek_last(f);
                int ok = top ? can_place_on_foundation(top, c)
                             : (c->rank == RANK_A);
                if (ok) {
                    move_cards(col, f, 1);
                    moved++;
                    progress = 1;
                    break;
                }
            }
            if (progress) break;
        }
    }

    return moved;
}

/* Win = every foundation has 13 cards */
static int game_won(void)
{
    int i;
    for (i = 0; i < FDN_COUNT; i++)
        if (piles[P_FDN1 + i]->num_cards != 13) return 0;
    return 1;
}

/* ---------------- rendering ---------------- */
static void put_at(int x, int y, const char *text, int fg, int bg)
{
    tui_gotoxy(POP_X + 1 + x, GOY + y);
    tui_color(fg, bg);
    tui_puts(text);
}

static void clear_region(int x, int y, int w, int h, int bg)
{
    int i, j;
    tui_color(TUI_BLACK, bg);
    for (j = 0; j < h; j++) {
        tui_gotoxy(POP_X + 1 + x, GOY + y + j);
        for (i = 0; i < w; i++) tui_putc(' ');
    }
}

static void draw_felt(int x, int y, int w, int h)
{
    int i, j;
    const felt_variant *f = &felt_variants[felt_idx];
    tui_color(f->fg, f->bg);
    for (j = 0; j < h; j++) {
        tui_gotoxy(POP_X + 1 + x, GOY + y + j);
        for (i = 0; i < w; i++)
            tui_putc(f->on ? f->ch : ' ');
    }
}
static void draw_frame(void)
{
    int i;

    tui_color(TUI_WHITE, TUI_BLACK);

    tui_gotoxy(POP_X, POP_Y);
    tui_putc(0xDA);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xBF);

    tui_gotoxy(POP_X, POP_Y + 1);
    tui_putc(0xB3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, POP_Y + 1);
    tui_puts("SOLITAIRE");
    tui_gotoxy(POP_X + POP_W - 1, POP_Y + 1);
    tui_putc(0xB3);

    tui_gotoxy(POP_X, POP_Y + 2);
    tui_putc(0xC3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xB4);

    for (i = 0; i < POP_H - 4; i++) {
        tui_gotoxy(POP_X, GOY + i);
        tui_putc(0xB3);
        tui_gotoxy(POP_X + POP_W - 1, GOY + i);
        tui_putc(0xB3);
    }

    tui_gotoxy(POP_X, GOY + POP_H - 4);
    tui_putc(0xC3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xB4);

    tui_gotoxy(POP_X, GOY + POP_H - 3);
    tui_putc(0xB3);
    for (i = 0; i < POP_W - 2; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, GOY + POP_H - 3);
    tui_puts("Arrows  Space  A:auto  N:new  Esc");
    tui_gotoxy(POP_X + POP_W - 1, GOY + POP_H - 3);
    tui_putc(0xB3);

    tui_gotoxy(POP_X, GOY + POP_H - 2);
    tui_putc(0xC0);
    for (i = 0; i < POP_W - 2; i++) tui_putc(0xC4);
    tui_putc(0xD9);
}

static void draw_status(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "  S:%d", score);
    tui_gotoxy(POP_X + POP_W - 2 - (int)strlen(buf), POP_Y + 1);
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_puts(buf);
}

static void draw_card_at(int x, int y, card *c)
{
    char txt[4];
    const felt_variant *f = &felt_variants[felt_idx];

    if (!c) {
        put_at(x, y, "[]", TUI_DGRAY, f->bg);
        return;
    }
    if (!c->revealed) {
        put_at(x, y, "\xB1\xB1", TUI_CYAN, TUI_LBLUE);
        return;
    }
    snprintf(txt, sizeof txt, "%s%s", rank_ch(c->rank), suit_ch(c->suit));

    if (f->on)
        put_at(x, y, txt,
               is_red_card(c) ? TUI_LRED : TUI_BLACK,
               TUI_WHITE);
    else
        put_at(x, y, txt,
               is_red_card(c) ? TUI_LRED : TUI_WHITE,
               TUI_BLACK);
}

static void draw(void)
{
    int i;

    draw_felt(0, 0, POP_W - 2, POP_H - 4);

    draw_card_at(slot_x(SLOT_STOCK), 0, peek_last(piles[P_DECK]));
    draw_card_at(slot_x(SLOT_WASTE), 0, peek_last(piles[P_WASTE]));
    for (i = 0; i < FDN_COUNT; i++)
        draw_card_at(slot_x(SLOT_FDN1 + i), 0,
                     peek_last(piles[P_FDN1 + i]));

    for (i = 0; i < COL_COUNT; i++) {
        pile *p = piles[P_COL1 + i];
        int r;
        for (r = 0; r < p->num_cards && r < MAX_STACK; r++)
            draw_card_at(i * COL_SP, 2 + r, peek_at(p, r));
        if (p->num_cards == 0)
            draw_card_at(i * COL_SP, 2, NULL);
    }
}

static int last_cur_x = -1, last_cur_y = -1;
static int last_sel_x = -1, last_sel_y = -1;

static void paint_marker(int x, int y, int color, int *last_x, int *last_y)
{
    const felt_variant *f = &felt_variants[felt_idx];

    if (*last_x >= 0) {
        if (f->on)
            put_at(*last_x, *last_y, "\xB0", f->fg, f->bg);
        else
            put_at(*last_x, *last_y, " ", TUI_WHITE, TUI_BLACK);
    }
    if (x >= 0) {
        put_at(x, y, "<", color, f->bg);
    }
    *last_x = x;
    *last_y = y;
}

static void draw_cursor(int sel_row, int sel_col, int sel_depth,
                        int selected_slot, int selected_index)
{
    int cx = -1, cy = -1;
    int sx = -1, sy = -1;

    if (sel_row == 0) {
        cx = slot_x(sel_col) + 2;
        cy = 0;
    } else if (sel_depth >= 0) {
        cx = sel_col * COL_SP + 2;
        cy = 2 + sel_depth;
    } else {
        cx = sel_col * COL_SP + 2;
        cy = 2;
    }

    if (selected_slot >= 0) {
        if (selected_slot >= SLOT_COL1) {
            int cidx = selected_slot - SLOT_COL1;
            int row = (selected_index >= 0) ? (2 + selected_index) : 2;
            sx = cidx * COL_SP + 2;
            sy = row;
        } else {
            sx = slot_x(selected_slot) + 2;
            sy = 0;
        }
    }

    paint_marker(cx, cy, TUI_YELLOW, &last_cur_x, &last_cur_y);
    if (sx == cx && sy == cy) {
        paint_marker(-1, -1, 0, &last_sel_x, &last_sel_y);
    } else {
        paint_marker(sx, sy, TUI_LRED, &last_sel_x, &last_sel_y);
    }
}

static void draw_message(const char *msg)
{
    int i;
    tui_gotoxy(POP_X + 2, GOY + POP_H - 3);
    tui_color(TUI_WHITE, TUI_BLACK);
    for (i = 0; i < POP_W - 4; i++) tui_putc(' ');
    tui_gotoxy(POP_X + 2, GOY + POP_H - 3);
    tui_puts(msg);
    tui_gotoxy(POP_X, GOY + POP_H - 3);
    tui_color(TUI_WHITE, TUI_BLACK);
    tui_putc(0xB3);
    tui_gotoxy(POP_X + POP_W - 1, GOY + POP_H - 3);
    tui_putc(0xB3);
}

/* ---------------- setup / cleanup ---------------- */
static void prepare_game(void)
{
    int i;
    static const char types[] = "dwffffccccccc";
    for (i = 0; i < PILE_COUNT; i++)
        piles[i] = make_pile(types[i]);
    fill_deck(piles[P_DECK]);
    shuffle_pile(piles[P_DECK]);
    deal();
    score = 0;
}

static void free_game(void)
{
    int i;
    for (i = 0; i < PILE_COUNT; i++) {
        card_node *n = piles[i]->head;
        while (n) {
            card_node *nx = n->next;
            free(n->value);
            free(n);
            n = nx;
        }
        free(piles[i]);
        piles[i] = NULL;
    }
}

/* Clear the popup region (used both at start and on new game) */
static void clear_popup_region(void)
{
    int i;
    tui_color(TUI_BLACK, TUI_BLACK);
    for (i = 0; i < POP_H + 1; i++) {
        tui_gotoxy(POP_X, POP_Y + i);
        {
            int k;
            for (k = 0; k < POP_W; k++) tui_putc(' ');
        }
    }
}

/* Reset cursor marker tracking (positions from the old game are gone) */
static void reset_marker_tracking(void)
{
    last_cur_x = last_cur_y = -1;
    last_sel_x = last_sel_y = -1;
}

/* Prompt for y/n in the bottom banner. Returns 1 for yes, 0 for no. */
static int prompt_yes_no(const char *question)
{
    int c;
    draw_message(question);
    for (;;) {
        c = tui_getch();
        if (c == 'y' || c == 'Y') return 1;
        if (c == 'n' || c == 'N') return 0;
        if (c == 27) return 0;      /* Esc = no */
        if (c == 0 || c == 224) { tui_getch(); continue; }
    }
}

/* ---------------- entry ---------------- */
static int run_solitaire(void)
{
    int sel_row = 0;
    int sel_col = 0;
    int sel_depth = -1;
    int selected = -1;
    int selected_index = -1;
    int quit = 0;
    int won = 0;

    tui_cursor(0, 0);
    srand((unsigned)tui_millis());

    clear_popup_region();
    reset_marker_tracking();
    prepare_game();
    draw_frame();
    draw_status();
    draw();
    draw_message("Arrows move, Space selects");
    draw_cursor(sel_row, sel_col, sel_depth, selected, selected_index);

    while (!quit) {
        int c;

        /* if won, only N (new game) or Q/Esc work */
        if (won) {
            c = tui_getch();
            if (c == 'n' || c == 'N') {
                if (prompt_yes_no("New game? y/n")) {
                    free_game();
                    clear_popup_region();
                    reset_marker_tracking();
                    prepare_game();
                    score = 0;
                    sel_row = 0; sel_col = 0; sel_depth = -1;
                    selected = -1; selected_index = -1;
                    won = 0;
                    draw_frame();
                    draw_status();
                    draw();
                    draw_message("New game.");
                    draw_cursor(sel_row, sel_col, sel_depth,
                                selected, selected_index);
                } else {
                    draw_message("YOU WIN! N=new game, Q=quit");
                }
            } else if (c == 'q' || c == 'Q' || c == 27) {
                quit = 1;
            }
            continue;
        }

        c = tui_getch();

        if (c == 0 || c == 224) {
            int scan = tui_getch();
            if (scan == 72) {                       /* up */
                if (sel_row == 1 && sel_depth > 0) {
                    sel_depth--;                    /* walk up the stack */
                } else if (sel_row == 1 && sel_depth == 0) {
                    sel_depth = -1;                 /* exit stack to row */
                } else if (sel_row == 1 && sel_depth < 0) {
                    static const int to_top[7] = {0, 1, 2, 2, 3, 4, 5};
                    sel_row = 0;                    /* row to top row */
                    sel_col = to_top[sel_col];
                    sel_depth = -1;
                }
            } else if (scan == 80) {                /* down */
                if (sel_row == 0) {
                    static const int to_col[6] = {0, 1, 2, 3, 4, 5};
                    sel_row = 1;
                    sel_col = to_col[sel_col];
                    sel_depth = -1;
                } else if (sel_depth < 0) {
                    pile *p = piles[P_COL1 + sel_col];
                    if (p->num_cards > 0)
                        sel_depth = p->num_cards - 1;
                } else if (sel_depth >= 0) {
                    pile *p = piles[P_COL1 + sel_col];
                    if (sel_depth < p->num_cards - 1) sel_depth++;
                    else sel_depth = -1;
                }
            } else if (scan == 75) {                /* left */
                if (sel_row == 0) {
                    sel_col = (sel_col > 0) ? sel_col - 1 : 5;
                } else if (sel_depth >= 0) {
                    if (sel_col > 0) sel_col--;
                    else sel_col = COL_COUNT - 1;
                    {
                        pile *p = piles[P_COL1 + sel_col];
                        if (sel_depth >= p->num_cards)
                            sel_depth = (p->num_cards > 0)
                                        ? p->num_cards - 1 : -1;
                    }
                } else {
                    sel_col = (sel_col > 0) ? sel_col - 1 : COL_COUNT - 1;
                }
            } else if (scan == 77) {                /* right */
                if (sel_row == 0) {
                    sel_col = (sel_col < 5) ? sel_col + 1 : 0;
                } else if (sel_depth >= 0) {
                    if (sel_col < COL_COUNT - 1) sel_col++;
                    else sel_col = 0;
                    {
                        pile *p = piles[P_COL1 + sel_col];
                        if (sel_depth >= p->num_cards)
                            sel_depth = (p->num_cards > 0)
                                        ? p->num_cards - 1 : -1;
                    }
                } else {
                    sel_col = (sel_col < COL_COUNT - 1) ? sel_col + 1 : 0;
                }
            }
            draw_cursor(sel_row, sel_col, sel_depth, selected,
                        selected_index);
            continue;
        }

        if (c == 27 || c == 'q' || c == 'Q') { quit = 1; break; }
        /* Enter = cycle felt */
        if (c == 13 || c == 10) {
            felt_idx = (felt_idx + 1) % FELT_COUNT;
            clear_popup_region();
            draw_frame();
            draw_status();
            draw();
            draw_message(felt_variants[felt_idx].on
                         ? felt_variants[felt_idx].name
                         : "Felt: off");
            reset_marker_tracking();
            draw_cursor(sel_row, sel_col, sel_depth,
                        selected, selected_index);
            continue;
        }
        /* N = new game, always (with y/n confirm) */
        if (c == 'n' || c == 'N') {
            if (prompt_yes_no("New game? y/n")) {
                free_game();
                clear_popup_region();
                reset_marker_tracking();
                prepare_game();
                score = 0;
                sel_row = 0; sel_col = 0; sel_depth = -1;
                selected = -1; selected_index = -1;
                won = 0;
                draw_frame();
                draw_status();
                draw();
                draw_message("New game.");
                draw_cursor(sel_row, sel_col, sel_depth, selected,
                            selected_index);
            } else {
                draw_message("Arrows move, Space selects");
            }
            continue;
        }

        if (c == 'a' || c == 'A') {
            char buf[32];
            int n = auto_foundation();
            if (n > 0) {
                snprintf(buf, sizeof buf, "Auto: %d card%s to foundation",
                         n, n == 1 ? "" : "s");
            } else {
                snprintf(buf, sizeof buf, "No foundation moves");
            }
            draw_message(buf);
            draw_status();
            draw();
            draw_cursor(sel_row, sel_col, sel_depth, selected,
                        selected_index);
            if (game_won()) {
                won = 1;
                draw_message("YOU WIN! N=new game, Q=quit");
            }
            continue;
        }

        if (c == ' ') {
            int slot = (sel_row == 0) ? sel_col : (SLOT_COL1 + sel_col);

            if (selected < 0) {
                if (slot == SLOT_STOCK) {
                    draw_message(try_move(SLOT_STOCK, SLOT_STOCK, -1));
                    draw_status();
                    draw();
                } else {
                    selected = slot;
                    selected_index = (sel_depth >= 0) ? sel_depth : -1;
                    draw_message(selected_index >= 0
                                 ? "Split selected. Pick destination."
                                 : "Selected. Pick destination.");
                }
            } else if (selected == slot &&
                       selected_index ==
                       ((sel_depth >= 0) ? sel_depth : -1)) {
                selected = -1;
                selected_index = -1;
                draw_message("Cancelled.");
            } else {
                const char *r = try_move(selected, slot, selected_index);
                draw_message(r);
                if (r[0] == 'O' || r[0] == 'R' || r[0] == 'D') {
                    if (slot >= SLOT_COL1) {
                        sel_row = 1;
                        sel_col = slot - SLOT_COL1;
                        if (selected_index >= 0) {
                            pile *dp = piles[slot_to_pile(slot)];
                            sel_depth = dp->num_cards - 1;
                        } else {
                            sel_depth = -1;
                        }
                    } else {
                        sel_row = 0;
                        sel_col = slot;
                        sel_depth = -1;
                    }
                    selected = -1;
                    selected_index = -1;
                }
                draw_status();
                draw();
            }
            draw_cursor(sel_row, sel_col, sel_depth, selected,
                        selected_index);
            if (game_won()) {
                won = 1;
                draw_message("YOU WIN! N=new game, Q=quit");
            }
        }
    }

    clear_popup_region();
    free_game();
    tui_cursor(1, 0);
    return 0;
}

int fb_solitaire(void)
{
    return run_solitaire();
}