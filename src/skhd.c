#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <objc/objc-runtime.h>

#include "timing.h"
#include "log.h"
#define HASHTABLE_IMPLEMENTATION
#include "hashtable.h"
#include "sbuffer.h"
#include "hotload.h"
#include "event_tap.h"
#include "locale.h"
#include "carbon.h"
#include "tokenize.h"
#include "parse.h"
#include "hotkey.h"
#include "synthesize.h"
#include "service.h"

#include "hotload.c"
#include "event_tap.c"
#include "locale.c"
#include "carbon.c"
#include "tokenize.c"
#include "parse.c"
#include "hotkey.c"
#include "synthesize.c"
#include "notify.c"

extern void NSApplicationLoad(void);
extern CFDictionaryRef CGSCopyCurrentSessionDictionary(void);
extern bool CGSIsSecureEventInputSet(void);
#define secure_keyboard_entry_enabled CGSIsSecureEventInputSet

#define GLOBAL_CONNECTION_CALLBACK(name) void name(uint32_t type, void *data, size_t data_length, void *context)
typedef GLOBAL_CONNECTION_CALLBACK(global_connection_callback);
extern CGError CGSRegisterNotifyProc(void *handler, uint32_t type, void *context);

#define SKHD_CONFIG_FILE ".skhdrc"
#define SKHD_PIDFILE_FMT "/tmp/skhd_%s.pid"

#define VERSION_OPT_LONG        "--version"
#define VERSION_OPT_SHRT        "-v"

#define SERVICE_INSTALL_OPT     "--install-service"
#define SERVICE_UNINSTALL_OPT   "--uninstall-service"
#define SERVICE_START_OPT       "--start-service"
#define SERVICE_RESTART_OPT     "--restart-service"
#define SERVICE_STOP_OPT        "--stop-service"

#define MAJOR  0
#define MINOR  3
#define PATCH  9

static struct carbon_event carbon;
static struct event_tap event_tap;
static struct hotloader hotloader;
static struct mode *current_mode;
static struct table mode_map;
static struct table blacklst;
static bool thwart_hotloader;
static char config_file[4096];

static HOTLOADER_CALLBACK(config_handler);

static void
parse_config_helper(char *absolutepath)
{
    struct parser parser;
    if (parser_init(&parser, &mode_map, &blacklst, absolutepath)) {
        if (!thwart_hotloader) {
            hotloader_end(&hotloader);
            hotloader_add_file(&hotloader, absolutepath);
        }

        if (parse_config(&parser)) {
            parser_do_directives(&parser, &hotloader, thwart_hotloader);
        }
        parser_destroy(&parser);

        if (!thwart_hotloader) {
            if (hotloader_begin(&hotloader, config_handler)) {
                debug("skhd: watching files for changes:\n", absolutepath);
                for (int i = 0; i < hotloader.watch_count; ++i) {
                    debug("\t%s\n", hotloader.watch_list[i].file_info.absolutepath);
                }
            } else {
                warn("skhd: could not start watcher.. hotloading is not enabled\n");
            }
        }
    } else {
        warn("skhd: could not open file '%s'\n", absolutepath);
    }

    current_mode = table_find(&mode_map, "default");
}

static HOTLOADER_CALLBACK(config_handler)
{
    BEGIN_TIMED_BLOCK("hotload_config");
    debug("skhd: config-file has been modified.. reloading config\n");
    free_mode_map(&mode_map);
    free_blacklist(&blacklst);
    parse_config_helper(config_file);
    END_TIMED_BLOCK();
}

static CF_NOTIFICATION_CALLBACK(keymap_handler)
{
    BEGIN_TIMED_BLOCK("keymap_changed");
    if (initialize_keycode_map()) {
        debug("skhd: input source changed.. reloading config\n");
        free_mode_map(&mode_map);
        free_blacklist(&blacklst);
        parse_config_helper(config_file);
    }
    END_TIMED_BLOCK();
}

static EVENT_TAP_CALLBACK(key_observer_handler)
{
    switch (type) {
    case kCGEventTapDisabledByTimeout:
    case kCGEventTapDisabledByUserInput: {
        debug("skhd: restarting event-tap\n");
        struct event_tap *event_tap = (struct event_tap *) reference;
        CGEventTapEnable(event_tap->handle, 1);
    } break;
    case kCGEventKeyDown:
    case kCGEventFlagsChanged: {
        uint32_t flags = CGEventGetFlags(event);
        uint32_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

        if (keycode == kVK_ANSI_C && flags & 0x40000) {
            exit(0);
        }

        printf("\rkeycode: 0x%.2X\tflags: ", keycode);
        for (int i = 31; i >= 0; --i) {
            printf("%c", (flags & (1 << i)) ? '1' : '0');
        }
        fflush(stdout);

        return NULL;
    } break;
    }
    return event;
}

static EVENT_TAP_CALLBACK(key_handler)
{
    switch (type) {
    case kCGEventTapDisabledByTimeout:
    case kCGEventTapDisabledByUserInput: {
        debug("skhd: restarting event-tap\n");
        struct event_tap *event_tap = (struct event_tap *) reference;
        CGEventTapEnable(event_tap->handle, 1);
    } break;
    case kCGEventKeyDown: {
        if (table_find(&blacklst, carbon.process_name)) return event;
        if (!current_mode) return event;

        BEGIN_TIMED_BLOCK("handle_keypress");
        struct hotkey eventkey = create_eventkey(event);
        bool result = find_and_exec_hotkey(&eventkey, &mode_map, &current_mode, &carbon);
        END_TIMED_BLOCK();

        if (result) return NULL;
    } break;
    case NX_SYSDEFINED: {
        if (table_find(&blacklst, carbon.process_name)) return event;
        if (!current_mode) return event;

        struct hotkey eventkey;
        if (intercept_systemkey(event, &eventkey)) {
            bool result = find_and_exec_hotkey(&eventkey, &mode_map, &current_mode, &carbon);
            if (result) return NULL;
        }
    } break;
    }
    return event;
}

static void sigusr1_handler(int signal)
{
    BEGIN_TIMED_BLOCK("sigusr1");
    debug("skhd: SIGUSR1 received.. reloading config\n");
    free_mode_map(&mode_map);
    free_blacklist(&blacklst);
    parse_config_helper(config_file);
    END_TIMED_BLOCK();
}

static pid_t read_pid_file(void)
{
    char pid_file[255] = {};
    pid_t pid = 0;

    char *user = getenv("USER");
    if (user) {
        snprintf(pid_file, sizeof(pid_file), SKHD_PIDFILE_FMT, user);
    } else {
        error("skhd: could not create path to pid-file because 'env USER' was not set! abort..\n");
    }

    int handle = open(pid_file, O_RDWR);
    if (handle == -1) {
        error("skhd: could not open pid-file..\n");
    }

    if (flock(handle, LOCK_EX | LOCK_NB) == 0) {
        error("skhd: could not locate existing instance..\n");
    } else if (read(handle, &pid, sizeof(pid_t)) == -1) {
        error("skhd: could not read pid-file..\n");
    }

    close(handle);
    return pid;
}

static void create_pid_file(void)
{
    char pid_file[255] = {};
    pid_t pid = getpid();

    char *user = getenv("USER");
    if (user) {
        snprintf(pid_file, sizeof(pid_file), SKHD_PIDFILE_FMT, user);
    } else {
        error("skhd: could not create path to pid-file because 'env USER' was not set! abort..\n");
    }

    int handle = open(pid_file, O_CREAT | O_RDWR, 0644);
    if (handle == -1) {
        error("skhd: could not create pid-file! abort..\n");
    }

    struct flock lockfd = {
        .l_start  = 0,
        .l_len    = 0,
        .l_pid    = pid,
        .l_type   = F_WRLCK,
        .l_whence = SEEK_SET
    };

    if (fcntl(handle, F_SETLK, &lockfd) == -1) {
        error("skhd: could not lock pid-file! abort..\n");
    } else if (write(handle, &pid, sizeof(pid_t)) == -1) {
        error("skhd: could not write pid-file! abort..\n");
    }

    // NOTE(koekeishiya): we intentionally leave the handle open,
    // as calling close(..) will release the lock we just acquired.

    debug("skhd: successfully created pid-file..\n");
}

static inline bool string_equals(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static bool parse_arguments(int argc, char **argv)
{
    if ((string_equals(argv[1], VERSION_OPT_LONG)) ||
        (string_equals(argv[1], VERSION_OPT_SHRT))) {
        fprintf(stdout, "skhd-v%d.%d.%d\n", MAJOR, MINOR, PATCH);
        exit(EXIT_SUCCESS);
    }

    if (string_equals(argv[1], SERVICE_INSTALL_OPT)) {
        exit(service_install());
    }

    if (string_equals(argv[1], SERVICE_UNINSTALL_OPT)) {
        exit(service_uninstall());
    }

    if (string_equals(argv[1], SERVICE_START_OPT)) {
        exit(service_start());
    }

    if (string_equals(argv[1], SERVICE_RESTART_OPT)) {
        exit(service_restart());
    }

    if (string_equals(argv[1], SERVICE_STOP_OPT)) {
        exit(service_stop());
    }

    int option;
    char synthesize_opt = 0;
    const char *short_option = "VPvc:k:t:rho";
    struct option long_option[] = {
        { "verbose", no_argument, NULL, 'V' },
        { "profile", no_argument, NULL, 'P' },
        { "config", required_argument, NULL, 'c' },
        { "no-hotload", no_argument, NULL, 'h' },
        { "key", required_argument, NULL, 'k' },
        { "text", required_argument, NULL, 't' },
        { "reload", no_argument, NULL, 'r' },
        { "observe", no_argument, NULL, 'o' },
        { NULL, 0, NULL, 0 }
    };

    while ((option = getopt_long(argc, argv, short_option, long_option, NULL)) != -1) {
        if (synthesize_opt != 0 && option != synthesize_opt) {
            error("skhd: cannot pass arguments after -%c", synthesize_opt);
        }
        switch (option) {
        case 'V': {
            verbose = true;
        } break;
        case 'P': {
            profile = true;
        } break;
        case 'c': {
            snprintf(config_file, sizeof(config_file), "%s", optarg);
        } break;
        case 'h': {
            thwart_hotloader = true;
        } break;
        case 'k': {
            synthesize_key(optarg);
            synthesize_opt = option;
        } break;
        case 't': {
            synthesize_text(optarg);
            synthesize_opt = option;
        } break;
        case 'r': {
            pid_t pid = read_pid_file();
            if (pid) kill(pid, SIGUSR1);
            return true;
        } break;
        case 'o': {
            event_tap.mask = (1 << kCGEventKeyDown) |
                             (1 << kCGEventFlagsChanged);
            event_tap_begin(&event_tap, key_observer_handler);
            CFRunLoopRun();
        } break;
        }
    }

    if (synthesize_opt != 0) {
        return true;
    }

    return false;
}

static bool check_privileges(void)
{
    bool result;
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { kCFBooleanTrue };

    CFDictionaryRef options;
    options = CFDictionaryCreate(kCFAllocatorDefault,
                                 keys, values, sizeof(keys) / sizeof(*keys),
                                 &kCFCopyStringDictionaryKeyCallBacks,
                                 &kCFTypeDictionaryValueCallBacks);

    result = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);

    return result;
}

static inline bool file_exists(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    if (buffer.st_mode & S_IFDIR) {
        return false;
    }

    return true;
}

static bool get_config_file(char *restrict filename, char *restrict buffer, int buffer_size)
{
    char *xdg_home = getenv("XDG_CONFIG_HOME");
    if (xdg_home && *xdg_home) {
        snprintf(buffer, buffer_size, "%s/skhd/%s", xdg_home, filename);
        if (file_exists(buffer)) return true;
    }

    char *home = getenv("HOME");
    if (!home) return false;

    snprintf(buffer, buffer_size, "%s/.config/skhd/%s", home, filename);
    if (file_exists(buffer)) return true;

    snprintf(buffer, buffer_size, "%s/.%s", home, filename);
    return file_exists(buffer);
}

static char *secure_keyboard_entry_process_info(pid_t *pid)
{
    char *process_name = NULL;

    CFDictionaryRef session = CGSCopyCurrentSessionDictionary();
    if (!session) return NULL;

    CFNumberRef pid_ref = (CFNumberRef) CFDictionaryGetValue(session, CFSTR("kCGSSessionSecureInputPID"));
    if (pid_ref) {
        CFNumberGetValue(pid_ref, CFNumberGetType(pid_ref), pid);
        process_name = find_process_name_for_pid(*pid);
    }

    CFRelease(session);
    return process_name;
}

static void dump_secure_keyboard_entry_process_info(void)
{
    pid_t pid;
    char *process_name = secure_keyboard_entry_process_info(&pid);
    if (process_name) {
        error("skhd: secure keyboard entry is enabled by (%lld) '%s'! abort..\n", pid, process_name);
    } else {
        error("skhd: secure keyboard entry is enabled! abort..\n");
    }
}

static GLOBAL_CONNECTION_CALLBACK(connection_handler)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        pid_t pid;
        char *process_name = secure_keyboard_entry_process_info(&pid);

        if (type == 752) {
            if (process_name) {
                notify("Secure Keyboard Entry", "Enabled by '%s' (%d)", process_name, pid);
            } else {
                notify("Secure Keyboard Entry", "Enabled by unknown application..");
            }
        } else if (type == 753) {
            if (process_name) {
                notify("Secure Keyboard Entry", "Disabled by '%s' (%d)", process_name, pid);
            } else {
                notify("Secure Keyboard Entry", "Disabled by unknown application..");
            }
        }

        if (process_name) free(process_name);
    });
}

int main(int argc, char **argv)
{
    if (getuid() == 0 || geteuid() == 0) {
        require("skhd: running as root is not allowed! abort..\n");
    }

    if (parse_arguments(argc, argv)) {
        return EXIT_SUCCESS;
    }

    BEGIN_SCOPED_TIMED_BLOCK("total_time");
    BEGIN_SCOPED_TIMED_BLOCK("init");
    create_pid_file();

    if (secure_keyboard_entry_enabled()) {
        dump_secure_keyboard_entry_process_info();
    }

    if (!check_privileges()) {
        require("skhd: must be run with accessibility access! abort..\n");
    }

    if (!initialize_keycode_map()) {
        error("skhd: could not initialize keycode map! abort..\n");
    }

    if (!carbon_event_init(&carbon)) {
        error("skhd: could not initialize carbon events! abort..\n");
    }

    if (config_file[0] == 0) {
        get_config_file("skhdrc", config_file, sizeof(config_file));
    }

    CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(),
                                    NULL,
                                    &keymap_handler,
                                    kTISNotifySelectedKeyboardInputSourceChanged,
                                    NULL,
                                    CFNotificationSuspensionBehaviorCoalesce);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGUSR1, sigusr1_handler);

    init_shell();
    table_init(&mode_map, 13, (table_hash_func) hash_string, (table_compare_func) compare_string);
    table_init(&blacklst, 13, (table_hash_func) hash_string, (table_compare_func) compare_string);
    END_SCOPED_TIMED_BLOCK();

    BEGIN_SCOPED_TIMED_BLOCK("parse_config");
    debug("skhd: using config '%s'\n", config_file);
    parse_config_helper(config_file);
    END_SCOPED_TIMED_BLOCK();

    BEGIN_SCOPED_TIMED_BLOCK("begin_eventtap");
    event_tap.mask = (1 << kCGEventKeyDown) | (1 << NX_SYSDEFINED);
    event_tap_begin(&event_tap, key_handler);
    END_SCOPED_TIMED_BLOCK();
    END_SCOPED_TIMED_BLOCK();

    NSApplicationLoad();
    notify_init();
    // CGSRegisterNotifyProc((void*)connection_handler, 752, NULL);
    // CGSRegisterNotifyProc((void*)connection_handler, 753, NULL);

    CFRunLoopRun();
    return EXIT_SUCCESS;
}
