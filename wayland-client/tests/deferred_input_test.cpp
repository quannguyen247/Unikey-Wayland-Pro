// Exercise deferred input with a silent peer: no incoming event can flush it.
#define main unikey_main
#include "../src/main.cpp"
#undef main

#include <sys/socket.h>
#include <QTemporaryDir>

static bool check_surrounding_echo(WaylandState* state) {
    bool passed = true;
    auto start = [&](int method, const char* text, uint32_t cursor) {
        reset_composition(state);
        clear_pending_edit(state);
        state->recent_tails.clear();
        eaten_keys.clear();
        Bamboo_SetInputMethod(method);
        input_method_context_surrounding_text(state, state->context, text, cursor, cursor);
    };
    auto press = [&](uint32_t key) {
        process_keyboard_key(state, 1, 1, key, 1, 0);
        process_keyboard_key(state, 2, 2, key, 0, 0);
    };
    auto check = [&](const char* name, const std::string& expected) {
        const bool ok = state->composed_word == expected;
        std::cout << name << ": " << ok << '\n';
        passed = passed && ok;
    };

    start(2, "", 0);
    press(32); // d
    input_method_context_surrounding_text(state, state->context, "", 0, 0);
    input_method_context_surrounding_text(state, state->context, "", 0, 0);
    press(10); // 9
    check("VNI survives repeated empty echoes", "đ");

    start(1, "", 0);
    press(30); // a
    input_method_context_surrounding_text(state, state->context, "", 0, 0);
    press(31); // s
    check("Telex survives an empty echo", "á");

    start(2, "prefix suffix", 7);
    press(32);
    input_method_context_surrounding_text(state, state->context, "prefix suffix", 7, 7);
    press(10);
    check("VNI preserves composition in the middle of text", "đ");
    const bool suffix_ok = state->surrounding_text == "prefix đsuffix";
    std::cout << "text after the caret is preserved: " << suffix_ok << '\n';
    passed = passed && suffix_ok;

    start(2, "abcx", 2);
    press(32);
    // A cursor update produced before the append can arrive after its key.
    input_method_context_surrounding_text(state, state->context, "abcx", 4, 4);
    press(10);
    check("VNI survives a delayed pre-append cursor update", "đ");
    input_method_context_surrounding_text(state, state->context, "abcxđ", 6, 6);
    const bool cursor_ok = state->surrounding_text == "abcxđ" &&
                           state->surrounding_cursor == 6 &&
                           !state->pending_edit.active;
    std::cout << "the acknowledged cursor replaces the prediction: " << cursor_ok << '\n';
    passed = passed && cursor_ok;

    start(2, "", 0);
    press(32);
    input_method_context_surrounding_text(state, state->context, "d", 0, 0);
    press(10);
    check("a real caret move resets composition", "9");

    start(2, "abcx", 2);
    press(32);
    input_method_context_surrounding_text(state, state->context, "abcx", 0, 4);
    press(10);
    check("a reported selection is not ignored as an echo", "9");

    start(2, "abcx", 2);
    press(32);
    input_method_context_surrounding_text(state, state->context, "abcx", 4, 0);
    press(10);
    check("the reverse selection is not ignored as an echo", "9");

    start(2, "", 0);
    press(32);
    input_method_context_reset(state, state->context);
    input_method_context_surrounding_text(state, state->context, "", 0, 0);
    press(10);
    check("reset does not carry composition into another field", "9");

    start(2, "", 0);
    press(32);
    input_method_context_surrounding_text(state, state->context, "d", 1, 1);
    input_method_context_surrounding_text(state, state->context, "", 0, 0);
    press(10);
    check("deletion after an acknowledged append is accepted", "9");

    start(2, "", 0);
    press(32);
    input_method_context_surrounding_text(state, state->context, "other", 5, 5);
    press(10);
    check("unrelated surrounding text is accepted", "9");

    start(1, "", 0);
    return passed;
}

// Decode the requests sent to the peer: "+K"/"-K" for key press/release,
// "=text" for commit_string and "d" for delete_surrounding_text.
static std::string take_requests(WaylandState* state, int peer) {
    wl_display_flush(state->display);
    std::string bytes;
    char buffer[4096];
    ssize_t count;
    while ((count = recv(peer, buffer, sizeof(buffer), 0)) > 0) bytes.append(buffer, count);

    std::string out;
    size_t offset = 0;
    while (offset + 8 <= bytes.size()) {
        uint32_t header;
        memcpy(&header, bytes.data() + offset + 4, sizeof(header));
        const size_t length = header >> 16;
        if (length < 8 || offset + length > bytes.size()) break;
        const char* body = bytes.data() + offset + 8;
        if (!out.empty()) out += ' ';
        switch (header & 0xffff) {
        case ZWP_INPUT_METHOD_CONTEXT_V1_KEY: {
            uint32_t key, key_state;
            memcpy(&key, body + 8, sizeof(key));
            memcpy(&key_state, body + 12, sizeof(key_state));
            out += (key_state ? "+" : "-") + std::to_string(key);
            break;
        }
        case ZWP_INPUT_METHOD_CONTEXT_V1_COMMIT_STRING: {
            uint32_t text_length;
            memcpy(&text_length, body + 4, sizeof(text_length));
            out += "=" + std::string(body + 8, text_length ? text_length - 1 : 0);
            break;
        }
        case ZWP_INPUT_METHOD_CONTEXT_V1_DELETE_SURROUNDING_TEXT:
            out += "d";
            break;
        default:
            out += "?";
        }
        offset += length;
    }
    return out;
}

static bool check_key_events(WaylandState* state, int peer) {
    bool passed = true;
    auto start = [&](int method, const char* text, uint32_t cursor, uint32_t anchor) {
        reset_composition(state);
        clear_pending_edit(state);
        state->recent_tails.clear();
        state->content_purpose = 0;
        eaten_keys.clear();
        Bamboo_SetInputMethod(method);
        input_method_context_surrounding_text(state, state->context, text, cursor, anchor);
        take_requests(state, peer);
    };
    auto press = [&](uint32_t key, uint32_t mods = 0) {
        route_keyboard_key(state, 1, 1, key, 1, mods);
        route_keyboard_key(state, 2, 2, key, 0, mods);
    };
    auto check = [&](const char* name, bool ok) {
        std::cout << name << ": " << ok << '\n';
        passed = passed && ok;
    };
    auto expect = [&](const char* name, const std::string& wanted) {
        const std::string got = take_requests(state, peer);
        const bool ok = got == wanted;
        std::cout << name << ": " << ok;
        if (!ok) std::cout << " (got \"" << got << "\", want \"" << wanted << "\")";
        std::cout << '\n';
        passed = passed && ok;
    };

    g_key_event_app = true;
    start(2, "", 0, 0); // VNI
    press(47); press(23); press(18); // v i e
    expect("unchanged letters reach the client as keys", "+47 -47 +23 -23 +18 -18");
    press(7); // 6 -> ê
    expect("a diacritic sends Backspace before the new text", "+14 -14 =ê");
    check("Backspace edits do not hold back later keys",
          state->pending_edit.active && !input_blocked(state));
    press(20); // t, while the edit above is unacknowledged
    expect("the next key is sent at once", "+20 -20");
    input_method_context_surrounding_text(state, state->context, "vi", 2, 2);
    check("the report between Backspace and commit is skipped",
          state->pending_edit.active && state->composed_word == "viêt");
    input_method_context_surrounding_text(state, state->context, "viêt", 5, 5);
    check("the final report is accepted", !state->pending_edit.active);
    press(6); // 5 -> việt
    expect("a tone mark rewrites the tail after the vowel", "+14 -14 +14 -14 =ệt");
    press(57);
    expect("an unchanged word ends with the real Space key", "+57 -57");
    check("the caret prediction includes the Space",
          state->composed_word.empty() && state->surrounding_text == "việt " &&
          state->surrounding_cursor == std::string("việt ").size());

    start(4, "", 0, 0); // Telex 2
    press(17); // w -> ư
    expect("a mapped key is typed, then replaced", "+17 +14 -14 =ư -17");

    start(2, "abc", 3, 1);
    press(30);
    expect("a selection keeps the surrounding-text replacement", "=a");

    start(2, "", 0, 0);
    state->content_purpose = 5;
    press(30);
    expect("URL fields keep the surrounding-text replacement", "=a");

    g_key_event_app = false;
    start(2, "", 0, 0);
    press(30);
    expect("other applications keep direct commits", "=a");

    // Electron apps are recognised by Chromium's resource pack, even after an
    // update deletes the mapped file; Qt WebEngine's own pack is not a match.
    std::istringstream electron(
        "7f1e0 r--s 00000000 fd:00 381415 /usr/lib/app/chrome_100_percent.pak (deleted)\n");
    std::istringstream webengine(
        "7f1e0 r--s 00000000 fd:00 1 /usr/share/qt6/resources/qtwebengine_resources_100p.pak\n");
    check("an Electron process uses key events", maps_show_chromium(electron));
    check("a Qt WebEngine process keeps direct commits", !maps_show_chromium(webengine));
    check("this test process is not Chromium", !is_chromium_process(getpid()));

    start(1, "", 0, 0);
    return passed;
}

// The input method and the settings window are separate processes that share
// ~/UnikeyWayland/global.json; neither may undo the other's keys.
static bool check_config_ownership() {
    bool passed = true;
    auto check = [&](const char* name, bool ok) {
        std::cout << name << ": " << ok << '\n';
        passed = passed && ok;
    };
    QTemporaryDir home;
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());

    QFile legacy(uk_config_path());
    QDir().mkpath(uk_config_dir());
    legacy.open(QIODevice::WriteOnly);
    legacy.write("{\"method\": 0, \"vietKey\": false, \"futureKey\": 7}");
    legacy.close();
    check("an unset method falls back to Telex", uk_load_config().method == 1);

    UkConfig options = uk_load_config();
    options.method = 2;
    options.macros["ko"] = "không";
    uk_save_options(options);
    UkConfig saved = uk_load_config();
    check("saving options keeps the input method's V/E mode", !saved.vietKey);
    check("saving options keeps keys it does not know",
          uk_read_config_object().value("futureKey").toInt() == 7);

    uk_save_viet_mode(true);
    saved = uk_load_config();
    check("switching V/E keeps the window's options",
          saved.vietKey && saved.method == 2 && saved.macros["ko"] == "không");

    qputenv("HOME", oldHome);
    return passed;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sockets) != 0) return 1;

    Bamboo_Init();
    WaylandState state = {};
    state.display = wl_display_connect_to_fd(sockets[0]);
    if (!state.display) return 1;
    state.context = reinterpret_cast<zwp_input_method_context_v1*>(
        wl_proxy_create(reinterpret_cast<wl_proxy*>(state.display),
                        &zwp_input_method_context_v1_interface));
    state.active = true;

    const bool surrounding_ok = check_surrounding_echo(&state);
    take_requests(&state, sockets[1]);
    const bool key_events_ok = check_key_events(&state, sockets[1]);
    const bool config_ok = check_config_ownership();
    // Keep the synchronous probes separate from the timer-delivery checks.
    take_requests(&state, sockets[1]);

    QTimer drainTimer;
    drainTimer.setSingleShot(true);
    drainTimer.setInterval(0);
    state.drain_timer = &drainTimer;
    QObject::connect(&drainTimer, &QTimer::timeout,
                     [&]() { drain_queued_keys(&state); });

    std::string received;
    QSocketNotifier reader(sockets[1], QSocketNotifier::Read);
    QObject::connect(&reader, &QSocketNotifier::activated, [&]() {
        char buffer[4096];
        ssize_t count;
        while ((count = recv(sockets[1], buffer, sizeof(buffer), 0)) > 0) {
            received.append(buffer, count);
        }
    });

    // A queued press/release must reach the peer without another physical key.
    state.queued_keys.push_back({1, 1, 30, 1, 0}); // a
    state.queued_keys.push_back({2, 2, 30, 0, 0});
    schedule_queued_keys(&state);

    bool queued_ok = false;
    bool repair_ok = false;
    bool timeout_ok = false;
    QTimer::singleShot(50, &app, [&]() {
        queued_ok = received.find(std::string("a\0", 2)) != std::string::npos;
        std::cout << "queued key delivered without incoming event: " << queued_ok << '\n';

        // Discard the first case's output before exercising the repair timer.
        wl_display_flush(state.display);
        char buffer[4096];
        while (recv(sockets[1], buffer, sizeof(buffer), 0) > 0) {}
        received.clear();
        state.pending_edit.active = true;
        state.pending_edit.generation = ++state.next_edit_generation;
        state.pending_edit.repair_bytes = std::string("aá").size();
        state.pending_edit.repair_text = "á";
        state.pending_edit.observed_failure = {"aá", 3, 3, true};
        arm_failed_edit_repair(&state);
    });

    QTimer::singleShot(100, &app, [&]() {
        repair_ok = received.find(std::string("á\0", 3)) != std::string::npos;
        std::cout << "tone repair delivered without incoming event: " << repair_ok << '\n';

        wl_display_flush(state.display);
        char buffer[4096];
        while (recv(sockets[1], buffer, sizeof(buffer), 0) > 0) {}
        received.clear();
        state.pending_edit.generation = ++state.next_edit_generation;
        state.queued_keys.push_back({3, 3, 48, 1, 0}); // b
        state.queued_keys.push_back({4, 4, 48, 0, 0});
        arm_pending_timeout(&state);
    });

    QTimer::singleShot(1000, &app, [&]() {
        timeout_ok = received.find(std::string("b\0", 2)) != std::string::npos;
        std::cout << "key delivered after missing surrounding-text ACK: " << timeout_ok << '\n';
    });

    bool burst_ok = false;
    bool burst_delivery_ok = false;
    QTimer::singleShot(1100, &app, [&]() {
        received.clear();
        state.pending_edit.active = true;
        state.pending_edit.generation = ++state.next_edit_generation;
        // A stalled client can receive a batch larger than the old 512-event
        // limit. Keep the trailing Alt release along with the whole batch.
        for (uint32_t i = 0; i < 600; ++i) {
            route_keyboard_key(&state, i + 10, i, 15, i % 2 == 0 ? 1 : 0, 0);
        }
        route_keyboard_key(&state, 610, 600, 56, 1, 8);
        route_keyboard_key(&state, 611, 601, 15, 1, 8);
        route_keyboard_key(&state, 612, 602, 15, 0, 8);
        route_keyboard_key(&state, 613, 603, 56, 0, 0);
        burst_ok = state.queued_keys.size() == 604;
        uint32_t serial = 10;
        for (const auto& event : state.queued_keys) {
            burst_ok = burst_ok && event.serial == serial++;
        }
        std::cout << "a stalled-client burst preserves every event in order: "
                  << burst_ok << " (" << state.queued_keys.size() << "/604)\n";
        clear_pending_edit(&state);
        schedule_queued_keys(&state);
    });

    QTimer::singleShot(1300, &app, [&]() {
        // Tab and Alt are forwarded as v1::key, each 24 bytes.
        // Decode the wire rather than counting a key already in the queue.
        size_t keys = 0;
        size_t offset = 0;
        bool wire_order_ok = true;
        uint32_t last_key = 0, last_state = 1;
        while (offset + 8 <= received.size()) {
            uint32_t header;
            memcpy(&header, received.data() + offset + 4, sizeof(header));
            const size_t length = header >> 16;
            if (length < 8 || offset + length > received.size()) break;
            if ((header & 0xffff) == ZWP_INPUT_METHOD_CONTEXT_V1_KEY && length == 24) {
                ++keys;
                uint32_t serial, time;
                memcpy(&serial, received.data() + offset + 8, sizeof(serial));
                memcpy(&time, received.data() + offset + 12, sizeof(time));
                memcpy(&last_key, received.data() + offset + 16, sizeof(last_key));
                memcpy(&last_state, received.data() + offset + 20, sizeof(last_state));
                const uint32_t expected_key = keys == 601 || keys == 604 ? 56 : 15;
                const uint32_t expected_state = keys <= 600
                    ? (keys % 2 == 1 ? 1u : 0u) : (keys <= 602 ? 1u : 0u);
                wire_order_ok = wire_order_ok && serial == keys + 9 &&
                                time == keys - 1 &&
                                last_key == expected_key && last_state == expected_state;
            }
            offset += length;
        }
        burst_delivery_ok = keys == 604 && wire_order_ok && state.queued_keys.empty() &&
                            last_key == 56 && last_state == 0;
        std::cout << "the complete burst and final release reach the peer: "
                  << burst_delivery_ok << " (" << keys << "/604)\n";
        app.quit();
    });
    app.exec();

    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(state.context));
    wl_display_disconnect(state.display);
    close(sockets[1]);
    return surrounding_ok && key_events_ok && config_ok && queued_ok && repair_ok &&
           timeout_ok && burst_ok && burst_delivery_ok ? 0 : 1;
}
