#include "core/profile_store.h"
#include "protocol.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define PATH_SEP_CHAR '\\'
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#define PATH_SEP_CHAR '/'
#else
#include <limits.h>
#include <unistd.h>
#define PATH_SEP_CHAR '/'
#endif

struct profile_store {
    sqlite3 *db;
};

// PRAGMA foreign_keys is connection-scoped (not persisted in the file), so
// it's re-applied on every open, same as the CREATE TABLE IF NOT EXISTS
// statements below being safely re-run every time too.
static const char *SCHEMA_SQL =
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS profiles ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE IF NOT EXISTS profile_macro_labels ("
    "  profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
    "  switch_index INTEGER NOT NULL,"
    "  label TEXT NOT NULL,"
    "  PRIMARY KEY (profile_id, switch_index)"
    ");"
    "CREATE TABLE IF NOT EXISTS profile_gui_layout ("
    "  profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
    "  position INTEGER NOT NULL,"
    "  component_id INTEGER NOT NULL,"
    "  PRIMARY KEY (profile_id, position)"
    ");"
    "CREATE TABLE IF NOT EXISTS profile_macros ("
    "  profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
    "  trigger_id INTEGER NOT NULL,"
    "  action_type INTEGER NOT NULL,"
    "  target_slot INTEGER NOT NULL,"
    "  PRIMARY KEY (profile_id, trigger_id)"
    ");"
    "CREATE TABLE IF NOT EXISTS profile_macro_steps ("
    "  profile_id INTEGER NOT NULL,"
    "  trigger_id INTEGER NOT NULL,"
    "  step_index INTEGER NOT NULL,"
    "  modifiers INTEGER NOT NULL,"
    "  key INTEGER NOT NULL,"
    "  PRIMARY KEY (profile_id, trigger_id, step_index),"
    "  FOREIGN KEY (profile_id, trigger_id)"
    "    REFERENCES profile_macros(profile_id, trigger_id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS profile_slot_assignments ("
    "  profile_id INTEGER NOT NULL REFERENCES profiles(id) ON DELETE CASCADE,"
    "  slot_index INTEGER NOT NULL,"
    "  session_name TEXT NOT NULL,"
    "  PRIMARY KEY (profile_id, slot_index)"
    ");"
    "CREATE TABLE IF NOT EXISTS app_settings ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL"
    ");";

// --- tiny statement helpers ------------------------------------------------

static bool exec_ints(sqlite3 *db, const char *sql, const int64_t *values, int count)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        sqlite3_bind_int64(stmt, i + 1, values[i]);
    }
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static bool exec_ints_and_text(sqlite3 *db, const char *sql, const int64_t *values, int count,
                               const char *text)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    int i = 0;
    for (; i < count; i++) {
        sqlite3_bind_int64(stmt, i + 1, values[i]);
    }
    sqlite3_bind_text(stmt, i + 1, text, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static bool delete_profile_rows(sqlite3 *db, const char *table, int64_t profile_id)
{
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE profile_id = ?;", table);
    return exec_ints(db, sql, &profile_id, 1);
}

static bool set_active_id(profile_store_t *store, int64_t id)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db,
                           "INSERT INTO app_settings (key, value) VALUES ('active_profile_id', ?1) "
                           "ON CONFLICT(key) DO UPDATE SET value = ?1;",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)id);
    sqlite3_bind_text(stmt, 1, buf, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// --- public API -------------------------------------------------------------

bool profile_store_default_path(char *out, size_t out_size)
{
#if defined(_WIN32)
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
    if (len == 0 || len == sizeof(path)) {
        return false;
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return false;
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len < 0) {
        return false;
    }
    path[len] = '\0';
#endif

    char *slash = strrchr(path, PATH_SEP_CHAR);
    if (!slash) {
        return false;
    }
    *slash = '\0';

    int n = snprintf(out, out_size, "%s%ctinypad.db", path, PATH_SEP_CHAR);
    return n > 0 && (size_t)n < out_size;
}

bool profile_store_save_as(profile_store_t *store, const char *name, const macro_map_t *macros,
                           device_settings_t *settings, mixer_state_t *mixer, int64_t *out_id)
{
    if (!store || !name || !name[0] || !macros || !settings) {
        return false;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "INSERT INTO profiles (name) VALUES (?);", -1, &stmt, NULL) !=
        SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return false; // e.g. a profile with that name already exists (UNIQUE)
    }

    int64_t new_id = sqlite3_last_insert_rowid(store->db);
    bool ok = profile_store_save(store, new_id, macros, settings, mixer);
    if (ok) {
        set_active_id(store, new_id);
        if (out_id) {
            *out_id = new_id;
        }
    }
    return ok;
}

profile_store_t *profile_store_open(const char *db_path)
{
    if (!db_path) {
        return NULL;
    }

    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "profile_store: failed to open '%s': %s\n", db_path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }

    char *err = NULL;
    if (sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "profile_store: schema init failed: %s\n", err ? err : "(unknown error)");
        sqlite3_free(err);
        sqlite3_close(db);
        return NULL;
    }

    profile_store_t *store = calloc(1, sizeof(profile_store_t));
    if (!store) {
        sqlite3_close(db);
        return NULL;
    }
    store->db = db;

    sqlite3_stmt *count_stmt;
    int profile_count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM profiles;", -1, &count_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            profile_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }

    if (profile_count == 0) {
        // Seed "Default" from the *actual* in-code defaults (rather than
        // hand-duplicating them here), so the seed can never drift out of
        // sync with macro_map_create()/device_settings_create() as those
        // evolve.
        macro_map_t *default_macros = macro_map_create();
        device_settings_t *default_settings = device_settings_create();
        if (default_macros && default_settings) {
            profile_store_save_as(store, "Default", default_macros, default_settings, NULL, NULL);
        }
        macro_map_destroy(default_macros);
        device_settings_destroy(default_settings);
    }

    return store;
}

void profile_store_close(profile_store_t *store)
{
    if (!store) {
        return;
    }
    sqlite3_close(store->db);
    free(store);
}

size_t profile_store_list(profile_store_t *store, profile_summary_t *out, size_t max_out)
{
    if (!store) {
        return 0;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "SELECT id, name FROM profiles ORDER BY id;", -1, &stmt, NULL) !=
        SQLITE_OK) {
        return 0;
    }

    size_t n = 0;
    while (n < max_out && sqlite3_step(stmt) == SQLITE_ROW) {
        out[n].id = sqlite3_column_int64(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        snprintf(out[n].name, sizeof(out[n].name), "%s", name ? name : "");
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

bool profile_store_get_active_id(profile_store_t *store, int64_t *out_id)
{
    if (!store) {
        return false;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "SELECT value FROM app_settings WHERE key = 'active_profile_id';",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        if (val) {
            *out_id = strtoll(val, NULL, 10);
            ok = true;
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool profile_store_save(profile_store_t *store, int64_t profile_id, const macro_map_t *macros,
                        device_settings_t *settings, mixer_state_t *mixer)
{
    if (!store || !macros || !settings) {
        return false;
    }

    sqlite3_exec(store->db, "BEGIN;", NULL, NULL, NULL);

    bool ok = delete_profile_rows(store->db, "profile_macro_labels", profile_id) &&
             delete_profile_rows(store->db, "profile_gui_layout", profile_id) &&
             delete_profile_rows(store->db, "profile_macros", profile_id); // cascades _steps
    if (mixer) {
        ok = ok && delete_profile_rows(store->db, "profile_slot_assignments", profile_id);
    }

    for (int i = 0; i < MACRO_BUTTON_COUNT && ok; i++) {
        char label[MACRO_LABEL_LEN];
        device_settings_get_macro_label(settings, i, label, sizeof(label));
        int64_t values[2] = {profile_id, i};
        ok = exec_ints_and_text(
            store->db, "INSERT INTO profile_macro_labels (profile_id, switch_index, label) VALUES (?,?,?);",
            values, 2, label);
    }

    uint8_t layout[GUI_COMPONENT_COUNT];
    device_settings_get_gui_layout(settings, layout);
    for (int i = 0; i < GUI_COMPONENT_COUNT && ok; i++) {
        int64_t values[3] = {profile_id, i, layout[i]};
        ok = exec_ints(store->db,
                       "INSERT INTO profile_gui_layout (profile_id, position, component_id) VALUES (?,?,?);",
                       values, 3);
    }

    for (int t = 0; t < MACRO_TRIGGER_COUNT && ok; t++) {
        macro_action_t action = macro_map_get(macros, (macro_trigger_t)t);
        int64_t values[4] = {profile_id, t, action.type, action.target_slot};
        ok = exec_ints(
            store->db,
            "INSERT INTO profile_macros (profile_id, trigger_id, action_type, target_slot) VALUES (?,?,?,?);",
            values, 4);
        for (int s = 0; s < action.step_count && ok; s++) {
            int64_t step_values[5] = {profile_id, t, s, action.steps[s].modifiers,
                                      (int64_t)action.steps[s].key};
            ok = exec_ints(store->db,
                          "INSERT INTO profile_macro_steps "
                          "(profile_id, trigger_id, step_index, modifiers, key) VALUES (?,?,?,?,?);",
                          step_values, 5);
        }
    }

    if (mixer) {
        for (int i = 0; i < MIXER_CHANNELS && ok; i++) {
            mixer_slot_t slot;
            if (mixer_state_get_slot(mixer, i, &slot) && slot.assigned) {
                int64_t values[2] = {profile_id, i};
                ok = exec_ints_and_text(
                    store->db,
                    "INSERT INTO profile_slot_assignments (profile_id, slot_index, session_name) VALUES (?,?,?);",
                    values, 2, slot.name);
            }
        }
    }

    sqlite3_exec(store->db, ok ? "COMMIT;" : "ROLLBACK;", NULL, NULL, NULL);
    return ok;
}

bool profile_store_load(profile_store_t *store, int64_t profile_id, macro_map_t *macros,
                        device_settings_t *settings, mixer_state_t *mixer)
{
    if (!store || !macros || !settings) {
        return false;
    }

    sqlite3_stmt *check;
    if (sqlite3_prepare_v2(store->db, "SELECT 1 FROM profiles WHERE id = ?;", -1, &check, NULL) !=
        SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(check, 1, profile_id);
    bool exists = sqlite3_step(check) == SQLITE_ROW;
    sqlite3_finalize(check);
    if (!exists) {
        return false;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db,
                           "SELECT switch_index, label FROM profile_macro_labels WHERE profile_id = ?;",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, profile_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int idx = sqlite3_column_int(stmt, 0);
            const char *label = (const char *)sqlite3_column_text(stmt, 1);
            device_settings_set_macro_label(settings, idx, label ? label : "");
        }
        sqlite3_finalize(stmt);
    }

    uint8_t layout[GUI_COMPONENT_COUNT];
    memset(layout, GUI_COMPONENT_NONE, sizeof(layout));
    if (sqlite3_prepare_v2(store->db,
                           "SELECT position, component_id FROM profile_gui_layout WHERE profile_id = ?;",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, profile_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int pos = sqlite3_column_int(stmt, 0);
            int comp = sqlite3_column_int(stmt, 1);
            if (pos >= 0 && pos < GUI_COMPONENT_COUNT) {
                layout[pos] = (uint8_t)comp;
            }
        }
        sqlite3_finalize(stmt);
    }
    device_settings_set_gui_layout(settings, layout);

    if (sqlite3_prepare_v2(store->db,
                           "SELECT trigger_id, action_type, target_slot FROM profile_macros WHERE profile_id = ?;",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, profile_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int trigger = sqlite3_column_int(stmt, 0);
            macro_action_t action = {0};
            action.type = (macro_action_type_t)sqlite3_column_int(stmt, 1);
            action.target_slot = sqlite3_column_int(stmt, 2);

            sqlite3_stmt *steps;
            if (sqlite3_prepare_v2(store->db,
                                   "SELECT modifiers, key FROM profile_macro_steps "
                                   "WHERE profile_id = ? AND trigger_id = ? ORDER BY step_index;",
                                   -1, &steps, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(steps, 1, profile_id);
                sqlite3_bind_int(steps, 2, trigger);
                while (action.step_count < MACRO_KEYSTROKE_MAX_STEPS &&
                      sqlite3_step(steps) == SQLITE_ROW) {
                    action.steps[action.step_count].modifiers = (uint32_t)sqlite3_column_int(steps, 0);
                    action.steps[action.step_count].key = (macro_key_t)sqlite3_column_int(steps, 1);
                    action.step_count++;
                }
                sqlite3_finalize(steps);
            }

            if (trigger >= 0 && trigger < MACRO_TRIGGER_COUNT) {
                macro_map_set(macros, (macro_trigger_t)trigger, action);
            }
        }
        sqlite3_finalize(stmt);
    }

    if (mixer) {
        for (int i = 0; i < MIXER_CHANNELS; i++) {
            mixer_state_clear_slot(mixer, i);
        }
        if (sqlite3_prepare_v2(store->db,
                               "SELECT slot_index, session_name FROM profile_slot_assignments WHERE profile_id = ?;",
                               -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, profile_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int slot = sqlite3_column_int(stmt, 0);
                const char *name = (const char *)sqlite3_column_text(stmt, 1);
                if (slot >= 0 && slot < MIXER_CHANNELS && name) {
                    mixer_state_set_pending_assignment(mixer, slot, name);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    set_active_id(store, profile_id);
    return true;
}

bool profile_store_rename(profile_store_t *store, int64_t profile_id, const char *new_name)
{
    if (!store || !new_name || !new_name[0]) {
        return false;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "UPDATE profiles SET name = ? WHERE id = ?;", -1, &stmt, NULL) !=
        SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, profile_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(store->db) > 0;
}

bool profile_store_delete(profile_store_t *store, int64_t profile_id)
{
    if (!store) {
        return false;
    }

    sqlite3_stmt *count_stmt;
    int count = 0;
    if (sqlite3_prepare_v2(store->db, "SELECT COUNT(*) FROM profiles;", -1, &count_stmt, NULL) ==
        SQLITE_OK) {
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    if (count <= 1) {
        return false; // refuse to delete the last remaining profile
    }

    int64_t active_id = 0;
    bool had_active = profile_store_get_active_id(store, &active_id);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(store->db, "DELETE FROM profiles WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, profile_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    bool ok = rc == SQLITE_DONE && sqlite3_changes(store->db) > 0;

    if (ok && had_active && active_id == profile_id) {
        sqlite3_stmt *fallback;
        if (sqlite3_prepare_v2(store->db, "SELECT id FROM profiles ORDER BY id LIMIT 1;", -1, &fallback,
                               NULL) == SQLITE_OK) {
            if (sqlite3_step(fallback) == SQLITE_ROW) {
                set_active_id(store, sqlite3_column_int64(fallback, 0));
            }
            sqlite3_finalize(fallback);
        }
    }
    return ok;
}
