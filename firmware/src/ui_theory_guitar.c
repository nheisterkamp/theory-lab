/* SPDX-License-Identifier: GPL-3.0-only */
/* Standard E-A-D-G-B-E chord voicings. Frets are low E to high E; -1 is muted. */
static const uint8_t THEORY_GUITAR_TUNING[6] = {40, 45, 50, 55, 59, 64};
static const uint8_t THEORY_GUITAR_CHORD[] = {0, 1, 7, 6, 8};
static const char *const THEORY_GUITAR_QUALITY[] = {"Major", "Minor", "Dominant 7", "Major 7", "Minor 7"};
static const struct { int8_t fret[6]; uint8_t finger[6]; } THEORY_GUITAR_E[5] = {
    {{0,2,2,1,0,0}, {1,3,4,2,1,1}},
    {{0,2,2,0,0,0}, {1,3,4,1,1,1}},
    {{0,2,0,1,0,0}, {1,3,1,2,1,1}},
    {{0,2,1,1,0,0}, {1,4,2,3,1,1}},
    {{0,2,0,0,0,0}, {1,3,1,1,1,1}},
}, THEORY_GUITAR_A[5] = {
    {{-1,0,2,2,2,0}, {0,1,3,3,3,1}},
    {{-1,0,2,2,1,0}, {0,1,3,4,2,1}},
    {{-1,0,2,0,2,0}, {0,1,3,1,4,1}},
    {{-1,0,2,1,2,0}, {0,1,3,2,4,1}},
    {{-1,0,2,0,1,0}, {0,1,3,1,2,1}},
};
static const struct {
    uint8_t root, quality;
    int8_t fret[6];
    uint8_t finger[6];
} THEORY_GUITAR_OPEN[] = {
    {0,0, {-1,3,2,0,1,0}, {0,3,2,0,1,0}},
    {2,0, {-1,-1,0,2,3,2}, {0,0,0,1,3,2}},
    {4,0, {0,2,2,1,0,0}, {0,2,3,1,0,0}},
    {7,0, {3,2,0,0,0,3}, {2,1,0,0,0,3}},
    {9,0, {-1,0,2,2,2,0}, {0,0,1,2,3,0}},
    {2,1, {-1,-1,0,2,3,1}, {0,0,0,2,3,1}},
    {4,1, {0,2,2,0,0,0}, {0,2,3,0,0,0}},
    {9,1, {-1,0,2,2,1,0}, {0,0,2,3,1,0}},
    {0,2, {-1,3,2,3,1,0}, {0,3,2,4,1,0}},
    {2,2, {-1,-1,0,2,1,2}, {0,0,0,2,1,3}},
    {4,2, {0,2,0,1,0,0}, {0,2,0,1,0,0}},
    {7,2, {3,2,0,0,0,1}, {3,2,0,0,0,1}},
    {9,2, {-1,0,2,0,2,0}, {0,0,2,0,3,0}},
    {11,2, {-1,2,1,2,0,2}, {0,2,1,3,0,4}},
    {0,3, {-1,3,2,0,0,0}, {0,3,2,0,0,0}},
    {2,3, {-1,-1,0,2,2,2}, {0,0,0,1,2,3}},
    {4,3, {0,2,1,1,0,0}, {0,3,1,2,0,0}},
    {7,3, {3,2,0,0,0,2}, {3,1,0,0,0,2}},
    {9,3, {-1,0,2,1,2,0}, {0,0,2,1,3,0}},
    {2,4, {-1,-1,0,2,1,1}, {0,0,0,2,1,1}},
    {4,4, {0,2,0,0,0,0}, {0,2,0,0,0,0}},
    {9,4, {-1,0,2,0,1,0}, {0,0,2,0,1,0}},
};
static struct {
    uint32_t held;
    uint8_t root, quality, shape, unsupported;
} theory_guitar;

static uint32_t theory_guitar_mask(void)
{
    uint32_t mask = THEORY_CHORD[THEORY_GUITAR_CHORD[theory_guitar.quality]].mask;
    return ((mask << theory_guitar.root) | (mask >> (12u - theory_guitar.root))) & 0xFFFu;
}

static int32_t theory_guitar_open(void)
{
    for (uint32_t i = 0; i < sizeof THEORY_GUITAR_OPEN / sizeof THEORY_GUITAR_OPEN[0]; i++)
        if (THEORY_GUITAR_OPEN[i].root == theory_guitar.root && THEORY_GUITAR_OPEN[i].quality == theory_guitar.quality)
            return (int32_t)i;
    return -1;
}

static uint32_t theory_guitar_voicing(int8_t frets[6], uint8_t fingers[6])
{
    int32_t open = theory_guitar_open();
    uint32_t shapes = open >= 0 ? 3u : 2u;
    uint32_t choice = theory_guitar.shape % shapes;
    if (open >= 0 && !choice) {
        for (uint32_t i = 0; i < 6u; i++) {
            frets[i] = THEORY_GUITAR_OPEN[open].fret[i];
            fingers[i] = THEORY_GUITAR_OPEN[open].finger[i];
        }
        return 0;
    }
    if (open >= 0)
        choice--;
    /* Show the lower barre first when there is no open voicing. */
    uint32_t e = (theory_guitar.root + 8u) % 12u, a = (theory_guitar.root + 3u) % 12u;
    uint32_t use_a = choice ^ (uint32_t)(a < e), offset = use_a ? a : e;
    for (uint32_t i = 0; i < 6u; i++) {
        int32_t fret = use_a ? THEORY_GUITAR_A[theory_guitar.quality].fret[i] : THEORY_GUITAR_E[theory_guitar.quality].fret[i];
        frets[i] = fret < 0 ? -1 : (int8_t)(fret + (int32_t)offset);
        fingers[i] = use_a ? THEORY_GUITAR_A[theory_guitar.quality].finger[i] : THEORY_GUITAR_E[theory_guitar.quality].finger[i];
        if (frets[i] <= 0)
            fingers[i] = 0;
    }
    return use_a ? 2u : 1u;
}

static void theory_guitar_poll(uint32_t held)
{
    uint32_t mask, root = 0, bass = 128;
    int32_t chord;
    if (held == theory_guitar.held)
        return;
    theory_guitar.held = held;
    mask = theory_lab_pitch_mask(held);
    if (theory_cardinality(mask) < 3u)
        return;
    for (uint32_t i = 0; i < 27u; i++)
        if ((held & TH_NOTE(i)) && kb_note[i] < bass)
            bass = kb_note[i];
    chord = theory_match(mask, bass % 12u, &root);
    theory_guitar.unsupported = 1;
    for (uint32_t i = 0; i < 5u; i++)
        if (chord == THEORY_GUITAR_CHORD[i]) {
            theory_guitar.root = (uint8_t)root;
            theory_guitar.quality = (uint8_t)i;
            theory_guitar.shape = theory_guitar.unsupported = 0;
        }
    ui.force = 1;
}

static void theory_guitar_input(uint32_t pressed, int32_t root, int32_t quality, int32_t shape)
{
    if (root || quality) {
        theory_guitar.root = (uint8_t)((theory_guitar.root + root % 12 + 12) % 12);
        theory_guitar.quality = (uint8_t)((theory_guitar.quality + quality % 5 + 5) % 5);
        theory_guitar.shape = theory_guitar.unsupported = 0;
    }
    if (shape) {
        int32_t count = theory_guitar_open() >= 0 ? 3 : 2;
        theory_guitar.shape = (uint8_t)((theory_guitar.shape + shape % count + count) % count);
    }
    if (pressed & (1u << panel.btn[B_PLAY])) {
        theory_preview_request = theory_preview_pitches(theory_guitar_mask(), theory_guitar.root);
        theory_app.audition_at = fm1_ms | 1u;
    }
    if (root || quality || shape || pressed)
        ui.force = 1;
}

static void theory_guitar_draw(void)
{
    int8_t frets[6];
    uint8_t fingers[6];
    uint32_t form = theory_guitar_voicing(frets, fingers), first = 24, max = 0, i, band;
    char name[16], number[6], note[4];
    theory_lab_chord(name, theory_guitar_mask(), theory_guitar.root);
    for (i = 0; i < 6u; i++) {
        if (frets[i] > 0 && (uint32_t)frets[i] < first)
            first = (uint32_t)frets[i];
        if (frets[i] > 0 && (uint32_t)frets[i] > max)
            max = (uint32_t)frets[i];
    }
    if (first == 24u || max <= 4u)
        first = 1;
    for (band = 0; band < 2u; band++) {
        int32_t oy = (int32_t)band * 120;
        cv_begin(240, 120, TH_BG);
        if (!band)
            theory_heading("Guitar chords", name);
        cv_text(12, 43 - oy, &AF_S, THEORY_GUITAR_QUALITY[theory_guitar.quality], TH_MUTED);
        cv_text_r(228, 43 - oy, &AF_S, form == 0 ? "Open shape" : form == 1 ? "E shape" : "A shape", TH_SCALE, TH_BG);
        fmt_int(number, (int32_t)first);
        cv_text(15, 79 - oy, &AF_S, number, TH_MUTED);
        cv_text(12, 94 - oy, &AF_S, "fret", TH_MUTED);
        for (i = 0; i < 6u; i++) {
            int32_t x = 55 + (int32_t)i * 27;
            cv_line(x, 74 - oy, x, 174 - oy, TH_RULE);
            if (frets[i] <= 0)
                cv_text_in(x - 8, 57 - oy, 16, &AF_S, frets[i] < 0 ? "x" : "o", frets[i] < 0 ? TH_MUTED : TH_PLAY, TH_BG);
            if (frets[i] >= 0) {
                theory_chord_note(note, (THEORY_GUITAR_TUNING[i] + (uint32_t)frets[i]) % 12u,
                                  theory_guitar.root, THEORY_GUITAR_CHORD[theory_guitar.quality]);
                cv_text_in(x - 12, 178 - oy, 24, &AF_S, note, TH_CHORD, TH_BG);
            }
        }
        for (i = 0; i <= 5u; i++)
            cv_rect(55, 74 + (int32_t)i * 20 - oy, 136, first == 1 && !i ? 3 : 1, TH_RULE);
        /* A repeated finger on a fret is a barre. The other dots show the
         * remaining fingers above it. */
        for (uint32_t finger = 1; finger <= 4u; finger++)
            for (uint32_t fret = first; fret < first + 5u; fret++) {
                int32_t left = 6, right = -1;
                for (i = 0; i < 6u; i++)
                    if (fingers[i] == finger && frets[i] == (int32_t)fret) {
                        if (left == 6)
                            left = (int32_t)i;
                        right = (int32_t)i;
                    }
                if (right > left)
                    cv_rrect(49 + left * 27, 78 + (int32_t)(fret - first) * 20 - oy,
                             (right - left) * 27 + 13, 13, 6, TH_SCALE, TH_BG);
            }
        for (i = 0; i < 6u; i++)
            if (frets[i] > 0) {
                int32_t x = 55 + (int32_t)i * 27, y = 84 + ((int32_t)frets[i] - (int32_t)first) * 20 - oy;
                uint16_t bg = (THEORY_GUITAR_TUNING[i] + (uint32_t)frets[i]) % 12u == theory_guitar.root ? TH_PLAY : TH_SCALE;
                cv_rrect(x - 7, y - 7, 15, 15, 7, bg, TH_BG);
                fmt_int(number, fingers[i]);
                cv_text_in(x - 7, y - 7, 15, &AF_S, number, TH_BG, bg);
            }
        if (band) {
            cv_text(12, 78, &AF_S, theory_guitar.unsupported ? "Choose major, minor, 7, maj7 or m7" : "E A D G B E   /   x mute   o open", TH_MUTED);
            cv_rect(12, 96, 216, 1, TH_RULE);
            cv_text(12, 101, &AF_S, "K1 Root  K2 Type  PRESETS Shape", TH_MUTED);
        }
        cv_blit(0, band * 120u);
    }
}
