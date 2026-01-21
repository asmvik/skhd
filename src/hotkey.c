#include "hotkey.h"

#define HOTKEY_FOUND           ((1) << 0)
#define MODE_CAPTURE(a)        ((a) << 1)
#define HOTKEY_PASSTHROUGH(a)  ((a) << 2)

#define LRMOD_ALT   0
#define LRMOD_CMD   6
#define LRMOD_CTRL  9
#define LRMOD_SHIFT 3
#define LMOD_OFFS   1
#define RMOD_OFFS   2

static char arg[] = "-c";
static char *shell = NULL;

static uint32_t cgevent_lrmod_flag[] =
{
    Event_Mask_Alt,     Event_Mask_LAlt,     Event_Mask_RAlt,
    Event_Mask_Shift,   Event_Mask_LShift,   Event_Mask_RShift,
    Event_Mask_Cmd,     Event_Mask_LCmd,     Event_Mask_RCmd,
    Event_Mask_Control, Event_Mask_LControl, Event_Mask_RControl,
};

static uint32_t hotkey_lrmod_flag[] =
{
    Hotkey_Flag_Alt,     Hotkey_Flag_LAlt,     Hotkey_Flag_RAlt,
    Hotkey_Flag_Shift,   Hotkey_Flag_LShift,   Hotkey_Flag_RShift,
    Hotkey_Flag_Cmd,     Hotkey_Flag_LCmd,     Hotkey_Flag_RCmd,
    Hotkey_Flag_Control, Hotkey_Flag_LControl, Hotkey_Flag_RControl,
};

static bool
compare_lr_mod(struct hotkey *a, struct hotkey *b, int mod)
{
    bool result = has_flags(a, hotkey_lrmod_flag[mod])
                ? has_flags(b, hotkey_lrmod_flag[mod + LMOD_OFFS]) ||
                  has_flags(b, hotkey_lrmod_flag[mod + RMOD_OFFS]) ||
                  has_flags(b, hotkey_lrmod_flag[mod])
                : has_flags(a, hotkey_lrmod_flag[mod + LMOD_OFFS]) == has_flags(b, hotkey_lrmod_flag[mod + LMOD_OFFS]) &&
                  has_flags(a, hotkey_lrmod_flag[mod + RMOD_OFFS]) == has_flags(b, hotkey_lrmod_flag[mod + RMOD_OFFS]) &&
                  has_flags(a, hotkey_lrmod_flag[mod])             == has_flags(b, hotkey_lrmod_flag[mod]);
    return result;
}

static bool
compare_fn(struct hotkey *a, struct hotkey *b)
{
    return has_flags(a, Hotkey_Flag_Fn) == has_flags(b, Hotkey_Flag_Fn);
}

static bool
compare_nx(struct hotkey *a, struct hotkey *b)
{
    return has_flags(a, Hotkey_Flag_NX) == has_flags(b, Hotkey_Flag_NX);
}

bool same_hotkey(struct hotkey *a, struct hotkey *b)
{
    return compare_lr_mod(a, b, LRMOD_ALT)   &&
           compare_lr_mod(a, b, LRMOD_CMD)   &&
           compare_lr_mod(a, b, LRMOD_CTRL)  &&
           compare_lr_mod(a, b, LRMOD_SHIFT) &&
           compare_fn(a, b) &&
           compare_nx(a, b) &&
           a->key == b->key;
}

unsigned long hash_hotkey(struct hotkey *a)
{
    return a->key;
}

bool compare_string(char *a, char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

unsigned long hash_string(char *key)
{
    unsigned long hash = 0, high;
    while(*key) {
        hash = (hash << 4) + *key++;
        high = hash & 0xF0000000;
        if(high) {
            hash ^= (high >> 24);
        }
        hash &= ~high;
    }
    return hash;
}

static inline void
fork_and_exec(char *command)
{
    int cpid = fork();
    if (cpid == 0) {
        setsid();
        char *exec[] = { shell, arg, command, NULL};
        int status_code = execvp(exec[0], exec);
        exit(status_code);
    }
}

static inline void
passthrough(struct hotkey *hotkey, uint32_t *capture)
{
    *capture |= HOTKEY_PASSTHROUGH((int)has_flags(hotkey, Hotkey_Flag_Passthrough));
}

static inline struct hotkey *
find_hotkey(struct mode *mode, struct hotkey *hotkey, uint32_t *capture)
{
    struct hotkey *result = table_find(&mode->hotkey_map, hotkey);
    if (result) *capture |= HOTKEY_FOUND;
    return result;
}

static inline bool
should_capture_hotkey(uint32_t capture)
{
    if ((capture & HOTKEY_FOUND)) {
        if (!(capture & MODE_CAPTURE(1)) &&
            !(capture & HOTKEY_PASSTHROUGH(1))) {
            return true;
        }

        if (!(capture & HOTKEY_PASSTHROUGH(1)) &&
             (capture & MODE_CAPTURE(1))) {
            return true;
        }

        return false;
    }

    return (capture & MODE_CAPTURE(1));
}

static inline char *
find_process_command_mapping(struct hotkey *hotkey, uint32_t *capture, struct carbon_event *carbon)
{
    char *result = NULL;
    bool found = false;

    for (int i = 0; i < buf_len(hotkey->process_name); ++i) {
        if (same_string(carbon->process_name, hotkey->process_name[i])) {
            result = hotkey->command[i];
            found = true;
            break;
        }
    }

    if (!found) result = hotkey->wildcard_command;
    if (!result) *capture &= ~HOTKEY_FOUND;

    return result;
}

bool find_and_exec_hotkey(struct hotkey *k, struct table *t, struct mode **m, struct carbon_event *carbon)
{
    uint32_t c = MODE_CAPTURE((int)(*m)->capture);
    for (struct hotkey *h = find_hotkey(*m, k, &c); h; passthrough(h, &c), h = 0) {
        char *cmd = h->command[0];
        if (has_flags(h, Hotkey_Flag_Activate)) {
            *m = table_find(t, cmd);
            cmd = (*m)->command;
        } else if (buf_len(h->process_name) > 0) {
            cmd = find_process_command_mapping(h, &c, carbon);
        }
        if (cmd) fork_and_exec(cmd);
    }
    return should_capture_hotkey(c);
}

void free_mode_map(struct table *mode_map)
{
    struct hotkey **freed_pointers = NULL;

    int mode_count;
    void **modes = table_reset(mode_map, &mode_count);
    for (int mode_index = 0; mode_index < mode_count; ++mode_index) {
        struct mode *mode = (struct mode *) modes[mode_index];

        int hk_count;
        void **hotkeys = table_reset(&mode->hotkey_map, &hk_count);
        for (int hk_index = 0; hk_index < hk_count; ++hk_index) {
            struct hotkey *hotkey = (struct hotkey *) hotkeys[hk_index];

            for (int i = 0; i < buf_len(freed_pointers); ++i) {
                if (freed_pointers[i] == hotkey) {
                    goto next;
                }
            }

            buf_push(freed_pointers, hotkey);
            buf_free(hotkey->mode_list);

            for (int i = 0; i < buf_len(hotkey->process_name); ++i) {
                free(hotkey->process_name[i]);
            }
            buf_free(hotkey->process_name);

            for (int i = 0; i < buf_len(hotkey->command); ++i) {
                free(hotkey->command[i]);
            }
            buf_free(hotkey->command);

            free(hotkey);
next:;
        }

        if (hk_count) free(hotkeys);
        if (mode->command) free(mode->command);
        if (mode->name) free(mode->name);
        free(mode);
    }

    if (mode_count) {
        free(modes);
        buf_free(freed_pointers);
    }
}

void free_blacklist(struct table *blacklst)
{
    int count;
    void **items = table_reset(blacklst, &count);
    for (int index = 0; index < count; ++index) {
        char *item = (char *) items[index];
        free(item);
    }
}

static void
cgevent_lrmod_flag_to_hotkey_lrmod_flag(CGEventFlags eventflags, uint32_t *flags, int mod)
{
    enum osx_event_mask mask  = cgevent_lrmod_flag[mod];
    enum osx_event_mask lmask = cgevent_lrmod_flag[mod + LMOD_OFFS];
    enum osx_event_mask rmask = cgevent_lrmod_flag[mod + RMOD_OFFS];

    if ((eventflags & mask) == mask) {
        bool left  = (eventflags & lmask) == lmask;
        bool right = (eventflags & rmask) == rmask;

        if (left)            *flags |= hotkey_lrmod_flag[mod + LMOD_OFFS];
        if (right)           *flags |= hotkey_lrmod_flag[mod + RMOD_OFFS];
        if (!left && !right) *flags |= hotkey_lrmod_flag[mod];
    }
}

static uint32_t
cgevent_flags_to_hotkey_flags(uint32_t eventflags)
{
    uint32_t flags = 0;

    cgevent_lrmod_flag_to_hotkey_lrmod_flag(eventflags, &flags, LRMOD_ALT);
    cgevent_lrmod_flag_to_hotkey_lrmod_flag(eventflags, &flags, LRMOD_CMD);
    cgevent_lrmod_flag_to_hotkey_lrmod_flag(eventflags, &flags, LRMOD_CTRL);
    cgevent_lrmod_flag_to_hotkey_lrmod_flag(eventflags, &flags, LRMOD_SHIFT);

    if ((eventflags & Event_Mask_Fn) == Event_Mask_Fn) {
        flags |= Hotkey_Flag_Fn;
    }

    return flags;
}

struct hotkey create_eventkey(CGEventRef event)
{
    struct hotkey eventkey = {
        .key = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode),
        .flags = cgevent_flags_to_hotkey_flags(CGEventGetFlags(event))
    };
    return eventkey;
}

bool intercept_systemkey(CGEventRef event, struct hotkey *eventkey)
{
    CFDataRef event_data = CGEventCreateData(kCFAllocatorDefault, event);
    const uint8_t *data  = CFDataGetBytePtr(event_data);
    uint8_t key_code  = data[129];
    uint8_t key_state = data[130];
    uint8_t key_stype = data[123];
    CFRelease(event_data);

    bool result = ((key_state == NX_KEYDOWN) &&
                   (key_stype == NX_SUBTYPE_AUX_CONTROL_BUTTONS));

    if (result) {
        eventkey->key = key_code;
        eventkey->flags = cgevent_flags_to_hotkey_flags(CGEventGetFlags(event)) | Hotkey_Flag_NX;
    }

    return result;
}

void init_shell(void)
{
    if (!shell) {
        char *env_shell = getenv("SHELL");
        shell = env_shell ? env_shell : "/bin/bash";
    }
}
