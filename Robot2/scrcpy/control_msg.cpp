#include "control_msg.h"

#include <assert.h>
#include <string.h>

#include "config.h"
#include "util/buffer_util.h"
#include "util/log.h"
#include "util/str_util.h"

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
    buf[0] = (uint8_t)this->type;
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
        case CONTROL_MSG_TYPE_SET_CLIPBOARD: {
            buf[1] = !!this->set_clipboard.paste;
            size_t len = write_string(this->set_clipboard.text,
                                      CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH,
                                      &buf[2]);
            return 2 + len;
        }
        case CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE:
            buf[1] = this->set_screen_power_mode.mode;
            return 2;
        case CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
        case CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        case CONTROL_MSG_TYPE_COLLAPSE_NOTIFICATION_PANEL:
        case CONTROL_MSG_TYPE_GET_CLIPBOARD:
        case CONTROL_MSG_TYPE_ROTATE_DEVICE:
            // no additional data
            return 1;
        default:
            LOGW("Unknown message type: %u", (unsigned)this->type);
            return 0;
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