// Exercise deferred input with a silent peer: no incoming event can flush it.
#define main unikey_main
#include "../src/main.cpp"
#undef main

#include <sys/socket.h>

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
    // Keep the synchronous probes separate from the timer-delivery checks.
    wl_display_flush(state.display);
    char probe_buffer[4096];
    while (recv(sockets[1], probe_buffer, sizeof(probe_buffer), 0) > 0) {}

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
        app.quit();
    });
    app.exec();

    wl_proxy_destroy(reinterpret_cast<wl_proxy*>(state.context));
    wl_display_disconnect(state.display);
    close(sockets[1]);
    return surrounding_ok && queued_ok && repair_ok && timeout_ok ? 0 : 1;
}
