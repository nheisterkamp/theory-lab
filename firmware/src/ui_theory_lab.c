/* SPDX-License-Identifier: GPL-3.0-only */
/* Interactive lessons. Included by ui_theory.c after its drawing and theory helpers. */
enum { TH_LIVE, TH_GPS, TH_EAR, TH_RIFF, TH_PASS, TH_MISSION, TH_PAGES };
enum { TH_RIFF_MAX = 24, TH_RIFF_MS = 12000, TH_MISSION_LEN = 4, TH_LAND_MS = 500 };
static const char *const TH_PAGES_NAME[] = {
    "Theory", "Chord GPS", "Chord ear", "Mode switch", "Chord passport", "Landing mission",
};
static struct {
    uint8_t page;
    uint32_t held;
    struct {
        uint16_t next;
        uint8_t bass;
    } gps;
    struct {
        uint32_t heard_at, seed;
        uint16_t target, answer;
        uint8_t root, heard, blind, result, ready;
    } ear;
    struct {
        uint32_t started;
        struct { uint16_t at, end; uint8_t note; } event[TH_RIFF_MAX];
        uint16_t duration;
        uint8_t count, root, source, target, recording, playing, compare, sound;
        uint8_t live_key, original;
    } riff;
    struct {
        uint16_t pitches;
        uint8_t bass, cursor;
    } passport;
    struct {
        uint32_t changed_at;
        uint16_t chord;
        uint8_t bass, note, degree, blind, armed, count, hits, pending;
        uint8_t grade[TH_MISSION_LEN];
    } mission;
} theory_lab;

static uint32_t theory_lab_pitch_mask(uint32_t keys)
{
    uint32_t pc = 0, k;
    for (k = 0; keys && k < 27u; k++)
        if (keys & TH_NOTE(k))
            pc |= TH_NOTE(kb_note[k] % 12u);
    return pc;
}

static uint32_t theory_first_key(uint32_t keys)
{
    uint32_t k;
    for (k = 0; k < 27u && !(keys & TH_NOTE(k)); k++)
        ;
    return k;
}

static uint32_t theory_cardinality(uint32_t mask)
{
    uint32_t count = 0;
    for (; mask; mask &= mask - 1u)
        count++;
    return count;
}

static uint32_t theory_mode_tone(uint32_t mode, uint32_t degree)
{
    uint32_t mask = SCALE_MASK[THEORY_MODE[mode].scale], i;
    for (i = 0; i < 12u; i++) {
        if (!(mask & TH_NOTE(i)))
            continue;
        if (!degree)
            return i;
        degree--;
    }
    return 0;
}

static uint32_t theory_stack(uint32_t root, uint32_t mode, uint32_t degree, uint32_t count)
{
    uint32_t mask = 0, i;
    for (i = 0; i < count; i++)
        mask |= TH_NOTE((root + theory_mode_tone(mode, (degree + 2u * i) % 7u)) % 12u);
    return mask;
}

static uint32_t theory_preview_pitches(uint32_t mask, uint32_t root)
{
    uint32_t base = 60u + root, i, n = 0, packed = 0;
    for (i = 0; i < 12u && n < 4u; i++)
        if (mask & TH_NOTE((root + i) % 12u))
            packed |= (base + i + 1u) << (8u * n++);
    return packed;
}

static void theory_gps_poll(void)
{
    uint32_t offset, len, step, count, i, mask, bass;
    const track_t *track;
    const step_t *steps;
    theory_lab.gps.next = 0;
    if (!theory_ui.backing || !theory_ui.track)
        return;
    track = &trk[theory_ui.track - 1u];
    steps = seq_steps(track);
    step = track->seq_idx;
    len = (uint32_t)clamp(track->p[P_SLEN], 1, NSTEP);
    for (offset = 1; offset <= len; offset++) {
        const step_t *s = &steps[(step + offset) % len];
        if (s->time != ST_NOTE || !step_chance(s))
            continue;
        mask = 0;
        bass = 128;
        count = s->n < 4u ? s->n : 4u;
        for (i = 0; i < count; i++) {
            int32_t note = s->note[i];
            if (track->p[P_QUANT] >= QN_SEQ) {
                if (track->p[P_QUANT] == QN_SEQT)
                    note += track->p[P_TRANS];
                note = clamp(scale_snap(track, note), 0, 127);
            }
            if ((uint32_t)note < bass)
                bass = (uint32_t)note;
            mask |= TH_NOTE((uint32_t)note % 12u);
        }
        if (mask && mask != theory_ui.backing) {
            theory_lab.gps.next = (uint16_t)mask;
            theory_lab.gps.bass = (uint8_t)(bass % 12u);
            return;
        }
    }
}

/* Pair the remaining pitches after reserving shared tones. At most four
 * voices: a bounded search minimizes total travel on the chromatic circle. */
static uint32_t theory_gps_pair(const uint8_t from[4], uint32_t n, const uint8_t to[4],
                                uint32_t available, uint8_t result[4])
{
    uint32_t i, best = 999, distance, score;
    uint8_t tail[4];
    if (!n)
        return 0;
    if (!available) {
        for (i = 0; i < n; i++)
            result[i] = 255;
        return 7u * n;
    }
    for (i = 0; i < 4u; i++) {
        if (!(available & TH_NOTE(i)))
            continue;
        distance = (to[i] + 12u - from[0]) % 12u;
        if (distance > 6u)
            distance = 12u - distance;
        score = distance + theory_gps_pair(from + 1, n - 1u, to, available ^ TH_NOTE(i), tail);
        if (score < best) {
            best = score;
            result[0] = to[i];
            for (uint32_t j = 1; j < n; j++)
                result[j] = tail[j - 1u];
        }
    }
    /* If the next chord has fewer voices, dropping an earlier voice is also a candidate. */
    if (theory_cardinality(available) < n) {
        score = 7u + theory_gps_pair(from + 1, n - 1u, to, available, tail);
        if (score < best) {
            best = score;
            result[0] = 255;
            for (i = 1; i < n; i++)
                result[i] = tail[i - 1u];
        }
    }
    return best;
}

static void theory_ear_next(void)
{
    uint32_t degree, seed = theory_lab.ear.seed;
    seed = seed ? seed : (fm1_ms | 1u);
    seed = seed * 1664525u + 1013904223u;
    degree = (seed >> 16) % 7u;
    theory_lab.ear.seed = seed;
    theory_lab.ear.root = (uint8_t)((theory_root() + theory_mode_tone(theory_ui.mode, degree)) % 12u);
    theory_lab.ear.target = (uint16_t)theory_stack(theory_root(), theory_ui.mode, degree, 3);
    theory_lab.ear.answer = 0;
    theory_lab.ear.result = 0;
    theory_lab.ear.heard = 1;
    theory_lab.ear.ready = 0;
    theory_lab.ear.heard_at = fm1_ms;
    theory_preview_request = theory_preview_pitches(theory_lab.ear.target, theory_lab.ear.root);
}

static void theory_ear_poll(uint32_t held)
{
    uint32_t mask;
    if (theory_preview_request && fm1_ms - theory_lab.ear.heard_at >= 650u)
        theory_preview_request = 0;
    if (!held)
        theory_lab.ear.ready = 1;
    if (!theory_lab.ear.heard || !theory_lab.ear.ready || theory_lab.ear.result == 1u ||
        theory_preview_request || !held || held == theory_lab.held)
        return;
    mask = theory_lab_pitch_mask(held);
    if (theory_cardinality(mask) < 3u)
        return;
    theory_lab.ear.answer = (uint16_t)mask;
    theory_lab.ear.result = mask == theory_lab.ear.target ? 1u : 2u;
    theory_lab.ear.ready = 0;
    ui.force = 1;
}

static uint32_t theory_riff_map(uint32_t note)
{
    uint32_t interval = (note + 12u - theory_lab.riff.root) % 12u, i;
    for (i = 0; i < 7u; i++)
        if (theory_mode_tone(theory_lab.riff.source, i) == interval) {
            int32_t target = (int32_t)theory_mode_tone(theory_lab.riff.target, i);
            return (uint32_t)clamp((int32_t)note + target - (int32_t)interval, 0, 127);
        }
    return note;
}

static void theory_riff_finish(uint32_t elapsed)
{
    uint32_t n = theory_lab.riff.count;
    if (n && !theory_lab.riff.event[n - 1u].end)
        theory_lab.riff.event[n - 1u].end = (uint16_t)elapsed;
    theory_lab.riff.recording = 0;
    theory_lab.riff.duration = n ? (uint16_t)(elapsed < 250u ? 250u : elapsed) : 0;
    theory_lab.riff.live_key = 27;
    ui.force = 1;
}

static void theory_riff_poll(uint32_t held)
{
    uint32_t i, elapsed = fm1_ms - theory_lab.riff.started;
    if (theory_lab.riff.recording) {
        uint32_t key = theory_lab.riff.live_key, attacks = held & ~theory_lab.held;
        uint32_t next = attacks ? theory_first_key(attacks) : key;
        if (next == key && key < 27u && !(held & TH_NOTE(key)))
            next = 27;
        if (elapsed >= TH_RIFF_MS) {
            theory_riff_finish(TH_RIFF_MS);
        } else if (next != key) {
            i = theory_lab.riff.count;
            if (key < 27u && i)
                theory_lab.riff.event[i - 1u].end = (uint16_t)elapsed;
            theory_lab.riff.live_key = (uint8_t)next;
            if (next < 27u) {
                if (i == TH_RIFF_MAX) {
                    theory_riff_finish(elapsed);
                } else {
                    theory_lab.riff.event[i].note = kb_note[next];
                    theory_lab.riff.event[i].at = (uint16_t)elapsed;
                    theory_lab.riff.event[i].end = 0;
                    theory_lab.riff.count++;
                }
            }
            ui.force = 1;
        }
    } else if (theory_lab.riff.playing && theory_lab.riff.duration) {
        uint32_t note = 0, original = 0;
        elapsed %= theory_lab.riff.duration;
        for (i = 0; i < theory_lab.riff.count; i++)
            if (elapsed >= theory_lab.riff.event[i].at && elapsed < theory_lab.riff.event[i].end) {
                note = theory_lab.riff.event[i].note;
                original = note + 1u;
                if (theory_lab.riff.compare)
                    note = theory_riff_map(note);
                note++;
                break;
            }
        theory_preview_request = note;
        if (theory_lab.riff.sound != note || theory_lab.riff.original != original) {
            theory_lab.riff.sound = (uint8_t)note;
            theory_lab.riff.original = (uint8_t)original;
            ui.force = 1;
        }
    }
}

static uint32_t theory_pass_matches(uint8_t keys[24], uint8_t degrees[24])
{
    uint32_t n = 0, fifth, mode, d, count = theory_cardinality(theory_lab.passport.pitches);
    if (count != 3u && count != 4u)
        return 0;
    for (mode = 0; mode <= 5u; mode += 5u)
        for (fifth = 0; fifth < 12u; fifth++) {
            uint32_t root = (fifth * 7u + THEORY_MODE[mode].offset) % 12u;
            for (d = 0; d < 7u; d++)
                if (theory_stack(root, mode, d, count) == theory_lab.passport.pitches) {
                    keys[n] = (uint8_t)(fifth + (mode ? 12u : 0u));
                    degrees[n++] = (uint8_t)d;
                    break;
                }
        }
    return n;
}

static void theory_mission_score(uint32_t hit)
{
    if (!theory_lab.mission.pending)
        return;
    theory_lab.mission.grade[theory_lab.mission.count++] = (uint8_t)(hit ? 2u : 1u);
    theory_lab.mission.hits += (uint8_t)hit;
    theory_lab.mission.pending = 0;
    if (theory_lab.mission.count == TH_MISSION_LEN)
        theory_lab.mission.armed = 0;
    ui.force = 1;
}

static void theory_mission_poll(uint32_t attacks)
{
    uint32_t root = 0, pitches = theory_ui.backing, degree;
    int32_t match;
    if (!theory_lab.mission.armed)
        return;
    if (!pitches) {
        theory_lab.mission.chord = theory_lab.mission.pending = 0;
        return;
    }
    if (pitches != theory_lab.mission.chord || theory_ui.bass != theory_lab.mission.bass) {
        theory_mission_score(0);
        theory_lab.mission.chord = (uint16_t)pitches;
        theory_lab.mission.bass = theory_ui.bass;
        match = theory_match(pitches, theory_ui.bass, &root);
        degree = theory_lab.mission.degree;
        if (match >= 0 && theory_lab.mission.armed) {
            uint32_t shape = THEORY_CHORD[match].mask;
            uint32_t interval = degree == 0u ? 0u : degree == 1u ?
                (shape & TH_NOTE(3) ? 3u : 4u) : (shape & TH_NOTE(6) ? 6u : shape & TH_NOTE(8) ? 8u : 7u);
            if (shape & TH_NOTE(interval)) {
                theory_lab.mission.note = (uint8_t)((root + interval) % 12u);
                theory_lab.mission.changed_at = fm1_ms;
                theory_lab.mission.pending = 1;
            }
        }
        ui.force = 1;
    }
    if (theory_lab.mission.pending && fm1_ms - theory_lab.mission.changed_at >= TH_LAND_MS)
        theory_mission_score(0);
    if (theory_lab.mission.pending && attacks)
        theory_mission_score((theory_lab_pitch_mask(attacks) & TH_NOTE(theory_lab.mission.note)) != 0);
}

static void theory_lab_poll(uint32_t held)
{
    uint32_t attacks = held & ~theory_lab.held;
    switch (theory_lab.page) {
    case TH_GPS: {
        uint32_t before = theory_lab.gps.next | (uint32_t)theory_lab.gps.bass << 12;
        theory_gps_poll();
        if (before != (theory_lab.gps.next | (uint32_t)theory_lab.gps.bass << 12))
            ui.force = 1;
        break;
    }
    case TH_EAR:
        theory_ear_poll(held);
        break;
    case TH_RIFF:
        theory_riff_poll(held);
        break;
    case TH_PASS:
        if (attacks && theory_cardinality(theory_lab_pitch_mask(held)) >= 3u) {
            uint32_t k, bass = 128;
            for (k = 0; k < 27u; k++)
                if ((held & TH_NOTE(k)) && kb_note[k] < bass)
                    bass = kb_note[k];
            theory_lab.passport.pitches = (uint16_t)theory_lab_pitch_mask(held);
            theory_lab.passport.bass = (uint8_t)(bass % 12u);
            theory_lab.passport.cursor = 0;
            ui.force = 1;
        }
        break;
    case TH_MISSION:
        theory_mission_poll(attacks);
        break;
    }
    theory_lab.held = held;
}

static void theory_lab_leave(void)
{
    if (theory_lab.riff.recording)
        theory_riff_finish((uint32_t)clamp(fm1_ms - theory_lab.riff.started, 0, TH_RIFF_MS));
    theory_lab.riff.playing = theory_lab.riff.sound = 0;
    theory_lab.mission.armed = theory_lab.mission.pending = 0;
    theory_preview_request = 0;
}

static void theory_lab_page(int32_t step)
{
    theory_lab_leave();
    theory_lab.page = (uint8_t)((theory_lab.page + step % TH_PAGES + TH_PAGES) % TH_PAGES);
    theory_lab.held = theory_keys;
    if ((theory_lab.page == TH_EAR || theory_lab.page == TH_RIFF) &&
        (song.playing || seq_counting()))
        transport_req = 2;
    ui.force = 1;
}

/* Returns the button edges consumed by this page. Other edges (octave,
 * transport on GPS/mission, and HOME) keep their ordinary Theory meaning. */
static uint32_t theory_lab_input(uint32_t pressed, int32_t *key, int32_t *mode)
{
    uint32_t play = 1u << panel.btn[B_PLAY], rec = 1u << panel.btn[B_REC], scl = 1u << panel.btn[B_SCL];
    uint32_t used = 0;
    switch (theory_lab.page) {
    case TH_EAR:
        if (pressed & rec || ((pressed & play) && !theory_lab.ear.heard)) {
            theory_ear_next();
        } else if (pressed & play) {
            theory_lab.ear.heard_at = fm1_ms;
            theory_preview_request = theory_preview_pitches(theory_lab.ear.target, theory_lab.ear.root);
        }
        if (pressed & scl)
            theory_lab.ear.blind ^= 1u;
        used = play | rec | scl;
        break;
    case TH_RIFF:
        if (pressed & rec) {
            if (theory_lab.riff.recording) {
                theory_riff_finish((uint32_t)clamp(fm1_ms - theory_lab.riff.started, 0, TH_RIFF_MS));
            } else {
                theory_lab.riff.playing = theory_lab.riff.compare = theory_lab.riff.sound = 0;
                theory_lab.riff.count = theory_lab.riff.duration = 0;
                theory_lab.riff.recording = 1;
                theory_lab.riff.started = fm1_ms;
                theory_lab.riff.source = theory_lab.riff.target = theory_ui.mode;
                theory_lab.riff.root = (uint8_t)theory_root();
                theory_lab.riff.live_key = 27;
                theory_lab.held = theory_keys;
                theory_preview_request = 0;
            }
        } else if ((pressed & play) && !theory_lab.riff.recording && theory_lab.riff.duration) {
            theory_lab.riff.playing ^= 1u;
            theory_lab.riff.started = fm1_ms;
            theory_lab.riff.sound = 0;
            theory_preview_request = 0;
        }
        if ((pressed & scl) && theory_lab.riff.count && !theory_lab.riff.recording)
            theory_lab.riff.compare ^= 1u;
        if (theory_lab.riff.recording)
            *mode = 0;
        else if (*mode && theory_lab.riff.count) {
            theory_lab.riff.target = (uint8_t)((theory_lab.riff.target + *mode % THEORY_MODES + THEORY_MODES) % THEORY_MODES);
            theory_lab.riff.compare = 1;
            *mode = 0;
            ui.force = 1;
        }
        if (theory_lab.riff.recording || theory_lab.riff.count)
            *key = 0;
        used = play | rec | scl;
        break;
    case TH_PASS:
        if (*key || (pressed & scl)) {
            uint8_t keys[24], degrees[24];
            int32_t count = (int32_t)theory_pass_matches(keys, degrees);
            if (count) {
                theory_lab.passport.cursor = (uint8_t)((theory_lab.passport.cursor + *key % count + count) % count);
                if (pressed & scl) {
                    uint32_t selection = keys[theory_lab.passport.cursor];
                    theory_ui.fifth = (uint8_t)(selection % 12u);
                    theory_ui.mode = selection >= 12u ? 5u : 0u;
                    theory_lab_page(-TH_PASS);
                }
            }
        }
        if (*key)
            ui.force = 1;
        *key = 0;
        used = scl;
        break;
    case TH_MISSION:
        if (*key && !theory_lab.mission.armed) {
            theory_lab.mission.degree = (uint8_t)((theory_lab.mission.degree + *key % 3 + 3) % 3);
            ui.force = 1;
        }
        if (pressed & rec) {
            theory_lab.mission.count = theory_lab.mission.hits = 0;
            theory_lab.mission.chord = theory_lab.mission.pending = 0;
            for (uint32_t i = 0; i < TH_MISSION_LEN; i++)
                theory_lab.mission.grade[i] = 0;
            theory_lab.mission.armed = 1;
        }
        if (pressed & scl)
            theory_lab.mission.blind ^= 1u;
        *key = 0;
        used = rec | scl;
        break;
    case TH_GPS:
        used = scl;
        break;
    }
    if (pressed & used)
        ui.force = 1;
    return used;
}

static void theory_lab_guides(uint32_t *scale, uint32_t *roots, uint32_t *guide, uint32_t *alt)
{
    *scale = theory_scale_leds(roots);
    *guide = theory_backing_keys();
    *alt = 0;
    switch (theory_lab.page) {
    case TH_GPS:
        *guide = theory_keys_for(theory_ui.backing & theory_lab.gps.next);
        *alt = theory_keys_for(theory_lab.gps.next & ~theory_ui.backing);
        break;
    case TH_EAR:
        *scale = *roots = 0;
        *guide = (!theory_lab.ear.blind || theory_lab.ear.result == 1u) ?
            theory_keys_for(theory_lab.ear.target) : 0;
        break;
    case TH_RIFF:
        if (theory_lab.riff.count) {
            uint32_t mode = theory_lab.riff.compare ? theory_lab.riff.target : theory_lab.riff.source;
            *scale = theory_scale_keys(SCALE_MASK[THEORY_MODE[mode].scale], theory_lab.riff.root, roots);
        }
        *guide = theory_lab.riff.sound ? theory_keys_for(TH_NOTE((theory_lab.riff.sound - 1u) % 12u)) : 0;
        break;
    case TH_PASS:
        *guide = theory_keys_for(theory_lab.passport.pitches);
        break;
    case TH_MISSION:
        *guide = !theory_lab.mission.blind && theory_lab.mission.pending ?
            theory_keys_for(TH_NOTE(theory_lab.mission.note)) : 0;
        if (theory_lab.mission.blind)
            *scale = *roots = 0;
        break;
    }
}

static void theory_lab_chord(char out[16], uint32_t pitches, uint32_t bass)
{
    uint32_t root = bass;
    int32_t chord = theory_match(pitches, bass, &root);
    if (!pitches)
        str_cpy(out, "Waiting", 16);
    else if (chord >= 0 || !(pitches & (pitches - 1u))) {
        theory_note(out, root);
        if (chord >= 0)
            theory_append(out, THEORY_CHORD[chord].suffix, 16);
    } else {
        str_cpy(out, "Notes", 16);
    }
}

static void theory_lab_names(char out[38], uint32_t pitches, uint32_t root, int32_t chord)
{
    uint32_t pc;
    out[0] = 0;
    for (pc = 0; pc < 12u; pc++)
        if (pitches & TH_NOTE(pc)) {
            char note[4];
            if (chord >= 0)
                theory_chord_note(note, pc, root, (uint32_t)chord);
            else
                theory_note(note, pc);
            if (out[0])
                theory_append(out, " ", 38);
            theory_append(out, note, 38);
        }
    if (!out[0])
        str_cpy(out, "None", 38);
}

static void theory_lab_top(uint32_t page, const char *badge)
{
    cv_begin(240, 120, TH_BG);
    theory_heading(TH_PAGES_NAME[page], badge);
}

static void theory_lab_bottom(uint32_t held)
{
    uint32_t scale, roots, guide, alt;
    cv_begin(240, 120, TH_BG);
    theory_lab_guides(&scale, &roots, &guide, &alt);
    if (theory_lab.page == TH_EAR)
        scale = held & ~theory_keys_for(theory_lab.ear.answer & ~theory_lab.ear.target);
    theory_piano_overlay(held, scale, roots, guide, alt);
    cv_rect(12, 99, 216, 1, TH_RULE);
}

static void theory_lab_foot(const char *controls)
{
    cv_free_text(12, 104, &AF_S, controls, TH_MUTED, TH_BG, 160);
    cv_text_r(228, 104, &AF_S, theory_app.primary ? "HOME Menu" : "K4 Pages", TH_SCALE, TH_BG);
    cv_blit(0, 120);
}

static void theory_gps_draw(uint32_t held)
{
    uint32_t current = theory_ui.backing, next = theory_lab.gps.next;
    uint32_t shared = current & next, n = 0, m = 0, used = 0, pc, lane = 0, i;
    uint8_t from[4], to[4], result[4], left[4], right[4];
    char a[16], b[16], note[4];
    uint32_t from_root = theory_ui.bass, to_root = theory_lab.gps.bass;
    int32_t from_chord = theory_match(current, theory_ui.bass, &from_root);
    int32_t to_chord = theory_match(next, theory_lab.gps.bass, &to_root);
    for (pc = 0; pc < 12u; pc++) {
        if ((current & ~shared & TH_NOTE(pc)) && n < 4u)
            from[n++] = (uint8_t)pc;
        if ((next & ~shared & TH_NOTE(pc)) && m < 4u)
            to[m++] = (uint8_t)pc;
        if ((shared & TH_NOTE(pc)) && lane < 4u) {
            left[lane] = right[lane] = (uint8_t)pc;
            lane++;
        }
    }
    theory_gps_pair(from, n, to, (1u << m) - 1u, result);
    for (i = 0; i < n && lane < 4u; i++, lane++) {
        left[lane] = from[i];
        right[lane] = result[i];
        if (result[i] < 12u)
            used |= TH_NOTE(result[i]);
    }
    for (i = 0; i < m && lane < 4u; i++)
        if (!(used & TH_NOTE(to[i]))) {
            left[lane] = 255;
            right[lane++] = to[i];
        }
    theory_lab_chord(a, current, theory_ui.bass);
    theory_lab_chord(b, next, theory_lab.gps.bass);
    theory_lab_top(TH_GPS, "Next in pattern");
    if (current && next) {
        cv_text_in(12, 43, 102, &AF_M, a, TH_TEXT, TH_BG);
        cv_text_in(126, 43, 102, &AF_M, b, TH_CHORD, TH_BG);
        cv_line(106, 55, 133, 55, TH_RULE);
        cv_line(129, 52, 133, 55, TH_RULE);
        cv_line(129, 58, 133, 55, TH_RULE);
        for (i = 0; i < lane; i++) {
            int32_t x = 12 + 55 * (int32_t)i;
            uint16_t ink = left[i] == right[i] ? TH_CHORD : TH_OUT;
            cv_rrect(x, 71, 51, 43, 5, TH_SURF, TH_BG);
            if (left[i] < 12u) {
                if (from_chord >= 0)
                    theory_chord_note(note, left[i], from_root, (uint32_t)from_chord);
                else
                    theory_note(note, left[i]);
                cv_text_in(x, 71, 51, &AF_S, note, TH_TEXT, TH_SURF);
            }
            cv_rect(x + 23, 87, 5, 2, ink);
            if (right[i] < 12u) {
                if (to_chord >= 0)
                    theory_chord_note(note, right[i], to_root, (uint32_t)to_chord);
                else
                    theory_note(note, right[i]);
                cv_text_in(x, 92, 51, &AF_S, note, ink, TH_SURF);
            } else {
                cv_text_in(x, 92, 51, &AF_S, "Lift", TH_MUTED, TH_SURF);
            }
        }
    } else {
        cv_text(12, 51, &AF_M, "Follow a progression", TH_TEXT);
        cv_text(12, 80, &AF_S, theory_app.primary ? "HOME > Play along chooses a loop." :
                "K3 selects a backing track. Press PLAY.", TH_MUTED);
        cv_text(12, 99, &AF_S, theory_app.primary ? "Press PLAY there to start it." :
                "Add two different chords in SEQ.", TH_MUTED);
    }
    cv_blit(0, 0);
    theory_lab_bottom(held);
    cv_text(12, 66, &AF_S, "Blue = keep     Orange = move or add", TH_TEXT);
    if (theory_app.primary && song.playing && theory_ui.track) {
        cv_rect(12, 88, 216, 3, TH_RULE);
        cv_rect(12, 88, theory_app.progress_px, 3, TH_CHORD);
    }
    theory_lab_foot(theory_app.primary ? "PLAY Start / stop" : "K3 Back   PLAY Start / stop");
}

static void theory_ear_draw(uint32_t held)
{
    uint32_t target = theory_lab.ear.target, answer = theory_lab.ear.answer;
    char text[38];
    const char *prompt = !theory_lab.ear.heard ? "Hear a chord" :
        theory_lab.ear.result == 1u ? "You got it" : theory_lab.ear.result == 2u ? "Try again" : "Play three notes";
    uint16_t ink = theory_lab.ear.result == 2u ? TH_OUT : TH_PLAY;
    theory_lab_top(TH_EAR, theory_lab.ear.blind ? "By ear" : "With hints");
    cv_text(12, 48, &AF_M, prompt, ink);
    if (answer) {
        uint32_t root = theory_lab.ear.root;
        int32_t chord = theory_match(target, root, &root);
        theory_lab_names(text, target & ~answer, root, chord);
        cv_text(12, 80, &AF_S, "Missing", TH_MUTED);
        cv_text(68, 80, &AF_S, text, TH_CHORD);
        theory_lab_names(text, answer & ~target, 0, -1);
        cv_text(12, 100, &AF_S, "Extra", TH_MUTED);
        cv_text(68, 100, &AF_S, text, TH_OUT);
    } else {
        cv_text(12, 82, &AF_S, "Any octave or inversion works.", TH_MUTED);
    }
    cv_blit(0, 0);
    theory_lab_bottom(held);
    cv_text(12, 63, &AF_S, "Hold three different notes together", TH_TEXT);
    cv_text(12, 80, &AF_S, "SCL toggles hints", TH_MUTED);
    theory_lab_foot("PLAY Hear  REC New");
}

static uint32_t theory_riff_fifth(uint32_t mode)
{
    return ((theory_lab.riff.root + 12u - THEORY_MODE[mode].offset) * 7u) % 12u;
}

static void theory_riff_name(char out[24], uint32_t mode)
{
    theory_note_in_fifth(out, theory_lab.riff.root, theory_riff_fifth(mode));
    theory_append(out, " ", 24);
    theory_append(out, THEORY_MODE[mode].name, 24);
}

static void theory_riff_draw(uint32_t held)
{
    uint32_t i, count = theory_lab.riff.count;
    uint32_t width = theory_lab.riff.duration ? theory_lab.riff.duration :
        (count ? theory_lab.riff.event[count - 1u].at + 250u : 1u);
    char source[24], target[24];
    uint16_t color = theory_lab.riff.compare ? TH_CHORD : TH_PLAY;
    theory_lab_top(TH_RIFF, theory_lab.riff.recording ? "Recording" : theory_lab.riff.compare ? "B / Compare" : "A / Original");
    if (!count) {
        cv_text(12, 48, &AF_M, theory_lab.riff.recording ? "Play your riff" : "Record a short riff", color);
        cv_text(12, 81, &AF_S, "One note at a time, up to 12 seconds.", TH_MUTED);
    } else {
        theory_riff_name(source, theory_lab.riff.source);
        theory_riff_name(target, theory_lab.riff.target);
        cv_text(12, 43, &AF_S, source, TH_PLAY);
        cv_text_r(228, 43, &AF_S, target, TH_CHORD, TH_BG);
        cv_rect(12, 66, 216, 47, TH_SURF);
        for (i = 0; i < count; i++) {
            uint32_t original = theory_lab.riff.event[i].note;
            uint32_t pitch = theory_lab.riff.compare ? theory_riff_map(original) : original;
            uint32_t start = theory_lab.riff.event[i].at;
            uint32_t end = theory_lab.riff.event[i].end ? theory_lab.riff.event[i].end : width;
            int32_t x = 14 + (int32_t)(start * 211u / width);
            int32_t w = (int32_t)((end - start) * 211u / width);
            int32_t y = 106 - (int32_t)((pitch + 12u - theory_lab.riff.root) % 12u) * 3;
            cv_rect(x, y, w < 2 ? 2 : w, 3, pitch != original ? TH_OUT : color);
        }
        if (theory_app.primary && theory_lab.riff.playing)
            cv_line(14 + theory_app.riff_px, 67, 14 + theory_app.riff_px, 111, TH_TEXT);
    }
    cv_blit(0, 0);
    theory_lab_bottom(held);
    if (theory_lab.riff.sound && theory_lab.riff.original != theory_lab.riff.sound) {
        char change[24], note[4];
        theory_note_in_fifth(change, (theory_lab.riff.original - 1u) % 12u,
                             theory_riff_fifth(theory_lab.riff.source));
        theory_note_in_fifth(note, (theory_lab.riff.sound - 1u) % 12u,
                             theory_riff_fifth(theory_lab.riff.target));
        theory_append(change, " > ", sizeof change);
        theory_append(change, note, sizeof change);
        cv_text(12, 61, &AF_M, change, TH_OUT);
    } else {
        cv_text(12, 63, &AF_S, "Orange = a scale degree moved", TH_TEXT);
    }
    cv_text(12, 80, &AF_S, "K2 changes the comparison mode", TH_MUTED);
    theory_lab_foot("REC Take  PLAY Loop  SCL A/B");
}

static void theory_pass_wheel(const uint8_t keys[24], uint32_t count)
{
    uint32_t i, ring, matches = 0, selected = keys[theory_lab.passport.cursor];
    for (i = 0; i < count; i++)
        matches |= TH_NOTE(keys[i]);
    for (ring = 0; ring < 2u; ring++) {
        int32_t radius = ring ? 24 : 38;
        for (i = 0; i < 12u; i++) {
            uint32_t next = (i + 1u) % 12u;
            cv_line(184 + THEORY_WHEEL_X[i] * radius / 92, 80 + THEORY_WHEEL_Y[i] * radius / 92,
                    184 + THEORY_WHEEL_X[next] * radius / 92, 80 + THEORY_WHEEL_Y[next] * radius / 92, TH_RULE);
        }
        for (i = 0; i < 12u; i++) {
            uint32_t key = ring * 12u + i, match = matches & TH_NOTE(key);
            int32_t x = 184 + THEORY_WHEEL_X[i] * radius / 92, y = 80 + THEORY_WHEEL_Y[i] * radius / 92;
            int32_t size = selected == key ? 9 : match ? 6 : 3;
            cv_rrect(x - size / 2, y - size / 2, size, size, 3,
                     selected == key ? TH_PLAY : match ? TH_SCALE : TH_RULE, TH_BG);
        }
    }
}

static void theory_pass_draw(uint32_t held)
{
    uint8_t keys[24], degrees[24];
    uint32_t n = theory_pass_matches(keys, degrees), i;
    uint32_t first = theory_lab.passport.cursor / 3u * 3u;
    char name[16];
    uint32_t root = 0;
    int32_t chord = theory_match(theory_lab.passport.pitches, theory_lab.passport.bass, &root);
    theory_lab_chord(name, theory_lab.passport.pitches, theory_lab.passport.bass);
    theory_lab_top(TH_PASS, "Major + minor");
    cv_text(12, 48, &AF_M, theory_lab.passport.pitches ? name : "Play a chord", TH_PLAY);
    if (n) {
        cv_text(12, 77, &AF_S, "Same chord, new job.", TH_MUTED);
        cv_text(12, 97, &AF_S, "Outer major / inner minor", TH_MUTED);
        theory_pass_wheel(keys, n);
    } else {
        cv_text(12, 78, &AF_S, theory_lab.passport.pitches ? "No diatonic match for these notes." :
                "Try a triad or a seventh chord.", TH_MUTED);
    }
    cv_blit(0, 0);
    theory_lab_bottom(held);
    for (i = first; i < n && i < first + 3u; i++) {
        uint32_t key = keys[i], selected = i == theory_lab.passport.cursor;
        int32_t x = 12 + 73 * (int32_t)(i - first);
        uint16_t bg = selected ? TH_SCALE : TH_SURF, fg = selected ? TH_BG : TH_TEXT;
        int minor = chord >= 0 && (THEORY_CHORD[chord].mask & TH_NOTE(3));
        cv_rect(x, 60, 69, 33, bg);
        cv_text_in(x, 60, 69, &AF_S, key < 12u ? THEORY_MAJOR[key] : THEORY_MINOR[key - 12u], fg, bg);
        cv_text_in(x, 76, 69, &AF_S, THEORY_ROMAN[minor][degrees[i]], fg, bg);
    }
    theory_lab_foot("K1 Select  SCL Open key");
}

static void theory_mission_draw(uint32_t held)
{
    static const char *const GOAL[] = {"Land on the root", "Land on the third", "Land on the fifth"};
    char name[16], score[16] = "";
    uint32_t i;
    theory_lab_chord(name, theory_ui.backing, theory_ui.bass);
    theory_lab_top(TH_MISSION, theory_lab.mission.blind ? "No hints" : "With hints");
    cv_text(12, 45, &AF_M, GOAL[theory_lab.mission.degree], TH_TEXT);
    if (theory_lab.mission.count == TH_MISSION_LEN) {
        fmt_int(score, theory_lab.mission.hits);
        theory_append(score, " / 4 landed", sizeof score);
        cv_text(12, 76, &AF_M, score, TH_PLAY);
    } else if (theory_lab.mission.armed) {
        cv_text(12, 74, &AF_M, name, TH_CHORD);
        cv_text(12, 98, &AF_S, theory_lab.mission.pending ? "Play now" : "Wait for the next chord", TH_MUTED);
    } else {
        cv_text(12, 76, &AF_S, "REC arms a round of four changes.", TH_MUTED);
        cv_text(12, 97, &AF_S, "K3 picks backing. PLAY starts it.", TH_MUTED);
    }
    cv_blit(0, 0);
    theory_lab_bottom(held);
    for (i = 0; i < TH_MISSION_LEN; i++) {
        uint32_t grade = theory_lab.mission.grade[i];
        uint16_t bg = grade == 2u ? TH_PLAY : grade == 1u ? TH_OUT : TH_SURF;
        int32_t x = 12 + 55 * (int32_t)i;
        cv_rrect(x, 64, 51, 25, 5, bg, TH_BG);
        cv_text_in(x, 67, 51, &AF_S, grade == 2u ? "Hit" : grade == 1u ? "Miss" :
                   i == theory_lab.mission.count && theory_lab.mission.pending ? "Now" : "Wait",
                   grade ? TH_BG : TH_TEXT, bg);
    }
    theory_lab_foot("K1 Goal  REC Arm  SCL Hints");
}

static void theory_lab_draw(uint32_t held)
{
    switch (theory_lab.page) {
    case TH_GPS: theory_gps_draw(held); break;
    case TH_EAR: theory_ear_draw(held); break;
    case TH_RIFF: theory_riff_draw(held); break;
    case TH_PASS: theory_pass_draw(held); break;
    case TH_MISSION: theory_mission_draw(held); break;
    }
}
