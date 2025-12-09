#ifndef SERVICE_H
#define SERVICE_H

#include <spawn.h>
#include <sys/stat.h>

#define MAXLEN 512

#define _PATH_LAUNCHCTL   "/bin/launchctl"
#define _NAME_SKHD_PLIST "com.asmvik.skhd"
#define _PATH_SKHD_PLIST "%s/Library/LaunchAgents/"_NAME_SKHD_PLIST".plist"

#define _SKHD_PLIST \
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" \
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n" \
    "<plist version=\"1.0\">\n" \
    "<dict>\n" \
    "    <key>Label</key>\n" \
    "    <string>"_NAME_SKHD_PLIST"</string>\n" \
    "    <key>ProgramArguments</key>\n" \
    "    <array>\n" \
    "        <string>%s</string>\n" \
    "    </array>\n" \
    "    <key>EnvironmentVariables</key>\n" \
    "    <dict>\n" \
    "        <key>PATH</key>\n" \
    "        <string>%s</string>\n" \
    "    </dict>\n" \
    "    <key>RunAtLoad</key>\n" \
    "    <true/>\n" \
    "    <key>KeepAlive</key>\n" \
    "    <dict>\n" \
    "        <key>SuccessfulExit</key>\n" \
    " 	     <false/>\n" \
    " 	     <key>Crashed</key>\n" \
    " 	     <true/>\n" \
    "    </dict>\n" \
    "    <key>StandardOutPath</key>\n" \
    "    <string>/tmp/skhd_%s.out.log</string>\n" \
    "    <key>StandardErrorPath</key>\n" \
    "    <string>/tmp/skhd_%s.err.log</string>\n" \
    "    <key>ProcessType</key>\n" \
    "    <string>Interactive</string>\n" \
    "    <key>Nice</key>\n" \
    "    <integer>-20</integer>\n" \
    "</dict>\n" \
    "</plist>"

//
// NOTE(asmvik): A launchd service has the following states:
//
//          1. Installed / Uninstalled
//          2. Active (Enable / Disable)
//          3. Bootstrapped (Load / Unload)
//          4. Running (Start / Stop)
//

static int safe_exec(char *const argv[], bool suppress_output)
{
    pid_t pid;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (suppress_output) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY|O_APPEND, 0);
    }

    int status = posix_spawn(&pid, argv[0], &actions, NULL, argv, NULL);
    if (status) return 1;

    while ((waitpid(pid, &status, 0) == -1) && (errno == EINTR)) {
        usleep(1000);
    }

    if (WIFSIGNALED(status)) {
        return 1;
    } else if (WIFSTOPPED(status)) {
        return 1;
    } else {
        return WEXITSTATUS(status);
    }
}

static inline char *cfstring_copy(CFStringRef string)
{
    CFIndex num_bytes = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    char *result = malloc(num_bytes + 1);
    if (!result) return NULL;

    if (!CFStringGetCString(string, result, num_bytes + 1, kCFStringEncodingUTF8)) {
        free(result);
        result = NULL;
    }

    return result;
}

extern CFURLRef CFCopyHomeDirectoryURLForUser(void *user);
static char *populate_plist_path(void)
{
    CFURLRef homeurl_ref = CFCopyHomeDirectoryURLForUser(NULL);
    CFStringRef home_ref = homeurl_ref ? CFURLCopyFileSystemPath(homeurl_ref, kCFURLPOSIXPathStyle) : NULL;
    char *home = home_ref ? cfstring_copy(home_ref) : NULL;

    if (!home) {
        error("skhd: unable to retrieve home directory! abort..\n");
    }

    int size = strlen(_PATH_SKHD_PLIST)-2 + strlen(home) + 1;
    char *result = malloc(size);
    if (!result) {
        error("skhd: could not allocate memory for plist path! abort..\n");
    }

    memset(result, 0, size);
    snprintf(result, size, _PATH_SKHD_PLIST, home);

    return result;
}

static char *populate_plist(int *length)
{
    char *user = getenv("USER");
    if (!user) {
        error("skhd: 'env USER' not set! abort..\n");
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        error("skhd: 'env PATH' not set! abort..\n");
    }

    char exe_path[4096];
    unsigned int exe_path_size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &exe_path_size) < 0) {
        error("skhd: unable to retrieve path of executable! abort..\n");
    }

    int size = strlen(_SKHD_PLIST)-8 + strlen(exe_path) + strlen(path_env) + (2*strlen(user)) + 1;
    char *result = malloc(size);
    if (!result) {
        error("skhd: could not allocate memory for plist contents! abort..\n");
    }

    memset(result, 0, size);
    snprintf(result, size, _SKHD_PLIST, exe_path, path_env, user, user);
    *length = size-1;

    return result;
}


static inline bool directory_exists(char *filename)
{
    struct stat buffer;

    if (stat(filename, &buffer) != 0) {
        return false;
    }

    return S_ISDIR(buffer.st_mode);
}

static inline void ensure_directory_exists(char *skhd_plist_path)
{
    //
    // NOTE(asmvik): Temporarily remove filename.
    // We know the filepath will contain a slash, as
    // it is controlled by us, so don't bother checking
    // the result..
    //

    char *last_slash = strrchr(skhd_plist_path, '/');
    *last_slash = '\0';

    if (!directory_exists(skhd_plist_path)) {
        mkdir(skhd_plist_path, 0755);
    }

    //
    // NOTE(asmvik): Restore original filename.
    //

    *last_slash = '/';
}

static int service_install_internal(char *skhd_plist_path)
{
    int skhd_plist_length;
    char *skhd_plist = populate_plist(&skhd_plist_length);
    ensure_directory_exists(skhd_plist_path);

    FILE *handle = fopen(skhd_plist_path, "w");
    if (!handle) return 1;

    size_t bytes = fwrite(skhd_plist, skhd_plist_length, 1, handle);
    int result = bytes == 1 ? 0 : 1;
    fclose(handle);

    return result;
}

static bool file_exists(char *filename);

static int service_install(void)
{
    char *skhd_plist_path = populate_plist_path();

    if (file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is already installed! abort..\n", skhd_plist_path);
    }

    return service_install_internal(skhd_plist_path);
}

static int service_uninstall(void)
{
    char *skhd_plist_path = populate_plist_path();

    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    return unlink(skhd_plist_path) == 0 ? 0 : 1;
}

static int service_start(void)
{
    char *skhd_plist_path = populate_plist_path();
    if (!file_exists(skhd_plist_path)) {
        warn("skhd: service file '%s' is not installed! attempting installation..\n", skhd_plist_path);

        int result = service_install_internal(skhd_plist_path);
        if (result) {
            error("skhd: service file '%s' could not be installed! abort..\n", skhd_plist_path);
        }
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_SKHD_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped and could be disabled.
        // There is no way to query if the service is disabled, and we cannot
        // bootstrap a disabled service. Try to enable the service. This will be
        // a no-op if the service is already enabled.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "enable", service_target, NULL };
        safe_exec((char *const*)args, false);

        //
        // NOTE(asmvik): Bootstrap service into the target domain.
        // This will also start the program **iff* RunAtLoad is set to true.
        //

        const char *const args2[] = { _PATH_LAUNCHCTL, "bootstrap", domain_target, skhd_plist_path, NULL };
        return safe_exec((char *const*)args2, false);
    } else {

        //
        // NOTE(asmvik): The service has already been bootstrapped.
        // Tell the bootstrapped service to launch immediately; it is an
        // error to bootstrap a service that has already been bootstrapped.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", service_target, NULL };
        return safe_exec((char *const*)args, false);
    }
}

static int service_restart(void)
{
    char *skhd_plist_path = populate_plist_path();
    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_SKHD_PLIST);

    const char *const args[] = { _PATH_LAUNCHCTL, "kickstart", "-k", service_target, NULL };
    return safe_exec((char *const*)args, false);
}

static int service_stop(void)
{
    char *skhd_plist_path = populate_plist_path();
    if (!file_exists(skhd_plist_path)) {
        error("skhd: service file '%s' is not installed! abort..\n", skhd_plist_path);
    }

    char service_target[MAXLEN];
    snprintf(service_target, sizeof(service_target), "gui/%d/%s", getuid(), _NAME_SKHD_PLIST);

    char domain_target[MAXLEN];
    snprintf(domain_target, sizeof(domain_target), "gui/%d", getuid());

    //
    // NOTE(asmvik): Check if service is bootstrapped
    //

    const char *const args[] = { _PATH_LAUNCHCTL, "print", service_target, NULL };
    int is_bootstrapped = safe_exec((char *const*)args, true);

    if (is_bootstrapped != 0) {

        //
        // NOTE(asmvik): Service is not bootstrapped, but the program
        // could still be running an instance that was started **while the service
        // was bootstrapped**, so we tell it to stop said service.
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "kill", "SIGTERM", service_target, NULL };
        return safe_exec((char *const*)args, false);
    } else {

        //
        // NOTE(asmvik): Service is bootstrapped; we stop a potentially
        // running instance of the program and unload the service, making it
        // not trigger automatically in the future.
        //
        // This is NOT the same as disabling the service, which will prevent
        // it from being boostrapped in the future (without explicitly re-enabling
        // it first).
        //

        const char *const args[] = { _PATH_LAUNCHCTL, "bootout", domain_target, skhd_plist_path, NULL };
        return safe_exec((char *const*)args, false);
    }
}

#endif
