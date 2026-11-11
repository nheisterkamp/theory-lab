/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int screenshot(const char *dir, const char *name)
{
    char path[1024];
    FILE *file;
    if (!dir)
        return 0;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    file = fopen(path, "wb");
    if (!file)
        return 1;
    fprintf(file, "P6\n240 240\n255\n");
    for (uint32_t i = 0; i < 240u * 240u; i++) {
        uint16_t pixel = swap16(host_screen[i]);
        fputc(((pixel >> 11) & 31u) * 255u / 31u, file);
        fputc(((pixel >> 5) & 63u) * 255u / 63u, file);
        fputc((pixel & 31u) * 255u / 31u, file);
    }
    return fclose(file) != 0;
}

static void play_keys(uint32_t keys)
{
    fm1_in.notes = keys;
    keyboard_block();
    frame();
}

static void lab_keys(uint32_t keys)
{
    fm1_in.notes = keys;
    keyboard_block();
    ui_leds();
    frame();
}

static void lab_audio(uint32_t samples)
{
    events_block(samples);
    ui_leds();
    frame();
}

static uint32_t lab_one_key_each(uint32_t pitches)
{
    uint32_t keys = 0;
    /* Start at C4, so whichever chord is asked may be answered in an inversion. */
    for (uint32_t k = 7; k < 19u; k++)
        if (pitches & TH_NOTE((53u + k) % 12u))
            keys |= TH_NOTE(k);
    return keys;
}

static int lab_ear_test(const char *shots)
{
    uint32_t target, wrong = 0, pc, scale, roots, guide, alt;
    int bad = 0;
    turn(EN_K4, 2);
    press(B_PLAY);
    lab_audio(64);
    target = theory_lab.ear.target;
    bad += check("Chord ear plays a generated triad through the practice synth",
                 theory_lab.page == TH_EAR && theory_cardinality(target) == 3 && theory_preview_playing);
    press(B_SCL);
    theory_lab_guides(&scale, &roots, &guide, &alt);
    bad += check("Blind chord ear disables answer and scale LEDs", !scale && !roots && !guide);
    fm1_ms += 700;
    lab_audio(64);
    lab_audio(64);
    bad += check("Chord playback stops before the response is scored", !theory_preview_playing);
    for (pc = 0; pc < 12u && theory_cardinality(wrong) < 3u; pc++)
        if (!(target & TH_NOTE(pc)))
            wrong |= TH_NOTE(pc);
    lab_keys(lab_one_key_each(wrong));
    bad += check("A wrong answer reports the exact extra and missing pitches",
                 theory_lab.ear.result == 2u && theory_lab.ear.answer == wrong);
    bad += screenshot(shots, "07-chord-ear");
    lab_keys(0);
    lab_keys(lab_one_key_each(target));
    bad += check("A correct pitch-class set is accepted in any inversion", theory_lab.ear.result == 1u);
    lab_keys(0);
    press(B_PLAY);
    lab_audio(64);
    turn(EN_K4, 1);
    lab_audio(64);
    bad += check("Leaving the lesson releases its preview chord", !theory_preview_playing);
    return bad;
}

static int lab_riff_test(const char *shots)
{
    int bad = 0;
    theory_ui.fifth = 2;                       /* D Ionian */
    theory_ui.mode = 0;
    turn(EN_K2, -2);                            /* D Aeolian */
    press(B_REC);
    lab_keys(TH_NOTE(9));                       /* D4 */
    fm1_ms += 120;
    lab_keys(0);
    fm1_ms += 40;
    lab_keys(TH_NOTE(17));                      /* Bb4 */
    fm1_ms += 160;
    lab_keys(0);
    press(B_REC);
    bad += check("Mode switch records note timing and note releases",
                 theory_lab.page == TH_RIFF && theory_lab.riff.count == 2 &&
                 theory_lab.riff.event[1].note == 70 && theory_lab.riff.event[1].end > theory_lab.riff.event[1].at);
    turn(EN_K2, 3);                             /* D Dorian */
    press(B_PLAY);
    fm1_ms = theory_lab.riff.started + theory_lab.riff.event[1].at + 1u;
    lab_audio(64);
    lab_audio(64);
    bad += check("D Aeolian to D Dorian raises Bb to B during loop playback",
                 theory_root() == 2 && theory_lab.riff.compare && theory_preview_playing == 72u);
    bad += screenshot(shots, "08-mode-switch");
    press(B_SCL);
    lab_audio(64);
    lab_audio(64);
    bad += check("A/B playback restores the original Bb", theory_preview_playing == 71u);
    press(B_PLAY);
    lab_audio(64);
    bad += check("Stopping the riff releases its note", !theory_preview_playing);
    return bad;
}

static int lab_passport_test(const char *shots)
{
    uint8_t keys[24], degrees[24];
    uint32_t n, i, known = 0, selection = 0;
    int bad = 0;
    turn(EN_K4, 1);
    lab_keys(TH_NOTE(9) | TH_NOTE(12) | TH_NOTE(16));   /* Dm */
    lab_keys(0);
    n = theory_pass_matches(keys, degrees);
    for (i = 0; i < n; i++) {
        if (keys[i] == 0 && degrees[i] == 1)
            known |= 1u;                         /* ii in C */
        if (keys[i] == 11 && degrees[i] == 5) {
            known |= 2u;                         /* vi in F */
            selection = i;
        }
        if (keys[i] == 23 && degrees[i] == 0)
            known |= 4u;                         /* i in Dm */
    }
    bad += check("Passport keeps Dm after release and finds ii in C, vi in F, i in D minor",
                 theory_lab.page == TH_PASS && known == 7u);
    bad += screenshot(shots, "09-chord-passport");
    turn(EN_K1, (int32_t)selection);
    press(B_SCL);
    bad += check("Passport applies the selected key to the live screen",
                 theory_lab.page == TH_LIVE && theory_root() == 5 && theory_ui.mode == 0);
    return bad;
}

static int lab_mission_test(const char *shots)
{
    uint32_t period = div_samples((uint32_t)trk[1].p[P_SDIV]), scale, roots, guide, alt;
    int bad = 0;
    turn(EN_K4, -1);
    turn(EN_K1, 1);                            /* Third */
    press(B_REC);
    press(B_PLAY);
    lab_audio(64);                             /* G7 */
    bad += check("A third landing round finds B in G7 and lights its keys",
                 theory_lab.page == TH_MISSION && theory_lab.mission.pending && theory_lab.mission.note == 11);
    theory_lab_guides(&scale, &roots, &guide, &alt);
    bad += check("Landing guides show the selected chord tone", (guide & TH_NOTE(6)) != 0);
    press(B_SCL);
    theory_lab_guides(&scale, &roots, &guide, &alt);
    bad += check("Blind landing hides the target and scale", !guide && !scale);
    lab_keys(TH_NOTE(6));                       /* B, hit */
    lab_keys(0);
    lab_audio(period);                         /* C */
    lab_keys(TH_NOTE(11));                      /* E, hit */
    lab_keys(0);
    lab_audio(period);                         /* G7 */
    lab_keys(TH_NOTE(16));                      /* A, miss */
    lab_keys(0);
    lab_audio(period);                         /* C */
    fm1_ms += TH_LAND_MS;                      /* No attack, miss */
    ui_leds();
    frame();
    bad += check("Landing mission grades hits, wrong notes and missed windows",
                 theory_lab.mission.count == 4 && theory_lab.mission.hits == 2 &&
                 theory_lab.mission.grade[0] == 2 && theory_lab.mission.grade[1] == 2 &&
                 theory_lab.mission.grade[2] == 1 && theory_lab.mission.grade[3] == 1);
    bad += screenshot(shots, "10-landing-mission");
    press(B_PLAY);
    lab_audio(64);
    return bad;
}

int main(int argc, char **argv)
{
    int bad = 0;
    uint32_t root, roots, lit, played_pitch;
    int32_t saved_octave;
    char note[4];
    int32_t chord;
    const char *shots = argc > 1 ? argv[1] : NULL;
    ui_power_on();
    TSEL->p[P_VOICE] = V_POLY;
    TSEL->p[P_QUANT] = QN_WHITE;
    TSEL->p[P_CHRD] = CH_DIA7;
    TSEL->p[P_TRANS] = 5;
    TSEL->p[P_AMODE] = 1;
    saved_octave = song.octave;
    btn_down(B_HOME);
    frame();
    press(B_ARP);
    btn_up(B_HOME);
    frame();
    bad += check("HOME + ARP opens Theory without opening the menu", theory_active && !ui.menu);
    lit = theory_scale_leds(&roots);
    bad += check("C major guides light B and C, leaving C# dark",
                 (lit & (1u << 6)) && (roots & (1u << 7)) && !(lit & (1u << 8)));
    play_keys((1u << 9) | (1u << 12) | (1u << 16));
    bad += check("D-F-A sounds as three chromatic keys despite scale, chord and arp settings",
                 kb_note[9] == 62 && kb_note[12] == 65 && kb_note[16] == 69 &&
                 kb_chn[9] == 1u && kb_chn[12] == 1u && kb_chn[16] == 1u && !TSEL->nheld);
    bad += check("D-F-A is D minor and C-E-G-A is C6 with C in the bass",
                 theory_match(TH_NOTE(2) | TH_NOTE(5) | TH_NOTE(9), 2, &root) == 1 && root == 2 &&
                 theory_match(TH_NOTE(0) | TH_NOTE(4) | TH_NOTE(7) | TH_NOTE(9), 0, &root) == 12 && root == 0);
    bad += screenshot(shots, "01-live-dm");
    play_keys((1u << 7) | (1u << 11) | (1u << 14) | (1u << 17));
    chord = theory_match(TH_NOTE(0) | TH_NOTE(4) | TH_NOTE(7) | TH_NOTE(10), 0, &root);
    if (chord >= 0)
        theory_chord_note(note, 10, root, (uint32_t)chord);
    bad += check("C7 spells the seventh Bb rather than A#", chord >= 0 && str_eq(note, "Bb"));
    bad += screenshot(shots, "02-outside-c7");
    play_keys(0);
    turn(EN_K1, 2);
    turn(EN_K2, 1);
    bad += check("Changing mode preserves the tonic and selects the correct parent key",
                 theory_root() == 2u && theory_ui.mode == 1u && theory_ui.fifth == 0u);
    press(B_SCL);
    bad += screenshot(shots, "03-circle-d-dorian");
    press(B_SCL);
    play_keys((1u << 9) | (1u << 12) | (1u << 16));
    bad += screenshot(shots, "04-live-d-dorian");
    played_pitch = kb_note[9];
    press(B_OCTUP);
    bad += check("Octave changes apply to new key presses without changing the project octave",
                 theory_octave == 1 && song.octave == saved_octave && kb_note[9] == played_pitch);
    press(B_HOME);
    play_keys(0);
    bad += check("Leaving Theory with held notes releases them and keeps the patch settings",
                 !theory_active && !theory_keys && !kb_chn[9] &&
                 TSEL->p[P_QUANT] == QN_WHITE && TSEL->p[P_CHRD] == CH_DIA7 && TSEL->p[P_AMODE] == 1);
    TSEL->p[P_VOICE] = V_MONO;
    trk[1].p[P_VOICE] = V_POLY;
    btn_down(B_HOME);
    frame();
    press(B_ARP);
    btn_up(B_HOME);
    frame();
    bad += check("A mono patch falls back to a polyphonic track without changing selection",
                 theory_active && theory_track == 1 && song.sel == 0);
    press(B_HOME);
    TSEL->p[P_VOICE] = V_POLY;
    btn_down(B_HOME);
    frame();
    press(B_ARP);
    btn_up(B_HOME);
    frame();
    theory_ui.fifth = theory_ui.mode = 0;
    theory_octave = 0;
    trk[1].p[P_SLEN] = 2;
    trk[1].step[0] = (step_t){.time = ST_NOTE, .n = 4, .note = {55, 59, 62, 65}};
    trk[1].step[1] = (step_t){.time = ST_NOTE, .n = 3, .note = {60, 64, 67}};
    turn(EN_K3, 2);
    press(B_PLAY);
    events_block(64);
    ui_leds();
    frame();
    bad += check("Backing track reads G7 from the running sequencer and marks G across the keyboard",
                 song.playing && theory_ui.track == 2 &&
                 theory_ui.backing == (TH_NOTE(7) | TH_NOTE(11) | TH_NOTE(2) | TH_NOTE(5)) &&
                 (theory_backing_keys() & (TH_NOTE(2) | TH_NOTE(14) | TH_NOTE(26))) ==
                 (TH_NOTE(2) | TH_NOTE(14) | TH_NOTE(26)));
    play_keys(TH_NOTE(9) | TH_NOTE(12));
    bad += screenshot(shots, "05-backing-g7");
    turn(EN_K4, 1);
    ui_leds();
    frame();
    bad += check("Chord GPS forecasts C and retains the shared G from G7",
                 theory_lab.page == TH_GPS &&
                 theory_lab.gps.next == (TH_NOTE(0) | TH_NOTE(4) | TH_NOTE(7)) &&
                 (theory_lab.gps.next & theory_ui.backing) == TH_NOTE(7));
    {
        const uint8_t from[4] = {5, 2, 11}, to[4] = {4, 0};
        uint8_t result[4];
        theory_gps_pair(from, 3, to, 3, result);
        bad += check("Voice leading pairs F with E and B with C", result[0] == 4 && result[2] == 0);
    }
    bad += screenshot(shots, "06-chord-gps");
    turn(EN_K4, -1);
    events_block(div_samples((uint32_t)trk[1].p[P_SDIV]));
    ui_leds();
    frame();
    bad += check("The next sequencer step switches the harmonic guide to C",
                 theory_ui.backing == (TH_NOTE(0) | TH_NOTE(4) | TH_NOTE(7)));
    press(B_PLAY);
    events_block(64);
    ui_leds();
    frame();
    bad += check("Stopping the backing track clears its lights", !song.playing && !theory_backing_keys());
    play_keys(0);
    bad += lab_ear_test(shots);
    bad += lab_riff_test(shots);
    bad += lab_passport_test(shots);
    bad += lab_mission_test(shots);
    return bad != 0;
}
