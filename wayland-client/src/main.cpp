#include <wayland-client.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <algorithm>
#include <set>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <malloc.h>
#include <functional>

static void log_to_file(const std::string& msg) {
    static const bool enabled = [] {
        const char* value = getenv("UNIKEY_WAYLAND_DEBUG");
        return value && strcmp(value, "0") != 0;
    }();
    if (!enabled) return;

    std::ofstream f("/tmp/uk_debug.log", std::ios::app);
    if (f.is_open()) {
        f << msg << std::endl;
    }
}

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include "config.h"
#include "control.h"
#include "trayicon.h"

#include "input-method-unstable-v1-client-protocol.h"
// No ukengine_wrapper needed
#include "windowtracker.h"
#include "libbamboo.h"
#include "text_transaction.h"

struct QueuedKeyEvent {
    uint32_t serial;
    uint32_t time;
    uint32_t key;
    uint32_t state;
    uint32_t modifiers;
};

struct PendingEdit {
    bool active = false;
    // Key-event edits keep their order in the client, so later keys need not
    // wait; only their intermediate surrounding-text reports are skipped.
    bool blocks_input = true;
    uint64_t generation = 0;
    uint64_t timeout_token = 0;
    uint64_t repair_token = 0;
    std::string expected_tail;
    std::string failure_tail;
    std::string desired_text;
    size_t failure_bytes = 0;
    size_t repair_bytes = 0;
    std::string repair_text;
    SurroundingSnapshot observed_failure;
    unsigned repair_attempts = 0;
};

struct WaylandState {
    wl_display* display;
    wl_registry* registry;
    wl_seat* seat;
    zwp_input_method_v1* input_method;
    zwp_input_method_context_v1* context;
    wl_keyboard* keyboard;
    
    bool viet_mode = true;
    bool active;
    uint32_t latest_serial;
    std::string composed_word = "";
    uint32_t content_purpose = 0;
    
    std::string surrounding_text = "";
    uint32_t surrounding_cursor = 0;
    uint32_t surrounding_anchor = 0;
    bool has_surrounding_text = false;
    SurroundingSnapshot composition_start;
    PendingEdit pending_edit;
    std::deque<QueuedKeyEvent> queued_keys;
    std::deque<std::string> recent_tails;
    bool draining_keys = false;
    QTimer* drain_timer = nullptr;
    uint64_t next_edit_generation = 0;
    uint64_t next_timer_token = 0;
};

// Evdev keycodes map
static char get_ascii_from_keycode(uint32_t key, uint32_t mods) {
    bool shift = (mods & 1); // Shift check
    bool capslock = (mods & 2); // CapsLock check
    bool uppercase = shift ^ capslock; // XOR shift and capslock for letter case

    if (key >= 2 && key <= 10) {
        const char* symbols = "!@#$%^&*(";
        return shift ? symbols[key-2] : '1' + (key-2);
    }
    if (key == 11) return shift ? ')' : '0';
    if (key == 12) return shift ? '_' : '-';
    if (key == 13) return shift ? '+' : '=';
    if (key == 41) return shift ? '~' : '`';
    
    if (key >= 16 && key <= 25) {
        const char* row1 = "qwertyuiop";
        return uppercase ? (row1[key-16] - 32) : row1[key-16];
    }
    if (key >= 30 && key <= 38) {
        const char* row2 = "asdfghjkl";
        return uppercase ? (row2[key-30] - 32) : row2[key-30];
    }
    if (key >= 44 && key <= 50) {
        const char* row3 = "zxcvbnm";
        return uppercase ? (row3[key-44] - 32) : row3[key-44];
    }
    
    if (key == 57) return ' '; // Space
    if (key == 14) return '\b'; // Backspace
    if (key == 28) return '\n'; // Enter
    if (key == 26) return shift ? '{' : '[';
    if (key == 27) return shift ? '}' : ']';
    if (key == 43) return shift ? '|' : '\\';
    if (key == 39) return shift ? ':' : ';';
    if (key == 40) return shift ? '"' : '\'';
    if (key == 51) return shift ? '<' : ',';
    if (key == 52) return shift ? '>' : '.';
    if (key == 53) return shift ? '?' : '/';
    
    return 0; // Unhandled
}

static uint32_t g_modifiers = 0;
static std::set<uint32_t> eaten_keys;

static bool g_ctrl_pressed = false;
static bool g_shift_pressed = false;
static bool g_alt_pressed = false;
static bool g_other_pressed = false;

WindowTracker* g_windowTracker = nullptr;
static UkConfig g_config;
// Updates the tray, the OSD and the saved mode; set by main().
static std::function<void(bool)> g_viet_mode_changed;

static bool g_app_excluded = false;
static bool g_terminal_app = false;
static bool g_key_event_app = false;

static constexpr uint32_t kBackspaceKey = 14;

// Restarted by every key; when typing pauses, freed memory goes back to the OS.
static QTimer* g_idle_timer = nullptr;

static void reset_composition(WaylandState* state, bool clear_client_preedit = false) {
    Bamboo_Reset();
    if (state) {
        state->composed_word.clear();
        state->composition_start = {};
    }
}

static bool has_fresh_surrounding(const WaylandState* state) {
    return state->has_surrounding_text && !state->pending_edit.active;
}

static bool input_blocked(const WaylandState* state) {
    return state->pending_edit.active && state->pending_edit.blocks_input;
}

static void clear_pending_edit(WaylandState* state) {
    state->pending_edit = {};
}

static void remember_committed_tail(WaylandState* state,
                                    const std::string& tail) {
    if (tail.empty()) return;
    state->recent_tails.push_back(tail);
    if (state->recent_tails.size() > 32) state->recent_tails.pop_front();
}

static void drain_queued_keys(WaylandState* state);

static void arm_pending_timeout(WaylandState* state) {
    const uint64_t generation = state->pending_edit.generation;
    const uint64_t token = ++state->next_timer_token;
    state->pending_edit.timeout_token = token;
    QTimer::singleShot(750, QCoreApplication::instance(),
                       [state, generation, token]() {
        if (!state->pending_edit.active ||
            state->pending_edit.generation != generation ||
            state->pending_edit.timeout_token != token) {
            return;
        }
        // A client that never reports surrounding text must not stall input.
        clear_pending_edit(state);
        state->has_surrounding_text = false;
        reset_composition(state);
        drain_queued_keys(state);
    });
}

static void schedule_queued_keys(WaylandState* state) {
    if (!input_blocked(state) && !state->queued_keys.empty() &&
        state->drain_timer && !state->drain_timer->isActive()) {
        state->drain_timer->start();
    }
}

static void begin_composition(WaylandState* state) {
    if (state->composed_word.empty() && state->has_surrounding_text &&
        state->surrounding_cursor == state->surrounding_anchor &&
        state->surrounding_cursor <= state->surrounding_text.size()) {
        state->composition_start = {state->surrounding_text,
                                    state->surrounding_cursor,
                                    state->surrounding_anchor, true};
    }
}

static SurroundingSnapshot current_surrounding(const WaylandState* state) {
    if (!state->has_surrounding_text) return {};
    return {state->surrounding_text, state->surrounding_cursor,
            state->surrounding_anchor, true};
}

static void predict_surrounding(WaylandState* state,
                                const SurroundingSnapshot& expected) {
    if (!expected.valid) return;
    state->surrounding_text = expected.text;
    state->surrounding_cursor = expected.cursor;
    state->surrounding_anchor = expected.anchor;
}

static void replace_native_composition(WaylandState* state,
                                       const std::string& new_composition,
                                       const std::string& trailing_text = "") {
    const std::string old_composition = state->composed_word;
    const size_t common = utf8_common_prefix_bytes(old_composition, new_composition);
    const size_t old_tail_bytes = old_composition.size() - common;
    const std::string suffix = new_composition.substr(common) + trailing_text;

    if (old_tail_bytes == 0 && suffix.empty()) {
        state->composed_word = new_composition;
        return;
    }

    if (!new_composition.empty()) begin_composition(state);

    SurroundingReplacement replacement;
    if (state->has_surrounding_text) {
        replacement = make_surrounding_replacement(old_tail_bytes,
                                                   state->surrounding_text,
                                                   state->surrounding_cursor,
                                                   state->surrounding_anchor);
    } else {
        replacement.index = -static_cast<int32_t>(old_tail_bytes);
        replacement.length = static_cast<uint32_t>(old_tail_bytes);
    }

    const auto expected = apply_surrounding_replacement(
        state->surrounding_text, state->surrounding_cursor,
        state->surrounding_anchor, replacement, suffix);

    if (old_tail_bytes > 0) {
        zwp_input_method_context_v1_delete_surrounding_text(
            state->context, replacement.index, replacement.length);
    }

    // An empty commit still applies the pending deletion.
    zwp_input_method_context_v1_commit_string(
        state->context, state->latest_serial, suffix.c_str());
    remember_committed_tail(state, new_composition + trailing_text);

    if (expected.valid) {
        state->surrounding_text = expected.text;
        state->surrounding_cursor = expected.cursor;
        state->surrounding_anchor = expected.anchor;
    }

    if (old_tail_bytes > 0 && expected.valid) {
        state->pending_edit.active = true;
        state->pending_edit.generation = ++state->next_edit_generation;
        state->pending_edit.expected_tail = new_composition + trailing_text;
        state->pending_edit.desired_text = new_composition + trailing_text;
        state->pending_edit.failure_tail = old_composition + suffix;
        state->pending_edit.failure_bytes =
            old_composition.size() + suffix.size();
        state->pending_edit.repair_attempts = 0;
        arm_pending_timeout(state);
    }
    state->composed_word = new_composition;
}

static bool repair_failed_edit(WaylandState* state,
                               const std::string& text,
                               uint32_t cursor,
                               uint32_t anchor) {
    if (state->pending_edit.repair_attempts >= 3 ||
        cursor > text.size() || anchor > text.size()) {
        return false;
    }

    const auto replacement = make_surrounding_replacement(
        state->pending_edit.repair_bytes, text, cursor, anchor);
    if (!replacement.uses_surrounding) return false;

    zwp_input_method_context_v1_delete_surrounding_text(
        state->context, replacement.index, replacement.length);
    zwp_input_method_context_v1_commit_string(
        state->context, state->latest_serial,
        state->pending_edit.repair_text.c_str());
    remember_committed_tail(state, state->pending_edit.repair_text);

    const auto expected = apply_surrounding_replacement(
        text, cursor, anchor, replacement, state->pending_edit.repair_text);
    if (expected.valid) {
        state->surrounding_text = expected.text;
        state->surrounding_cursor = expected.cursor;
        state->surrounding_anchor = expected.anchor;
    }
    state->pending_edit.failure_tail += state->pending_edit.repair_text;
    state->pending_edit.failure_bytes += state->pending_edit.repair_text.size();
    ++state->pending_edit.repair_attempts;
    arm_pending_timeout(state);
    return true;
}

static void arm_failed_edit_repair(WaylandState* state) {
    const uint64_t generation = state->pending_edit.generation;
    const uint64_t token = ++state->next_timer_token;
    const SurroundingSnapshot failure = state->pending_edit.observed_failure;
    state->pending_edit.repair_token = token;
    QTimer::singleShot(12, QCoreApplication::instance(),
                       [state, generation, token, failure]() {
        if (!state->pending_edit.active ||
            state->pending_edit.generation != generation ||
            state->pending_edit.repair_token != token || !failure.valid) {
            return;
        }
        repair_failed_edit(state, failure.text,
                           failure.cursor, failure.anchor);
        // Timer callbacks run outside the Wayland read notifier.
        wl_display_flush(state->display);
    });
}

// Chromium dispatches keydown only for keys that reach it as key events; a
// commit alone becomes keypress/input. Pages that check typing against keydown
// (10FastFingers) discard such text. Chromium, and so every Electron app, keeps
// wl_keyboard keys and text-input commits in order, so send what a typist
// would: the key itself, or Backspace followed by the new characters.
static bool use_key_events(const WaylandState* state) {
    // URL fields complete inline; keep the selection-aware replacement there.
    return g_key_event_app && state->content_purpose != 5 &&
           !(state->has_surrounding_text &&
             state->surrounding_cursor != state->surrounding_anchor);
}

static void expect_key_event_text(WaylandState* state, const std::string& tail) {
    remember_committed_tail(state, tail);
    if (state->pending_edit.active && !state->pending_edit.blocks_input) {
        state->pending_edit.expected_tail = tail;
        arm_pending_timeout(state);
    }
}

static void forward_key_text(WaylandState* state, uint32_t serial,
                             uint32_t time, uint32_t key, uint32_t key_state,
                             char c) {
    zwp_input_method_context_v1_key(state->context, serial, time, key, key_state);
    predict_surrounding(state, apply_forwarded_key(current_surrounding(state), c));
}

static void replace_with_key_events(WaylandState* state,
                                    const std::string& next,
                                    uint32_t serial, uint32_t time) {
    const std::string old = state->composed_word;
    const size_t common = utf8_common_prefix_bytes(old, next);
    const std::string removed = old.substr(common);
    const std::string inserted = next.substr(common);
    if (removed.empty() && inserted.empty()) return;
    if (!next.empty()) begin_composition(state);

    for (size_t i = utf8_codepoint_count(removed); i > 0; --i) {
        zwp_input_method_context_v1_key(state->context, serial, time,
                                        kBackspaceKey, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_input_method_context_v1_key(state->context, serial, time,
                                        kBackspaceKey, WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    if (!inserted.empty()) {
        zwp_input_method_context_v1_commit_string(
            state->context, state->latest_serial, inserted.c_str());
    }

    const auto snapshot = current_surrounding(state);
    SurroundingSnapshot expected;
    if (snapshot.valid) {
        expected = apply_surrounding_replacement(
            snapshot.text, snapshot.cursor, snapshot.anchor,
            make_surrounding_replacement(removed.size(), snapshot.text,
                                         snapshot.cursor, snapshot.anchor),
            inserted);
    }
    predict_surrounding(state, expected);
    state->composed_word = next;
    if (!removed.empty() && expected.valid) {
        // The client reports the text between the Backspaces and the commit.
        state->pending_edit = {};
        state->pending_edit.active = true;
        state->pending_edit.blocks_input = false;
        state->pending_edit.generation = ++state->next_edit_generation;
    }
    expect_key_event_text(state, next);
}

static void type_with_key_events(WaylandState* state, const std::string& next,
                                 char c, uint32_t serial, uint32_t time,
                                 uint32_t key, uint32_t key_state) {
    if (utf8_common_prefix_bytes(state->composed_word, next) <
        state->composed_word.size()) {
        // The key edits earlier text; its Backspaces provide the keydown.
        replace_with_key_events(state, next, serial, time);
        eaten_keys.insert(key);
        return;
    }
    // Let the client insert the key, then fix a mapped key (Telex w -> ư).
    begin_composition(state);
    forward_key_text(state, serial, time, key, key_state, c);
    state->composed_word.push_back(c);
    if (state->composed_word != next) {
        replace_with_key_events(state, next, serial, time);
    } else {
        expect_key_event_text(state, next);
    }
}

// Chromium and Electron map their resource pack; Qt WebEngine ships its own,
// and Qt reorders key events against commits, so it must not match.
static bool maps_show_chromium(std::istream& maps) {
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("/chrome_100_percent.pak") != std::string::npos) return true;
    }
    return false;
}

static bool is_chromium_process(long pid) {
    if (pid <= 0) return false;
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    return maps && maps_show_chromium(maps);
}

static std::string bamboo_string(bool final) {
    char* value = final ? Bamboo_GetCommitString() : Bamboo_GetPreeditString();
    std::string result = value ? value : "";
    if (value) free(value);
    return result;
}

static void set_viet_mode(WaylandState* state, bool viet) {
    state->viet_mode = viet;
    if (g_viet_mode_changed) g_viet_mode_changed(viet);
}

// The settings window is a separate program, so the input method never loads
// a widget toolkit and only costs memory while the window is open.
static QString settings_program() {
    const QString local = QCoreApplication::applicationDirPath() + "/unikey-wayland-settings";
    return QFileInfo::exists(local) ? local : QStringLiteral("unikey-wayland-settings");
}

static bool show_settings_window(const QStringList& args = {}) {
    return QProcess::startDetached(settings_program(), args);
}

static void keyboard_keymap(void* data, struct wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size) {
    close(fd);
}

static void keyboard_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {}
static void keyboard_leave(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface) {}

static void process_keyboard_key(WaylandState* state, uint32_t serial,
                                 uint32_t time, uint32_t key,
                                 uint32_t state_key, uint32_t modifiers) {
    static bool g_switched = false;

    // Track modifier key states
    if (state_key == 1) { // Pressed
        if (key == 29 || key == 97) { // Left/Right Ctrl
            g_ctrl_pressed = true;
            if (!g_shift_pressed && !g_alt_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 42 || key == 54) { // Left/Right Shift
            g_shift_pressed = true;
            if (!g_ctrl_pressed && !g_alt_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 56 || key == 100) { // Left/Right Alt
            g_alt_pressed = true;
        } else {
            g_other_pressed = true;
        }
    } else if (state_key == 0) { // Released
        const int switchKeyConfig = g_config.switchKey;
        if (key == 29 || key == 97) {
            if (switchKeyConfig == 0 && g_ctrl_pressed && g_shift_pressed && !g_other_pressed && !g_switched) {
                reset_composition(state, true);
                set_viet_mode(state, !state->viet_mode);
                g_switched = true;
            }
            g_ctrl_pressed = false;
            if (!g_shift_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 42 || key == 54) {
            if (switchKeyConfig == 0 && g_ctrl_pressed && g_shift_pressed && !g_other_pressed && !g_switched) {
                reset_composition(state, true);
                set_viet_mode(state, !state->viet_mode);
                g_switched = true;
            }
            g_shift_pressed = false;
            if (!g_ctrl_pressed) {
                g_other_pressed = false;
                g_switched = false;
            }
        } else if (key == 56 || key == 100) {
            g_alt_pressed = false;
            g_other_pressed = false;
        }
    }



    // Alt + Z hotkey
    const int switchKeyConfig = g_config.switchKey;
    bool real_alt_pressed = (modifiers & 8) != 0; // Better check to avoid sticky Alt bug
    if (switchKeyConfig == 1 && key == 44 && real_alt_pressed && state_key == 1) {
        reset_composition(state, true);
        set_viet_mode(state, !state->viet_mode);
        eaten_keys.insert(key);
        return;
    }

    // Ctrl + Shift + F5 (CS+F5) hotkey to show settings
    if (key == 63 && g_ctrl_pressed && g_shift_pressed && state_key == 1) {
        show_settings_window();
        eaten_keys.insert(key);
        return;
    }

    if (state_key == 0) {
        if (eaten_keys.count(key)) {
            eaten_keys.erase(key);
            return; // Key release for an eaten key: drop it
        }
        // Key release: forward it so the app doesn't get stuck
        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
        return;
    }
    
    bool has_modifiers = (modifiers & (4 | 8 | 64)) != 0;
    char c = has_modifiers ? 0 : get_ascii_from_keycode(key, modifiers);
    
    std::stringstream ss_key;
    ss_key << "DEBUG: Key received. code=" << key << ", state=" << state_key 
           << ", ascii=" << (c ? c : '?');
    log_to_file(ss_key.str());
    
    std::stringstream ss_viet;
    ss_viet << "DEBUG: viet_mode=" << state->viet_mode;
    log_to_file(ss_viet.str());

    if (!state->viet_mode || g_terminal_app || state->content_purpose == 12) {
        log_to_file("DEBUG: Forwarding in E mode");
        reset_composition(state, true);
        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
        return;
    }

    // Let the client own both Backspace press and release, including repeat.
    if (key == 14) {
        reset_composition(state);
        state->recent_tails.clear();
        state->has_surrounding_text = false;
        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
        return;
    }

    if (c != 0) {
        {
            // Normal Mode (Chrome, Gtk, Qt apps) - Use Bamboo Diffing
            if (has_fresh_surrounding(state) &&
                !composition_matches_surrounding(state->surrounding_text,
                                                 state->surrounding_cursor,
                                                 state->surrounding_anchor,
                                                 state->composed_word)) {
                reset_composition(state);
            }

            const bool key_events = use_key_events(state);
            if (!Bamboo_CanProcessKey(c)) {
                std::string final_word = bamboo_string(true);
                const bool had_composition = !state->composed_word.empty();
                
                // Gõ tắt (Macro)
                if (g_config.macroEnabled) {
                    const auto& macros = g_config.macros;
                    auto macro = macros.find(final_word);
                    if (macro != macros.end()) {
                        final_word = macro->second;
                    }
                }

                if (key_events) {
                    if (had_composition) {
                        replace_with_key_events(state, final_word, serial, time);
                    }
                    reset_composition(state);
                    if (c == '\n') {
                        // Enter may submit or clear the field.
                        clear_pending_edit(state);
                        zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
                        return;
                    }
                    forward_key_text(state, serial, time, key, state_key, c);
                    if (had_composition) {
                        expect_key_event_text(state, final_word + c);
                    }
                    return;
                }

                if (had_composition && c != '\n') {
                    replace_native_composition(state, final_word, std::string(1, c));
                    reset_composition(state);
                    eaten_keys.insert(key);
                    return;
                }

                replace_native_composition(state, final_word);
                reset_composition(state);
                zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
                return;
            } else {
                Bamboo_ProcessKey(c);
            }

            if (key_events) {
                type_with_key_events(state, bamboo_string(false), c, serial,
                                     time, key, state_key);
                return;
            }
            replace_native_composition(state, bamboo_string(false));
            eaten_keys.insert(key);
            return;
        }
    } else {
        reset_composition(state);
        clear_pending_edit(state);
    }
    
    // If we didn't handle it (or if it was a backspace/unhandled), forward it to the client
    zwp_input_method_context_v1_key(state->context, serial, time, key, state_key);
}

static void route_keyboard_key(WaylandState* state, uint32_t serial,
                               uint32_t time, uint32_t key,
                               uint32_t state_key, uint32_t modifiers) {
    if (!state->active || !state->context) return;
    if (g_idle_timer) g_idle_timer->start();

    if (input_blocked(state) || !state->queued_keys.empty()) {
        // Preserve releases as well as presses in a delayed input batch.
        // The pending-edit timeout already recovers from a missing ACK.
        state->queued_keys.push_back({serial, time, key, state_key, modifiers});
        schedule_queued_keys(state);
        return;
    }

    process_keyboard_key(state, serial, time, key, state_key, modifiers);
}

static void drain_queued_keys(WaylandState* state) {
    if (state->draining_keys || input_blocked(state) ||
        state->queued_keys.empty()) return;
    state->draining_keys = true;
    const auto event = state->queued_keys.front();
    state->queued_keys.pop_front();
    process_keyboard_key(state, event.serial, event.time, event.key,
                         event.state, event.modifiers);
    // Deliver queued input even when no new Wayland event arrives.
    wl_display_flush(state->display);
    state->draining_keys = false;
    schedule_queued_keys(state);
}

static void keyboard_key(void* data, struct wl_keyboard* keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state_key) {
    route_keyboard_key(static_cast<WaylandState*>(data), serial, time, key,
                       state_key, g_modifiers);
}

static void keyboard_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    WaylandState* state = static_cast<WaylandState*>(data);
    g_modifiers = mods_depressed | mods_latched | mods_locked;
    
    if (mods_depressed == 0) {
        g_ctrl_pressed = false;
        g_shift_pressed = false;
        g_alt_pressed = false;
        g_other_pressed = false;
    }

    if (state->context) {
        zwp_input_method_context_v1_modifiers(state->context, serial, mods_depressed, mods_latched, mods_locked, group);
    }
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* keyboard, int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};


static void input_method_context_surrounding_text(void* data, struct zwp_input_method_context_v1* context, const char* text, uint32_t cursor, uint32_t anchor) {
    WaylandState* state = (WaylandState*)data;
    const std::string reported_text = text ? text : "";
    const bool was_pending = state->pending_edit.active;

    if (was_pending) {
        if (!state->pending_edit.failure_tail.empty() &&
            surrounding_prefix_ends_with(
                reported_text, cursor, anchor,
                state->pending_edit.failure_tail)) {
            state->pending_edit.repair_bytes =
                state->pending_edit.failure_bytes;
            state->pending_edit.repair_text =
                state->pending_edit.desired_text;
            state->pending_edit.observed_failure = {
                reported_text, cursor, anchor, true};
            arm_failed_edit_repair(state);
            return;
        }
        if (!surrounding_prefix_ends_with(
                reported_text, cursor, anchor,
                state->pending_edit.expected_tail)) {
            // KWin exposes the delete and commit as separate text-input-v3
            // transactions. Ignore the intermediate or an older callback.
            return;
        }
    } else if (!state->composed_word.empty() &&
               state->composition_start.valid &&
               cursor == anchor &&
               state->composition_start.text == reported_text) {
        // This text has not acknowledged the append, even if an older
        // cursor update moves it. Keep the prediction until fresh text/reset.
        return;
    } else if (!state->recent_tails.empty()) {
        for (size_t i = state->recent_tails.size(); i-- > 0;) {
            if (!surrounding_prefix_ends_with(
                    reported_text, cursor, anchor, state->recent_tails[i])) {
                continue;
            }
            if (i + 1 != state->recent_tails.size()) {
                // A commit callback can arrive after a newer key has already
                // been processed. Keep the newer optimistic caret state.
                return;
            }
            break;
        }
    }

    state->surrounding_text = reported_text;
    state->surrounding_cursor = cursor;
    state->surrounding_anchor = anchor;
    state->has_surrounding_text = true;
    state->composition_start = {};
    clear_pending_edit(state);
    drain_queued_keys(state);
}
static void input_method_context_reset(void* data, struct zwp_input_method_context_v1* context) {
    WaylandState* state = static_cast<WaylandState*>(data);
    if (state) {
        reset_composition(state);
        state->has_surrounding_text = false;
        clear_pending_edit(state);
        state->queued_keys.clear();
        state->recent_tails.clear();
        if (state->drain_timer) state->drain_timer->stop();
    }
}
static void input_method_context_content_type(void* data, struct zwp_input_method_context_v1* context, uint32_t hint, uint32_t purpose) {
    WaylandState* state = static_cast<WaylandState*>(data);
    if (state) {
        state->content_purpose = purpose;
        std::stringstream ss_ct;
        ss_ct << "DEBUG: content_type hint=" << hint << ", purpose=" << purpose;
        log_to_file(ss_ct.str());
    }
}
static void input_method_context_invoke_action(void* data, struct zwp_input_method_context_v1* context, uint32_t button, uint32_t index) {}

static void input_method_context_commit_state(void* data, struct zwp_input_method_context_v1* context, uint32_t serial) {
    WaylandState* state = static_cast<WaylandState*>(data);
    state->latest_serial = serial;
}

static void input_method_context_preferred_language(void* data, struct zwp_input_method_context_v1* context, const char* language) {}

static const struct zwp_input_method_context_v1_listener input_method_context_listener = {
    .surrounding_text = input_method_context_surrounding_text,
    .reset = input_method_context_reset,
    .content_type = input_method_context_content_type,
    .invoke_action = input_method_context_invoke_action,
    .commit_state = input_method_context_commit_state,
    .preferred_language = input_method_context_preferred_language,
};


static void input_method_activate(void* data, struct zwp_input_method_v1* input_method, struct zwp_input_method_context_v1* context) {
    log_to_file("DEBUG: input_method_activate triggered by KWin!");
    WaylandState* state = static_cast<WaylandState*>(data);
    state->active = true;

    g_ctrl_pressed = false;
    g_shift_pressed = false;
    g_alt_pressed = false;
    g_other_pressed = false;
    eaten_keys.clear();
    state->queued_keys.clear();
    state->recent_tails.clear();
    if (state->drain_timer) state->drain_timer->stop();
    reset_composition(state);
    state->content_purpose = 0;
    state->has_surrounding_text = false;
    clear_pending_edit(state);

    if (state->keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state->keyboard);
        state->keyboard = nullptr;
    }

    if (state->context) {
        zwp_input_method_context_v1_destroy(state->context);
    }
    
    state->context = context;
    zwp_input_method_context_v1_add_listener(state->context, &input_method_context_listener, state);
    
    state->keyboard = zwp_input_method_context_v1_grab_keyboard(state->context);
    if (state->keyboard) {
        log_to_file("DEBUG: grab_keyboard succeeded! Adding keyboard listener.");
        wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
    } else {
        log_to_file("ERROR: grab_keyboard returned NULL!");
    }
}

static void input_method_deactivate(void* data, struct zwp_input_method_v1* input_method, struct zwp_input_method_context_v1* context) {
    WaylandState* state = static_cast<WaylandState*>(data);
    state->active = false;

    g_ctrl_pressed = false;
    g_shift_pressed = false;
    g_alt_pressed = false;
    g_other_pressed = false;
    eaten_keys.clear();
    state->queued_keys.clear();
    state->recent_tails.clear();
    if (state->drain_timer) state->drain_timer->stop();
    reset_composition(state);
    state->has_surrounding_text = false;
    clear_pending_edit(state);

    // Do not send wl_keyboard.release: when KWin deactivates a context before
    // our grab_keyboard arrives, it never creates the grab, and releasing that
    // id is a protocol error that disconnects the input method.
    if (state->keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state->keyboard);
        state->keyboard = nullptr;
    }
    
    if (state->context == context) {
        zwp_input_method_context_v1_destroy(state->context);
        state->context = nullptr;
    } else {
        zwp_input_method_context_v1_destroy(context);
    }
}

static const struct zwp_input_method_v1_listener input_method_listener = {
    .activate = input_method_activate,
    .deactivate = input_method_deactivate,
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    WaylandState* state = static_cast<WaylandState*>(data);
    
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 7));
    } else if (strcmp(interface, zwp_input_method_v1_interface.name) == 0) {
        state->input_method = static_cast<zwp_input_method_v1*>(wl_registry_bind(registry, name, &zwp_input_method_v1_interface, 1));
    }
}

static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static bool same_engine_config(const UkConfig& a, const UkConfig& b) {
    return a.method == b.method && a.freeMarking == b.freeMarking &&
           a.modernStyle == b.modernStyle && a.spellCheck == b.spellCheck &&
           a.autoRestore == b.autoRestore;
}

static void apply_engine_config() {
    Bamboo_SetInputMethod(g_config.method);
    Bamboo_SetOptions(g_config.freeMarking, g_config.modernStyle,
                      g_config.spellCheck, g_config.autoRestore);
}

static void show_mode_osd(bool viet) {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.plasmashell", "/org/kde/osdService", "org.kde.osdService", "showText");
    msg << QString("input-keyboard")
        << QString(viet ? "UniKey: Tiếng Việt [V]" : "UniKey: Tiếng Anh [E]");
    QDBusConnection::sessionBus().send(msg);
}

static QString kconfig_tool(const char* name) {
    for (const char* version : {"6", "5"}) {
        const QString tool = QString("%1%2").arg(name, version);
        if (!QStandardPaths::findExecutable(tool).isEmpty()) return tool;
    }
    return {};
}

// KWin restarts the input method only after a crash, so after "Thoát bộ gõ"
// it is started again the way System Settings does it: clear the configured
// input method and restore it, each change announced to KWin's config watcher.
static bool restart_input_method() {
    const QString reader = kconfig_tool("kreadconfig");
    const QString writer = kconfig_tool("kwriteconfig");
    if (reader.isEmpty() || writer.isEmpty()) return false;

    QProcess read;
    read.start(reader, {"--file", "kwinrc", "--group", "Wayland", "--key", "InputMethod"});
    if (!read.waitForFinished(3000)) return false;
    const QString inputMethod = QString::fromUtf8(read.readAllStandardOutput()).trimmed();
    // Leave another input method that the user picked alone.
    if (!inputMethod.contains("UnikeyWayland") && !inputMethod.contains("unikey-wayland")) {
        return false;
    }

    auto write = [&](const QString& value) {
        return QProcess::execute(writer, {"--file", "kwinrc", "--group", "Wayland",
                                          "--key", "InputMethod", "--notify", value}) == 0;
    };
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    for (int delay : {300, 1000}) {
        if (!write(QString())) {
            write(inputMethod);
            return false;
        }
        // KWin rereads the file when the notification arrives; let it see "".
        QThread::msleep(delay);
        if (!write(inputMethod)) return false;
        for (int i = 0; i < 30; ++i) {
            if (bus && bus->isServiceRegistered(UK_DBUS_SERVICE)) return true;
            QThread::msleep(100);
        }
    }
    return false;
}

// Started without KWin's input-method socket, e.g. from the application menu.
static int run_launcher(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QStringList windowArgs;
    if (args.contains("--exclude")) windowArgs << "--exclude";
    const bool wantsWindow = !windowArgs.isEmpty() || args.contains("--setup") ||
                             args.contains("--settings");

    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    const bool running = bus && bus->isServiceRegistered(UK_DBUS_SERVICE);
    if (!running && !wantsWindow) {
        if (restart_input_method()) {
            std::cout << "Unikey-Wayland đã được bật lại." << std::endl;
            return 0;
        }
        std::cerr << "Không bật lại được bộ gõ qua KWin; mở bảng điều khiển." << std::endl;
    }
    return show_settings_window(windowArgs) ? 0 : 1;
}

int main(int argc, char **argv) {
    // The Go runtime sizes itself before main runs. One processor keeps it to
    // a few MiB instead of per-CPU caches and threads, so start over with it.
    if (!getenv("GOMAXPROCS")) {
        setenv("GOMAXPROCS", "1", 1);
        // With transparent huge pages set to "always", the Go heap may be
        // faulted in 2 MiB at a time, which doubles the input method's size.
        const char* godebug = getenv("GODEBUG");
        const std::string debug = godebug && *godebug
            ? std::string(godebug) + ",disablethp=1" : "disablethp=1";
        setenv("GODEBUG", debug.c_str(), 1);
        // Exec the real path: the process is named after it (pkill, monitors).
        char self[4096];
        const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (length > 0) {
            self[length] = '\0';
            execv(self, argv);
        }
    }

    int im_socket_fd = -1;
    char* wayland_socket_env = getenv("WAYLAND_SOCKET");
    if (wayland_socket_env) {
        int orig_fd = atoi(wayland_socket_env);
        im_socket_fd = dup(orig_fd);
        unsetenv("WAYLAND_SOCKET");
    }
    if (im_socket_fd < 0) {
        return run_launcher(argc, argv);
    }

    QCoreApplication app(argc, argv);

    Bamboo_Init();
    g_config = uk_load_config();
    apply_engine_config();

    WaylandState state = {};
    state.viet_mode = g_config.vietKey;
    QTimer drainTimer;
    drainTimer.setSingleShot(true);
    // Drain as soon as the event loop is idle. A 1 ms timer adds visible
    // latency when every composition edit is waiting for a client ACK.
    drainTimer.setInterval(0);
    state.drain_timer = &drainTimer;
    QObject::connect(&drainTimer, &QTimer::timeout,
                     [&state]() { drain_queued_keys(&state); });

    QTimer idleTimer;
    idleTimer.setSingleShot(true);
    idleTimer.setInterval(10000);
    g_idle_timer = &idleTimer;
    QObject::connect(&idleTimer, &QTimer::timeout, []() {
        Bamboo_ReleaseMemory();
        malloc_trim(0);
    });
    // Start-up (D-Bus, tray, config) leaves garbage too.
    idleTimer.start();

    state.display = wl_display_connect_to_fd(im_socket_fd);
    if (!state.display) {
        std::cerr << "Failed to connect to the input method socket." << std::endl;
        return 1;
    }
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);
    if (!state.input_method) {
        std::cerr << "Compositor does not support zwp_input_method_v1." << std::endl;
        return 1;
    }
    zwp_input_method_v1_add_listener(state.input_method, &input_method_listener, &state);

    WindowTracker windowTracker;
    g_windowTracker = &windowTracker;
    QObject::connect(&windowTracker, &WindowTracker::activeWindowChangedSignal, [&](const QString& windowClass) {
        const bool was_terminal = g_terminal_app;
        const QString appId = windowClass.section("|||", 0, 0).toLower();
        const QStringList terminals = {"kitty", "alacritty", "konsole", "org.kde.konsole",
            "gnome-terminal", "org.gnome.terminal", "org.gnome.ptyxis", "ptyxis",
            "org.gnome.console", "kgx", "xfce4-terminal", "lxterminal", "foot",
            "footclient", "wezterm", "org.wezfurlong.wezterm"};
        g_terminal_app = terminals.contains(appId);
        const bool was_key_event_app = g_key_event_app;
        const QStringList chromiumBrowsers = {"google-chrome", "google-chrome-beta",
            "google-chrome-unstable", "chromium", "chromium-browser",
            "org.chromium.chromium", "brave-browser", "com.brave.browser",
            "microsoft-edge", "microsoft-edge-beta", "microsoft-edge-dev",
            "com.microsoft.edge", "vivaldi", "vivaldi-stable", "com.vivaldi.vivaldi",
            "opera", "com.opera.opera", "thorium-browser", "yandex-browser"};
        // Electron apps have their own app IDs, so also look at the process
        // the tracker reports last. Installed web apps use chrome-<id>-<profile>
        // or crx_<id>; the names remain a fallback when /proc is not readable.
        const long pid = windowClass.count("|||") >= 2
            ? windowClass.section("|||", -1).toLong() : 0;
        g_key_event_app = chromiumBrowsers.contains(appId) ||
            appId.startsWith("chrome-") || appId.startsWith("crx_") ||
            appId.startsWith("brave-") || appId.startsWith("msedge-") ||
            is_chromium_process(pid);
        const bool was_excluded = g_app_excluded;
        g_app_excluded = windowTracker.isAppExcluded(windowClass.toStdString());
        if (was_excluded != g_app_excluded || was_terminal != g_terminal_app ||
            was_key_event_app != g_key_event_app) {
            state.queued_keys.clear();
            state.recent_tails.clear();
            if (state.drain_timer) state.drain_timer->stop();
            reset_composition(&state, true);
            clear_pending_edit(&state);
            state.has_surrounding_text = false;
        }
        if (g_app_excluded) {
            std::stringstream ss;
            ss << "DEBUG: Application excluded: " << windowClass.toStdString();
            log_to_file(ss.str());
        }
    });

    windowTracker.injectKWinScript();

    // A normal exit: KWin restarts the input method only when it crashes, so
    // keys go straight to applications until it is started again.
    auto quit = [&app]() {
        log_to_file("DEBUG: quit requested");
        app.quit();
    };
    auto toggle_mode = [&state]() {
        reset_composition(&state, true);
        set_viet_mode(&state, !state.viet_mode);
    };
    TrayIcon trayIcon(state.viet_mode,
                      {toggle_mode, []() { show_settings_window(); }, quit});
    g_viet_mode_changed = [&trayIcon](bool viet) {
        trayIcon.setVietMode(viet);
        uk_save_viet_mode(viet);
        show_mode_osd(viet);
    };

    QObject control;
    new ControlAdaptor(&control, {
        [&state]() { return state.viet_mode; },
        [&state, &windowTracker]() {
            // The window owns these options; the current V/E mode stays.
            const UkConfig previous = g_config;
            g_config = uk_load_config();
            // Saving the macro table or the app list must not break a word
            // being typed, so restart the engine only when its options change.
            if (!same_engine_config(previous, g_config)) {
                reset_composition(&state, true);
                apply_engine_config();
            }
            windowTracker.reloadExcludedApps();
        },
        []() { show_settings_window(); },
        quit,
    });
    QDBusConnection::sessionBus().registerObject(UK_CONTROL_PATH, &control,
                                                 QDBusConnection::ExportAdaptors);

    log_to_file("Wayland IM v1 Client started. Waiting for events...");

    QSocketNotifier waylandNotifier(wl_display_get_fd(state.display), QSocketNotifier::Read);
    QObject::connect(&waylandNotifier, &QSocketNotifier::activated,
                     [&state, &app, &waylandNotifier]() {
        if (wl_display_dispatch(state.display) == -1) {
            waylandNotifier.setEnabled(false);
            std::cerr << "Wayland display disconnected or error." << std::endl;
            app.quit();
            return;
        }
        while (wl_display_dispatch_pending(state.display) > 0) {}
        wl_display_flush(state.display);
    });
    while (wl_display_dispatch_pending(state.display) > 0) {}
    wl_display_flush(state.display);

    int ret = app.exec();

    g_viet_mode_changed = nullptr;
    g_idle_timer = nullptr;
    if (state.keyboard) {
        wl_proxy_destroy((struct wl_proxy*)state.keyboard);
    }
    if (state.context) {
        zwp_input_method_context_v1_destroy(state.context);
    }
    wl_display_disconnect(state.display);
    return ret;
}
