/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - which bands are open from here: what the receivers near the station decoded from far away, from the
 *  lines of x6100-propagation. No radio and no screen in here, so that tools/propband/ can test it.
 *
 *  This is what was heard, not a forecast. Most of it is FT8, so an open band says the path is there, not that a
 *  voice signal will make it; and a band with nothing heard may only have no one listening near you.
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROP_BANDS      11      /* 160 m .. 6 m: the order of field/bands.h */
#define PROP_NEAR_KM    800     /* a receiver this close hears roughly what you would */
#define PROP_DX_KM      1000    /* further than this is the ionosphere, not the ground wave or a local path */
#define PROP_OPEN_DX    5       /* senders from afar one receiver must hear to count towards open */
#define PROP_OPEN_RX    2       /* receivers that must: one remote site can hear far more than the rest */
#define PROP_SEEN_MAX   256     /* different senders counted on a band */
#define PROP_RX_MAX     64      /* different receivers counted */
#define PROP_PAIR_MAX   1024    /* different sender and receiver pairs counted on a band */

typedef enum { PROP_NONE, PROP_LOCAL, PROP_THIN, PROP_OPEN } prop_state_t;

typedef struct {
    int      reports;   /* from the receivers near the station */
    int      dx;        /* different senders over PROP_DX_KM */
    int      max_km;    /* the longest path heard */
    int      seen_n;
    int      rx_n;      /* the receivers that heard someone from afar on this band */
    int      strong_rx; /* those of them that heard PROP_OPEN_DX or more */
    int      pair_n;
    uint32_t seen[PROP_SEEN_MAX];
    uint32_t rx[PROP_RX_MAX];
    int      rx_dx[PROP_RX_MAX];
    uint32_t pair[PROP_PAIR_MAX];
} prop_band_t;

typedef struct {
    prop_band_t band[PROP_BANDS];
    int         receivers;              /* different ones near the station */
    uint32_t    rx_seen[PROP_RX_MAX];
    int         fields, fields_ok;      /* the map fields asked for, and those that answered */
    int         flux;                   /* solar flux, -1 not known */
    float       kp;                     /* -1 not known */
    double      lat, lon;
    char        grid[9];
    time_t      at;                     /* when it was asked for */
} prop_t;

/* Empties p for a locator */
void prop_init(prop_t *p, const char *grid, time_t now);

/* Takes one line of x6100-propagation */
void prop_line(prop_t *p, const char *line);

/* -1 outside the 11 bands */
int prop_band_of(int64_t hz);

prop_state_t prop_state(const prop_band_t *b);

/* "OPEN", "THIN", "LOCAL", "-" */
const char *prop_word(prop_state_t s);

/* 0 (nothing heard) .. 6, doubling senders a step, 2 at most for a thin band: the bar */
int prop_level(const prop_band_t *b);

/* The open band with the most senders from afar, else the thin one, -1 none */
int prop_best(const prop_t *p);

#ifdef __cplusplus
}
#endif
