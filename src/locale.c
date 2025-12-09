#include "locale.h"
#include "hashtable.h"
#include "sbuffer.h"
#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/ev_keymap.h>

#define array_count(a) (sizeof((a)) / sizeof(*(a)))

static struct table keymap_table;
static char **keymap_keys;

static char *copy_cfstring(CFStringRef string)
{
    CFIndex num_bytes = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    char *result = malloc(num_bytes + 1);

    // NOTE(asmvik): Boolean: typedef -> unsigned char; false = 0, true != 0
    if (!CFStringGetCString(string, result, num_bytes + 1, kCFStringEncodingUTF8)) {
        free(result);
        result = NULL;
    }

    return result;
}

static int hash_keymap(const char *a)
{
    unsigned long hash = 0, high;
    while (*a) {
        hash = (hash << 4) + *a++;
        high = hash & 0xF0000000;
        if (high) {
            hash ^= (high >> 24);
        }
        hash &= ~high;
    }
    return hash;
}

static bool same_keymap(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void free_keycode_map(void)
{
    for (int i = 0; i < buf_len(keymap_keys); ++i) {
        free(keymap_keys[i]);
    }

    buf_free(keymap_keys);
    keymap_keys = NULL;
}

static uint32_t layout_dependent_keycodes[] =
{
    kVK_ANSI_A,            kVK_ANSI_B,           kVK_ANSI_C,
    kVK_ANSI_D,            kVK_ANSI_E,           kVK_ANSI_F,
    kVK_ANSI_G,            kVK_ANSI_H,           kVK_ANSI_I,
    kVK_ANSI_J,            kVK_ANSI_K,           kVK_ANSI_L,
    kVK_ANSI_M,            kVK_ANSI_N,           kVK_ANSI_O,
    kVK_ANSI_P,            kVK_ANSI_Q,           kVK_ANSI_R,
    kVK_ANSI_S,            kVK_ANSI_T,           kVK_ANSI_U,
    kVK_ANSI_V,            kVK_ANSI_W,           kVK_ANSI_X,
    kVK_ANSI_Y,            kVK_ANSI_Z,           kVK_ANSI_0,
    kVK_ANSI_1,            kVK_ANSI_2,           kVK_ANSI_3,
    kVK_ANSI_4,            kVK_ANSI_5,           kVK_ANSI_6,
    kVK_ANSI_7,            kVK_ANSI_8,           kVK_ANSI_9,
    kVK_ANSI_Grave,        kVK_ANSI_Equal,       kVK_ANSI_Minus,
    kVK_ANSI_RightBracket, kVK_ANSI_LeftBracket, kVK_ANSI_Quote,
    kVK_ANSI_Semicolon,    kVK_ANSI_Backslash,   kVK_ANSI_Comma,
    kVK_ANSI_Slash,        kVK_ANSI_Period,      kVK_ISO_Section
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wint-to-void-pointer-cast"
bool initialize_keycode_map(void)
{
    UniChar chars[255];
    UniCharCount len;
    UInt32 state;

    TISInputSourceRef keyboard = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    CFDataRef uchr = (CFDataRef) TISGetInputSourceProperty(keyboard, kTISPropertyUnicodeKeyLayoutData);
    CFRelease(keyboard);

    UCKeyboardLayout *keyboard_layout = (UCKeyboardLayout *) CFDataGetBytePtr(uchr);
    if (!keyboard_layout) return false;

    free_keycode_map();
    table_free(&keymap_table);
    table_init(&keymap_table,
               61,
               (table_hash_func) hash_keymap,
               (table_compare_func) same_keymap);

    for (int i = 0; i < array_count(layout_dependent_keycodes); ++i) {
        if (UCKeyTranslate(keyboard_layout,
                           layout_dependent_keycodes[i],
                           kUCKeyActionDown,
                           0,
                           LMGetKbdType(),
                           kUCKeyTranslateNoDeadKeysMask,
                           &state,
                           array_count(chars),
                           &len,
                           chars) == noErr && len > 0) {
            CFStringRef key_cfstring = CFStringCreateWithCharacters(NULL, chars, len);
            char *key_cstring = copy_cfstring(key_cfstring);
            CFRelease(key_cfstring);

            if (key_cstring) {
                table_add(&keymap_table, key_cstring, (void *)layout_dependent_keycodes[i]);
                buf_push(keymap_keys, key_cstring);
            }
        }
    }

    return true;
}
#pragma clang diagnostic pop

uint32_t keycode_from_char(char key)
{
    char lookup_key[] = { key, '\0' };
    uint32_t keycode = (uint32_t) (uintptr_t) table_find(&keymap_table, &lookup_key);
    return keycode;
}
