// Exercise deferred input with a silent peer: no incoming event can flush it.
#define main unikey_main
#include "../src/main.cpp"
#undef main

#include <sys/socket.h>

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
    return queued_ok && repair_ok && timeout_ok ? 0 : 1;
}
