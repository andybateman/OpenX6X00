/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - host test of the receive scan (src/rxscan.c); see the Makefile. The band is a list of what each stop
 *  reads, the clock the test's own, moved on 100 ms a tick as the window's timer does.
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rxscan.h"

#define TICK_MS 100
#define QUIET   (-110.0f)
#define S9      (-73.0f)
#define LEVEL   (-97)       /* S5 */

static int cases, good;

static void check(bool ok, const char *what) {
    cases++;
    if (ok) good++;
    else printf("FAILED: %s\n", what);
}

/* Runs until the scan has been at stop `until` or stopped, at most max_ms. Returns the ms it took */
static uint64_t run(rxscan_t *s, const float *band, uint64_t *now, uint16_t until, uint32_t max_ms,
                    rxscan_action_t *last)
{
    uint64_t from = *now;

    *last = RXSCAN_WAIT;
    while (*now - from < max_ms && rxscan_running(s)) {
        *now += TICK_MS;
        *last = rxscan_tick(s, *now, band[s->pos]);
        if (*last == RXSCAN_FOUND) break;
        if (*last == RXSCAN_TUNE && s->pos == until) break;
    }
    return *now - from;
}

static void moves_along_a_quiet_band(void) {
    float           band[3] = { QUIET, QUIET, QUIET };
    rxscan_t        s;
    uint64_t        now = 1000;
    rxscan_action_t a;

    check(rxscan_start(&s, 3, LEVEL, 0, now), "starts");
    check(s.pos == 0, "from the first stop");
    run(&s, band, &now, 1, 2000, &a);
    check(a == RXSCAN_TUNE && s.pos == 1, "on to the second");
    run(&s, band, &now, 2, 2000, &a);
    check(s.pos == 2, "on to the third");
    run(&s, band, &now, 0, 2000, &a);
    check(a == RXSCAN_TUNE && s.pos == 0, "round to the first");
    check(rxscan_running(&s), "still scanning");
}

static void one_stop_goes_on_tuning_itself(void) {
    float           band[1] = { QUIET };
    rxscan_t        s;
    uint64_t        now = 0;
    rxscan_action_t a;

    rxscan_start(&s, 1, LEVEL, 0, now);
    run(&s, band, &now, 0, 2000, &a);
    check(a == RXSCAN_TUNE && s.pos == 0 && rxscan_running(&s), "one stop, no crash");
}

static void nothing_to_scan(void) {
    rxscan_t s;

    check(!rxscan_start(&s, 0, LEVEL, 0, 0), "refused");
    check(!rxscan_running(&s), "off");
    check(rxscan_tick(&s, 1000, S9) == RXSCAN_WAIT, "no action when off");
    rxscan_skip(&s, 1000);     /* % 0 */
    check(!rxscan_running(&s), "skip leaves it off");
}

static void stops_on_a_signal(void) {
    float           band[4] = { QUIET, QUIET, S9, QUIET };
    rxscan_t        s;
    uint64_t        now = 0;
    rxscan_action_t a;

    rxscan_start(&s, 4, LEVEL, 0, now);
    run(&s, band, &now, 99, 10000, &a);
    check(a == RXSCAN_FOUND && s.pos == 2, "stopped on the signal");
    check(!rxscan_running(&s), "and off");
    check(rxscan_tick(&s, now + 1000, QUIET) == RXSCAN_WAIT, "and it stays there");
}

/* The S-meter still reads the last stop: a strong one must not stop the scan on the next */
static void ignores_the_last_stop_while_settling(void) {
    rxscan_t s;
    uint64_t now = 0;
    bool     tuned = false;

    rxscan_start(&s, 2, LEVEL, 0, now);
    for (int i = 0; i * TICK_MS < RXSCAN_SETTLE_MS - TICK_MS; i++) {
        now += TICK_MS;
        check(rxscan_tick(&s, now, S9) == RXSCAN_WAIT, "no stop while settling");
    }
    for (int i = 0; i < 20 && !tuned; i++) {
        now += TICK_MS;
        tuned = rxscan_tick(&s, now, QUIET) == RXSCAN_TUNE;
    }
    check(tuned, "moved on once it was quiet");
}

static void one_spike_stops_nothing(void) {
    rxscan_t s;
    uint64_t now = 0;
    float    read[] = { QUIET, QUIET, S9, QUIET };
    bool     moved = false;

    rxscan_start(&s, 2, LEVEL, 0, now);
    for (unsigned i = 0; i < 20 && !moved; i++) {
        rxscan_action_t a;

        now += TICK_MS;
        a = rxscan_tick(&s, now, read[i % 4]);
        check(a != RXSCAN_FOUND, "no stop on a spike");
        moved = a == RXSCAN_TUNE;
    }
    check(moved, "moved on");
}

static void at_the_level_is_a_signal_nan_is_not(void) {
    float           band[2] = { (float)LEVEL, QUIET };
    float           nan_band[2] = { NAN, NAN };
    rxscan_t        s;
    uint64_t        now = 0;
    rxscan_action_t a;

    rxscan_start(&s, 2, LEVEL, 0, now);
    run(&s, band, &now, 99, 5000, &a);
    check(a == RXSCAN_FOUND, "at the level");

    rxscan_start(&s, 2, LEVEL, 0, now);
    run(&s, nan_band, &now, 1, 5000, &a);
    check(a == RXSCAN_TUNE, "NaN is quiet");
}

static void holds_then_goes_on(void) {
    float           band[3] = { S9, QUIET, QUIET };
    rxscan_t        s;
    uint64_t        now = 0, took;
    rxscan_action_t a;

    rxscan_start(&s, 3, LEVEL, 5000, now);
    run(&s, band, &now, 1, 1000, &a);
    check(s.state == RXSCAN_HOLD && s.pos == 0, "holding");
    run(&s, band, &now, 1, 30000, &a);
    check(s.state == RXSCAN_HOLD, "held while the signal lasts");

    band[0] = QUIET;
    took = run(&s, band, &now, 1, 30000, &a);
    check(a == RXSCAN_TUNE && s.pos == 1, "moved on after it went");
    check(took >= 5000 && took < 5300, "after the hold time");
}

/* A pause in speech, shorter than the hold, keeps the scan there */
static void a_pause_keeps_the_hold(void) {
    rxscan_t s;
    uint64_t now = 0;
    bool     held = true;

    rxscan_start(&s, 2, LEVEL, 5000, now);
    for (int i = 0; i < 300; i++) {
        now += TICK_MS;
        if (rxscan_tick(&s, now, i % 30 < 5 || i < 8 ? S9 : QUIET) == RXSCAN_TUNE) held = false;
    }
    check(held && s.state == RXSCAN_HOLD, "held through the pauses");
}

static void skip_leaves_a_signal(void) {
    float           band[3] = { S9, QUIET, QUIET };
    rxscan_t        s;
    uint64_t        now = 0;
    rxscan_action_t a;

    rxscan_start(&s, 3, LEVEL, 5000, now);
    run(&s, band, &now, 1, 1000, &a);
    check(s.state == RXSCAN_HOLD, "on the carrier");
    rxscan_skip(&s, now);
    check(s.pos == 1 && s.state == RXSCAN_SETTLE, "skipped");
}

static void stop_is_stop(void) {
    rxscan_t s;

    rxscan_start(&s, 3, LEVEL, 0, 0);
    rxscan_stop(&s);
    check(!rxscan_running(&s), "stopped");
    check(rxscan_tick(&s, 5000, S9) == RXSCAN_WAIT, "does nothing after");
}

int main(void) {
    moves_along_a_quiet_band();
    one_stop_goes_on_tuning_itself();
    nothing_to_scan();
    stops_on_a_signal();
    ignores_the_last_stop_while_settling();
    one_spike_stops_nothing();
    at_the_level_is_a_signal_nan_is_not();
    holds_then_goes_on();
    a_pause_keeps_the_hold();
    skip_leaves_a_signal();
    stop_is_stop();
    printf("%d of %d receive scan cases as they should be\n", good, cases);
    return good == cases ? 0 : 1;
}
