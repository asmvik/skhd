#ifndef SKHD_HOTKEY_H
#define SKHD_HOTKEY_H

#include <Carbon/Carbon.h>
#include <stdint.h>
#include <stdbool.h>

#define Modifier_Keycode_Alt     0x3A
#define Modifier_Keycode_Shift   0x38
#define Modifier_Keycode_Cmd     0x37
#define Modifier_Keycode_Ctrl    0x3B
#define Modifier_Keycode_Fn      0x3F

enum osx_event_mask
{
    Event_Mask_Alt      = 0x00080000,
    Event_Mask_LAlt     = 0x00000020,
    Event_Mask_RAlt     = 0x00000040,
    Event_Mask_Shift    = 0x00020000,
    Event_Mask_LShift   = 0x00000002,
    Event_Mask_RShift   = 0x00000004,
    Event_Mask_Cmd      = 0x00100000,
    Event_Mask_LCmd     = 0x00000008,
    Event_Mask_RCmd     = 0x00000010,
    Event_Mask_Control  = 0x00040000,
    Event_Mask_LControl = 0x00000001,
    Event_Mask_RControl = 0x00002000,
    Event_Mask_Fn       = kCGEventFlagMaskSecondaryFn,
};

enum hotkey_flag
{
    Hotkey_Flag_Alt         = (1 <<  0),
    Hotkey_Flag_LAlt        = (1 <<  1),
    Hotkey_Flag_RAlt        = (1 <<  2),
    Hotkey_Flag_Shift       = (1 <<  3),
    Hotkey_Flag_LShift      = (1 <<  4),
    Hotkey_Flag_RShift      = (1 <<  5),
    Hotkey_Flag_Cmd         = (1 <<  6),
    Hotkey_Flag_LCmd        = (1 <<  7),
    Hotkey_Flag_RCmd        = (1 <<  8),
    Hotkey_Flag_Control     = (1 <<  9),
    Hotkey_Flag_LControl    = (1 << 10),
    Hotkey_Flag_RControl    = (1 << 11),
    Hotkey_Flag_Fn          = (1 << 12),
    Hotkey_Flag_Passthrough = (1 << 13),
    Hotkey_Flag_Activate    = (1 << 14),
    Hotkey_Flag_NX          = (1 << 15),
    Hotkey_Flag_Hyper       = (Hotkey_Flag_Cmd |
                               Hotkey_Flag_Alt |
                               Hotkey_Flag_Shift |
                               Hotkey_Flag_Control),
    Hotkey_Flag_RHyper      = (Hotkey_Flag_RCmd |
			       Hotkey_Flag_RAlt |
			       Hotkey_Flag_RShift |
			       Hotkey_Flag_RControl),
    Hotkey_Flag_LHyper      = (Hotkey_Flag_LCmd |
			       Hotkey_Flag_LAlt |
			       Hotkey_Flag_LShift |
			       Hotkey_Flag_LControl),
    Hotkey_Flag_Meh         = (Hotkey_Flag_Control |
                               Hotkey_Flag_Shift |
                               Hotkey_Flag_Alt)
};

#include "hashtable.h"

struct carbon_event;

struct mode
{
    char *name;
    char *command;
    bool capture;
    bool initialized;
    struct table hotkey_map;
};

struct hotkey
{
    uint32_t flags;
    uint32_t key;
    char **process_name;
    char **command;
    char *wildcard_command;
    struct mode **mode_list;
};

static inline void
add_flags(struct hotkey *hotkey, uint32_t flag)
{
    hotkey->flags |= flag;
}

static inline bool
has_flags(struct hotkey *hotkey, uint32_t flag)
{
    bool result = hotkey->flags & flag;
    return result;
}

static inline void
clear_flags(struct hotkey *hotkey, uint32_t flag)
{
    hotkey->flags &= ~flag;
}

bool compare_string(char *a, char *b);
unsigned long hash_string(char *key);

bool same_hotkey(struct hotkey *a, struct hotkey *b);
unsigned long hash_hotkey(struct hotkey *a);

struct hotkey create_eventkey(CGEventRef event);
bool intercept_systemkey(CGEventRef event, struct hotkey *eventkey);

bool find_and_exec_hotkey(struct hotkey *k, struct table *t, struct mode **m, struct carbon_event *carbon);
void free_mode_map(struct table *mode_map);
void free_blacklist(struct table *blacklst);

void init_shell(void);

#endif
