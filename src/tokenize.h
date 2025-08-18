#ifndef SKHD_TOKENIZE_H
#define SKHD_TOKENIZE_H

static const char *modifier_flags_str[] =
{
    "alt",   "lalt",    "ralt",
    "shift", "lshift",  "rshift",
    "cmd",   "lcmd",    "rcmd",
    "ctrl",  "lctrl",   "rctrl",
    "fn",    "hyper",   "lhyper",
    "rhyper","meh",
};

static const char *literal_keycode_str[] =
{
    "return",          "tab",             "space",
    "backspace",       "escape",          "delete",
    "home",            "end",             "pageup",
    "pagedown",        "insert",          "left",
    "right",           "up",              "down",
    "f1",              "f2",              "f3",
    "f4",              "f5",              "f6",
    "f7",              "f8",              "f9",
    "f10",             "f11",             "f12",
    "f13",             "f14",             "f15",
    "f16",             "f17",             "f18",
    "f19",             "f20",

    "sound_up",        "sound_down",      "mute",
    "play",            "previous",        "next",
    "rewind",          "fast",            "brightness_up",
    "brightness_down", "illumination_up", "illumination_down"
};

enum token_type
{
    Token_Identifier,
    Token_Activate,

    Token_Command,
    Token_Modifier,
    Token_Literal,
    Token_Key_Hex,
    Token_Key,

    Token_Decl,
    Token_Comma,
    Token_Insert,
    Token_Plus,
    Token_Dash,
    Token_Arrow,
    Token_Capture,
    Token_Unbound,
    Token_Wildcard,
    Token_String,
    Token_Option,

    Token_BeginList,
    Token_EndList,

    Token_Unknown,
    Token_EndOfStream,
};

struct token
{
    enum token_type type;
    char *text;
    unsigned length;

    unsigned line;
    unsigned cursor;
};

struct tokenizer
{
    char *buffer;
    char *at;
    unsigned line;
    unsigned cursor;
};

void tokenizer_init(struct tokenizer *tokenizer, char *buffer);
struct token get_token(struct tokenizer *tokenizer);
struct token peek_token(struct tokenizer tokenizer);
int token_equals(struct token token, const char *match);

#endif
