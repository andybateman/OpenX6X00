/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - which bands are open from here; see propband.h
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include "propband.h"

#include "qth/qth.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    int64_t lo, hi;
} bands[PROP_BANDS] = {
    {1800000, 2000000},   {3500000, 4000000},   {5250000, 5450000},   {7000000, 7300000},
    {10100000, 10150000}, {14000000, 14350000}, {18068000, 18168000}, {21000000, 21450000},
    {24890000, 24990000}, {28000000, 29700000}, {50000000, 54000000},
};

void prop_init(prop_t *p, const char *grid, time_t now) {
    memset(p, 0, sizeof(*p));
    p->flux = -1;
    p->kp   = -1;
    p->at   = now;
    snprintf(p->grid, sizeof(p->grid), "%s", grid);
    if (qth_grid_check(p->grid))
        qth_str_to_pos(p->grid, &p->lat, &p->lon);
}

int prop_band_of(int64_t hz) {
    for (int i = 0; i < PROP_BANDS; i++)
        if (hz >= bands[i].lo && hz <= bands[i].hi)
            return i;
    return -1;
}

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;

    for (; *s; s++)
        h = (h ^ (uint8_t)toupper((unsigned char)*s)) * 16777619u;
    return h ? h : 1;
}

/* True when v is new; a full set counts nothing more */
static bool add_seen(uint32_t *set, int *n, int max, uint32_t v) {
    for (int i = 0; i < *n; i++)
        if (set[i] == v)
            return false;
    if (*n >= max)
        return false;
    set[(*n)++] = v;
    return true;
}

/* PSK Reporter's locators come in either case and in 2, 4, 6, 8 or 10 characters. Up to 6, as qth.c wants them */
static bool position(const char *grid, double *lat, double *lon) {
    char   g[7];
    size_t n = strlen(grid);

    if (n > 6)
        n = 6;
    if (n != 4 && n != 6)
        return false;
    for (size_t i = 0; i < n; i++)
        g[i] = i < 2 ? toupper((unsigned char)grid[i]) : tolower((unsigned char)grid[i]);
    g[n] = 0;
    if (!qth_grid_check(g))
        return false;
    qth_str_to_pos(g, lat, lon);
    return true;
}

/* Splits on '|' in place. Returns the number of fields */
static int split(char *s, char **f, int max) {
    int n = 0;

    while (n < max) {
        f[n++] = s;
        s      = strchr(s, '|');
        if (!s)
            break;
        *s++ = 0;
    }
    return n;
}

/* One more sender at this receiver, if it had not heard that one yet */
static void count_at_receiver(prop_band_t *pb, uint32_t rx, uint32_t sender) {
    int i;

    if (!add_seen(pb->pair, &pb->pair_n, PROP_PAIR_MAX, (rx * 31u + sender) | 1u))
        return;
    for (i = 0; i < pb->rx_n && pb->rx[i] != rx; i++)
        ;
    if (i == pb->rx_n) {
        if (i >= PROP_RX_MAX)
            return;
        pb->rx[pb->rx_n++] = rx;
    }
    if (++pb->rx_dx[i] == PROP_OPEN_DX)
        pb->strong_rx++;
}

static void report(prop_t *p, const char *call, const char *freq, const char *rx, const char *tx) {
    double rx_lat, rx_lon, tx_lat, tx_lon;
    int    b = prop_band_of(strtoll(freq, NULL, 10));

    if (!call[0] || !position(rx, &rx_lat, &rx_lon))
        return;
    if (qth_pos_dist(p->lat, p->lon, rx_lat, rx_lon) > PROP_NEAR_KM)
        return;
    add_seen(p->rx_seen, &p->receivers, PROP_RX_MAX, hash(rx));
    if (b < 0)
        return;

    prop_band_t *pb = &p->band[b];

    pb->reports++;
    if (!position(tx, &tx_lat, &tx_lon))
        return;     /* heard here, from who knows where */

    int km = (int)(qth_pos_dist(rx_lat, rx_lon, tx_lat, tx_lon) + 0.5);

    if (km > pb->max_km)
        pb->max_km = km;
    if (km <= PROP_DX_KM)
        return;
    if (add_seen(pb->seen, &pb->seen_n, PROP_SEEN_MAX, hash(call)))
        pb->dx++;
    count_at_receiver(pb, hash(rx), hash(call));
}

void prop_line(prop_t *p, const char *line) {
    char  buf[128];
    char *f[5];
    int   n;

    snprintf(buf, sizeof(buf), "%s", line);
    buf[strcspn(buf, "\r\n")] = 0;
    n = split(buf, f, 5);
    if (!strcmp(f[0], "R") && n == 5) {
        report(p, f[1], f[2], f[3], f[4]);
    } else if (!strcmp(f[0], "F") && n == 3) {
        p->fields++;
        if (!strcmp(f[2], "1"))
            p->fields_ok++;
    } else if (!strcmp(f[0], "S") && n == 3) {
        p->flux = atoi(f[1]);
        p->kp   = (float)atof(f[2]);
    }
}

prop_state_t prop_state(const prop_band_t *b) {
    if (b->strong_rx >= PROP_OPEN_RX)
        return PROP_OPEN;
    if (b->dx)
        return PROP_THIN;
    return b->reports ? PROP_LOCAL : PROP_NONE;
}

const char *prop_word(prop_state_t s) {
    return s == PROP_OPEN ? "OPEN" : s == PROP_THIN ? "THIN" : s == PROP_LOCAL ? "LOCAL" : "-";
}

int prop_level(const prop_band_t *b) {
    int lv = 0;

    if (!b->dx)
        return b->reports ? 1 : 0;
    for (int d = b->dx; d && lv < 6; d >>= 1)
        lv++;
    return prop_state(b) == PROP_THIN && lv > 2 ? 2 : lv;
}

int prop_best(const prop_t *p) {
    int best = -1, most = 0;

    for (int i = 0; i < PROP_BANDS; i++) {
        int score = p->band[i].dx + (prop_state(&p->band[i]) == PROP_OPEN ? 100000 : 0);

        if (score > most) {
            most = score;
            best = i;
        }
    }
    return best;
}
