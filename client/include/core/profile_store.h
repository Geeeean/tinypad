#pragma once

// SQLite-backed persistence for named "profiles": a bundle of macro_map's
// trigger bindings, device_settings' switch labels/GUI layout, and which
// app (by name) was assigned to each mixer slot. Explicit save/load, not
// auto-save -- edits apply to the live macro_map_t/device_settings_t/
// mixer_state_t objects instantly (as before this existed), but only reach
// the database when the caller explicitly saves.

#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct profile_store profile_store_t;

typedef struct {
    int64_t id;
    char name[64];
} profile_summary_t;

// db_path: a real file path, or ":memory:" for tests/no persistence.
// Creates and migrates the schema if needed; on a brand new (empty)
// database, seeds one "Default" profile matching macro_map_create()'s and
// device_settings_create()'s own hardcoded defaults, and marks it active.
profile_store_t *profile_store_open(const char *db_path);
void profile_store_close(profile_store_t *store);

// Resolves the real on-disk database path next to the running executable
// (mirrors ui_bridge.c's get_executable_dir()). Separate from
// profile_store_open() so callers (tests) can pass ":memory:" directly
// instead.
bool profile_store_default_path(char *out, size_t out_size);

// Copies up to max_out profiles (id + name) into out. Returns the number
// copied.
size_t profile_store_list(profile_store_t *store, profile_summary_t *out, size_t max_out);

bool profile_store_get_active_id(profile_store_t *store, int64_t *out_id);

// Overwrites profile_id's stored macro bindings/labels/layout/slot
// assignments from the given live objects' current state, in one
// transaction. mixer may be NULL to leave that profile's slot assignments
// untouched (e.g. a caller that only wants to persist macros/settings).
bool profile_store_save(profile_store_t *store, int64_t profile_id, const macro_map_t *macros,
                        device_settings_t *settings, mixer_state_t *mixer);

// Creates a new profile named `name` from the given live objects' current
// state and marks it active. Fails if a profile with that name already
// exists. *out_id (if non-NULL) receives the new profile's id.
bool profile_store_save_as(profile_store_t *store, const char *name, const macro_map_t *macros,
                           device_settings_t *settings, mixer_state_t *mixer, int64_t *out_id);

// Populates macros/settings from profile_id's stored rows, and -- if mixer
// is non-NULL -- calls mixer_state_set_pending_assignment() per stored
// slot assignment so each one reconnects to a same-named session
// immediately if already running, or automatically once it appears. Marks
// profile_id active.
bool profile_store_load(profile_store_t *store, int64_t profile_id, macro_map_t *macros,
                        device_settings_t *settings, mixer_state_t *mixer);

bool profile_store_rename(profile_store_t *store, int64_t profile_id, const char *new_name);

// Fails (false, no-op) if this would delete the last remaining profile.
bool profile_store_delete(profile_store_t *store, int64_t profile_id);
