/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - receive scan: when to move on and when to stay. No radio and no screen here: the window
 *  (dialog_scan.c) feeds it the clock and the S-meter, and tunes what it says. It never transmits.
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RXSCAN_SETTLE_MS    250     /* the S-meter still shows the last stop for this long after a retune */
#define RXSCAN_LISTEN_MS    300     /* then this long to hear something */
#define RXSCAN_HITS         2       /* readings in a row over the level: one noise spike stops nothing */

typedef enum {
    RXSCAN_OFF,
    RXSCAN_SETTLE,                  /* just tuned, the readings don't count yet */
    RXSCAN_LISTEN,                  /* looking for a signal */
    RXSCAN_HOLD                     /* on a signal, until it has been gone for hold_ms */
} rxscan_state_t;

typedef enum {
    RXSCAN_WAIT,                    /* nothing to do */
    RXSCAN_TUNE,                    /* tune rxscan_t.pos */
    RXSCAN_FOUND                    /* a signal at pos, and the scan has stopped there */
} rxscan_action_t;

typedef struct {
    rxscan_state_t state;
    uint16_t       count;           /* stops */
    uint16_t       pos;             /* the current one */
    int16_t        level_db;        /* a signal: at or over this */
    uint32_t       hold_ms;         /* 0: stop at the first signal; else go on once it has been gone this long */
    uint64_t       since_ms;        /* of the state, or of the signal last heard in HOLD */
    uint8_t        hits;
} rxscan_t;

/* From stop 0; the caller tunes pos. False, and off, with nothing to scan */
bool rxscan_start(rxscan_t *s, uint16_t count, int16_t level_db, uint32_t hold_ms, uint64_t now_ms);

/* Called often (every 100 ms or so) with the S-meter in dB */
rxscan_action_t rxscan_tick(rxscan_t *s, uint64_t now_ms, float db);

/* On to the next stop at once, from a signal too; the caller tunes pos */
void rxscan_skip(rxscan_t *s, uint64_t now_ms);

void rxscan_stop(rxscan_t *s);

bool rxscan_running(const rxscan_t *s);
