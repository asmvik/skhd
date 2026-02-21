#include "carbon.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
static inline char *
find_process_name_for_psn(ProcessSerialNumber *psn)
{
    CFStringRef process_name_ref;
    if (CopyProcessName(psn, &process_name_ref) == noErr) {
        char *process_name = copy_cfstring(process_name_ref);
        for (char *s = process_name; *s; ++s) *s = tolower(*s);
        CFRelease(process_name_ref);
        return process_name;
    }
    return NULL;
}

inline char *
find_process_name_for_pid(pid_t pid)
{
    ProcessSerialNumber psn;
    GetProcessForPID(pid, &psn);
    return find_process_name_for_psn(&psn);
}

static inline char *
find_active_process_name(void)
{
    ProcessSerialNumber psn;
    GetFrontProcess(&psn);
    return find_process_name_for_psn(&psn);
}

static inline char *
find_process_bundle_id_for_psn(ProcessSerialNumber *psn)
{
    CFDictionaryRef process_info_ref = ProcessInformationCopyDictionary(psn, kProcessDictionaryIncludeAllInformationMask);
    if (!process_info_ref) {
        return NULL;
    }
    CFStringRef process_bundle_id_ref = CFDictionaryGetValue(process_info_ref, kCFBundleIdentifierKey);
    CFRelease(process_info_ref);
    if (!process_bundle_id_ref) {
        return NULL;
    }
    char *process_bundle_id = copy_cfstring(process_bundle_id_ref);
    CFRelease(process_bundle_id_ref);
    for (char *s = process_bundle_id; *s; ++s) *s = tolower(*s);
    return process_bundle_id;
}

static inline char *
find_active_process_bundle_id(void)
{
    ProcessSerialNumber psn;
    GetFrontProcess(&psn);
    return find_process_bundle_id_for_psn(&psn);
}

static inline char *
find_process_bundle_name_for_psn(ProcessSerialNumber *psn)
{
    CFDictionaryRef process_info_ref = ProcessInformationCopyDictionary(psn, kProcessDictionaryIncludeAllInformationMask);
    if (!process_info_ref) {
        return NULL;
    }
    CFStringRef process_bundle_name_ref = CFDictionaryGetValue(process_info_ref, kCFBundleNameKey);
    CFRelease(process_info_ref);
    if (process_bundle_name_ref) {
        return NULL;
    }
    char *process_bundle_name = copy_cfstring(process_bundle_name_ref);
    CFRelease(process_bundle_name_ref);
    for (char *s = process_bundle_name; *s; ++s) *s = tolower(*s);
    return process_bundle_name;
}

static inline char *
find_active_process_bundle_name(void)
{
    ProcessSerialNumber psn;
    GetFrontProcess(&psn);
    return find_process_bundle_name_for_psn(&psn);
}
#pragma clang diagnostic pop

static OSStatus
carbon_event_handler(EventHandlerCallRef ref, EventRef event, void *context)
{
    struct carbon_event *carbon = (struct carbon_event *) context;

    ProcessSerialNumber psn;
    if (GetEventParameter(event,
                          kEventParamProcessID,
                          typeProcessSerialNumber,
                          NULL,
                          sizeof(psn),
                          NULL,
                          &psn) != noErr) {
        return -1;
    }

    if (carbon->process_name) {
        free(carbon->process_name);
        carbon->process_name = NULL;
    }

    carbon->process_name = find_process_name_for_psn(&psn);

    if (carbon->process_bundle_id) {
        free(carbon->process_bundle_id);
        carbon->process_bundle_id = NULL;
    }

    carbon->process_bundle_id = find_process_bundle_id_for_psn(&psn);

    if (carbon->process_bundle_name)  {
        free(carbon->process_bundle_name);
        carbon->process_bundle_name = NULL;
    }

    carbon->process_bundle_name = find_process_bundle_name_for_psn(&psn);

    return noErr;
}

bool carbon_event_init(struct carbon_event *carbon)
{
    carbon->target = GetApplicationEventTarget();
    carbon->handler = NewEventHandlerUPP(carbon_event_handler);
    carbon->type.eventClass = kEventClassApplication;
    carbon->type.eventKind = kEventAppFrontSwitched;
    carbon->process_name = find_active_process_name();
    carbon->process_bundle_id = find_active_process_bundle_id();
    carbon->process_bundle_name = find_active_process_bundle_name();

    return InstallEventHandler(carbon->target,
                               carbon->handler,
                               1,
                               &carbon->type,
                               carbon,
                               &carbon->handler_ref) == noErr;
}
