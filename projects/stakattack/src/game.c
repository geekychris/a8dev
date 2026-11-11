// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * StakAttack - block-stacking game logic
 * Pure game state management, no rendering or input reading.
 */

#include <string.h>
#include "game.h"

/* port: Atari 8-bit (cc65). Changes are marked "port:". */

/* ------------------------------------------------------------------ */
/* Piece shape data: 7 pieces x 4 rotations                          */
/* Cell values = piece type + 1, so each piece has a unique color 1-7 */
/* ------------------------------------------------------------------ */

static const PieceShape piece_shapes[NUM_PIECES][4] = {
    /* PIECE_I (color 1) */
    {
        /* Rotation 0 */
        {{{ 0,0,0,0 },
          { 1,1,1,1 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,0,1,0 },
          { 0,0,1,0 },
          { 0,0,1,0 },
          { 0,0,1,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 0,0,0,0 },
          { 1,1,1,1 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,1,0,0 },
          { 0,1,0,0 },
          { 0,1,0,0 },
          { 0,1,0,0 }}}
    },
    /* PIECE_O (color 2) */
    {
        /* All 4 rotations identical */
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        {{{ 0,2,2,0 },
          { 0,2,2,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_T (color 3) */
    {
        /* Rotation 0: flat, point up */
        {{{ 0,3,0,0 },
          { 3,3,3,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1: point right */
        {{{ 0,3,0,0 },
          { 0,3,3,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2: point down */
        {{{ 0,0,0,0 },
          { 3,3,3,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3: point left */
        {{{ 0,3,0,0 },
          { 3,3,0,0 },
          { 0,3,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_S (color 4) */
    {
        /* Rotation 0 */
        {{{ 0,4,4,0 },
          { 4,4,0,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,4,0,0 },
          { 0,4,4,0 },
          { 0,0,4,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 0,4,4,0 },
          { 4,4,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 4,0,0,0 },
          { 4,4,0,0 },
          { 0,4,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_Z (color 5) */
    {
        /* Rotation 0 */
        {{{ 5,5,0,0 },
          { 0,5,5,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,0,5,0 },
          { 0,5,5,0 },
          { 0,5,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 5,5,0,0 },
          { 0,5,5,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,5,0,0 },
          { 5,5,0,0 },
          { 5,0,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_L (color 6) */
    {
        /* Rotation 0 */
        {{{ 0,0,6,0 },
          { 6,6,6,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,6,0,0 },
          { 0,6,0,0 },
          { 0,6,6,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 6,6,6,0 },
          { 6,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 6,6,0,0 },
          { 0,6,0,0 },
          { 0,6,0,0 },
          { 0,0,0,0 }}}
    },
    /* PIECE_J (color 7) */
    {
        /* Rotation 0 */
        {{{ 7,0,0,0 },
          { 7,7,7,0 },
          { 0,0,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 1 */
        {{{ 0,7,7,0 },
          { 0,7,0,0 },
          { 0,7,0,0 },
          { 0,0,0,0 }}},
        /* Rotation 2 */
        {{{ 0,0,0,0 },
          { 7,7,7,0 },
          { 0,0,7,0 },
          { 0,0,0,0 }}},
        /* Rotation 3 */
        {{{ 0,7,0,0 },
          { 0,7,0,0 },
          { 7,7,0,0 },
          { 0,0,0,0 }}}
    }
};

/* ------------------------------------------------------------------ */
/* Public accessor for piece shape data                               */
/* ------------------------------------------------------------------ */

const PieceShape *piece_get_shape(int type, int rotation)
{
    return &piece_shapes[type][rotation & 3];
}

/* ------------------------------------------------------------------ */
/* Random number generator (LCG)                                      */
/* ------------------------------------------------------------------ */

static int game_rand(GameState *gs)
{
    gs->rng_state = gs->rng_state * 1103515245UL + 12345UL;
    return (int)((gs->rng_state >> 16) & 0x7FFF);
}

static int random_piece(GameState *gs)
{
    return game_rand(gs) % NUM_PIECES;
}

/* ------------------------------------------------------------------ */
/* Collision detection                                                 */
/* ------------------------------------------------------------------ */

/* port: the same test as the original's (kept below under #if 0), on
 * bitmasks: fm_lo/fm_hi[row] have bit c set for a filled cell in column c, and
 * the four rows below the floor are full. A piece's row is 4 bits
 * (row_bits[][][]), shifted to x; the ghost search tests 4 ANDs a row
 * instead of 16 cell lookups. cc65 would otherwise spend ~3000 cycles
 * a test, and the ghost takes up to 19 of them a frame. */
/* (fm_lo/fm_hi, pm_lo/pm_hi and pm_fits_y are in fits.s) */
extern UBYTE fm_lo[FIELD_H + 4], fm_hi[FIELD_H + 4];   /* the well's rows: bits 0-7, 8-11 */
extern UBYTE pm_lo[4], pm_hi[4];                         /* the piece's rows at x */
extern UBYTE __fastcall__ pm_fits_y(UBYTE y);            /* no overlap at row y */
/* by piece and rotation (idx = type * 4 + rotation): the four rows' bits,
   the leftmost and rightmost columns used, and the four cells (row, column:
   main.c draws from them) */
static UBYTE row_bits[NUM_PIECES * 4 * 4];
static UBYTE col_min[NUM_PIECES * 4], col_max[NUM_PIECES * 4];
UBYTE piece_cell_r[NUM_PIECES * 4 * 4], piece_cell_c[NUM_PIECES * 4 * 4];
/* a 4-bit row shifted to x (-2..11): sh_lo/sh_hi[(x + 2) * 16 + bits] */
static UBYTE sh_lo[14 * 16], sh_hi[14 * 16];
static const UBYTE row12[FIELD_H + 4] = {   /* row * FIELD_W */
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168, 180, 192, 204, 216,
    228, 240, 252, 252 };

static void masks_init(void)
{
    static UBYTE t, rot, r, c, b, idx, n;
    static UWORD m;
    for (t = 0; t < NUM_PIECES; t++)
        for (rot = 0; rot < 4; rot++) {
            idx = t * 4 + rot;
            col_min[idx] = 4;
            col_max[idx] = 0;
            n = idx * 4;
            for (r = 0; r < 4; r++) {
                b = 0;
                for (c = 0; c < 4; c++)
                    if (piece_shapes[t][rot].cells[r][c]) {
                        b |= 1 << c;
                        if (c < col_min[idx]) col_min[idx] = c;
                        if (c > col_max[idx]) col_max[idx] = c;
                        piece_cell_r[n] = r;
                        piece_cell_c[n] = c;
                        n++;
                    }
                row_bits[idx * 4 + r] = b;
            }
        }
    for (r = 0; r < 14; r++)
        for (b = 0; b < 16; b++) {
            m = r >= 2 ? (UWORD)b << (r - 2) : (UWORD)(b >> (2 - r));
            sh_lo[r * 16 + b] = (UBYTE)m;
            sh_hi[r * 16 + b] = (UBYTE)(m >> 8);
        }
}

static void fmask_rebuild(GameState *gs)
{
    static UBYTE r, c;
    static UWORD m;
    static const UBYTE *f;
    f = gs->field[0];
    for (r = 0; r < FIELD_H; r++) {
        m = 0;
        for (c = 0; c < FIELD_W; c++, f++)
            if (*f) m |= 1 << c;
        fm_lo[r] = (UBYTE)m;
        fm_hi[r] = (UBYTE)(m >> 8);
    }
    for (; r < FIELD_H + 4; r++) fm_lo[r] = fm_hi[r] = 0xFF;
}

/* the piece's rows at x into pm_lo/pm_hi; FALSE when a cell is off the sides */
static BOOL piece_at(int type, int rotation, int x)
{
    static UBYTE idx, xi;
    static const UBYTE *rb, *lo, *hi;
    if (x < -2 || x > FIELD_W - 1) return FALSE;
    idx = ((UBYTE)type << 2) | ((UBYTE)rotation & 3);
    xi = (UBYTE)x + 2;
    if ((UBYTE)(xi + col_min[idx]) < 2 || (UBYTE)(xi + col_max[idx]) >= FIELD_W + 2)
        return FALSE;
    rb = row_bits + (idx << 2);
    lo = sh_lo + (xi << 4);
    hi = sh_hi + (xi << 4);
    pm_lo[0] = lo[rb[0]]; pm_hi[0] = hi[rb[0]];
    pm_lo[1] = lo[rb[1]]; pm_hi[1] = hi[rb[1]];
    pm_lo[2] = lo[rb[2]]; pm_hi[2] = hi[rb[2]];
    pm_lo[3] = lo[rb[3]]; pm_hi[3] = hi[rb[3]];
    return TRUE;
}

/* the piece's rows at y (no piece goes above the top) */
static BOOL pm_fits(int y)
{
    return y >= 0 && pm_fits_y((UBYTE)y);
}

static BOOL piece_fits(GameState *gs, int type, int rotation, int x, int y)
{
    (void)gs;
    return piece_at(type, rotation, x) && pm_fits(y);
}

#if 0   /* the original */
static BOOL piece_fits(GameState *gs, int type, int rotation, int x, int y)
{
    const PieceShape *shape = piece_get_shape(type, rotation);
    int r, c;
    int fx, fy;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (shape->cells[r][c] == 0) continue;

            fx = x + c;
            fy = y + r;

            /* Out of bounds? */
            if (fx < 0 || fx >= FIELD_W) return FALSE;
            if (fy < 0 || fy >= FIELD_H) return FALSE;

            /* Overlaps existing cell? */
            if (gs->field[fy][fx] != 0) return FALSE;
        }
    }

    return TRUE;
}

#endif

/* ------------------------------------------------------------------ */
/* Piece spawning                                                     */
/* ------------------------------------------------------------------ */

static void spawn_piece(GameState *gs)
{
    gs->current.type = gs->next_type;
    gs->current.rotation = 0;
    gs->current.x = (FIELD_W - 4) / 2;
    gs->current.y = 0;

    gs->next_type = random_piece(gs);

    gs->drop_timer = gs->drop_speed;
    gs->lock_timer = 30;

    /* Check if the new piece fits */
    if (!piece_fits(gs, gs->current.type, gs->current.rotation,
                    gs->current.x, gs->current.y)) {
        gs->state = STATE_GAMEOVER;
    }
}

/* ------------------------------------------------------------------ */
/* Line clearing                                                      */
/* ------------------------------------------------------------------ */

#if 0   /* port: check_lines tests the masks */
static BOOL line_full(GameState *gs, int row)
{
    /* port: a full row's mask (the original tests its 12 cells) */
    (void)gs;
    return fm_lo[row] == 0xFF && fm_hi[row] == 0x0F;
}
#endif

static void check_lines(GameState *gs)
{
    /* port: the original's loop with a byte index and the masks */
    static UBYTE r, n;

    gs->clear_count = 0;
    gs->clear_lines[0] = gs->clear_lines[1] = gs->clear_lines[2] = gs->clear_lines[3] = -1;

    n = 0;
    for (r = 0; r < FIELD_H; r++) {
        if (fm_lo[r] == 0xFF && fm_hi[r] == 0x0F) {
            gs->clear_lines[n] = r;
            if (++n >= 4) break;
        }
    }
    gs->clear_count = n;

    if (gs->clear_count > 0) {
        gs->clear_timer = 15;
        gs->just_cleared = TRUE;
    }
}

static void remove_lines(GameState *gs)
{
    static const ULONG line_scores[5] = { 0, 100, 300, 500, 800 };
    int i;

    /* Remove completed lines from bottom up */
    for (i = gs->clear_count - 1; i >= 0; i--) {
        int line = gs->clear_lines[i];

        /* Shift everything above down by one */
        /* port: the rows are contiguous: one memmove (was a cell at a time) */
        memmove(gs->field[1], gs->field[0], (size_t)line * FIELD_W);
        memmove(fm_lo + 1, fm_lo, line);   /* port: and the masks */
        memmove(fm_hi + 1, fm_hi, line);

        /* Clear top row */
        memset(gs->field[0], 0, FIELD_W);
        fm_lo[0] = fm_hi[0] = 0;           /* port */

        /* Adjust remaining clear_lines indices that are above this line */
        {
            int j;
            for (j = 0; j < i; j++) {
                if (gs->clear_lines[j] < line) {
                    gs->clear_lines[j]++;
                }
            }
        }
    }

    /* Update score */
    if (gs->clear_count > 0 && gs->clear_count <= 4) {
        gs->score += line_scores[gs->clear_count] * (ULONG)gs->level;
    }

    /* Update line count and level */
    gs->lines += gs->clear_count;
    gs->level = 1 + gs->lines / 10;
    if (gs->level > 20) gs->level = 20;

    /* Recalculate drop speed */
    gs->drop_speed = 30 - gs->level * 2;
    if (gs->drop_speed < 2) gs->drop_speed = 2;

    gs->clear_count = 0;
}

/* ------------------------------------------------------------------ */
/* Lock piece into field                                              */
/* ------------------------------------------------------------------ */

static void lock_piece(GameState *gs)
{
    /* port: the original's loop (kept below) over the piece's four cells
       (piece_cell_r/c), byte offsets into the field */
    static UBYTE i, n, v, y, x, *f;
    n = (((UBYTE)gs->current.type << 2) | ((UBYTE)gs->current.rotation & 3)) << 2;
    v = (UBYTE)gs->current.type + 1;
    y = (UBYTE)gs->current.y;
    x = (UBYTE)gs->current.x;
    f = gs->field[0];
    for (i = 0; i < 4; i++, n++)            /* (a locked piece is in the well) */
        f[row12[(UBYTE)(y + piece_cell_r[n])] + (UBYTE)(x + piece_cell_c[n])] = v;

#if 0   /* the original */
    const PieceShape *shape = piece_get_shape(gs->current.type,
                                              gs->current.rotation);
    int r, c;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (shape->cells[r][c] != 0) {
                int fx = gs->current.x + c;
                int fy = gs->current.y + r;
                if (fx >= 0 && fx < FIELD_W && fy >= 0 && fy < FIELD_H) {
                    gs->field[fy][fx] = shape->cells[r][c];
                }
            }
        }
    }
#endif

    /* port: the piece's rows into the masks (rows below the floor stay full) */
    piece_at(gs->current.type, gs->current.rotation, gs->current.x);
    for (i = 0; i < 4; i++, y++) {
        fm_lo[y] |= pm_lo[i];
        fm_hi[y] |= pm_hi[i];
    }
    gs->just_locked = TRUE;
    check_lines(gs);

    if (gs->clear_count == 0) {
        spawn_piece(gs);
    }
}

/* ------------------------------------------------------------------ */
/* Ghost piece (drop preview)                                         */
/* ------------------------------------------------------------------ */

int game_ghost_y(const GameState *gs)
{
    /* Need non-const to call piece_fits, but we won't modify state */
    GameState *mgs = (GameState *)gs;
    int gy = gs->current.y;

    (void)mgs;
    /* port: the piece's masks once, then one test a row */
    piece_at(gs->current.type, gs->current.rotation, gs->current.x);
    while (pm_fits(gy + 1)) {
        gy++;
    }

    return gy;
}

/* ------------------------------------------------------------------ */
/* Initialization                                                     */
/* ------------------------------------------------------------------ */

void game_init(GameState *gs)
{
    int r, c, i;

    for (r = 0; r < FIELD_H; r++) {
        for (c = 0; c < FIELD_W; c++) {
            gs->field[r][c] = 0;
        }
    }

    gs->current.type = 0;
    gs->current.rotation = 0;
    gs->current.x = 0;
    gs->current.y = 0;

    gs->next_type = 0;
    gs->state = STATE_TITLE;
    gs->score = 0;
    gs->level = 1;
    gs->lines = 0;
    gs->drop_timer = 0;
    gs->drop_speed = 28;
    gs->lock_timer = 30;
    gs->clear_timer = 0;
    gs->clear_count = 0;

    for (i = 0; i < 4; i++) {
        gs->clear_lines[i] = -1;
    }

    gs->das_timer = 0;
    gs->das_dir = 0;
    gs->just_locked = FALSE;
    gs->just_cleared = FALSE;
    gs->just_dropped = FALSE;
    gs->rng_state = 123456789UL;
    masks_init();                      /* port */
    fmask_rebuild(gs);
}

void game_start(GameState *gs)
{
    int r, c, i;

    /* Clear the field */
    for (r = 0; r < FIELD_H; r++) {
        for (c = 0; c < FIELD_W; c++) {
            gs->field[r][c] = 0;
        }
    }

    gs->score = 0;
    gs->level = 1;
    gs->lines = 0;
    gs->drop_speed = 28;
    gs->clear_timer = 0;
    gs->clear_count = 0;

    for (i = 0; i < 4; i++) {
        gs->clear_lines[i] = -1;
    }

    gs->das_timer = 0;
    gs->das_dir = 0;
    gs->just_locked = FALSE;
    gs->just_cleared = FALSE;
    gs->just_dropped = FALSE;

    fmask_rebuild(gs);                 /* port */

    /* Pick first and next pieces */
    gs->next_type = random_piece(gs);
    spawn_piece(gs);

    gs->state = STATE_PLAYING;
}

/* ------------------------------------------------------------------ */
/* Main update                                                        */
/* ------------------------------------------------------------------ */

void game_update(GameState *gs, int input)
{
    switch (gs->state) {

    case STATE_TITLE:
        if (input & GINPUT_START) {
            game_start(gs);
        }
        break;

    case STATE_GAMEOVER:
        if (input & GINPUT_START) {
            gs->state = STATE_TITLE;
        }
        break;

    case STATE_PAUSED:
        if (input & GINPUT_START) {
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        /* Clear SFX flags */
        gs->just_locked = FALSE;
        gs->just_cleared = FALSE;
        gs->just_dropped = FALSE;

        /* Pause */
        if (input & GINPUT_START) {
            gs->state = STATE_PAUSED;
            break;
        }

        /* If line clear animation is running, just tick it down */
        if (gs->clear_timer > 0) {
            gs->clear_timer--;
            if (gs->clear_timer == 0) {
                remove_lines(gs);
                spawn_piece(gs);
            }
            break;
        }

        /* Rotation with wall kicks */
        if (input & GINPUT_ROTATE) {
            int new_rot = (gs->current.rotation + 1) & 3;
            if (piece_fits(gs, gs->current.type, new_rot,
                           gs->current.x, gs->current.y)) {
                gs->current.rotation = new_rot;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x - 1, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x -= 1;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x + 1, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x += 1;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x - 2, gs->current.y)) {
                /* Extra kick for I piece */
                gs->current.rotation = new_rot;
                gs->current.x -= 2;
            } else if (piece_fits(gs, gs->current.type, new_rot,
                                  gs->current.x + 2, gs->current.y)) {
                gs->current.rotation = new_rot;
                gs->current.x += 2;
            }
        }

        /* Horizontal movement with DAS */
        {
            int h_dir = 0;
            if (input & GINPUT_LEFT)  h_dir = -1;
            if (input & GINPUT_RIGHT) h_dir = 1;

            if (h_dir != 0) {
                if (h_dir != gs->das_dir) {
                    /* New direction: move immediately, reset DAS */
                    gs->das_dir = h_dir;
                    gs->das_timer = 0;
                    if (piece_fits(gs, gs->current.type, gs->current.rotation,
                                   gs->current.x + h_dir, gs->current.y)) {
                        gs->current.x += h_dir;
                        gs->lock_timer = 30; /* Reset lock on horizontal move */
                    }
                } else {
                    /* Same direction held: DAS */
                    gs->das_timer++;
                    if (gs->das_timer >= 10) {
                        /* Auto-repeat phase: move every 3 frames */
                        if ((gs->das_timer - 10) % 3 == 0) {
                            if (piece_fits(gs, gs->current.type,
                                           gs->current.rotation,
                                           gs->current.x + h_dir,
                                           gs->current.y)) {
                                gs->current.x += h_dir;
                                gs->lock_timer = 30;
                            }
                        }
                    }
                }
            } else {
                gs->das_dir = 0;
                gs->das_timer = 0;
            }
        }

        /* Hard drop */
        if (input & GINPUT_DROP) {
            int ghost_y = game_ghost_y(gs);
            int rows = ghost_y - gs->current.y;
            gs->score += (ULONG)(rows * 2);
            gs->current.y = ghost_y;
            gs->just_dropped = TRUE;
            lock_piece(gs);
            break;
        }

        /* Soft drop: accelerate */
        if (input & GINPUT_DOWN) {
            if (piece_fits(gs, gs->current.type, gs->current.rotation,
                           gs->current.x, gs->current.y + 1)) {
                /* Successful move: award score, refresh lock timer, and
                 * arm the auto-drop for a full drop_speed interval so we
                 * don't trigger a second automatic move this frame. */
                gs->current.y++;
                gs->score += 1;
                gs->drop_timer = gs->drop_speed;
                gs->lock_timer = 30;
            } else {
                /* Blocked: keep the timer expired so we retry immediately. */
                gs->drop_timer = 0;
            }
        }

        /* Auto-drop */
        gs->drop_timer--;
        if (gs->drop_timer <= 0) {
            gs->drop_timer = gs->drop_speed;

            if (piece_fits(gs, gs->current.type, gs->current.rotation,
                           gs->current.x, gs->current.y + 1)) {
                gs->current.y++;
                gs->lock_timer = 30;
            } else {
                /* Piece can't move down: tick lock timer */
                gs->lock_timer--;
                if (gs->lock_timer <= 0) {
                    lock_piece(gs);
                }
            }
        } else {
            /* Even when not auto-dropping, check if grounded and tick lock */
            if (!piece_fits(gs, gs->current.type, gs->current.rotation,
                            gs->current.x, gs->current.y + 1)) {
                gs->lock_timer--;
                if (gs->lock_timer <= 0) {
                    lock_piece(gs);
                }
            }
        }
        break;
    }
}
