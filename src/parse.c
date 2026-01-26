#include "parse.h"
#include "tokenize.h"
#include "locale.h"
#include "hotkey.h"
#include "hashtable.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

static struct mode *
find_or_init_default_mode(struct parser *parser)
{
    struct mode *default_mode;

    if ((default_mode = table_find(parser->mode_map, "default"))) {
        return default_mode;
    }

    default_mode = malloc(sizeof(struct mode));
    default_mode->name = copy_string("default");
    default_mode->initialized = false;

    table_init(&default_mode->hotkey_map, 131,
               (table_hash_func) hash_hotkey,
               (table_compare_func) same_hotkey);

    default_mode->capture = false;
    default_mode->command = NULL;
    table_add(parser->mode_map, default_mode->name, default_mode);

    return default_mode;
}

static char *
read_file(const char *file)
{
    unsigned length;
    char *buffer = NULL;
    FILE *handle = fopen(file, "r");

    if (handle) {
        fseek(handle, 0, SEEK_END);
        length = ftell(handle);
        fseek(handle, 0, SEEK_SET);
        buffer = malloc(length + 1);
        fread(buffer, length, 1, handle);
        buffer[length] = '\0';
        fclose(handle);
    }

    return buffer;
}

static char *
copy_string_count(char *s, int length)
{
    char *result = malloc(length + 1);
    memcpy(result, s, length);
    result[length] = '\0';
    return result;
}

static uint32_t
keycode_from_hex(char *hex)
{
    uint32_t result;
    sscanf(hex, "%x", &result);
    return result;
}

static void
parse_command(struct parser *parser, struct hotkey *hotkey)
{
    struct token command = parser_previous(parser);
    char *result = copy_string_count(command.text, command.length);
    debug("\tcmd: '%s'\n", result);
    buf_push(hotkey->command, result);
}

static void
parse_process_command_list(struct parser *parser, struct hotkey *hotkey)
{
    if (parser_match(parser, Token_String)) {
        struct token name_token = parser_previous(parser);
        char *name = copy_string_count(name_token.text, name_token.length);
        for (char *s = name; *s; ++s) *s = tolower(*s);
        buf_push(hotkey->process_name, name);
        if (parser_match(parser, Token_Command)) {
            parse_command(parser, hotkey);
            parse_process_command_list(parser, hotkey);
        } else if (parser_match(parser, Token_Unbound)) {
            buf_push(hotkey->command, NULL);
            parse_process_command_list(parser, hotkey);
        } else {
            parser_report_error(parser, parser_peek(parser), "expected '~' or ':' followed by command\n");
        }
    } else if (parser_match(parser, Token_Wildcard)) {
        if (parser_match(parser, Token_Command)) {
            struct token command = parser_previous(parser);
            char *result = copy_string_count(command.text, command.length);
            debug("\tcmd: '%s'\n", result);
            hotkey->wildcard_command = result;
            parse_process_command_list(parser, hotkey);
        } else if (parser_match(parser, Token_Unbound)) {
            hotkey->wildcard_command = NULL;
            parse_process_command_list(parser, hotkey);
        } else {
            parser_report_error(parser, parser_peek(parser), "expected '~' or ':' followed by command\n");
        }
    } else if (parser_match(parser, Token_EndList)) {
        if (!buf_len(hotkey->process_name)) {
            parser_report_error(parser, parser_previous(parser), "list must contain at least one value\n");
        }
    } else {
        parser_report_error(parser, parser_peek(parser), "expected process command mapping or ']'\n");
    }
}

static void
parse_activate(struct parser *parser, struct hotkey *hotkey)
{
    parse_command(parser, hotkey);
    hotkey->flags |= Hotkey_Flag_Activate;

    if (!table_find(parser->mode_map, hotkey->command[0])) {
        parser_report_error(parser, parser_previous(parser), "undeclared identifier\n");
    }
}

static uint32_t
parse_key_hex(struct parser *parser)
{
    struct token key = parser_previous(parser);
    char *hex = copy_string_count(key.text, key.length);
    uint32_t keycode = keycode_from_hex(hex);
    free(hex);
    debug("\tkey: '%.*s' (0x%02x)\n", key.length, key.text, keycode);
    return keycode;
}

static uint32_t
parse_key(struct parser *parser)
{
    uint32_t keycode;
    struct token key = parser_previous(parser);
    keycode = keycode_from_char(*key.text);
    debug("\tkey: '%c' (0x%02x)\n", *key.text, keycode);
    return keycode;
}

#define KEY_HAS_IMPLICIT_FN_MOD  4
#define KEY_HAS_IMPLICIT_NX_MOD  35
static uint32_t literal_keycode_value[] =
{
    kVK_Return,     kVK_Tab,           kVK_Space,
    kVK_Delete,     kVK_Escape,        kVK_ForwardDelete,
    kVK_Home,       kVK_End,           kVK_PageUp,
    kVK_PageDown,   kVK_Help,          kVK_LeftArrow,
    kVK_RightArrow, kVK_UpArrow,       kVK_DownArrow,
    kVK_F1,         kVK_F2,            kVK_F3,
    kVK_F4,         kVK_F5,            kVK_F6,
    kVK_F7,         kVK_F8,            kVK_F9,
    kVK_F10,        kVK_F11,           kVK_F12,
    kVK_F13,        kVK_F14,           kVK_F15,
    kVK_F16,        kVK_F17,           kVK_F18,
    kVK_F19,        kVK_F20,

    NX_KEYTYPE_SOUND_UP,        NX_KEYTYPE_SOUND_DOWN,      NX_KEYTYPE_MUTE,
    NX_KEYTYPE_PLAY,            NX_KEYTYPE_PREVIOUS,        NX_KEYTYPE_NEXT,
    NX_KEYTYPE_REWIND,          NX_KEYTYPE_FAST,            NX_KEYTYPE_BRIGHTNESS_UP,
    NX_KEYTYPE_BRIGHTNESS_DOWN, NX_KEYTYPE_ILLUMINATION_UP, NX_KEYTYPE_ILLUMINATION_DOWN
};

static inline void
handle_implicit_literal_flags(struct hotkey *hotkey, int literal_index)
{
    if ((literal_index > KEY_HAS_IMPLICIT_FN_MOD) &&
        (literal_index < KEY_HAS_IMPLICIT_NX_MOD)) {
        hotkey->flags |= Hotkey_Flag_Fn;
    } else if (literal_index >= KEY_HAS_IMPLICIT_NX_MOD) {
        hotkey->flags |= Hotkey_Flag_NX;
    }
}

static void
parse_key_literal(struct parser *parser, struct hotkey *hotkey)
{
    struct token key = parser_previous(parser);
    for (int i = 0; i < array_count(literal_keycode_str); ++i) {
        if (token_equals(key, literal_keycode_str[i])) {
            handle_implicit_literal_flags(hotkey, i);
            hotkey->key = literal_keycode_value[i];
            debug("\tkey: '%.*s' (0x%02x)\n", key.length, key.text, hotkey->key);
            break;
        }
    }
}

static enum hotkey_flag modifier_flags_value[] =
{
    Hotkey_Flag_Alt,        Hotkey_Flag_LAlt,       Hotkey_Flag_RAlt,
    Hotkey_Flag_Shift,      Hotkey_Flag_LShift,     Hotkey_Flag_RShift,
    Hotkey_Flag_Cmd,        Hotkey_Flag_LCmd,       Hotkey_Flag_RCmd,
    Hotkey_Flag_Control,    Hotkey_Flag_LControl,   Hotkey_Flag_RControl,
    Hotkey_Flag_Fn,         Hotkey_Flag_Hyper,      Hotkey_Flag_LHyper,
    Hotkey_Flag_RHyper,     Hotkey_Flag_Meh,
};

static uint32_t
parse_modifier(struct parser *parser)
{
    struct token modifier = parser_previous(parser);
    uint32_t flags = 0;

    for (int i = 0; i < array_count(modifier_flags_str); ++i) {
        if (token_equals(modifier, modifier_flags_str[i])) {
            flags |= modifier_flags_value[i];
            debug("\tmod: '%s'\n", modifier_flags_str[i]);
            break;
        }
    }

    if (parser_match(parser, Token_Plus)) {
        if (parser_match(parser, Token_Modifier)) {
            flags |= parse_modifier(parser);
        } else {
            parser_report_error(parser, parser_peek(parser), "expected modifier\n");
        }
    }

    return flags;
}

static void
parse_mode(struct parser *parser, struct hotkey *hotkey)
{
    struct token identifier = parser_previous(parser);

    char *name = copy_string_count(identifier.text, identifier.length);
    struct mode *mode = table_find(parser->mode_map, name);
    free(name);

    if (!mode && token_equals(identifier, "default")) {
        mode = find_or_init_default_mode(parser);
    } else if (!mode) {
        parser_report_error(parser, identifier, "undeclared identifier\n");
        return;
    }

    buf_push(hotkey->mode_list, mode);
    debug("\tmode: '%s'\n", mode->name);

    if (parser_match(parser, Token_Comma)) {
        if (parser_match(parser, Token_Identifier)) {
            parse_mode(parser, hotkey);
        } else {
            parser_report_error(parser, parser_peek(parser), "expected identifier\n");
        }
    }
}

static struct hotkey *
parse_hotkey(struct parser *parser)
{
    struct hotkey *hotkey = malloc(sizeof(struct hotkey));
    memset(hotkey, 0, sizeof(struct hotkey));
    bool found_modifier;

    debug("hotkey :: #%d {\n", parser->current_token.line);

    if (parser_match(parser, Token_Identifier)) {
        parse_mode(parser, hotkey);
        if (parser->error) {
            goto err;
        }
    }

    if (buf_len(hotkey->mode_list) > 0) {
        if (!parser_match(parser, Token_Insert)) {
            parser_report_error(parser, parser_peek(parser), "expected '<'\n");
            goto err;
        }
    } else {
        buf_push(hotkey->mode_list, find_or_init_default_mode(parser));
    }

    if ((found_modifier = parser_match(parser, Token_Modifier))) {
        hotkey->flags = parse_modifier(parser);
        if (parser->error) {
            goto err;
        }
    }

    if (found_modifier) {
        if (!parser_match(parser, Token_Dash)) {
            parser_report_error(parser, parser_peek(parser), "expected '-'\n");
            goto err;
        }
    }

    if (parser_match(parser, Token_Key)) {
        hotkey->key = parse_key(parser);
    } else if (parser_match(parser, Token_Key_Hex)) {
        hotkey->key = parse_key_hex(parser);
    } else if (parser_match(parser, Token_Literal)) {
        parse_key_literal(parser, hotkey);
    } else {
        parser_report_error(parser, parser_peek(parser), "expected key-literal\n");
        goto err;
    }

    if (parser_match(parser, Token_Arrow)) {
        hotkey->flags |= Hotkey_Flag_Passthrough;
    }

    if (parser_match(parser, Token_Command)) {
        parse_command(parser, hotkey);
    } else if (parser_match(parser, Token_BeginList)) {
        parse_process_command_list(parser, hotkey);
        if (parser->error) {
            goto err;
        }
    } else if (parser_match(parser, Token_Activate)) {
        parse_activate(parser, hotkey);
        if (parser->error) {
            goto err;
        }
    } else {
        parser_report_error(parser, parser_peek(parser), "expected ':' followed by command or ';' followed by mode\n");
        goto err;
    }

    debug("}\n");
    return hotkey;

err:
    free(hotkey);
    return NULL;
}

static struct mode *
parse_mode_decl(struct parser *parser)
{
    struct mode *mode = malloc(sizeof(struct mode));
    struct token identifier = parser_previous(parser);

    mode->name = copy_string_count(identifier.text, identifier.length);
    mode->initialized = true;

    table_init(&mode->hotkey_map, 131,
               (table_hash_func) hash_hotkey,
               (table_compare_func) same_hotkey);

    if (parser_match(parser, Token_Capture)) {
        mode->capture = true;
    } else {
        mode->capture = false;
    }

    if (parser_match(parser, Token_Command)) {
        mode->command = copy_string_count(parser->previous_token.text, parser->previous_token.length);
    } else {
        mode->command = NULL;
    }

    return mode;
}

void parse_declaration(struct parser *parser)
{
    parser_match(parser, Token_Decl);
    if (parser_match(parser, Token_Identifier)) {
        struct token identifier = parser_previous(parser);
        struct mode *mode = parse_mode_decl(parser);

        struct mode *existing_mode = table_find(parser->mode_map, mode->name);
        if  (existing_mode) {
            if (same_string(existing_mode->name, "default") && !existing_mode->initialized) {
                existing_mode->initialized = true;
                existing_mode->capture = mode->capture;
                existing_mode->command = mode->command;
            } else {
                parser_report_error(parser, identifier, "duplicate declaration '%s'\n", mode->name);
                if (mode->command) free(mode->command);
            }

            free(mode->name);
            free(mode);
        } else {
            table_add(parser->mode_map, mode->name, mode);
        }
    } else {
        parser_report_error(parser, parser_peek(parser), "expected identifier\n");
    }
}

void parse_option_blacklist(struct parser *parser)
{
    if (parser_match(parser, Token_String)) {
        struct token name_token = parser_previous(parser);
        char *name = copy_string_count(name_token.text, name_token.length);
        for (char *s = name; *s; ++s) *s = tolower(*s);
        debug("\t%s\n", name);
        table_add(parser->blacklst, name, name);
        parse_option_blacklist(parser);
    } else if (parser_match(parser, Token_EndList)) {
        if (parser->blacklst->count == 0) {
            parser_report_error(parser, parser_previous(parser), "list must contain at least one value\n");
        }
    } else {
        parser_report_error(parser, parser_peek(parser), "expected process name or ']'\n");
    }
}

void parse_option_load(struct parser *parser, struct token option)
{
    struct token filename_token = parser_previous(parser);
    char *filename = copy_string_count(filename_token.text, filename_token.length);
    debug("\t%s\n", filename);

    if (*filename != '/') {
        char *directory = file_directory(parser->file);

        size_t directory_length = strlen(directory);
        size_t filename_length  = strlen(filename);
        size_t total_length     = directory_length + filename_length + 2;

        char *absolutepath = malloc(total_length * sizeof(char));
        snprintf(absolutepath, total_length, "%s/%s", directory, filename);
        free(filename);

        filename = absolutepath;
    }

    buf_push(parser->load_directives, ((struct load_directive) {
        .file  = filename,
        .option = option
    }));
}

void parse_option(struct parser *parser)
{
    parser_match(parser, Token_Option);
    struct token option = parser_previous(parser);
    if (token_equals(option, "blacklist")) {
        if (parser_match(parser, Token_BeginList)) {
            debug("blacklist :: #%d {\n", option.line);
            parse_option_blacklist(parser);
            debug("}\n");
        } else {
            parser_report_error(parser, option, "expected '[' followed by list of process names\n");
        }
    } else if (token_equals(option, "load")) {
        if (parser_match(parser, Token_String)) {
            debug("load :: #%d {\n", option.line);
            parse_option_load(parser, option);
            debug("}\n");
        } else {
            parser_report_error(parser, option, "expected filename\n");
        }
    } else {
        parser_report_error(parser, option, "invalid option specified\n");
    }
}

bool parse_config(struct parser *parser)
{
    struct mode *mode;
    struct hotkey *hotkey;

    while (!parser_eof(parser)) {
        if (parser->error) break;

        if ((parser_check(parser, Token_Identifier)) ||
            (parser_check(parser, Token_Modifier)) ||
            (parser_check(parser, Token_Literal)) ||
            (parser_check(parser, Token_Key_Hex)) ||
            (parser_check(parser, Token_Key))) {
            if ((hotkey = parse_hotkey(parser))) {
                for (int i = 0; i < buf_len(hotkey->mode_list); ++i) {
                    mode = hotkey->mode_list[i];
                    table_add(&mode->hotkey_map, hotkey, hotkey);
                }
            }
        } else if (parser_check(parser, Token_Decl)) {
            parse_declaration(parser);
        } else if (parser_check(parser, Token_Option)) {
            parse_option(parser);
        } else {
            parser_report_error(parser, parser_peek(parser), "expected decl, modifier or key-literal\n");
        }
    }

    if (parser->error) {
        free_mode_map(parser->mode_map);
        free_blacklist(parser->blacklst);
        return false;
    }

    return true;
}

struct hotkey *
parse_keypress(struct parser *parser)
{
    if ((parser_check(parser, Token_Modifier)) ||
        (parser_check(parser, Token_Literal)) ||
        (parser_check(parser, Token_Key_Hex)) ||
        (parser_check(parser, Token_Key))) {
        struct hotkey *hotkey = malloc(sizeof(struct hotkey));
        memset(hotkey, 0, sizeof(struct hotkey));
        bool found_modifier;

        if ((found_modifier = parser_match(parser, Token_Modifier))) {
            hotkey->flags = parse_modifier(parser);
            if (parser->error) {
                goto err;
            }
        }

        if (found_modifier) {
            if (!parser_match(parser, Token_Dash)) {
                goto err;
            }
        }

        if (parser_match(parser, Token_Key)) {
            hotkey->key = parse_key(parser);
        } else if (parser_match(parser, Token_Key_Hex)) {
            hotkey->key = parse_key_hex(parser);
        } else if (parser_match(parser, Token_Literal)) {
            parse_key_literal(parser, hotkey);
        } else {
            goto err;
        }

        return hotkey;

    err:
        free(hotkey);
        return NULL;
    }

    return NULL;
}

struct token
parser_peek(struct parser *parser)
{
    return parser->current_token;
}

struct token
parser_previous(struct parser *parser)
{
    return parser->previous_token;
}

bool parser_eof(struct parser *parser)
{
    struct token token = parser_peek(parser);
    return token.type == Token_EndOfStream;
}

struct token
parser_advance(struct parser *parser)
{
    if (!parser_eof(parser)) {
        parser->previous_token = parser->current_token;
        parser->current_token = get_token(&parser->tokenizer);
    }
    return parser_previous(parser);
}

bool parser_check(struct parser *parser, enum token_type type)
{
    if (parser_eof(parser)) return false;
    struct token token = parser_peek(parser);
    return token.type == type;
}

bool parser_match(struct parser *parser, enum token_type type)
{
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

void parser_report_error(struct parser *parser, struct token token, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "#%d:%d ", token.line, token.cursor);
    vfprintf(stderr, format, args);
    va_end(args);
    parser->error = true;
}

void parser_do_directives(struct parser *parser, struct hotloader *hotloader, bool thwart_hotloader)
{
    bool error = false;

    for (int i = 0; i < buf_len(parser->load_directives); ++i) {
        struct load_directive load = parser->load_directives[i];

        struct parser directive_parser;
        if (parser_init(&directive_parser, parser->mode_map, parser->blacklst, load.file)) {
            if (!thwart_hotloader) {
                hotloader_add_file(hotloader, load.file);
            }

            if (parse_config(&directive_parser)) {
                parser_do_directives(&directive_parser, hotloader, thwart_hotloader);
            } else {
                error = true;
            }

            parser_destroy(&directive_parser);
        } else {
            warn("skhd: could not open file '%s' from load directive #%d:%d\n", load.file, load.option.line, load.option.cursor);
        }

        free(load.file);
    }
    buf_free(parser->load_directives);

    if (error) {
        free_mode_map(parser->mode_map);
        free_blacklist(parser->blacklst);
    }
}

bool parser_init(struct parser *parser, struct table *mode_map, struct table *blacklst, char *file)
{
    memset(parser, 0, sizeof(struct parser));
    char *buffer = read_file(file);
    if (buffer) {
        parser->file     = file;
        parser->mode_map = mode_map;
        parser->blacklst = blacklst;
        tokenizer_init(&parser->tokenizer, buffer);
        parser_advance(parser);
        return true;
    }
    return false;
}

bool parser_init_text(struct parser *parser, char *text)
{
    memset(parser, 0, sizeof(struct parser));
    tokenizer_init(&parser->tokenizer, text);
    parser_advance(parser);
    return true;
}

void parser_destroy(struct parser *parser)
{
    free(parser->tokenizer.buffer);
}
