#include "control_msg.h"

#include <assert.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"
#include "compat.h"
#include "util/buffer_util.h"
#include "util/log.h"
#include "util/str_util.h"

/**
 * Map an enum value to a string based on an array, without crashing on an
 * out-of-bounds index.
 */
#define ENUM_TO_LABEL(labels, value) \
    ((size_t) (value) < ARRAY_LEN(labels) ? labels[value] : "???")

#define KEYEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_keyevent_action_labels, value)

#define MOTIONEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_motionevent_action_labels, value)

#define SCREEN_POWER_MODE_LABEL(value) \
    ENUM_TO_LABEL(screen_power_mode_labels, value)

static const char* const android_keyevent_action_labels[] = {
    "down",
    "up",
    "multi",
};

static const char* const android_motionevent_action_labels[] = {
    "down",
    "up",
    "move",
    "cancel",
    "outside",
    "ponter-down",
    "pointer-up",
    "hover-move",
    "scroll",
    "hover-enter"
    "hover-exit",
    "btn-press",
    "btn-release",
};

static const char* const screen_power_mode_labels[] = {
    "off",
    "doze",
    "normal",
    "doze-suspend",
    "suspend",
};

static const char* const copy_key_labels[] = {
    "none",
    "copy",
    "cut",
};

static void
write_position(uint8_t *buf, const struct position *position) {
    buffer_write32be(&buf[0], position->point.x);
    buffer_write32be(&buf[4], position->point.y);
    buffer_write16be(&buf[8], position->screen_size.width);
    buffer_write16be(&buf[10], position->screen_size.height);
}

// write length (2 bytes) + string (non nul-terminated)
static size_t
write_string(const char *utf8, size_t max_len, unsigned char *buf) {
    size_t len = utf8_truncation_index(utf8, max_len);
    buffer_write32be(buf, len);
    memcpy(&buf[4], utf8, len);
    return 4 + len;
}

static uint16_t
to_fixed_point_16(float f) {
    assert(f >= 0.0f && f <= 1.0f);
    uint32_t u = f * 0x1p16f; // 2^16
    if (u >= 0xffff) {
        u = 0xffff;
    }
    return (uint16_t) u;
}

size_t ControlMsg::Serialize(unsigned char* buf) const
{
    uint16_t pressure;
    buf[0] = this->type;
    switch (this->type) {
    case CONTROL_MSG_TYPE_INJECT_KEYCODE:
        buf[1] = this->inject_keycode.action;
        buffer_write32be(&buf[2], this->inject_keycode.keycode);
        buffer_write32be(&buf[6], this->inject_keycode.repeat);
        buffer_write32be(&buf[10], this->inject_keycode.metastate);
        return 14;
    case CONTROL_MSG_TYPE_INJECT_TEXT: {
        size_t len =
            write_string(this->inject_text.text,
                CONTROL_MSG_INJECT_TEXT_MAX_LENGTH, &buf[1]);
        return 1 + len;
    }
    case CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT:
        buf[1] = this->inject_touch_event.action;
        buffer_write64be(&buf[2], this->inject_touch_event.pointer_id);
        write_position(&buf[10], &this->inject_touch_event.position);
        pressure = to_fixed_point_16(this->inject_touch_event.pressure);
        buffer_write16be(&buf[22], pressure);
        buffer_write32be(&buf[24], this->inject_touch_event.buttons);
        return 28;
    case CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
        write_position(&buf[1], &this->inject_scroll_event.position);
        buffer_write32be(&buf[13],
            (uint32_t)this->inject_scroll_event.hscroll);
        buffer_write32be(&buf[17],
            (uint32_t)this->inject_scroll_event.vscroll);
        return 21;
    case CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
        buf[1] = this->inject_keycode.action;
        return 2;
    case CONTROL_MSG_TYPE_GET_CLIPBOARD:
        buf[1] = this->get_clipboard.copy_key;
        return 2;
    case CONTROL_MSG_TYPE_SET_CLIPBOARD: {
        buffer_write64be(&buf[1], this->set_clipboard.sequence);
        buf[9] = !!this->set_clipboard.paste;
        size_t len = write_string(this->set_clipboard.text,
            CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH,
            &buf[10]);
        return 10 + len;
    }
    case CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
        buf[1] = this->set_screen_power_mode.mode;
        return 2;
    case CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
    case CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
    case CONTROL_MSG_TYPE_COLLAPSE_PANELS:
    case CONTROL_MSG_TYPE_ROTATE_DEVICE:
        // no additional data
        return 1;
    default:
        LOGW("Unknown message type: %u", (unsigned)this->type);
        return 0;
    }
}


void ControlMsg::Log() const {
#define LOG_CMSG(fmt, ...) LOGV("input: " fmt, ## __VA_ARGS__)
    switch (this->type) {
    case CONTROL_MSG_TYPE_INJECT_KEYCODE:
        LOG_CMSG("key %-4s code=%d repeat=%" PRIu32 " meta=%06lx",
            KEYEVENT_ACTION_LABEL(this->inject_keycode.action),
            (int)this->inject_keycode.keycode,
            this->inject_keycode.repeat,
            (long)this->inject_keycode.metastate);
        break;
    case CONTROL_MSG_TYPE_INJECT_TEXT:
        LOG_CMSG("text \"%s\"", this->inject_text.text);
        break;
    case CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
        int action = this->inject_touch_event.action
            & AMOTION_EVENT_ACTION_MASK;
        uint64_t id = this->inject_touch_event.pointer_id;
        if (id == POINTER_ID_MOUSE || id == POINTER_ID_VIRTUAL_FINGER) {
            // string pointer id
            LOG_CMSG("touch [id=%s] %-4s position=%" PRIi32 ",%" PRIi32
                " pressure=%g buttons=%06lx",
                id == POINTER_ID_MOUSE ? "mouse" : "vfinger",
                MOTIONEVENT_ACTION_LABEL(action),
                this->inject_touch_event.position.point.x,
                this->inject_touch_event.position.point.y,
                this->inject_touch_event.pressure,
                (long)this->inject_touch_event.buttons);
        }
        else {
            // numeric pointer id
            LOG_CMSG("touch [id=%" PRIu64_ "] %-4s position=%" PRIi32 ",%"
                PRIi32 " pressure=%g buttons=%06lx",
                id,
                MOTIONEVENT_ACTION_LABEL(action),
                this->inject_touch_event.position.point.x,
                this->inject_touch_event.position.point.y,
                this->inject_touch_event.pressure,
                (long)this->inject_touch_event.buttons);
        }
        break;
    }
    case CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
        LOG_CMSG("scroll position=%" PRIi32 ",%" PRIi32 " hscroll=%" PRIi32
            " vscroll=%" PRIi32,
            this->inject_scroll_event.position.point.x,
            this->inject_scroll_event.position.point.y,
            this->inject_scroll_event.hscroll,
            this->inject_scroll_event.vscroll);
        break;
    case CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
        LOG_CMSG("back-or-screen-on %s",
            KEYEVENT_ACTION_LABEL(this->inject_keycode.action));
        break;
    case CONTROL_MSG_TYPE_GET_CLIPBOARD:
        LOG_CMSG("get clipboard copy_key=%s",
            copy_key_labels[this->get_clipboard.copy_key]);
        break;
    case CONTROL_MSG_TYPE_SET_CLIPBOARD:
        LOG_CMSG("clipboard %" PRIu64_ " %s \"%s\"",
            this->set_clipboard.sequence,
            this->set_clipboard.paste ? "paste" : "nopaste",
            this->set_clipboard.text);
        break;
    case CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
        LOG_CMSG("power mode %s",
            SCREEN_POWER_MODE_LABEL(this->set_screen_power_mode.mode));
        break;
    case CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        LOG_CMSG("expand notification panel %i",0);
        break;
    case CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
        LOG_CMSG("expand settings panel %i", 0);
        break;
    case CONTROL_MSG_TYPE_COLLAPSE_PANELS:
        LOG_CMSG("collapse panels %i", 0);
        break;
    case CONTROL_MSG_TYPE_ROTATE_DEVICE:
        LOG_CMSG("rotate device %i", 0);
        break;
    default:
        LOG_CMSG("unknown type: %u", (unsigned)this->type);
        break;
    }
}

ControlMsg::~ControlMsg()
{
    switch (this->type) {
        case CONTROL_MSG_TYPE_INJECT_TEXT:
            SDL_free(this->inject_text.text);
            break;
        case CONTROL_MSG_TYPE_SET_CLIPBOARD:
            SDL_free(this->set_clipboard.text);
            break;
        default:
            // do nothing
            break;
    }
}