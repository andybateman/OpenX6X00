/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - Prop: which bands are open from the station now. Bands counts the world's spots; this counts what the
 *  receivers within 800 km of you decoded from over 1000 km away, on PSK Reporter in the last 30 minutes, from the
 *  GPS position or else the QTH locator. It never transmits. The MFK chooses a band, a press tunes it.
 *
 *  It asks only while it is open, and at most every five minutes, open or shut: the query is a free service, and
 *  the radio may be on a phone's data. What came last stays for the next opening.
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include "dialog_prop.h"

#include "app_frame.h"
#include "buttons.h"
#include "dialog.h"
#include "dialog_field.h"
#include "events.h"
#include "field/bands.h"
#include "cfg/cfg_api.h"
#include "keyboard.h"
#include "msg.h"
#include "params/params.h"
#include "propband.h"
#include "qth/qth.h"
#include "radio.h"
#include "station.h"
#include "styles.h"
#include "theme.h"
#include "wifi.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef PROP_FETCH
#define PROP_FETCH "/usr/sbin/x6100-propagation"
#endif
#define POLL_S  300
#define ROWS    6                   /* per column */
#define COL_W   ((APP_FRAME_W - 18) / 2)
#define ROW_H   30
#define BAR_W   84

_Static_assert(PROP_BANDS == FIELD_RBN_BANDS, "Prop's bands are field/bands.h's");

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);
static void refresh_cb(button_data_t *b);
static void tune_cb(button_data_t *b);

static button_data_t btn_page    = {.type = BTN_TEXT, .label = "(PROP 1:1)"};
static button_data_t btn_refresh = {.type = BTN_TEXT, .label = "Refresh", .press = refresh_cb};
static button_data_t btn_tune    = {.type = BTN_TEXT, .label = "Use\nband", .press = tune_cb};

static buttons_page_t btn_page_1 = {.items = {&btn_page, &btn_refresh, &btn_tune}};

static dialog_t dialog = {
    .run          = false,
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .key_cb       = key_cb,
};

dialog_t *dialog_prop = &dialog;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static prop_t          latest;      /* under lock */
static unsigned        latest_gen;  /* under lock */
static bool            running;     /* under lock */
static bool            failed;      /* under lock: the last try gave nothing */
static time_t          asked_at;    /* LVGL thread */
static char            ask_grid[9];

static prop_t      view;            /* LVGL thread */
static unsigned    view_gen;
static app_frame_t frame;
static lv_timer_t *timer;
static int         sel;
static lv_coord_t  bar_w;

static struct {
    lv_obj_t *cell, *name, *bar, *word, *counts;
} rows[PROP_BANDS];

static void *worker(void *arg) {
    static prop_t p;
    char          cmd[64], line[128];

    (void)arg;
    pthread_detach(pthread_self());
    prop_init(&p, ask_grid, time(NULL));
    snprintf(cmd, sizeof(cmd), PROP_FETCH " %s 2>/dev/null", ask_grid);
    FILE *f = popen(cmd, "r");
    if (f) {
        while (fgets(line, sizeof(line), f))
            prop_line(&p, line);
        pclose(f);
    }
    pthread_mutex_lock(&lock);
    failed = p.fields_ok == 0;
    if (!failed) {
        latest = p;
        latest_gen++;
    }
    running = false;
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* Checked already: ask_grid goes into a command line */
static bool position_known(void) {
    const char *qth = station_qth();

    return qth && qth[0] && strlen(qth) < sizeof(ask_grid) && qth_grid_check(qth);
}

static void ask(void) {
    pthread_t t;
    bool      busy;

    pthread_mutex_lock(&lock);
    busy = running;
    if (!busy)
        running = true;
    pthread_mutex_unlock(&lock);
    if (busy)
        return;
    snprintf(ask_grid, sizeof(ask_grid), "%s", station_qth());
    asked_at = time(NULL);
    if (pthread_create(&t, NULL, worker, NULL) != 0) {
        pthread_mutex_lock(&lock);
        running = false;
        pthread_mutex_unlock(&lock);
    }
}

static int wait_s(void) {
    time_t since = time(NULL) - asked_at;

    return asked_at && since >= 0 && since < POLL_S ? POLL_S - (int)since : 0;
}

static void fill(void) {
    int best = prop_best(&view);

    for (int b = 0; b < PROP_BANDS; b++) {
        const prop_band_t *pb = &view.band[b];
        prop_state_t       s  = prop_state(pb);
        int                lv = prop_level(pb);
        uint32_t           color = s == PROP_OPEN ? theme_hex(TC_ok) : s == PROP_THIN ? theme_hex(TC_warn)
                                 : s == PROP_LOCAL ? theme_hex(TC_err) : theme_hex(TC_text_faint);
        bool               on = b == sel;
        char               buf[32] = "";

        lv_obj_set_style_bg_opa(rows[b].cell, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rows[b].cell, on ? TC(sel_text) : TC(text), 0);
        lv_obj_set_style_border_width(rows[b].cell, b == best ? 1 : 0, 0);
        lv_obj_set_width(rows[b].bar, 6 + lv * (bar_w - 6) / 6);
        lv_obj_set_style_bg_color(rows[b].bar, lv ? lv_color_hex(color) : TC(track), 0);
        lv_label_set_text(rows[b].word, prop_word(s));
        lv_obj_set_style_text_color(rows[b].word, on ? TC(sel_text) : lv_color_hex(color), 0);
        if (pb->dx)
            snprintf(buf, sizeof(buf), "%d  %d km", pb->dx, pb->max_km);
        else if (pb->max_km)
            snprintf(buf, sizeof(buf), "%d km", pb->max_km);
        lv_label_set_text(rows[b].counts, buf);
    }
}

static void status(void) {
    char line[160];
    bool busy, bad;

    pthread_mutex_lock(&lock);
    busy = running;
    bad  = failed;
    pthread_mutex_unlock(&lock);

    if (!position_known())
        snprintf(line, sizeof(line), "No position: set the QTH locator, or get a GPS fix");
    else if (busy && !view.at)
        snprintf(line, sizeof(line), "Asking PSK Reporter...");
    else if (bad && !view.at)
        snprintf(line, sizeof(line), "PSK Reporter did not answer. Trying again in %d min", (wait_s() + 59) / 60);
    else if (!view.at)
        snprintf(line, sizeof(line), net_connected() ? "Starting..." : "No network");
    else {
        int age = (int)((time(NULL) - view.at) / 60);
        int p   = snprintf(line, sizeof(line), "%d receivers near you, %d min ago. Heard, mostly FT8, not a forecast",
                           view.receivers, age < 0 ? 0 : age);

        if (view.fields_ok < view.fields && p < (int)sizeof(line))
            snprintf(line + p, sizeof(line) - p, ". Part of the area missing");
    }
    lv_label_set_text(frame.status, line);
}

static void head(void) {
    char right[40] = "";
    int  p = 0;

    if (view.flux >= 0)
        p = snprintf(right, sizeof(right), "SFI %d  ", view.flux);
    if (view.kp >= 0)
        snprintf(right + p, sizeof(right) - p, "Kp %.1f", view.kp);
    lv_label_set_text_fmt(frame.head_left, "PROP   %s", view.at ? view.grid : station_qth());
    lv_label_set_text(frame.head_right, right);
}

static void timer_cb(lv_timer_t *t) {
    bool changed = false;

    (void)t;
    pthread_mutex_lock(&lock);
    if (latest_gen != view_gen) {
        view     = latest;
        view_gen = latest_gen;
        changed  = true;
    }
    pthread_mutex_unlock(&lock);
    if (!wait_s() && net_connected() && position_known())
        ask();
    if (changed || !t)
        fill();
    head();
    status();
}

static void select_band(int b) {
    sel = b < 0 ? 0 : b >= PROP_BANDS ? PROP_BANDS - 1 : b;
    fill();
}

static void refresh_cb(button_data_t *b) {
    int w = wait_s();

    (void)b;
    if (w)
        msg_update_text_fmt("Next update in %d:%02d", w / 60, w % 60);
    else if (!net_connected())
        msg_update_text_fmt("No network");
    else if (position_known())
        ask();
}

static void tune_cb(button_data_t *b) {
    (void)b;
    dialog_field_tune_band(sel);
    dialog_destruct();
}

static void cell_click_cb(lv_event_t *e) {
    select_band((int)(intptr_t)lv_event_get_user_data(e));
    tune_cb(NULL);
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct();
            break;
        case LV_KEY_UP:
        case LV_KEY_LEFT:
            select_band(sel - 1);
            break;
        case LV_KEY_DOWN:
        case LV_KEY_RIGHT:
            select_band(sel + 1);
            break;
        case LV_KEY_ENTER:
            tune_cb(NULL);
            break;
        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;
        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t x, lv_coord_t w) {
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
    lv_label_set_text(l, "");
    return l;
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);
    app_frame_create(&frame, dialog.obj);

    lv_obj_t *body = lv_obj_create(dialog.obj);
    lv_obj_remove_style_all(body);
    app_frame_body(body);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Columns: band, bar, word, senders from afar and the longest path. Wide enough for VISION's bigger type */
    lv_coord_t name_w = LV_MAX(62, lv_txt_get_width("160m", 4, &sony_22, 0, LV_TEXT_FLAG_NONE) + 8);
    lv_coord_t word_w = LV_MAX(58, lv_txt_get_width("LOCAL", 5, &sony_18, 0, LV_TEXT_FLAG_NONE) + 10);
    lv_coord_t text_w = LV_MAX(110, lv_txt_get_width("256  19999 km", 13, &sony_18, 0, LV_TEXT_FLAG_NONE) + 8);
    lv_coord_t bar_x  = 8 + name_w + 4;

    bar_w = LV_MAX(24, LV_MIN(BAR_W, COL_W - bar_x - 8 - word_w - text_w - 4));
    lv_coord_t word_x = bar_x + bar_w + 8, text_x = word_x + word_w;

    for (int b = 0; b < PROP_BANDS; b++) {
        lv_obj_t *c = lv_obj_create(body);

        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, COL_W, ROW_H);
        lv_obj_set_pos(c, 6 + (b / ROWS) * (COL_W + 6), 6 + (b % ROWS) * (ROW_H + 2));
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_set_style_bg_color(c, TC(sel_bg), 0);
        lv_obj_set_style_border_color(c, TC(ok), 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, cell_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)b);
        rows[b].cell = c;

        rows[b].name = label(c, &sony_22, 8, name_w);
        lv_label_set_text(rows[b].name, field_band_name(b));

        lv_obj_t *track = lv_obj_create(c);
        lv_obj_remove_style_all(track);
        lv_obj_set_size(track, bar_w, 8);
        lv_obj_align(track, LV_ALIGN_LEFT_MID, bar_x, 0);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_bg_color(track, TC(track), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        rows[b].bar = lv_obj_create(track);
        lv_obj_remove_style_all(rows[b].bar);
        lv_obj_set_height(rows[b].bar, 8);
        lv_obj_set_style_radius(rows[b].bar, 4, 0);
        lv_obj_set_style_bg_opa(rows[b].bar, LV_OPA_COVER, 0);

        rows[b].word   = label(c, &sony_18, word_x, word_w);
        rows[b].counts = label(c, &sony_18, text_x, COL_W - text_x - 4);
    }

    buttons_load_page(&btn_page_1);
    lv_group_add_obj(keyboard_group, dialog.obj);
    lv_obj_add_event_cb(dialog.obj, key_cb, LV_EVENT_KEY, NULL);
    lv_group_focus_obj(dialog.obj);
    lv_group_set_editing(keyboard_group, true);

    sel = field_band_of(cparam_i_get(cfg_fg_freq));
    if (sel < 0)
        sel = 0;
    timer_cb(NULL);
    timer = lv_timer_create(timer_cb, 1000, NULL);
}

static void destruct_cb(void) {
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}
