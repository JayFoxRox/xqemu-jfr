/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "ui/console.h"
#include "qapi/error.h"
#include "qmp-commands.h"
#include "qapi-types.h"
#include "ui/keymaps.h"
#include "ui/input.h"

struct QEMUPutMouseEntry {
    QEMUPutMouseEvent *qemu_put_mouse_event;
    void *qemu_put_mouse_event_opaque;
    int qemu_put_mouse_event_absolute;

    /* new input core */
    QemuInputHandler h;
    QemuInputHandlerState *s;
    int axis[INPUT_AXIS_MAX];
    int buttons;
};

struct QEMUPutKbdEntry {
    QEMUPutKBDEvent *put_kbd;
    void *opaque;
    QemuInputHandlerState *s;
};

struct QEMUPutLEDEntry {
    QEMUPutLEDEvent *put_led;
    void *opaque;
    QTAILQ_ENTRY(QEMUPutLEDEntry) next;
};

static QTAILQ_HEAD(, QEMUPutLEDEntry) led_handlers =
    QTAILQ_HEAD_INITIALIZER(led_handlers);
static QTAILQ_HEAD(, QEMUPutMouseEntry) mouse_handlers =
    QTAILQ_HEAD_INITIALIZER(mouse_handlers);

static const int key_defs[] = {
    [Q_KEY_CODE_SHIFT] = 0x2a,
    [Q_KEY_CODE_SHIFT_R] = 0x36,

    [Q_KEY_CODE_ALT] = 0x38,
    [Q_KEY_CODE_ALT_R] = 0xb8,
    [Q_KEY_CODE_ALTGR] = 0x64,
    [Q_KEY_CODE_ALTGR_R] = 0xe4,
    [Q_KEY_CODE_CTRL] = 0x1d,
    [Q_KEY_CODE_CTRL_R] = 0x9d,

    [Q_KEY_CODE_MENU] = 0xdd,

    [Q_KEY_CODE_ESC] = 0x01,

    [Q_KEY_CODE_1] = 0x02,
    [Q_KEY_CODE_2] = 0x03,
    [Q_KEY_CODE_3] = 0x04,
    [Q_KEY_CODE_4] = 0x05,
    [Q_KEY_CODE_5] = 0x06,
    [Q_KEY_CODE_6] = 0x07,
    [Q_KEY_CODE_7] = 0x08,
    [Q_KEY_CODE_8] = 0x09,
    [Q_KEY_CODE_9] = 0x0a,
    [Q_KEY_CODE_0] = 0x0b,
    [Q_KEY_CODE_MINUS] = 0x0c,
    [Q_KEY_CODE_EQUAL] = 0x0d,
    [Q_KEY_CODE_BACKSPACE] = 0x0e,

    [Q_KEY_CODE_TAB] = 0x0f,
    [Q_KEY_CODE_Q] = 0x10,
    [Q_KEY_CODE_W] = 0x11,
    [Q_KEY_CODE_E] = 0x12,
    [Q_KEY_CODE_R] = 0x13,
    [Q_KEY_CODE_T] = 0x14,
    [Q_KEY_CODE_Y] = 0x15,
    [Q_KEY_CODE_U] = 0x16,
    [Q_KEY_CODE_I] = 0x17,
    [Q_KEY_CODE_O] = 0x18,
    [Q_KEY_CODE_P] = 0x19,
    [Q_KEY_CODE_BRACKET_LEFT] = 0x1a,
    [Q_KEY_CODE_BRACKET_RIGHT] = 0x1b,
    [Q_KEY_CODE_RET] = 0x1c,

    [Q_KEY_CODE_A] = 0x1e,
    [Q_KEY_CODE_S] = 0x1f,
    [Q_KEY_CODE_D] = 0x20,
    [Q_KEY_CODE_F] = 0x21,
    [Q_KEY_CODE_G] = 0x22,
    [Q_KEY_CODE_H] = 0x23,
    [Q_KEY_CODE_J] = 0x24,
    [Q_KEY_CODE_K] = 0x25,
    [Q_KEY_CODE_L] = 0x26,
    [Q_KEY_CODE_SEMICOLON] = 0x27,
    [Q_KEY_CODE_APOSTROPHE] = 0x28,
    [Q_KEY_CODE_GRAVE_ACCENT] = 0x29,

    [Q_KEY_CODE_BACKSLASH] = 0x2b,
    [Q_KEY_CODE_Z] = 0x2c,
    [Q_KEY_CODE_X] = 0x2d,
    [Q_KEY_CODE_C] = 0x2e,
    [Q_KEY_CODE_V] = 0x2f,
    [Q_KEY_CODE_B] = 0x30,
    [Q_KEY_CODE_N] = 0x31,
    [Q_KEY_CODE_M] = 0x32,
    [Q_KEY_CODE_COMMA] = 0x33,
    [Q_KEY_CODE_DOT] = 0x34,
    [Q_KEY_CODE_SLASH] = 0x35,

    [Q_KEY_CODE_ASTERISK] = 0x37,

    [Q_KEY_CODE_SPC] = 0x39,
    [Q_KEY_CODE_CAPS_LOCK] = 0x3a,
    [Q_KEY_CODE_F1] = 0x3b,
    [Q_KEY_CODE_F2] = 0x3c,
    [Q_KEY_CODE_F3] = 0x3d,
    [Q_KEY_CODE_F4] = 0x3e,
    [Q_KEY_CODE_F5] = 0x3f,
    [Q_KEY_CODE_F6] = 0x40,
    [Q_KEY_CODE_F7] = 0x41,
    [Q_KEY_CODE_F8] = 0x42,
    [Q_KEY_CODE_F9] = 0x43,
    [Q_KEY_CODE_F10] = 0x44,
    [Q_KEY_CODE_NUM_LOCK] = 0x45,
    [Q_KEY_CODE_SCROLL_LOCK] = 0x46,

    [Q_KEY_CODE_KP_DIVIDE] = 0xb5,
    [Q_KEY_CODE_KP_MULTIPLY] = 0x37,
    [Q_KEY_CODE_KP_SUBTRACT] = 0x4a,
    [Q_KEY_CODE_KP_ADD] = 0x4e,
    [Q_KEY_CODE_KP_ENTER] = 0x9c,
    [Q_KEY_CODE_KP_DECIMAL] = 0x53,
    [Q_KEY_CODE_SYSRQ] = 0x54,

    [Q_KEY_CODE_KP_0] = 0x52,
    [Q_KEY_CODE_KP_1] = 0x4f,
    [Q_KEY_CODE_KP_2] = 0x50,
    [Q_KEY_CODE_KP_3] = 0x51,
    [Q_KEY_CODE_KP_4] = 0x4b,
    [Q_KEY_CODE_KP_5] = 0x4c,
    [Q_KEY_CODE_KP_6] = 0x4d,
    [Q_KEY_CODE_KP_7] = 0x47,
    [Q_KEY_CODE_KP_8] = 0x48,
    [Q_KEY_CODE_KP_9] = 0x49,

    [Q_KEY_CODE_LESS] = 0x56,

    [Q_KEY_CODE_F11] = 0x57,
    [Q_KEY_CODE_F12] = 0x58,

    [Q_KEY_CODE_PRINT] = 0xb7,

    [Q_KEY_CODE_HOME] = 0xc7,
    [Q_KEY_CODE_PGUP] = 0xc9,
    [Q_KEY_CODE_PGDN] = 0xd1,
    [Q_KEY_CODE_END] = 0xcf,

    [Q_KEY_CODE_LEFT] = 0xcb,
    [Q_KEY_CODE_UP] = 0xc8,
    [Q_KEY_CODE_DOWN] = 0xd0,
    [Q_KEY_CODE_RIGHT] = 0xcd,

    [Q_KEY_CODE_INSERT] = 0xd2,
    [Q_KEY_CODE_DELETE] = 0xd3,
#ifdef NEED_CPU_H
#if defined(TARGET_SPARC) && !defined(TARGET_SPARC64)
    [Q_KEY_CODE_STOP] = 0xf0,
    [Q_KEY_CODE_AGAIN] = 0xf1,
    [Q_KEY_CODE_PROPS] = 0xf2,
    [Q_KEY_CODE_UNDO] = 0xf3,
    [Q_KEY_CODE_FRONT] = 0xf4,
    [Q_KEY_CODE_COPY] = 0xf5,
    [Q_KEY_CODE_OPEN] = 0xf6,
    [Q_KEY_CODE_PASTE] = 0xf7,
    [Q_KEY_CODE_FIND] = 0xf8,
    [Q_KEY_CODE_CUT] = 0xf9,
    [Q_KEY_CODE_LF] = 0xfa,
    [Q_KEY_CODE_HELP] = 0xfb,
    [Q_KEY_CODE_META_L] = 0xfc,
    [Q_KEY_CODE_META_R] = 0xfd,
    [Q_KEY_CODE_COMPOSE] = 0xfe,
#endif
#endif
    [Q_KEY_CODE_MAX] = 0,
};

int index_from_key(const char *key)
{
    int i;

    for (i = 0; QKeyCode_lookup[i] != NULL; i++) {
        if (!strcmp(key, QKeyCode_lookup[i])) {
            break;
        }
    }

    /* Return Q_KEY_CODE_MAX if the key is invalid */
    return i;
}

static int *keycodes;
static int keycodes_size;
static QEMUTimer *key_timer;

static int keycode_from_keyvalue(const KeyValue *value)
{
    if (value->kind == KEY_VALUE_KIND_QCODE) {
        return key_defs[value->qcode];
    } else {
        assert(value->kind == KEY_VALUE_KIND_NUMBER);
        return value->number;
    }
}

static void free_keycodes(void)
{
    g_free(keycodes);
    keycodes = NULL;
    keycodes_size = 0;
}

static void release_keys(void *opaque)
{
    while (keycodes_size > 0) {
        qemu_input_event_send_key_number(NULL, keycodes[--keycodes_size],
                                         false);
    }

    free_keycodes();
}

void qmp_send_key(KeyValueList *keys, bool has_hold_time, int64_t hold_time,
                  Error **errp)
{
    int keycode;
    KeyValueList *p;

    if (!key_timer) {
        key_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, release_keys, NULL);
    }

    if (keycodes != NULL) {
        timer_del(key_timer);
        release_keys(NULL);
    }

    if (!has_hold_time) {
        hold_time = 100;
    }

    for (p = keys; p != NULL; p = p->next) {
        /* key down events */
        keycode = keycode_from_keyvalue(p->value);
        if (keycode < 0x01 || keycode > 0xff) {
            error_setg(errp, "invalid hex keycode 0x%x", keycode);
            free_keycodes();
            return;
        }

        qemu_input_event_send_key_number(NULL, keycode, true);

        keycodes = g_realloc(keycodes, sizeof(int) * (keycodes_size + 1));
        keycodes[keycodes_size++] = keycode;
    }

    /* delayed key up events */
    timer_mod(key_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                   muldiv64(get_ticks_per_sec(), hold_time, 1000));
}

static void legacy_kbd_event(DeviceState *dev, QemuConsole *src,
                             InputEvent *evt)
{
    QEMUPutKbdEntry *entry = (QEMUPutKbdEntry *)dev;
    int keycode = keycode_from_keyvalue(evt->key->key);

    if (!entry || !entry->put_kbd) {
        return;
    }
    if (evt->key->key->kind == KEY_VALUE_KIND_QCODE &&
        evt->key->key->qcode == Q_KEY_CODE_PAUSE) {
        /* specific case */
        int v = evt->key->down ? 0 : 0x80;
        entry->put_kbd(entry->opaque, 0xe1);
        entry->put_kbd(entry->opaque, 0x1d | v);
        entry->put_kbd(entry->opaque, 0x45 | v);
        return;
    }
    if (keycode & SCANCODE_GREY) {
        entry->put_kbd(entry->opaque, SCANCODE_EMUL0);
        keycode &= ~SCANCODE_GREY;
    }
    if (!evt->key->down) {
        keycode |= SCANCODE_UP;
    }
    entry->put_kbd(entry->opaque, keycode);
}

static QemuInputHandler legacy_kbd_handler = {
    .name  = "legacy-kbd",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = legacy_kbd_event,
};

QEMUPutKbdEntry *qemu_add_kbd_event_handler(QEMUPutKBDEvent *func, void *opaque)
{
    QEMUPutKbdEntry *entry;

    entry = g_new0(QEMUPutKbdEntry, 1);
    entry->put_kbd = func;
    entry->opaque = opaque;
    entry->s = qemu_input_handler_register((DeviceState *)entry,
                                           &legacy_kbd_handler);
    qemu_input_handler_activate(entry->s);
    return entry;
}

void qemu_remove_kbd_event_handler(QEMUPutKbdEntry *entry)
{
    qemu_input_handler_unregister(entry->s);
    g_free(entry);
}

static void legacy_mouse_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]   = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE] = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]  = MOUSE_EVENT_RBUTTON,
    };
    QEMUPutMouseEntry *s = (QEMUPutMouseEntry *)dev;

    switch (evt->kind) {
    case INPUT_EVENT_KIND_BTN:
        if (evt->btn->down) {
            s->buttons |= bmap[evt->btn->button];
        } else {
            s->buttons &= ~bmap[evt->btn->button];
        }
        if (evt->btn->down && evt->btn->button == INPUT_BUTTON_WHEEL_UP) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    -1,
                                    s->buttons);
        }
        if (evt->btn->down && evt->btn->button == INPUT_BUTTON_WHEEL_DOWN) {
            s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                                    s->axis[INPUT_AXIS_X],
                                    s->axis[INPUT_AXIS_Y],
                                    1,
                                    s->buttons);
        }
        break;
    case INPUT_EVENT_KIND_ABS:
        s->axis[evt->abs->axis] = evt->abs->value;
        break;
    case INPUT_EVENT_KIND_REL:
        s->axis[evt->rel->axis] += evt->rel->value;
        break;
    default:
        break;
    }
}

static void legacy_mouse_sync(DeviceState *dev)
{
    QEMUPutMouseEntry *s = (QEMUPutMouseEntry *)dev;

    s->qemu_put_mouse_event(s->qemu_put_mouse_event_opaque,
                            s->axis[INPUT_AXIS_X],
                            s->axis[INPUT_AXIS_Y],
                            0,
                            s->buttons);

    if (!s->qemu_put_mouse_event_absolute) {
        s->axis[INPUT_AXIS_X] = 0;
        s->axis[INPUT_AXIS_Y] = 0;
    }
}

QEMUPutMouseEntry *qemu_add_mouse_event_handler(QEMUPutMouseEvent *func,
                                                void *opaque, int absolute,
                                                const char *name)
{
    QEMUPutMouseEntry *s;

    s = g_malloc0(sizeof(QEMUPutMouseEntry));

    s->qemu_put_mouse_event = func;
    s->qemu_put_mouse_event_opaque = opaque;
    s->qemu_put_mouse_event_absolute = absolute;

    s->h.name = name;
    s->h.mask = INPUT_EVENT_MASK_BTN |
        (absolute ? INPUT_EVENT_MASK_ABS : INPUT_EVENT_MASK_REL);
    s->h.event = legacy_mouse_event;
    s->h.sync = legacy_mouse_sync;
    s->s = qemu_input_handler_register((DeviceState *)s,
                                       &s->h);

    return s;
}

void qemu_activate_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    qemu_input_handler_activate(entry->s);
}

void qemu_remove_mouse_event_handler(QEMUPutMouseEntry *entry)
{
    qemu_input_handler_unregister(entry->s);

    g_free(entry);
}

QEMUPutLEDEntry *qemu_add_led_event_handler(QEMUPutLEDEvent *func,
                                            void *opaque)
{
    QEMUPutLEDEntry *s;

    s = g_malloc0(sizeof(QEMUPutLEDEntry));

    s->put_led = func;
    s->opaque = opaque;
    QTAILQ_INSERT_TAIL(&led_handlers, s, next);
    return s;
}

void qemu_remove_led_event_handler(QEMUPutLEDEntry *entry)
{
    if (entry == NULL)
        return;
    QTAILQ_REMOVE(&led_handlers, entry, next);
    g_free(entry);
}

void kbd_put_ledstate(int ledstate)
{
    QEMUPutLEDEntry *cursor;

    QTAILQ_FOREACH(cursor, &led_handlers, next) {
        cursor->put_led(cursor->opaque, ledstate);
    }
}
