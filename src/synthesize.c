#include <Carbon/Carbon.h>

#include "synthesize.h"
#include "locale.h"
#include "parse.h"
#include "hotkey.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"

static inline void
create_and_post_keyevent(uint16_t key, bool pressed)
{
    CGPostKeyboardEvent((CGCharCode)0, (CGKeyCode)key, pressed);
}

static inline void
create_and_post_mouseclickevent(uint8_t button, bool pressed)
{
    button = pressed ? button : -1;
    CGPostMouseEvent((CGPoint){-1, -1}, false, 32,
                     0 == button,  1 == button,  2 == button,
                     3 == button,  4 == button,  5 == button,
                     6 == button,  7 == button,  8 == button,
                     9 == button,  10 == button, 11 == button,
                     12 == button, 13 == button, 14 == button,
                     15 == button, 16 == button, 17 == button,
                     18 == button, 19 == button, 20 == button,
                     21 == button, 22 == button, 23 == button,
                     24 == button, 25 == button, 26 == button,
                     27 == button, 28 == button, 29 == button,
                     30 == button, 31 == button);
}

static inline void
synthesize_modifiers(struct hotkey *hotkey, bool pressed)
{
    if (has_flags(hotkey, Hotkey_Flag_Alt)) {
        create_and_post_keyevent(Modifier_Keycode_Alt, pressed);
    }

    if (has_flags(hotkey, Hotkey_Flag_Shift)) {
        create_and_post_keyevent(Modifier_Keycode_Shift, pressed);
    }

    if (has_flags(hotkey, Hotkey_Flag_Cmd)) {
        create_and_post_keyevent(Modifier_Keycode_Cmd, pressed);
    }

    if (has_flags(hotkey, Hotkey_Flag_Control)) {
        create_and_post_keyevent(Modifier_Keycode_Ctrl, pressed);
    }

    if (has_flags(hotkey, Hotkey_Flag_Fn)) {
        create_and_post_keyevent(Modifier_Keycode_Fn, pressed);
    }
}

void synthesize_key(char *key_string)
{
    if (!initialize_keycode_map()) return;

    struct parser parser;
    parser_init_text(&parser, key_string);

    close(1);
    close(2);

    struct hotkey *hotkey = parse_keypress(&parser);
    if (!hotkey) return;

    CGSetLocalEventsSuppressionInterval(0.0f);
    CGEnableEventStateCombining(false);

    synthesize_modifiers(hotkey, true);
    if (hotkey->button == (uint8_t)-1) {
        create_and_post_keyevent(hotkey->key, true);
        create_and_post_keyevent(hotkey->key, false);
    } else {
        create_and_post_mouseclickevent(hotkey->button, true);
        create_and_post_mouseclickevent(hotkey->button, false);
    }
    synthesize_modifiers(hotkey, false);
}

void synthesize_text(char *text)
{
    CFStringRef text_ref = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    CFIndex text_length = CFStringGetLength(text_ref);

    CGEventRef de = CGEventCreateKeyboardEvent(NULL, 0, true);
    CGEventRef ue = CGEventCreateKeyboardEvent(NULL, 0, false);

    CGEventSetFlags(de, 0);
    CGEventSetFlags(ue, 0);

    UniChar c;
    for (CFIndex i = 0; i < text_length; ++i)
    {
        c = CFStringGetCharacterAtIndex(text_ref, i);
        CGEventKeyboardSetUnicodeString(de, 1, &c);
        CGEventPost(kCGAnnotatedSessionEventTap, de);
        usleep(1000);
        CGEventKeyboardSetUnicodeString(ue, 1, &c);
        CGEventPost(kCGAnnotatedSessionEventTap, ue);
    }

    CFRelease(ue);
    CFRelease(de);
    CFRelease(text_ref);
}

#pragma clang diagnostic pop
