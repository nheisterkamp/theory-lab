/* SPDX-License-Identifier: GPL-3.0-only */
/* Theory Lab's primary navigation, sound palette and backing selection. */
static const struct {
    const char *name, *description;
    uint8_t lesson;
} THEORY_HOME[TH_APP_ITEMS] = {
    {"Explore", "See what your notes are doing", TH_LIVE},
    {"Play along", "Choose a chord progression", TH_LIVE},
    {"Chord GPS", "Find the smoothest next move", TH_GPS},
    {"Ear training", "Hear a chord and play it back", TH_EAR},
    {"Mode switch", "Hear one riff in different modes", TH_RIFF},
    {"Chord passport", "Find the keys your chord fits", TH_PASS},
    {"Landing mission", "Aim for a chord tone in time", TH_MISSION},
    {"Guitar chords", "Turn harmony into finger positions", TH_LIVE},
    {"Sounds", "Choose a clear practice sound", TH_LIVE},
    {"Advanced", "Synth, effects and sequencer", TH_LIVE},
    {"Device settings", "Display, MIDI and audio options", TH_LIVE},
};
static const struct {
    const char *name, *description;
    uint8_t engine, preset;
} THEORY_SOUNDS[] = {
    {"Piano", "Natural attack, clear harmony", ENGI_SAMPLE, 0},
    {"Electric piano", "Soft attack and a rounded tone", ENGI_FM6, 0},
    {"Organ", "Steady notes for hearing intervals", ENGI_FM6, 6},
    {"Soft synth", "A gentle, plucked synthesizer", ENGI_FM6, 7},
};
#define TH_SOUNDS (sizeof THEORY_SOUNDS / sizeof THEORY_SOUNDS[0])
static const struct {
    const char *name, *description;
    uint8_t n, seventh, minor, degree[7];
} THEORY_PROGRESSIONS[] = {
    {"Pop loop", "I - V - vi - IV", 4, 0, 0, {0, 4, 5, 3}},
    {"Jazz landing", "ii7 - V7 - Imaj7", 3, 1, 0, {1, 4, 0}},
    {"Minor journey", "i - VI - III - VII", 4, 0, 1, {0, 5, 2, 6}},
    {"Three chords", "I - IV - V - I", 4, 0, 0, {0, 3, 4, 0}},
    {"Around the key", "I - IV - vii - iii - vi - ii - V", 7, 0, 0, {0, 3, 6, 2, 5, 1, 4}},
};
#define TH_PROGRESSIONS (sizeof THEORY_PROGRESSIONS / sizeof THEORY_PROGRESSIONS[0])

static void theory_home_draw(void)
{
    uint32_t band, first = theory_app.selected / 4u * 4u;
    char key[24], tempo[12];
    theory_key_name(key);
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, TH_BG);
        if (!band) {
            theory_heading(theory_app.sounds ? "Sounds" : theory_app.backing ? "Play along" : "Theory Lab", key);
            if (theory_app.sounds || theory_app.backing) {
                const char *name = theory_app.sounds ? THEORY_SOUNDS[theory_app.sound_cursor].name :
                                   THEORY_PROGRESSIONS[theory_app.progression].name;
                const char *desc = theory_app.sounds ? THEORY_SOUNDS[theory_app.sound_cursor].description :
                                   THEORY_PROGRESSIONS[theory_app.progression].description;
                cv_text(12, 50, &AF_M, name, TH_PLAY);
                cv_free_text(12, 81, &AF_S, desc, TH_MUTED, TH_BG, 216);
                cv_text(12, 102, &AF_S, theory_app.sounds ? "Turn PRESETS to compare sounds" : "K1 Progression   K2 Tempo", TH_MUTED);
            }
        }
        if (theory_app.sounds || theory_app.backing) {
            if (band) {
                uint32_t roots, lit = theory_scale_leds(&roots);
                theory_piano_overlay(theory_keys, lit, roots, 0, 0);
                if (theory_app.backing) {
                    fmt_int(tempo, song.g[G_BPM]);
                    theory_append(tempo, " BPM", sizeof tempo);
                    cv_text(12, 66, &AF_M, tempo, TH_CHORD);
                    cv_text(12, 79, &AF_S, "One chord per bar. PLAY starts the loop.", TH_MUTED);
                } else {
                    cv_text(12, 66, &AF_S, theory_app.sound_cursor == theory_app.sound ? "Selected practice sound" : "Press PLAY to select and hear it", TH_MUTED);
                }
            }
        } else {
            for (uint32_t row = first; row < TH_APP_ITEMS && row < first + 4u; row++) {
                int32_t y = 44 + (int32_t)(row - first) * 40 - (int32_t)band * 120;
                uint16_t bg = row == theory_app.selected ? TH_SURF : TH_BG;
                if (y + 37 < 0 || y >= 120)
                    continue;
                cv_rect(12, y, 216, 37, bg);
                if (y >= 0 && y + 18 < 120)
                    cv_text_on(22, y + 1, &AF_M, THEORY_HOME[row].name,
                               row == theory_app.selected ? TH_PLAY : TH_TEXT, bg);
                if (y + 20 >= 0 && y + 35 < 120)
                    cv_free_text(22, y + 20, &AF_S, THEORY_HOME[row].description, TH_MUTED, bg, 201);
            }
            cv_rect(12, theory_app.nav - (int32_t)band * 120 + 4, 3, 29, TH_PLAY);
            cv_rect(234, 44 - (int32_t)band * 120, 2, 159, TH_RULE);
            cv_rect(234, 44 + (int32_t)(theory_app.selected * 145u / (TH_APP_ITEMS - 1u)) -
                    (int32_t)band * 120, 2, 14, TH_SCALE);
        }
        if (band) {
            cv_rect(12, 94, 216, 1, TH_RULE);
            cv_text(12, 101, &AF_S, theory_app.sounds ? "PRESETS Choose   PLAY Hear" :
                    theory_app.backing ? "PLAY Start   HOME Menu" : "SELECT Choose   PLAY Open", TH_MUTED);
        }
        cv_blit(0, band * 120u);
    }
}

static void theory_splash(void)
{
    uint8_t mono = ux.mono, light = ux.light;
    ux.mono = ux.light = 0;
    ux.gen++;
    cv_begin(240, 120, TH_BG);
    cv_text_in(12, 40, 216, &AF_M, "Theory Lab", TH_TEXT, TH_BG);
    cv_text_in(12, 71, 216, &AF_S, FELUCCA_VERSION, TH_SCALE, TH_BG);
    for (uint32_t i = 0; i < 3u; i++) {
        int32_t x = 88 + (int32_t)i * 32;
        cv_rect(x, 103 - (int32_t)i * 7, 13, 4, TH_PLAY);
    }
    cv_blit(0, 0);
    cv_begin(240, 120, TH_BG);
    cv_text_in(12, 16, 216, &AF_S, "Notes, chords and the space between", TH_MUTED, TH_BG);
    cv_text_in(12, 71, 216, &AF_S, "Built on Felucca", TH_SCALE, TH_BG);
    cv_text_in(12, 89, 216, &AF_S, "Hugelton Instruments", TH_MUTED, TH_BG);
    cv_blit(0, 120);
    ux.mono = mono;
    ux.light = light;
    ux.gen++;
}
