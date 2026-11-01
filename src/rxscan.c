/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - receive scan: when to move on and when to stay; see rxscan.h
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include "rxscan.h"

static void enter(rxscan_t *s, rxscan_state_t state, uint64_t now_ms) {
    s->state = state;
    s->since_ms = now_ms;
    s->hits = 0;
}

bool rxscan_start(rxscan_t *s, uint16_t count, int16_t level_db, uint32_t hold_ms, uint64_t now_ms) {
    s->count = count;
    s->pos = 0;
    s->level_db = level_db;
    s->hold_ms = hold_ms;
    enter(s, count ? RXSCAN_SETTLE : RXSCAN_OFF, now_ms);
    return count > 0;
}

void rxscan_skip(rxscan_t *s, uint64_t now_ms) {
    if (s->state == RXSCAN_OFF) return;
    s->pos = (s->pos + 1) % s->count;
    enter(s, RXSCAN_SETTLE, now_ms);
}

rxscan_action_t rxscan_tick(rxscan_t *s, uint64_t now_ms, float db) {
    bool heard = db >= s->level_db;     /* a NaN is no signal */

    switch (s->state) {
        case RXSCAN_SETTLE:
            if (now_ms - s->since_ms >= RXSCAN_SETTLE_MS) enter(s, RXSCAN_LISTEN, now_ms);
            break;

        case RXSCAN_LISTEN:
            s->hits = heard ? s->hits + 1 : 0;
            if (s->hits >= RXSCAN_HITS) {
                if (s->hold_ms == 0) {
                    enter(s, RXSCAN_OFF, now_ms);
                    return RXSCAN_FOUND;
                }
                enter(s, RXSCAN_HOLD, now_ms);
            } else if (now_ms - s->since_ms >= RXSCAN_LISTEN_MS) {
                rxscan_skip(s, now_ms);
                return RXSCAN_TUNE;
            }
            break;

        case RXSCAN_HOLD:
            if (heard) {
                s->since_ms = now_ms;
            } else if (now_ms - s->since_ms >= s->hold_ms) {
                rxscan_skip(s, now_ms);
                return RXSCAN_TUNE;
            }
            break;

        case RXSCAN_OFF:
            break;
    }
    return RXSCAN_WAIT;
}

void rxscan_stop(rxscan_t *s) {
    s->state = RXSCAN_OFF;
}

bool rxscan_running(const rxscan_t *s) {
    return s->state != RXSCAN_OFF;
}
