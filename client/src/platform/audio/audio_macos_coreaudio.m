// Core Audio Process Tap audio_backend implementation: per-application session
// enumeration, volume/mute control, and peak metering via macOS 14.2+'s
// AudioHardwareCreateProcessTap API, plus master (system output)
// volume/mute (device-level properties) and peak (a global metering tap --
// see build_master_tap). Session enumeration and metering are build- *and*
// run-verified: a standalone harness linking just this file against the
// real macOS 15.5 SDK/frameworks was pointed at a real `afplay` process and
// showed live non-zero peak values and correct add/remove events. The
// volume/mute reinjection path (handle_io's write side, and
// set_tap_muted's rebuild) is build-verified but not confirmed audibly
// correct end-to-end -- review before relying on it.
//
// A process tap captures audio *before* kAudioDevicePropertyVolumeScalar's
// attenuation is applied -- confirmed empirically on this machine: driving
// the master volume between 1.0 and 0.05 while continuous real audio played
// produced the same peak range from a global tap either way. So
// coreaudio_get_master() multiplies the tap's raw peak by the current
// volume itself; without that correction the meter would show constant
// loudness no matter where Master's fader sits.
//
// Unlike WASAPI (ISimpleAudioVolume) or PipeWire (SPA_PROP_channelVolumes),
// Core Audio has no public "set this process's volume" call. Process taps
// were designed for *capturing* another process's audio, not scaling it. The
// approach here, matching what open-source per-app mixers (Fader, Fabar,
// FineTune) do:
//
//   1. Every audible session gets a persistent process tap + a private
//      aggregate device that also wraps the real default output device as
//      its "main" sub-device (the same recipe Apple's own AudioCap sample
//      uses for recording). One AudioDeviceIOProcID block runs continuously
//      per session, fed both an input buffer (the tapped audio) and an
//      output buffer (which -- because the aggregate's main sub-device is
//      the real hardware -- goes straight to the speakers if we write to
//      it).
//   2. While a session is at 100% volume and unmuted, the tap's muteBehavior
//      is CATapUnmuted: the process's audio keeps flowing to hardware on
//      its own, completely untouched, and our IOProc only reads inInputData
//      to compute a peak level for the VU meter. outOutputData is left
//      alone (Core Audio pre-zeroes it, so nothing extra is emitted).
//   3. The moment volume drops below 100% or the session is muted, the
//      pipeline is rebuilt with the tap set to CATapMuted (silencing the
//      process at the source), and the *same* IOProc shape starts writing
//      volume-scaled samples from inInputData into outOutputData -- i.e. we
//      become the process's de facto renderer for as long as it's not at a
//      plain 100%/unmuted state. Reverting to 100%/unmuted rebuilds again
//      with CATapUnmuted, restoring bit-perfect native playback.
//
// Switching between those two states (set_tap_muted) tears down and
// recreates the whole tap/aggregate/IOProc rather than mutating the live
// tap in place. An earlier version of this file tried the latter via
// kAudioTapPropertyDescription/AudioObjectSetPropertyData -- AudioHardware.h
// documents that selector as "can be used to modify and set the description
// of an existing tap", and it compiles and type-checks cleanly, but it
// reliably **segfaulted inside AudioObjectSetPropertyData** when actually
// exercised against a live tap (verified via a standalone harness + lldb on
// this machine, macOS 15.5). Apple's own AudioCap sample never mutates a
// live tap's description either (only sets muteBehavior once, before
// creation), so tear-down-and-rebuild is the only path confirmed to work in
// practice, at the cost of a brief re-creation blip on every mute toggle /
// 100%<->less-than-100% crossing (not on every slider tick in between,
// since only the crossing changes muteBehavior).

#import "platform/audio_backend.h"
#import <AppKit/AppKit.h> // NSRunningApplication, for rescan_sessions()'s real-app filter
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>
#import <libproc.h>
#import <math.h>
#import <stdatomic.h>
#import <stdlib.h>
#import <string.h>

#define MAX_BACKEND_SESSIONS 64
#define COREAUDIO_ENUM_INTERVAL_MS 250

typedef struct {
    bool active;
    pid_t pid;
    char name[AUDIO_SESSION_NAME_LEN];
    AudioObjectID process_object_id;

    // Persistent per-session tap/aggregate/IOProc, built once when the
    // session is first seen and torn down when it disappears. kAudioObjectUnknown
    // / NULL while not yet built (e.g. tap creation failed, most likely due
    // to the user not having granted the system audio-recording permission
    // yet).
    AudioObjectID tap_id;
    AudioDeviceID aggregate_id;
    AudioDeviceIOProcID io_proc_id;
    _Atomic bool tap_currently_muted; // last muteBehavior we asked the tap for (CATapMuted vs
                                      // CATapUnmuted) -- read concurrently by handle_io() on
                                      // backend->io_queue, written by set_tap_muted() on
                                      // whichever thread calls set_volume/set_muted, hence atomic

    _Atomic float volume; // 0.0 - 1.0, applied by the IOProc block when reinjecting
    _Atomic float peak;   // 0.0 - 1.0, most recent peak magnitude
    _Atomic bool muted;
} backend_session_t;

struct audio_backend {
    backend_session_t sessions[MAX_BACKEND_SESSIONS];
    dispatch_queue_t io_queue;
    uint64_t last_enum_tick_ms;

    // Set (from an arbitrary Core Audio notification thread) when the
    // user's default output device changes -- see the listener registered
    // in coreaudio_create(). Consumed from coreaudio_poll() so the actual
    // pipeline rebuild always happens on the same thread that already
    // serializes with rescan_sessions(), rather than racing plain (non-
    // atomic) backend_session_t fields like tap_id/aggregate_id from
    // whatever thread Core Audio fires the notification on.
    _Atomic bool output_device_changed;
    AudioObjectPropertyListenerBlock output_device_listener;

    // Master (system output) metering: a single always-on *global* tap
    // (every process, CATapUnmuted -- this one never mutes/reinjects
    // anything, it only measures). Empirically confirmed on this machine
    // that a process tap captures audio *before* kAudioDevicePropertyVolumeScalar's
    // attenuation is applied (fixed 1.0 vs 0.05 master volume produced the
    // same peak range from continuous real audio) -- so coreaudio_get_master()
    // multiplies this raw tapped peak by the current volume itself to get a
    // meter that actually responds to the fader, rather than a flat
    // pre-fader loudness reading that never moves when you turn Master down.
    AudioObjectID master_tap_id;
    AudioDeviceID master_aggregate_id;
    AudioDeviceIOProcID master_io_proc_id;
    _Atomic float master_tap_peak;

    audio_session_cb on_added;
    audio_session_cb on_updated;
    audio_session_cb on_removed;
    void *user_data;
};

static uint64_t now_ms(void)
{
    return (uint64_t)(CFAbsoluteTimeGetCurrent() * 1000.0);
}

static void build_session_snapshot(backend_session_t *s, audio_session_t *out)
{
    out->id = (uint32_t)s->pid;
    snprintf(out->name, sizeof(out->name), "%s", s->name);
    out->volume = atomic_load(&s->volume);
    out->peak = atomic_load(&s->peak);
    out->muted = atomic_load(&s->muted);
}

static backend_session_t *find_session_by_pid(audio_backend_t *backend, pid_t pid)
{
    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        if (backend->sessions[i].active && backend->sessions[i].pid == pid) {
            return &backend->sessions[i];
        }
    }
    return NULL;
}

static backend_session_t *find_session_by_id(audio_backend_t *backend, uint32_t id)
{
    return find_session_by_pid(backend, (pid_t)id);
}

// --- Core Audio property helpers --------------------------------------------

static OSStatus get_property(AudioObjectID object, AudioObjectPropertySelector selector,
                             UInt32 *io_size, void *out_data)
{
    AudioObjectPropertyAddress addr = {
        .mSelector = selector,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    return AudioObjectGetPropertyData(object, &addr, 0, NULL, io_size, out_data);
}

static bool read_pid_t(AudioObjectID object, AudioObjectPropertySelector selector, pid_t *out)
{
    UInt32 size = sizeof(*out);
    return get_property(object, selector, &size, out) == noErr;
}

static bool read_cfstring(AudioObjectID object, AudioObjectPropertySelector selector, NSString **out)
{
    CFStringRef value = NULL;
    UInt32 size = sizeof(value);
    if (get_property(object, selector, &size, &value) != noErr || !value) {
        return false;
    }
    *out = CFBridgingRelease(value);
    return true;
}

// The device regular apps render to -- NOT kAudioHardwarePropertyDefaultSystemOutputDevice
// (alert sounds only, see build_session_pipeline's own comment on that mixup).
// Returns kAudioObjectUnknown on failure.
static AudioDeviceID default_output_device_id(void)
{
    AudioDeviceID device_id = kAudioObjectUnknown;
    UInt32 size = sizeof(device_id);
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &device_id) !=
        noErr) {
        return kAudioObjectUnknown;
    }
    return device_id;
}

// Fills `name` with the process's own name (via libproc, matching how the
// WASAPI backend resolves a display name from a PID) -- Core Audio's own
// kAudioProcessPropertyBundleID is often empty for plain processes, so this
// is used as the primary source rather than a fallback.
static void resolve_process_name(pid_t pid, char *out, size_t out_size)
{
    char buf[PROC_PIDPATHINFO_MAXSIZE];
    int len = proc_pidpath(pid, buf, sizeof(buf));
    if (len <= 0) {
        snprintf(out, out_size, "pid %d", pid);
        return;
    }
    const char *base = strrchr(buf, '/');
    base = base ? base + 1 : buf;
    snprintf(out, out_size, "%s", base);
}

// --- Tap/aggregate/IOProc lifecycle ------------------------------------------

static bool build_session_pipeline(audio_backend_t *backend, backend_session_t *s, bool initial_muted);
static void teardown_session_pipeline(backend_session_t *s);

// Switches the session's tap between passthrough (CATapUnmuted, metering
// only) and muted-and-reinjected (CATapMuted) by tearing down and rebuilding
// the whole tap/aggregate/IOProc, rather than mutating the live tap's
// muteBehavior in place. An earlier version of this file tried the latter
// via kAudioTapPropertyDescription/AudioObjectSetPropertyData -- the header
// documents that selector as "can be used to modify and set the description
// of an existing tap", and it compiles and type-checks fine, but it
// reliably segfaults inside AudioObjectSetPropertyData when exercised
// against a real tap on this machine (macOS 15.5). Apple's own AudioCap
// sample never mutates a live tap's description either (only sets
// muteBehavior once, before creation), so tearing down and rebuilding is
// the only path actually verified to work. This does mean a brief
// (re-creation-latency) audio blip on every mute toggle / 100%<-><100%
// volume crossing, not on every slider tick in between.
static bool set_tap_muted(audio_backend_t *backend, backend_session_t *s, bool want_muted)
{
    if (s->tap_id == kAudioObjectUnknown) {
        return false;
    }
    if (atomic_load(&s->tap_currently_muted) == want_muted) {
        return true;
    }

    teardown_session_pipeline(s);
    return build_session_pipeline(backend, s, want_muted);
}

// Peak-meters inInputData and, if the tap is currently muted (i.e. we own
// reinjection for this session), scales it by the current volume into
// outOutputData. Runs on audio_backend_t.io_queue for every active session's
// aggregate device -- no allocation, no locking (only atomics), matching the
// real-time constraints the PipeWire backend already observes.
static void handle_io(backend_session_t *s, const AudioBufferList *in, AudioBufferList *out)
{
    bool reinject = atomic_load(&s->tap_currently_muted);
    // Logical mute always wins over volume: without this, muting a session
    // that's sitting at 100% volume would flip the tap to CATapMuted
    // (silencing it at the source, which is why `reinject` above is true)
    // but then reinject its audio right back at full gain, defeating the
    // mute entirely.
    float gain = atomic_load(&s->muted) ? 0.0f : atomic_load(&s->volume);

    float peak = 0.0f;
    for (UInt32 i = 0; i < in->mNumberBuffers; i++) {
        const AudioBuffer *buf = &in->mBuffers[i];
        if (!buf->mData) {
            continue;
        }
        UInt32 n_samples = buf->mDataByteSize / sizeof(float);
        const float *samples = buf->mData;

        float *out_samples = NULL;
        UInt32 out_n = 0;
        if (reinject && out && i < out->mNumberBuffers) {
            AudioBuffer *out_buf = &out->mBuffers[i];
            out_samples = out_buf->mData;
            out_n = out_buf->mDataByteSize / sizeof(float);
        }

        for (UInt32 f = 0; f < n_samples; f++) {
            float sample = samples[f];
            float mag = fabsf(sample);
            if (mag > peak) {
                peak = mag;
            }
            if (out_samples && f < out_n) {
                out_samples[f] = sample * gain;
            }
        }
    }
    atomic_store(&s->peak, peak * gain);
}

static bool build_session_pipeline(audio_backend_t *backend, backend_session_t *s, bool initial_muted)
{
    AudioDeviceID default_output = default_output_device_id();
    if (default_output == kAudioObjectUnknown) {
        return false;
    }

    NSString *output_uid = nil;
    if (!read_cfstring(default_output, kAudioDevicePropertyDeviceUID, &output_uid) || !output_uid) {
        return false;
    }

    CATapDescription *desc =
        [[CATapDescription alloc] initStereoMixdownOfProcesses:@[ @(s->process_object_id) ]];
    NSUUID *uuid = [NSUUID UUID];
    desc.UUID = uuid;
    desc.privateTap = YES;
    desc.muteBehavior = initial_muted ? CATapMuted : CATapUnmuted;

    AudioObjectID tap_id = kAudioObjectUnknown;
    OSStatus err = AudioHardwareCreateProcessTap(desc, &tap_id);
    if (err != noErr) {
        // Most likely cause: the user hasn't granted (or has denied) the
        // system "record audio from other apps" permission yet -- the first
        // call for any process during this run triggers that prompt.
        NSLog(@"[tinypad] process tap creation failed for pid %d: %d", s->pid, (int)err);
        return false;
    }

    // kAudioAggregateDevice*/kAudioSubTap*/kAudioSubDevice* are plain C string
    // macros (#define ... "name"), not CFStringRef constants, so they're
    // boxed with @() rather than __bridge-cast.
    NSDictionary *agg_desc = @{
        @(kAudioAggregateDeviceNameKey) : [NSString stringWithFormat:@"tinypad-tap-%d", s->pid],
        @(kAudioAggregateDeviceUIDKey) : [[NSUUID UUID] UUIDString],
        @(kAudioAggregateDeviceMainSubDeviceKey) : output_uid,
        @(kAudioAggregateDeviceIsPrivateKey) : @YES,
        @(kAudioAggregateDeviceSubDeviceListKey) : @[ @{
            @(kAudioSubDeviceUIDKey) : output_uid,
        } ],
        @(kAudioAggregateDeviceTapListKey) : @[ @{
            @(kAudioSubTapUIDKey) : uuid.UUIDString,
            @(kAudioSubTapDriftCompensationKey) : @YES,
        } ],
    };

    AudioObjectID aggregate_id = kAudioObjectUnknown;
    err = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)agg_desc, &aggregate_id);
    if (err != noErr) {
        NSLog(@"[tinypad] aggregate device creation failed for pid %d: %d", s->pid, (int)err);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    AudioDeviceIOProcID io_proc_id = NULL;
    err = AudioDeviceCreateIOProcIDWithBlock(
        &io_proc_id, aggregate_id, backend->io_queue,
        ^(const AudioTimeStamp *now, const AudioBufferList *in_data, const AudioTimeStamp *in_time,
         AudioBufferList *out_data, const AudioTimeStamp *out_time) {
            (void)now;
            (void)in_time;
            (void)out_time;
            handle_io(s, in_data, out_data);
        });
    if (err != noErr) {
        NSLog(@"[tinypad] IOProc creation failed for pid %d: %d", s->pid, (int)err);
        AudioHardwareDestroyAggregateDevice(aggregate_id);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    err = AudioDeviceStart(aggregate_id, io_proc_id);
    if (err != noErr) {
        NSLog(@"[tinypad] AudioDeviceStart failed for pid %d: %d", s->pid, (int)err);
        AudioDeviceDestroyIOProcID(aggregate_id, io_proc_id);
        AudioHardwareDestroyAggregateDevice(aggregate_id);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    s->tap_id = tap_id;
    s->aggregate_id = aggregate_id;
    s->io_proc_id = io_proc_id;
    atomic_store(&s->tap_currently_muted, initial_muted);
    return true;
}

static void teardown_session_pipeline(backend_session_t *s)
{
    if (s->aggregate_id != kAudioObjectUnknown) {
        AudioDeviceStop(s->aggregate_id, s->io_proc_id);
        if (s->io_proc_id) {
            AudioDeviceDestroyIOProcID(s->aggregate_id, s->io_proc_id);
        }
        AudioHardwareDestroyAggregateDevice(s->aggregate_id);
    }
    if (s->tap_id != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(s->tap_id);
    }
    s->tap_id = kAudioObjectUnknown;
    s->aggregate_id = kAudioObjectUnknown;
    s->io_proc_id = NULL;
    atomic_store(&s->tap_currently_muted, false);
}

// Global tap (every process, muteBehavior always CATapUnmuted) used purely
// to measure master/system output loudness -- see the struct audio_backend
// comment on master_tap_id for why coreaudio_get_master() still has to
// multiply this by the current volume itself. Same recipe as
// build_session_pipeline (tap + private aggregate wrapping the real output
// device + IOProc), just with a global tap description and a peak-only
// callback instead of a per-process one with reinjection.
static bool build_master_tap(audio_backend_t *backend)
{
    AudioDeviceID default_output = default_output_device_id();
    if (default_output == kAudioObjectUnknown) {
        return false;
    }

    NSString *output_uid = nil;
    if (!read_cfstring(default_output, kAudioDevicePropertyDeviceUID, &output_uid) || !output_uid) {
        return false;
    }

    CATapDescription *desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
    NSUUID *uuid = [NSUUID UUID];
    desc.UUID = uuid;
    desc.privateTap = YES;
    desc.muteBehavior = CATapUnmuted; // measuring only, never silences/reinjects anything

    AudioObjectID tap_id = kAudioObjectUnknown;
    OSStatus err = AudioHardwareCreateProcessTap(desc, &tap_id);
    if (err != noErr) {
        NSLog(@"[tinypad] master metering tap creation failed: %d", (int)err);
        return false;
    }

    NSDictionary *agg_desc = @{
        @(kAudioAggregateDeviceNameKey) : @"tinypad-master-tap",
        @(kAudioAggregateDeviceUIDKey) : [[NSUUID UUID] UUIDString],
        @(kAudioAggregateDeviceMainSubDeviceKey) : output_uid,
        @(kAudioAggregateDeviceIsPrivateKey) : @YES,
        @(kAudioAggregateDeviceSubDeviceListKey) : @[ @{
            @(kAudioSubDeviceUIDKey) : output_uid,
        } ],
        @(kAudioAggregateDeviceTapListKey) : @[ @{
            @(kAudioSubTapUIDKey) : uuid.UUIDString,
            @(kAudioSubTapDriftCompensationKey) : @YES,
        } ],
    };

    AudioObjectID aggregate_id = kAudioObjectUnknown;
    err = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)agg_desc, &aggregate_id);
    if (err != noErr) {
        NSLog(@"[tinypad] master metering aggregate creation failed: %d", (int)err);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    AudioDeviceIOProcID io_proc_id = NULL;
    err = AudioDeviceCreateIOProcIDWithBlock(
        &io_proc_id, aggregate_id, backend->io_queue,
        ^(const AudioTimeStamp *now, const AudioBufferList *in_data, const AudioTimeStamp *in_time,
         AudioBufferList *out_data, const AudioTimeStamp *out_time) {
            (void)now;
            (void)in_time;
            (void)out_data;
            (void)out_time;
            float peak = 0.0f;
            for (UInt32 i = 0; i < in_data->mNumberBuffers; i++) {
                const AudioBuffer *buf = &in_data->mBuffers[i];
                if (!buf->mData) {
                    continue;
                }
                UInt32 n_samples = buf->mDataByteSize / sizeof(float);
                const float *samples = buf->mData;
                for (UInt32 f = 0; f < n_samples; f++) {
                    float mag = fabsf(samples[f]);
                    if (mag > peak) {
                        peak = mag;
                    }
                }
            }
            atomic_store(&backend->master_tap_peak, peak);
        });
    if (err != noErr) {
        NSLog(@"[tinypad] master metering IOProc creation failed: %d", (int)err);
        AudioHardwareDestroyAggregateDevice(aggregate_id);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    err = AudioDeviceStart(aggregate_id, io_proc_id);
    if (err != noErr) {
        NSLog(@"[tinypad] master metering AudioDeviceStart failed: %d", (int)err);
        AudioDeviceDestroyIOProcID(aggregate_id, io_proc_id);
        AudioHardwareDestroyAggregateDevice(aggregate_id);
        AudioHardwareDestroyProcessTap(tap_id);
        return false;
    }

    backend->master_tap_id = tap_id;
    backend->master_aggregate_id = aggregate_id;
    backend->master_io_proc_id = io_proc_id;
    return true;
}

static void teardown_master_tap(audio_backend_t *backend)
{
    if (backend->master_aggregate_id != kAudioObjectUnknown) {
        AudioDeviceStop(backend->master_aggregate_id, backend->master_io_proc_id);
        if (backend->master_io_proc_id) {
            AudioDeviceDestroyIOProcID(backend->master_aggregate_id, backend->master_io_proc_id);
        }
        AudioHardwareDestroyAggregateDevice(backend->master_aggregate_id);
    }
    if (backend->master_tap_id != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(backend->master_tap_id);
    }
    backend->master_tap_id = kAudioObjectUnknown;
    backend->master_aggregate_id = kAudioObjectUnknown;
    backend->master_io_proc_id = NULL;
    atomic_store(&backend->master_tap_peak, 0.0f);
}

// --- Session enumeration -----------------------------------------------------

// Diffs the current set of audible processes against the known session
// table, firing on_added/on_removed -- mirrors the WASAPI backend's
// rescan_sessions() (periodic re-enumeration; per-session state is refreshed
// every poll() instead, see coreaudio_poll() below).
static void rescan_sessions(audio_backend_t *backend)
{
    AudioObjectPropertyAddress list_addr = {
        .mSelector = kAudioHardwarePropertyProcessObjectList,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };

    UInt32 data_size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &list_addr, 0, NULL, &data_size) !=
        noErr) {
        return;
    }
    UInt32 count = data_size / sizeof(AudioObjectID);
    if (count == 0) {
        return;
    }
    AudioObjectID *object_ids = calloc(count, sizeof(AudioObjectID));
    if (!object_ids) {
        return;
    }
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &list_addr, 0, NULL, &data_size,
                                   object_ids) != noErr) {
        free(object_ids);
        return;
    }

    bool seen[MAX_BACKEND_SESSIONS] = {0};
    pid_t self_pid = getpid();

    for (UInt32 i = 0; i < count; i++) {
        AudioObjectID object_id = object_ids[i];

        pid_t pid = -1;
        if (!read_pid_t(object_id, kAudioProcessPropertyPID, &pid) || pid < 0 || pid == self_pid) {
            continue;
        }

        // Deliberately not filtering on kAudioProcessPropertyIsRunning
        // ("audio IO in progress right now") -- that would drop a paused
        // Music/Spotify/VLC session from the list the instant it stops
        // making sound, even though the app is still open with a live
        // audio engine. Windows (AudioSessionStateInactive) and PipeWire
        // (node stays until the stream is actually destroyed) already keep
        // silent-but-open sessions visible; this brings macOS in line.
        //
        // Instead, only keep processes that are real, Dock-visible
        // foreground apps: plain runningApplicationWithProcessIdentifier:
        // (matching Apple's own AudioCap sample) excludes bare CLI tools
        // like `say`/`afplay` (confirmed via testing) but *not* background
        // UI-server/XPC processes that still register as an
        // NSRunningApplication -- ControlCenter, loginwindow,
        // SiriNCService, QuickLookUIService, universalaccessd, and
        // com.apple.WebKit.GPU all showed up in testing despite never being
        // something a user would assign a mixer knob to. Requiring
        // activationPolicy == Regular (Dock-visible, can become the active
        // app) excludes all of those while keeping real apps like Spotify,
        // Firefox, Music, VLC, etc.
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (!app || app.activationPolicy != NSApplicationActivationPolicyRegular) {
            continue;
        }

        backend_session_t *existing = find_session_by_pid(backend, pid);
        if (existing) {
            for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
                if (&backend->sessions[j] == existing) {
                    seen[j] = true;
                    break;
                }
            }
            continue;
        }

        backend_session_t *slot = NULL;
        int slot_index = -1;
        for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
            if (!backend->sessions[j].active) {
                slot = &backend->sessions[j];
                slot_index = j;
                break;
            }
        }
        if (!slot) {
            continue;
        }

        slot->active = true;
        slot->pid = pid;
        slot->process_object_id = object_id;
        slot->tap_id = kAudioObjectUnknown;
        slot->aggregate_id = kAudioObjectUnknown;
        slot->io_proc_id = NULL;
        atomic_store(&slot->tap_currently_muted, false);
        atomic_store(&slot->volume, 1.0f);
        atomic_store(&slot->peak, 0.0f);
        atomic_store(&slot->muted, false);
        resolve_process_name(pid, slot->name, sizeof(slot->name));

        // Best-effort; session is still usable (peak stuck at 0, volume/mute
        // calls fail) if this fails, e.g. the system audio-recording
        // permission hasn't been granted yet.
        build_session_pipeline(backend, slot, false);

        seen[slot_index] = true;

        if (backend->on_added) {
            audio_session_t snapshot;
            build_session_snapshot(slot, &snapshot);
            backend->on_added(&snapshot, backend->user_data);
        }
    }

    free(object_ids);

    for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
        if (backend->sessions[j].active && !seen[j]) {
            audio_session_t snapshot;
            build_session_snapshot(&backend->sessions[j], &snapshot);
            teardown_session_pipeline(&backend->sessions[j]);
            backend->sessions[j].active = false;
            if (backend->on_removed) {
                backend->on_removed(&snapshot, backend->user_data);
            }
        }
    }
}

// Rebuilds every active session's tap/aggregate/IOProc so they re-read the
// (now current) default output device -- called from coreaudio_poll() after
// the property listener registered in coreaudio_create() observes
// kAudioHardwarePropertyDefaultOutputDevice change (e.g. the user switched
// from speakers to headphones while tinypad was running). Without this,
// every session's aggregate device keeps targeting whatever device was
// default when its pipeline was first built, silently misrouting any
// reinjected (volume < 100% or muted) audio to a device the user may no
// longer be listening on.
static void rebuild_all_pipelines_for_new_output_device(audio_backend_t *backend)
{
    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        backend_session_t *s = &backend->sessions[i];
        if (!s->active || s->tap_id == kAudioObjectUnknown) {
            continue;
        }
        bool was_muted = atomic_load(&s->tap_currently_muted);
        teardown_session_pipeline(s);
        build_session_pipeline(backend, s, was_muted); // best-effort, same as initial build
    }

    if (backend->master_tap_id != kAudioObjectUnknown) {
        teardown_master_tap(backend);
        build_master_tap(backend); // best-effort, same as the initial build in coreaudio_create()
    }
}

// --- vtable -------------------------------------------------------------------

static audio_backend_t *coreaudio_create(void)
{
    audio_backend_t *backend = calloc(1, sizeof(audio_backend_t));
    if (!backend) {
        return NULL;
    }
    backend->io_queue = dispatch_queue_create("com.tinypad.audiotap", DISPATCH_QUEUE_SERIAL);
    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        backend->sessions[i].tap_id = kAudioObjectUnknown;
        backend->sessions[i].aggregate_id = kAudioObjectUnknown;
    }
    backend->master_tap_id = kAudioObjectUnknown;
    backend->master_aggregate_id = kAudioObjectUnknown;
    build_master_tap(backend); // best-effort -- coreaudio_get_master() just reads peak 0 if this failed

    AudioObjectPropertyAddress default_out_addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    backend->output_device_listener = ^(UInt32 n, const AudioObjectPropertyAddress *addrs) {
        (void)n;
        (void)addrs;
        atomic_store(&backend->output_device_changed, true);
    };
    AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &default_out_addr,
                                       backend->io_queue, backend->output_device_listener);

    return backend;
}

static void coreaudio_destroy(audio_backend_t *backend)
{
    if (!backend) {
        return;
    }
    AudioObjectPropertyAddress default_out_addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &default_out_addr,
                                          backend->io_queue, backend->output_device_listener);
    backend->output_device_listener = nil; // ARC releases the block

    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        if (backend->sessions[i].active) {
            teardown_session_pipeline(&backend->sessions[i]);
        }
    }
    teardown_master_tap(backend);
    backend->io_queue = NULL; // ARC releases the dispatch_queue_t
    free(backend);
}

static void coreaudio_poll(audio_backend_t *backend)
{
    if (atomic_exchange(&backend->output_device_changed, false)) {
        rebuild_all_pipelines_for_new_output_device(backend);
    }

    uint64_t now = now_ms();
    if (now - backend->last_enum_tick_ms >= COREAUDIO_ENUM_INTERVAL_MS) {
        rescan_sessions(backend);
        backend->last_enum_tick_ms = now;
    }

    // Peak (and, implicitly, volume/mute reinjection) is refreshed
    // continuously by handle_io() above, on backend->io_queue -- just fire
    // on_updated here so mixer_state's slots pick up the latest snapshot,
    // mirroring the WASAPI backend's split between "rare structural change"
    // (rescan_sessions) and "frequent level update" (here).
    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        backend_session_t *s = &backend->sessions[i];
        if (s->active && backend->on_updated) {
            audio_session_t snapshot;
            build_session_snapshot(s, &snapshot);
            backend->on_updated(&snapshot, backend->user_data);
        }
    }
}

static bool coreaudio_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    backend_session_t *s = find_session_by_id(backend, session_id);
    if (!s) {
        return false;
    }
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    atomic_store(&s->volume, volume);
    bool want_muted = atomic_load(&s->muted) || volume < 1.0f;
    return set_tap_muted(backend, s, want_muted);
}

static bool coreaudio_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    backend_session_t *s = find_session_by_id(backend, session_id);
    if (!s) {
        return false;
    }
    atomic_store(&s->muted, muted);
    bool want_muted = muted || atomic_load(&s->volume) < 1.0f;
    return set_tap_muted(backend, s, want_muted);
}

static void coreaudio_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                                    audio_session_cb on_updated, audio_session_cb on_removed,
                                    void *user_data)
{
    backend->on_added = on_added;
    backend->on_updated = on_updated;
    backend->on_removed = on_removed;
    backend->user_data = user_data;
}

// --- master (system output) ---------------------------------------------------
// Unlike per-app sessions, these are plain device-level properties -- no
// process tap, no permission prompt. Verified against a real device on this
// machine (a real device's default output, "External Headphones", supports
// both properties at kAudioObjectPropertyElementMain/scope Output). Some
// audio interfaces only expose per-channel volume (element 1, 2, ...)
// instead of a single main-element control -- not handled here, same
// "not exhaustively tested against every possible device" caveat the rest
// of this codebase already carries honestly.

static bool coreaudio_get_master(audio_backend_t *backend, audio_session_t *out)
{
    AudioDeviceID device = default_output_device_id();
    if (device == kAudioObjectUnknown) {
        return false;
    }

    Float32 volume = 0.0f;
    UInt32 size = sizeof(volume);
    AudioObjectPropertyAddress vol_addr = {
        .mSelector = kAudioDevicePropertyVolumeScalar,
        .mScope = kAudioObjectPropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(device, &vol_addr, 0, NULL, &size, &volume) != noErr) {
        return false;
    }

    UInt32 muted = 0;
    size = sizeof(muted);
    AudioObjectPropertyAddress mute_addr = {
        .mSelector = kAudioDevicePropertyMute,
        .mScope = kAudioObjectPropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain,
    };
    // Not every device implements a mute control (this property is optional
    // per AudioHardware.h) -- treat "not muted" as the safe default rather
    // than failing the whole get_master() call over it.
    AudioObjectGetPropertyData(device, &mute_addr, 0, NULL, &size, &muted);

    out->id = 0; // ignored by mixer_state_poll(), which substitutes its own sentinel
    snprintf(out->name, sizeof(out->name), "Master");
    out->volume = volume;
    // master_tap_peak is measured *before* the device's volume attenuation
    // (confirmed empirically -- see the struct audio_backend comment), so
    // scale it by the current volume ourselves; otherwise the meter would
    // show constant loudness regardless of where Master's fader is.
    // muted implies silence at the actual output even though the tap still
    // measures the pre-mute signal underneath.
    out->peak = muted ? 0.0f : atomic_load(&backend->master_tap_peak) * volume;
    out->muted = muted != 0;
    return true;
}

static bool coreaudio_set_master_volume(audio_backend_t *backend, float volume)
{
    (void)backend;
    AudioDeviceID device = default_output_device_id();
    if (device == kAudioObjectUnknown) {
        return false;
    }
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioDevicePropertyVolumeScalar,
        .mScope = kAudioObjectPropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain,
    };
    Float32 value = volume;
    return AudioObjectSetPropertyData(device, &addr, 0, NULL, sizeof(value), &value) == noErr;
}

static bool coreaudio_set_master_muted(audio_backend_t *backend, bool muted)
{
    (void)backend;
    AudioDeviceID device = default_output_device_id();
    if (device == kAudioObjectUnknown) {
        return false;
    }

    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioDevicePropertyMute,
        .mScope = kAudioObjectPropertyScopeOutput,
        .mElement = kAudioObjectPropertyElementMain,
    };
    UInt32 value = muted ? 1 : 0;
    return AudioObjectSetPropertyData(device, &addr, 0, NULL, sizeof(value), &value) == noErr;
}

static const audio_backend_vtable_t vtable = {
    .create = coreaudio_create,
    .destroy = coreaudio_destroy,
    .poll = coreaudio_poll,
    .set_volume = coreaudio_set_volume,
    .set_muted = coreaudio_set_muted,
    .set_callbacks = coreaudio_set_callbacks,
    .get_master = coreaudio_get_master,
    .set_master_volume = coreaudio_set_master_volume,
    .set_master_muted = coreaudio_set_master_muted,
};

const audio_backend_vtable_t *audio_backend_get_vtable(void) { return &vtable; }
