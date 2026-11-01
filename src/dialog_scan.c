/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  OpenX6X00 - Scan: the receiver steps through the channels, or a span around the VFO, and stops on a signal
 *  (rxscan.c says when). It only ever receives: transmitting, or anything else moving the VFO, ends the scan. It
 *  tunes as the Channels window and the DX cluster do, so stopping leaves the radio on what it found.
 *
 *  Copyright (c) 2026 the OpenX6X00 authors
 */

#include "dialog_scan.h"

#include "buttons.h"
#include "cfg/cfg_api.h"
#include "channels.h"
#include "fonts/font_select.h"
#include "gui_events.h"
#include "keyboard.h"
#include "msg.h"
#include "radio.h"
#include "rxscan.h"
#include "screen_main.h"
#include "smeter.h"
#include "styles.h"
#include "theme.h"
#include "util.h"

#include <stdio.h>

#define TICK_MS 100

static const int32_t  spans[]  = { 0, 50000, 100000, 200000, 500000 };    /* 0: the channels */
static const int32_t  steps[]  = { 2500, 5000, 10000 };
static const int16_t  levels[] = { S3, S5, S7, S9 };
static const char    *level_names[] = { "S3", "S5", "S7", "S9" };
static const uint32_t holds[]  = { 0, 5000, 10000 };

#define ITEMS(a) ((int)(sizeof(a) / sizeof((a)[0])))

static uint8_t   span_i = 2, step_i = 1, level_i = 1, hold_i;
static rxscan_t  scan;
static int32_t   low_hz, tuned_hz;
static lv_obj_t *freq_label, *name_label, *state_label;
static lv_timer_t *timer;

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);
static void rotary_cb(int32_t steps_turned);
static void run_cb(button_data_t *b);
static void span_cb(button_data_t *b);
static void step_cb(button_data_t *b);
static void level_cb(button_data_t *b);
static void hold_cb(button_data_t *b);
static const char *run_label(void);
static const char *span_label(void);
static const char *step_label(void);
static const char *level_label(void);
static const char *hold_label(void);

static button_data_t btn_run   = { .type = BTN_TEXT_FN, .label_fn = run_label,   .press = run_cb };
static button_data_t btn_span  = { .type = BTN_TEXT_FN, .label_fn = span_label,  .press = span_cb };
static button_data_t btn_step  = { .type = BTN_TEXT_FN, .label_fn = step_label,  .press = step_cb };
static button_data_t btn_level = { .type = BTN_TEXT_FN, .label_fn = level_label, .press = level_cb };
static button_data_t btn_hold  = { .type = BTN_TEXT_FN, .label_fn = hold_label,  .press = hold_cb };

static buttons_page_t page = { { &btn_run, &btn_span, &btn_step, &btn_level, &btn_hold } };

static dialog_t dialog = {
    .construct_cb = construct_cb, .destruct_cb = destruct_cb, .btn_page = &page, .key_cb = key_cb,
    .rotary_cb = rotary_cb
};

dialog_t *dialog_scan = &dialog;

static bool on_channels(void) {
    return spans[span_i] == 0;
}

static void show_freq(int32_t hz, const char *name) {
    lv_label_set_text_fmt(freq_label, "%i.%03i.%03i", (int)(hz / 1000000), (int)(hz / 1000 % 1000), (int)(hz % 1000));
    lv_label_set_text(name_label, name ? name : "");
}

/* The stop at scan.pos, as the Channels window or a spot tunes. False: it can't be tuned */
static bool tune_stop(void) {
    if (on_channels()) {
        const channel_t *c = channels_get(scan.pos);

        if (!c || !radio_check_freq(c->freq)) return false;
        tuned_hz = c->freq;
        main_screen_set_freq(c->freq);
        cparam_i_set(cfg_cur_mode, c->mode);
        cparam_i_set(cfg_cur_agc, c->agc);
        cparam_i_set(cfg_cur_pre, c->pre ? radio_pre_on : radio_pre_off);
        cparam_i_set(cfg_cur_att, c->att ? radio_att_on : radio_att_off);
        show_freq(c->freq, c->name[0] ? c->name : "Channel");
    } else {
        int32_t hz = low_hz + scan.pos * steps[step_i];

        if (!radio_check_freq(hz)) return false;
        tuned_hz = hz;
        main_screen_set_freq(hz);
        show_freq(hz, NULL);
    }
    return true;
}

/* On to the next stop that can be tuned; false when none can */
static bool tune_next(void) {
    for (uint16_t i = 0; i < scan.count; i++) {
        if (tune_stop()) return true;
        rxscan_skip(&scan, get_time());
    }
    return false;
}

static void set_state(const char *text) {
    lv_label_set_text(state_label, text);
}

/* The VFO stays where it is: what was found stays tuned */
static void stop(const char *why) {
    rxscan_stop(&scan);
    set_state(why);
    buttons_refresh(&btn_run);
}

static void timer_cb(lv_timer_t *t) {
    char s[48];

    if (!rxscan_running(&scan)) return;

    if (radio_get_state() != RADIO_RX) {
        stop("Stopped: transmitting");
        return;
    }
    if (cparam_i_get(cfg_fg_freq) != tuned_hz) {
        stop("Stopped: the VFO was moved");
        return;
    }

    switch (rxscan_tick(&scan, get_time(), meter_get_db())) {
        case RXSCAN_FOUND:
            snprintf(s, sizeof(s), "Signal: %i dBm. Stopped here", (int)meter_get_db());
            stop(s);
            return;
        case RXSCAN_TUNE:
            if (!tune_next()) {
                stop("Stopped: nothing here can be tuned");
                return;
            }
            break;
        case RXSCAN_WAIT:
            break;
    }
    if (scan.state == RXSCAN_HOLD) {
        snprintf(s, sizeof(s), "Signal: %i dBm. Main knob: next", (int)meter_get_db());
        set_state(s);
    } else {
        lv_label_set_text_fmt(state_label, "Scanning %u of %u", scan.pos + 1, scan.count);
    }
}

static void start(void) {
    uint16_t count;

    if (radio_get_state() != RADIO_RX) {
        msg_update_text_fmt("Scan: not while transmitting or tuning");
        return;
    }
    if (on_channels()) {
        channels_reload_if_changed();
        count = channels_count();
        if (!count) {
            msg_update_text_fmt("Scan: no channels yet");
            return;
        }
    } else {
        int32_t step = steps[step_i];

        low_hz = cparam_i_get(cfg_fg_freq) - spans[span_i] / 2;
        low_hz -= low_hz % step;
        count = spans[span_i] / step + 1;
    }
    rxscan_start(&scan, count, levels[level_i], holds[hold_i], get_time());
    if (!tune_next()) {
        stop("Stopped: nothing here can be tuned");
        return;
    }
    set_state("Scanning");
}

static void run_cb(button_data_t *b) {
    if (rxscan_running(&scan))
        stop("Stopped");
    else
        start();
    buttons_refresh(&btn_run);
}

/* Each setting takes effect at the next Run */
static void next(uint8_t *i, int n, button_data_t *b) {
    if (rxscan_running(&scan)) return;
    *i = (*i + 1) % n;
    buttons_refresh(b);
}

static void span_cb(button_data_t *b)  { next(&span_i, ITEMS(spans), b); }
static void step_cb(button_data_t *b)  { next(&step_i, ITEMS(steps), b); }
static void level_cb(button_data_t *b) { next(&level_i, ITEMS(levels), b); }
static void hold_cb(button_data_t *b)  { next(&hold_i, ITEMS(holds), b); }

static const char *run_label(void) {
    return rxscan_running(&scan) ? "Stop" : "Run";
}

static const char *span_label(void) {
    static char s[24];

    if (on_channels()) return "Scan:\nChannels";
    snprintf(s, sizeof(s), "Scan:\n%i kHz", (int)(spans[span_i] / 1000));
    return s;
}

static const char *step_label(void) {
    static char s[24];

    snprintf(s, sizeof(s), "Step:\n%i.%i kHz", (int)(steps[step_i] / 1000), (int)(steps[step_i] % 1000 / 100));
    return s;
}

static const char *level_label(void) {
    static char s[24];

    snprintf(s, sizeof(s), "Level:\n%s", level_names[level_i]);
    return s;
}

static const char *hold_label(void) {
    static char s[24];

    if (!holds[hold_i]) return "On signal:\nStop";
    snprintf(s, sizeof(s), "On signal:\n%i s", (int)(holds[hold_i] / 1000));
    return s;
}

/* The main knob, which can't tune here: on to the next stop */
static void rotary_cb(int32_t steps_turned) {
    if (!rxscan_running(&scan)) return;
    rxscan_skip(&scan, get_time());
    if (!tune_next()) stop("Stopped: nothing here can be tuned");
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *(uint32_t *)lv_event_get_param(e);

    if (key == LV_KEY_ESC)
        dialog_destruct();
    else if (key == KEY_VOL_LEFT_EDIT || key == KEY_VOL_LEFT_SELECT)
        radio_change_vol(-1);
    else if (key == KEY_VOL_RIGHT_EDIT || key == KEY_VOL_RIGHT_SELECT)
        radio_change_vol(1);
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_coord_t y, const char *s) {
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, 16, y);
    lv_label_set_text(l, s);
    return l;
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);

    text(dialog.obj, &sony_28, TC(text_head), 12, "SCAN");
    text(dialog.obj, &sony_22, TC(text_dim), 48, "Receive only: it never transmits, and any transmission stops it.");
    freq_label = text(dialog.obj, &sony_36, TC(text_value), 96, "");
    name_label = text(dialog.obj, &sony_24, TC(text), 148, "");
    state_label = text(dialog.obj, &sony_24, TC(text_soft), 196, "Press Run");

    lv_group_add_obj(keyboard_group, dialog.obj);
    lv_obj_add_event_cb(dialog.obj, key_cb, LV_EVENT_KEY, NULL);
    lv_group_focus_obj(dialog.obj);
    lv_group_set_editing(keyboard_group, true);

    show_freq(cparam_i_get(cfg_fg_freq), NULL);
    timer = lv_timer_create(timer_cb, TICK_MS, NULL);
}

static void destruct_cb(void) {
    rxscan_stop(&scan);
    if (timer) lv_timer_del(timer);
    timer = NULL;
    lv_group_remove_obj(dialog.obj);
}
