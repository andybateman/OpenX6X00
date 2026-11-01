/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - host test of the Prop app's logic (src/propband.c); see the Makefile
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include <stdio.h>
#include <string.h>

#include "propband.h"

#define B20 5
#define B15 7
#define B6  10

static int cases, good;

static void check(bool ok, const char *what) {
    cases++;
    if (ok) good++;
    else printf("FAILED: %s\n", what);
}

/* n different senders on hz, heard at rx from tx */
static void senders(prop_t *p, int n, const char *prefix, long hz, const char *rx, const char *tx) {
    char line[100];

    for (int i = 0; i < n; i++) {
        snprintf(line, sizeof(line), "R|%s%d|%ld|%s|%s\n", prefix, i, hz, rx, tx);
        prop_line(p, line);
    }
}

int main(void) {
    static prop_t p;

    /* Bands */
    check(prop_band_of(1840000) == 0, "160 m");
    check(prop_band_of(14074000) == B20, "20 m");
    check(prop_band_of(21074000) == B15, "15 m");
    check(prop_band_of(50313000) == B6, "6 m");
    check(prop_band_of(13999999) == -1, "just under 20 m");
    check(prop_band_of(144174000) == -1, "2 m is not one of them");

    /* Nothing heard */
    prop_init(&p, "RE78rw", 1000);
    check(prop_best(&p) == -1, "no best band at first");
    check(prop_state(&p.band[B20]) == PROP_NONE, "no reports: nothing");
    check(prop_level(&p.band[B20]) == 0, "no bar");
    check(!strcmp(prop_word(PROP_NONE), "-"), "the word for nothing");
    check(p.flux == -1 && p.kp < 0, "solar unknown at first");

    /* From afar, near here: Texas heard in Wellington */
    senders(&p, 4, "K", 14074000, "RE78js", "DM61VS");
    check(p.band[B20].dx == 4, "four senders from afar");
    check(prop_state(&p.band[B20]) == PROP_THIN, "four is thin");
    check(p.band[B20].max_km > 11000 && p.band[B20].max_km < 13000, "Texas to Wellington is about 12000 km");
    senders(&p, 1, "W", 14074000, "RE78js", "DM61VS");
    check(prop_state(&p.band[B20]) == PROP_THIN, "five at one receiver is still thin");
    senders(&p, 4, "K", 14074000, "RE66gb", "DM61VS");
    check(prop_state(&p.band[B20]) == PROP_THIN, "four at the second receiver is not enough");
    senders(&p, 1, "V", 14074000, "RE66gb", "DM61VS");
    check(prop_state(&p.band[B20]) == PROP_OPEN, "five each at two receivers is open");
    check(prop_best(&p) == B20, "20 m is the best");

    /* One remote site hears far more than the mainland: the Chathams on 1 Nov 2026 */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 72, "K", 14074000, "AE16ab", "DM61VS");
    senders(&p, 4, "J", 14074000, "RE78js", "PM95uq");
    check(p.band[B20].dx == 76, "all of them counted");
    check(prop_state(&p.band[B20]) == PROP_THIN, "one site is not an open band");
    check(prop_level(&p.band[B20]) == 2, "and the bar does not say so");
    senders(&p, 5, "L", 21074000, "RE78js", "PM95uq");
    senders(&p, 5, "L", 21074000, "RE66gb", "PM95uq");
    check(prop_best(&p) == B15, "an open band beats a thin one with more senders");

    /* The same sender again, or heard at a second receiver, is one sender */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 1, "K", 14074000, "RE78js", "DM61VS");
    senders(&p, 1, "K", 14074000, "RE78js", "DM61VS");
    senders(&p, 1, "K", 14075000, "RF73ab", "DM61VS");
    check(p.band[B20].dx == 1, "one sender");
    check(p.band[B20].reports == 3, "three reports");
    check(p.receivers == 2, "two receivers");

    /* The same sender on another band counts there too */
    senders(&p, 1, "K", 21074000, "RE78js", "DM61VS");
    check(p.band[B15].dx == 1, "counted again on 15 m");

    /* A receiver in another field but in range counts: Auckland, for Greytown */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 5, "K", 21074000, "RF73", "DM61");
    check(p.band[B15].dx == 5, "a receiver over the field boundary counts");

    /* A receiver out of range does not: Tokyo says nothing of Greytown */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 9, "K", 21074000, "PM95uq", "DM61VS");
    check(p.band[B15].reports == 0 && p.receivers == 0, "a far receiver is left out");

    /* Local paths are not the band being open */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 9, "ZL", 50313000, "RE78js", "RE66gb");
    check(prop_state(&p.band[B6]) == PROP_LOCAL, "local only");
    check(prop_level(&p.band[B6]) == 1, "local: the shortest bar");
    check(prop_best(&p) == -1, "local is no best band");

    /* Reports to skip, or half use */
    prop_init(&p, "RE78rw", 1000);
    prop_line(&p, "R|K1|14074000|RE78js|\n");
    check(p.band[B20].reports == 1 && p.band[B20].dx == 0, "a sender with no locator: a report, no path");
    prop_line(&p, "R|K2|14074000|garbage|DM61\n");
    prop_line(&p, "R|K3|14074000||DM61\n");
    prop_line(&p, "R||14074000|RE78js|DM61\n");
    check(p.band[B20].reports == 1, "a bad receiver or no call is left out");
    prop_line(&p, "R|K4|14074000\n");
    prop_line(&p, "nonsense\n");
    prop_line(&p, "\n");
    check(p.band[B20].reports == 1, "lines that are not whole");
    prop_line(&p, "R|K5|9999|RE78js|DM61VS\n");
    check(p.receivers == 1 && p.band[B20].reports == 1, "off the bands: the receiver is seen, no band");

    /* Case and length of locators */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, 1, "A", 14074000, "RE78JS", "dm61vs");
    senders(&p, 1, "B", 14074000, "re78js00aa", "DM61VS00");
    check(p.band[B20].dx == 2, "either case, and long locators");
    check(p.band[B20].max_km > 11000 && p.band[B20].max_km < 13000, "the same distance either way");

    /* The counts stop at the set's size, and do not run over */
    prop_init(&p, "RE78rw", 1000);
    senders(&p, PROP_SEEN_MAX + 40, "K", 14074000, "RE78js", "DM61VS");
    senders(&p, 5, "K", 14074000, "RE66gb", "DM61VS");
    check(p.band[B20].dx == PROP_SEEN_MAX, "senders stop at the set's size");
    check(prop_level(&p.band[B20]) == 6, "the bar stops at 6");

    /* The bar doubles */
    prop_band_t lv = {0};
    int         want[][2] = {{1, 1}, {3, 2}, {4, 3}, {8, 4}, {16, 5}, {32, 6}, {200, 6}};
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        lv.dx = want[i][0];
        lv.strong_rx = PROP_OPEN_RX;
        check(prop_level(&lv) == want[i][1], "bar level");
    }

    /* Fields and solar */
    prop_init(&p, "RE78rw", 1000);
    prop_line(&p, "F|RE|1\n");
    prop_line(&p, "F|RF|0\n");
    prop_line(&p, "S|92|3.7\n");
    check(p.fields == 2 && p.fields_ok == 1, "fields asked and answered");
    check(p.flux == 92 && p.kp > 3.6f && p.kp < 3.8f, "solar");
    prop_line(&p, "S|-1|-1.0\n");
    check(p.flux == -1 && p.kp < 0, "solar unknown");

    printf("%d of %d cases pass\n", good, cases);
    return good == cases ? 0 : 1;
}
