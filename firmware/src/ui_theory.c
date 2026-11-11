/* SPDX-License-Identifier: GPL-3.0-only */
/* Theory practice for the FM-1's 240 x 240 display. Included by ui_draw.c. */
enum { THEORY_MODES = 7 };
static const struct {
    const char *name;
    uint8_t scale, offset;
} THEORY_MODE[THEORY_MODES] = {
    {"Ionian", 1, 0}, {"Dorian", 3, 2}, {"Phrygian", 8, 4}, {"Lydian", 9, 5},
    {"Mixolydian", 4, 7}, {"Aeolian", 2, 9}, {"Locrian", 10, 11},
};
static const char *const THEORY_MAJOR[12] = {
    "C", "G", "D", "A", "E", "B", "F#", "Db", "Ab", "Eb", "Bb", "F",
};
static const char *const THEORY_MINOR[12] = {
    "Am", "Em", "Bm", "F#m", "C#m", "G#m", "D#m", "Bbm", "Fm", "Cm", "Gm", "Dm",
};
static const char *const THEORY_FLAT[12] = {
    "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
};
static struct {
    uint8_t fifth, mode, wheel, track, bass;
    uint16_t backing;
} theory_ui;

#define TH_BG RGB(17, 23, 45)
#define TH_SURF RGB(34, 44, 75)
#define TH_RULE RGB(64, 77, 116)
#define TH_MUTED RGB(162, 177, 214)
#define TH_TEXT RGB(241, 245, 255)
#define TH_SCALE RGB(181, 166, 255)
#define TH_PLAY RGB(91, 240, 197)
#define TH_OUT RGB(255, 183, 117)
#define TH_CHORD RGB(115, 196, 255)

static uint32_t theory_root(void)
{
    return (theory_ui.fifth * 7u + THEORY_MODE[theory_ui.mode].offset) % 12u;
}

static uint32_t theory_mask(void)
{
    return SCALE_MASK[THEORY_MODE[theory_ui.mode].scale];
}

static uint32_t theory_interval(uint32_t pc)
{
    return (pc + 12u - theory_root()) % 12u;
}

static void theory_set_mode(int32_t steps)
{
    uint32_t root = theory_root();
    theory_ui.mode = (uint8_t)((theory_ui.mode + steps % THEORY_MODES + THEORY_MODES) % THEORY_MODES);
    theory_ui.fifth = (uint8_t)(((root + 12u - THEORY_MODE[theory_ui.mode].offset) * 7u) % 12u);
}

static uint32_t theory_scale_keys(uint32_t mask, uint32_t root, uint32_t *roots)
{
    uint32_t k, lit = 0;
    *roots = 0;
    for (k = 0; k < 27u; k++) {
        uint32_t iv = (53u + k + 12u - root) % 12u;
        lit |= ((mask >> iv) & 1u) << k;
        *roots |= (uint32_t)(iv == 0u) << k;
    }
    return lit;
}

static uint32_t theory_scale_leds(uint32_t *roots)
{
    return theory_scale_keys(theory_mask(), theory_root(), roots);
}

/* A sequencer gate can end before the beat. Keep the latest harmonic target
 * until the next notes on that track, and clear it when transport stops. */
static void theory_backing_poll(void)
{
    uint32_t i, count, bass = 128, mask = 0;
    const track_t *track;
    uint8_t notes[4];
    if (!song.playing || !theory_ui.track) {
        theory_ui.backing = 0;
        return;
    }
    track = &trk[theory_ui.track - 1u];
    if (chord_kit(track) || track->p[P_MUTE]) {
        theory_ui.backing = 0;
        return;
    }
    fm1_irq_off();
    count = track->seq_n < 4u ? track->seq_n : 4u;
    for (i = 0; i < count; i++)
        notes[i] = track->seq_notes[i];
    fm1_irq_on();
    for (i = 0; i < count; i++) {
        if (notes[i] < bass)
            bass = notes[i];
        mask |= 1u << (notes[i] % 12u);
    }
    if (mask) {
        theory_ui.backing = (uint16_t)mask;
        theory_ui.bass = (uint8_t)(bass % 12u);
    }
}

static uint32_t theory_keys_for(uint32_t pitches)
{
    uint32_t mask = 0, k;
    for (k = 0; pitches && k < 27u; k++)
        mask |= ((pitches >> ((53u + k) % 12u)) & 1u) << k;
    return mask;
}

static uint32_t theory_backing_keys(void)
{
    return theory_keys_for(theory_ui.backing);
}

static uint32_t theory_degree(uint32_t iv)
{
    uint32_t degree = 0, n;
    for (n = 0; n < iv; n++)
        degree += (theory_mask() >> n) & 1u;
    return degree;
}

/* Spell the degrees against the parent major's letters, including E# and Cb. */
static void theory_note_in_fifth(char out[4], uint32_t pc, uint32_t fifth)
{
    static const char LETTERS[] = "CDEFGAB";
    static const uint8_t PITCH[] = {0, 2, 4, 5, 7, 9, 11};
    uint32_t parent = fifth * 7u % 12u;
    uint32_t iv = (pc + 12u - parent) % 12u;
    uint32_t letter = 0, step;
    if (!((SCALE_MASK[1] >> iv) & 1u)) {
        str_cpy(out, (fifth >= 7 ? THEORY_FLAT : N_NOTE)[pc], 4);
        return;
    }
    while (LETTERS[letter] != THEORY_MAJOR[fifth][0])
        letter++;
    for (step = 0; step < iv; step++)
        letter += (SCALE_MASK[1] >> step) & 1u;
    letter %= 7u;
    out[0] = LETTERS[letter];
    iv = (pc + 12u - PITCH[letter]) % 12u;
    out[1] = iv == 1u ? '#' : iv == 11u ? 'b' : '\0';
    out[2] = '\0';
}

static void theory_note(char out[4], uint32_t pc)
{
    theory_note_in_fifth(out, pc, theory_ui.fifth);
}

#define TH_NOTE(n) (1u << (n))
#define TH_PAIR(a, b) (1u | TH_NOTE(a) | TH_NOTE(b))
#define TH_SEVENTH(a, b, c) (TH_PAIR(a, b) | TH_NOTE(c))
static const struct {
    uint16_t mask;
    const char *suffix, *name;
} THEORY_CHORD[] = {
    {TH_PAIR(4,7), "", "major triad"},
    {TH_PAIR(3,7), "m", "minor triad"},
    {TH_PAIR(3,6), "dim", "diminished"},
    {TH_PAIR(4,8), "aug", "augmented"},
    {TH_PAIR(2,7), "sus2", "suspended 2"},
    {TH_PAIR(5,7), "sus4", "suspended 4"},
    {TH_SEVENTH(4,7,11), "maj7", "major seventh"},
    {TH_SEVENTH(4,7,10), "7", "dominant seventh"},
    {TH_SEVENTH(3,7,10), "m7", "minor seventh"},
    {TH_SEVENTH(3,6,10), "m7b5", "half diminished"},
    {TH_SEVENTH(3,6,9), "dim7", "diminished seventh"},
    {TH_SEVENTH(3,7,11), "mMaj7", "minor major seventh"},
    {TH_SEVENTH(4,7,9), "6", "major sixth"},
    {TH_SEVENTH(3,7,9), "m6", "minor sixth"},
    {TH_SEVENTH(2,4,7), "add9", "added ninth"},
    {TH_SEVENTH(2,3,7), "madd9", "minor added ninth"},
    {TH_SEVENTH(5,7,10), "7sus4", "suspended seventh"},
};
static const char *const THEORY_INTERVAL[12] = {
    "octave", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
    "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th",
};
static const char *const THEORY_COMPOUND[12] = {
    "octave", "minor 9th", "major 9th", "minor 10th", "major 10th", "perfect 11th",
    "augmented 11th", "perfect 12th", "minor 13th", "major 13th", "minor 14th", "major 14th",
};
static const char *const THEORY_ROMAN[2][7] = {
    {"I", "II", "III", "IV", "V", "VI", "VII"}, {"i", "ii", "iii", "iv", "v", "vi", "vii"},
};

static int32_t theory_match(uint32_t pitches, uint32_t bass, uint32_t *root)
{
    uint32_t i, r, relative, best = 0;
    int32_t match = -1;
    /* Bass takes priority for ambiguous sets like C6 / Am7. Then prefer a root in the selected mode. */
    for (r = 0; r < 12u; r++) {
        uint32_t score = r == bass ? 3u : 1u + ((theory_mask() >> theory_interval(r)) & 1u);
        if (score <= best || !(pitches & TH_NOTE(r)))
            continue;
        relative = ((pitches >> r) | (pitches << (12u - r))) & 0xFFFu;
        for (i = 0; i < sizeof THEORY_CHORD / sizeof THEORY_CHORD[0]; i++)
            if (relative == THEORY_CHORD[i].mask) {
                best = score;
                match = (int32_t)i;
                *root = r;
                break;
            }
    }
    return match;
}

/* Use the chord's letter intervals for altered notes, e.g. Bb in C7 and Bbb in Cdim7. */
static void theory_chord_note(char out[4], uint32_t pc, uint32_t root, uint32_t match)
{
    static const char LETTERS[] = "CDEFGAB";
    static const uint8_t PITCH[] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t LETTER_STEP[] = {0, 1, 1, 2, 2, 3, 4, 4, 4, 5, 6, 6};
    uint32_t iv = (pc + 12u - root) % 12u, letter = 0, n = 1;
    int32_t accidental;
    theory_note(out, root);
    while (LETTERS[letter] != out[0])
        letter++;
    letter = (letter + LETTER_STEP[iv] +
              (iv == 9u && THEORY_CHORD[match].mask == TH_SEVENTH(3,6,9))) % 7u;
    accidental = (int32_t)((pc + 18u - PITCH[letter]) % 12u) - 6;
    out[0] = LETTERS[letter];
    while (accidental && n < 3u) {
        out[n++] = accidental > 0 ? '#' : 'b';
        accidental += accidental > 0 ? -1 : 1;
    }
    out[n] = '\0';
}

static void theory_append(char *out, const char *tail, uint32_t size)
{
    uint32_t used = str_len(out);
    str_cpy(out + used, tail, size - used);
}

static void theory_key_name(char out[24])
{
    theory_note(out, theory_root());
    theory_append(out, " ", 24);
    theory_append(out, THEORY_MODE[theory_ui.mode].name, 24);
}

static void theory_heading(const char *title, const char *detail)
{
    cv_rect(12, 15, 4, 13, TH_SCALE);
    cv_text(23, 14, &AF_M, title, TH_TEXT);
    cv_text_r(228, 16, &AF_S, detail, TH_SCALE, TH_BG);
    cv_rect(12, 37, 216, 1, TH_RULE);
}

static void theory_live_top(uint32_t held)
{
    char title[20] = "", detail[20] = "", notes[88] = "", bass_name[4], name[4], key[24], degree[8] = "";
    const char *subtitle = "Hold notes to identify them";
    uint32_t pitches = 0, bass = 0, highest = 0, root = 0, count = 0, k;
    int32_t match;
    uint16_t accent = TH_PLAY;
    theory_key_name(key);
    cv_begin(240, 120, TH_BG);
    theory_heading("Theory", key);
    for (k = 0; k < 27u; k++) {
        uint32_t pitch;
        if (!(held & TH_NOTE(k)))
            continue;
        pitch = kb_note[k];
        if (!count || pitch < bass)
            bass = pitch;
        if (pitch > highest)
            highest = pitch;
        count++;
        pitches |= TH_NOTE(pitch % 12u);
    }
    match = theory_match(pitches, bass % 12u, &root);
    if (!count) {
        str_cpy(title, "Play a note", sizeof title);
        accent = TH_TEXT;
    } else if (match >= 0) {
        theory_note(name, root);
        title[0] = name[0];
        str_cpy(detail, name + 1, sizeof detail);
        theory_append(detail, THEORY_CHORD[match].suffix, sizeof detail);
        subtitle = THEORY_CHORD[match].name;
        if (root != bass % 12u) {
            theory_chord_note(bass_name, bass % 12u, root, (uint32_t)match);
            theory_append(detail, "/", sizeof detail);
            theory_append(detail, bass_name, sizeof detail);
        }
        if ((theory_mask() >> theory_interval(root)) & 1u) {
            uint32_t minor = (THEORY_CHORD[match].mask & TH_NOTE(3)) != 0u;
            str_cpy(degree, THEORY_ROMAN[minor][theory_degree(theory_interval(root))], sizeof degree);
        }
    } else if (count == 1u) {
        theory_note(name, bass % 12u);
        title[0] = name[0];
        str_cpy(detail, name + 1, sizeof detail);
        fmt_int(detail + str_len(detail), (int32_t)(bass / 12u) - 1 +
                (name[0] == 'C' && name[1] == 'b'));
        subtitle = "single note";
    } else if (count == 2u) {
        uint32_t semitones = highest - bass;
        str_cpy(title, "Interval", sizeof title);
        if (semitones == 0u)
            subtitle = "unison";
        else if (semitones < 12u)
            subtitle = THEORY_INTERVAL[semitones];
        else if (semitones < 24u)
            subtitle = THEORY_COMPOUND[semitones - 12u];
        else
            subtitle = semitones == 24u ? "two octaves" : "extended interval";
    } else {
        str_cpy(title, "Note cluster", sizeof title);
        subtitle = "Unrecognized chord shape";
    }
    for (k = 0; k < 12u; k++)
        if ((pitches & TH_NOTE(k)) && !(theory_mask() & TH_NOTE(theory_interval(k))))
            accent = TH_OUT;
    if (count == 1u || match >= 0) {
        cv_text(12, 42, &AF_L, title, accent);
        cv_text(16 + text_w(&AF_L, title), 52, &AF_M, detail, accent);
    } else {
        cv_text(12, 51, &AF_M, title, accent);
    }
    cv_text(13, 75, &AF_S, subtitle, TH_MUTED);
    for (k = 0; k < 27u; k++) {
        uint32_t pc;
        if (!(held & TH_NOTE(k)))
            continue;
        pc = kb_note[k] % 12u;
        if (match >= 0)
            theory_chord_note(name, pc, root, (uint32_t)match);
        else
            theory_note(name, pc);
        if (notes[0])
            theory_append(notes, "  ", sizeof notes);
        theory_append(notes, name, sizeof notes);
    }
    if (count) {
        uint16_t box = TH_SURF;
        cv_rrect(180, 46, 48, 40, 6, box, TH_BG);
        cv_text_in(180, 49, 48, &AF_M, degree[0] ? degree : "Scale", TH_TEXT, box);
        cv_text_in(180, 69, 48, &AF_S, accent == TH_OUT ? "Outside" : "In scale", accent, box);
    }
    cv_rect(12, 94, 216, 23, TH_SURF);
    cv_text_on(19, 99, &AF_S, "Notes", TH_MUTED, TH_SURF);
    cv_free_text(55, 99, &AF_S, notes[0] ? notes : "Press any key", TH_TEXT, TH_SURF, 165);
    cv_blit(0, 0);
}

static uint16_t theory_key_color(uint32_t k, uint32_t held, uint32_t lit, int black)
{
    uint32_t bit = TH_NOTE(k);
    if (held & bit)
        return lit & bit ? TH_PLAY : TH_OUT;
    if (lit & bit)
        return black ? RGB(107, 91, 178) : TH_SCALE;
    return black ? RGB(25, 31, 55) : RGB(87, 100, 134);
}

static void theory_piano_overlay(uint32_t held, uint32_t lit, uint32_t roots,
                                 uint32_t guide, uint32_t alternate)
{
    uint32_t k;
    int32_t x = 12;
    for (k = 0; k < 27u; k++) {
        if (key_black(k))
            continue;
        uint16_t color = theory_key_color(k, held, lit, 0);
        cv_rect(x, 5, 12, 49, color);
        if ((guide | alternate) & TH_NOTE(k))
            cv_rect(x, 50, 12, 4, alternate & TH_NOTE(k) ? TH_OUT : TH_CHORD);
        if (roots & TH_NOTE(k))
            cv_rect(x + 4, 44, 4, 4, TH_BG);
        x += 14;
    }
    x = 12;
    for (k = 0; k < 27u; k++) {
        if (!key_black(k)) {
            x += 14;
            continue;
        }
        cv_rect(x - 6, 5, 11, 32, TH_BG);
        cv_rect(x - 5, 5, 9, 29, theory_key_color(k, held, lit, 1));
        if ((guide | alternate) & TH_NOTE(k))
            cv_rect(x - 5, 30, 9, 4, alternate & TH_NOTE(k) ? TH_OUT : TH_CHORD);
        if (roots & TH_NOTE(k))
            cv_rect(x - 2, 25, 3, 3, TH_TEXT);
    }
}

static void theory_degrees(void)
{
    uint32_t degree = 0, iv, tones[7], i;
    for (iv = 0; iv < 12u; iv++)
        if (theory_mask() & TH_NOTE(iv))
            tones[degree++] = iv;
    for (i = 0; i < degree; i++) {
        char note[8], roman[8];
        int32_t x = 12 + 31 * (int32_t)i;
        uint32_t third = (tones[(i + 2u) % degree] + 12u - tones[i]) % 12u;
        uint32_t fifth = (tones[(i + 4u) % degree] + 12u - tones[i]) % 12u;
        uint16_t box = i ? TH_SURF : TH_SCALE, ink = i ? TH_TEXT : TH_BG;
        theory_note(note, (theory_root() + tones[i]) % 12u);
        theory_append(note, fifth == 6u ? "dim" : fifth == 8u ? "aug" : third == 3u ? "m" : "", sizeof note);
        str_cpy(roman, THEORY_ROMAN[third == 3u][i], sizeof roman);
        theory_append(roman, fifth == 6u ? "\xB0" : fifth == 8u ? "+" : "", sizeof roman);
        cv_rect(x, 71, 28, 28, box);
        cv_text_in(x, 72, 28, &AF_S, roman, i ? TH_SCALE : TH_BG, box);
        cv_text_in(x, 85, 28, &AF_S, note, ink, box);
    }
}

static void theory_backing_label(char out[32])
{
    uint32_t root = theory_ui.bass;
    int32_t match = theory_match(theory_ui.backing, theory_ui.bass, &root);
    char name[4];
    str_cpy(out, "Backing T", 32);
    fmt_int(out + str_len(out), theory_ui.track);
    theory_append(out, "  ", 32);
    if (match >= 0 || (theory_ui.backing && !(theory_ui.backing & (theory_ui.backing - 1u)))) {
        theory_note(name, root);
        theory_append(out, name, 32);
        if (match >= 0)
            theory_append(out, THEORY_CHORD[match].suffix, 32);
    } else {
        theory_append(out, theory_ui.backing ? "Chord tones" : song.playing ? "Waiting for notes" : "Press PLAY", 32);
    }
}

static void theory_live_bottom(uint32_t held)
{
    uint32_t roots, lit = theory_scale_leds(&roots);
    cv_begin(240, 120, TH_BG);
    theory_piano_overlay(held, lit, roots, theory_backing_keys(), 0);
    if (theory_ui.track) {
        char backing[32];
        theory_backing_label(backing);
        cv_text(12, 56, &AF_S, backing, TH_CHORD);
        if (theory_ui.backing)
            cv_text_r(234, 56, &AF_S, "Blue = chord tone", TH_CHORD, TH_BG);
    } else {
        cv_text(12, 56, &AF_S, "Diatonic triads", TH_MUTED);
        cv_text_r(234, 56, &AF_S, "Dot = tonic", TH_MUTED, TH_BG);
    }
    theory_degrees();
    cv_text(12, 105, &AF_S, "K1 Key  K2 Mode  K3 Back", TH_MUTED);
    cv_text_r(228, 105, &AF_S, "K4 Lab", TH_SCALE, TH_BG);
    cv_blit(0, 120);
}

static const int8_t THEORY_WHEEL_X[] = {0, 46, 80, 92, 80, 46, 0, -46, -80, -92, -80, -46};
static const int8_t THEORY_WHEEL_Y[] = {-81, -70, -40, 0, 40, 70, 81, 70, 40, 0, -40, -70};

static void theory_wheel_half(uint32_t lower)
{
    int32_t cy = 132 - 120 * (int32_t)lower;
    char key[24];
    uint32_t i;
    theory_key_name(key);
    cv_begin(240, 120, TH_BG);
    if (!lower)
        theory_heading("Circle of fifths", key);
    for (i = 0; i < 12u; i++) {
        uint32_t j = (i + 1u) % 12u;
        cv_line(120 + THEORY_WHEEL_X[i], cy + THEORY_WHEEL_Y[i], 120 + THEORY_WHEEL_X[j], cy + THEORY_WHEEL_Y[j], TH_RULE);
        cv_line(120 + THEORY_WHEEL_X[i] * 3 / 5, cy + THEORY_WHEEL_Y[i] * 3 / 5,
                120 + THEORY_WHEEL_X[j] * 3 / 5, cy + THEORY_WHEEL_Y[j] * 3 / 5, TH_RULE);
    }
    cv_line_t(120, cy, 120 + THEORY_WHEEL_X[theory_ui.fifth], cy + THEORY_WHEEL_Y[theory_ui.fifth], TH_SCALE, 2);
    for (i = 0; i < 12u; i++) {
        int32_t x = 120 + THEORY_WHEEL_X[i], y = cy + THEORY_WHEEL_Y[i];
        int32_t mx = 120 + THEORY_WHEEL_X[i] * 3 / 5, my = cy + THEORY_WHEEL_Y[i] * 3 / 5;
        uint16_t major = i == theory_ui.fifth ? TH_SCALE : TH_SURF;
        uint16_t minor = i == theory_ui.fifth && theory_ui.mode == 5u ? TH_PLAY : TH_BG;
        cv_rrect(x - 15, y - 8, 30, 17, 5, major, TH_BG);
        cv_rrect(mx - 14, my - 8, 28, 17, 4, minor, TH_BG);
        if (y - 8 >= 0 && y + 8 < 120)
            cv_text_in(x - 15, y - 7, 30, &AF_S, THEORY_MAJOR[i], i == theory_ui.fifth ? TH_BG : TH_TEXT, major);
        if (my - 8 >= 0 && my + 8 < 120)
            cv_text_in(mx - 14, my - 7, 28, &AF_S, THEORY_MINOR[i], minor == TH_PLAY ? TH_BG : TH_MUTED, minor);
    }
    cv_rrect(92, cy - 30, 56, 50, 8, TH_SURF, TH_BG);
    if (!lower) {
        cv_text_in(92, 108, 56, &AF_S, "Parent key", TH_MUTED, TH_SURF);
    } else {
        cv_text_in(92, 9, 56, &AF_M, THEORY_MAJOR[theory_ui.fifth], TH_SCALE, TH_SURF);
        cv_text(12, 105, &AF_S, "K1 Fifths   K2 Mode", TH_MUTED);
        cv_text_r(228, 105, &AF_S, "SCL Play", TH_SCALE, TH_BG);
    }
    cv_blit(0, lower * 120u);
}

#include "ui_theory_lab.c"

static void theory_draw(void)
{
    static uint32_t previous = ~0u;
    uint32_t held = theory_keys;
    uint32_t scene = theory_ui.fifth | (uint32_t)theory_ui.mode << 4 | (uint32_t)theory_ui.wheel << 7 |
                     (uint32_t)(theory_octave + 3) << 8;
    static uint32_t previous_held, previous_backing;
    uint32_t backing = theory_ui.backing | (uint32_t)theory_ui.bass << 12 |
                       (uint32_t)theory_ui.track << 16 | (uint32_t)(song.playing != 0) << 20;
    uint8_t mono = ux.mono, light = ux.light;
    scene |= (uint32_t)theory_lab.page << 12;
    if (!ui.force && previous == scene && (theory_ui.wheel || (previous_held == held && previous_backing == backing)))
        return;
    ux.mono = ux.light = 0;
    ux.gen++;
    if (theory_lab.page) {
        theory_lab_draw(held);
    } else if (theory_ui.wheel) {
        theory_wheel_half(0);
        theory_wheel_half(1);
    } else {
        theory_live_top(held);
        theory_live_bottom(held);
    }
    ux.mono = mono;
    ux.light = light;
    ux.gen++;
    previous = scene;
    previous_held = held;
    previous_backing = backing;
    ui.force = 0;
}
