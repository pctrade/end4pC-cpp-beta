#include <cairo/cairo.h>
#include "i18n.h"
#include "bardivider.h"
#include "oled.h"
#include "units.h"
#include "bubble.h"
#include "elastic.h"
#include "tabanim.h"
#include "deskplace.h"
#include "textsel_pango.h"
#include "clipboard.h"
#include "mediaplayer.h"
#include "mediacommand.h"
#include <pango/pangocairo.h>
#include "shellfont.h"
#include <poll.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <sys/inotify.h>
#include <sys/file.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <charconv>
#include <random>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cctype>
#include <cmath>
#include <malloc.h>
#include <dirent.h>
#include <strings.h>
#include <chrono>
#include <clocale>
#include <cstring>
#include <set>
#include <unordered_set>
#include <string>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <sstream>
#include <utility>
#include <vector>

#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#include "xdg-shell-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "ext-background-effect-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "notify_dbus.h"
#include "polkit_dbus.h"
#include "tray_dbus.h"

static cairo_t* mkcr(cairo_surface_t* s) {
    static cairo_font_options_t* fo = nullptr;
    if (!fo) {
        fo = cairo_font_options_create();
        cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    }
    cairo_t* c = cairo_create(s);
    cairo_set_font_options(c, fo);
    return c;
}

extern char** environ;

namespace {
#include "parts/01_types.inc"
#include "parts/02_layout_data.inc"
#include "parts/02b_blur_region.inc"
#include "parts/13b_pop_reveal.inc"
#include "parts/03_resources.inc"
#include "parts/03b_collage_model.inc"
#include "parts/04_theme_config.inc"
#include "parts/05_icons_tray_quick.inc"
#include "parts/06_desktop_entries.inc"
#include "parts/07_wallpaper_bars.inc"
#include "parts/07b_collage.inc"
#include "parts/08_shapes_selector.inc"
#include "parts/13c_compositor.inc"
#include "parts/08b_capture.inc"
#include "parts/09_launcher.inc"
#include "parts/10_power_control.inc"
#include "parts/10d_cc_radios.inc"
#include "parts/10e_cc_radios_draw.inc"
#include "parts/10b_switcher.inc"
#include "parts/10c_polkit.inc"
#include "parts/11_osd_notifications.inc"
#include "parts/12_notif_panel.inc"
#include "parts/13d_eq_backend.inc"
#include "parts/13_media_panel.inc"
#include "parts/14_tray_desktop_menu.inc"
#include "parts/14a_weather_service.inc"
#include "parts/14b_desk_widget.inc"
#include "parts/14c_todo_widget.inc"
#include "parts/14d_weather_widget.inc"
#include "parts/14e_lyrics_service.inc"
#include "parts/14f_media_widget.inc"
#include "parts/14g_resources_widget.inc"
#include "parts/14h_calendar_widget.inc"
#include "parts/14i_clock_widget.inc"
#include "parts/14j_user_widget.inc"
#include "parts/14k_image_widget.inc"
#include "parts/14l_bar_services.inc"
#include "parts/14m_clock_panel.inc"
#include "parts/14n_dock.inc"
#include "parts/14n_dock_draw.inc"
#include "parts/14n_dock_grid.inc"
#include "parts/14n_dock_input.inc"
#include "parts/14s_dock_panel.inc"
#include "parts/14o_active_window.inc"
#include "parts/14p_audio_menu.inc"
#include "parts/14q_bt_menu.inc"
#include "parts/14r_island_card.inc"
#include "parts/14r_identify.inc"
#include "parts/14t_accessibility.inc"
#include "parts/14u_timers.inc"
#include "parts/14v_osk.inc"
#include "parts/14w_qr_card.inc"
#include "parts/14wc_confirm_card.inc"
#include "parts/14wf_forcequit.inc"
#include "parts/14wg_conflicts.inc"
#include "parts/14wh_overview.inc"
#include "parts/14x_drives.inc"
#include "parts/14xa_privacy.inc"
#include "parts/14y_game_overlay.inc"
#include "parts/14z_desk_viz.inc"
#include "parts/14zb_text_widget.inc"
#include "parts/15_input.inc"
#include "parts/16_registry_ipc.inc"
}

int runShellDetached() {
    sockaddr_un a;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    bool running = fd >= 0 && unixAddr(a, runtimeDir() + "/end4pC.sock") == 0 && connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    if (running) {
        const char ask[] = "config\n";
        char buf[64];
        running = write(fd, ask, sizeof ask - 1) > 0 && read(fd, buf, sizeof buf) > 0;
    }
    if (fd >= 0) close(fd);
    if (running) { std::fprintf(stderr, "end4pC is already running\n"); return 0; }
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return 1;
    exe[n] = '\0';
    const pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        setsid();
        signal(SIGHUP, SIG_IGN);
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); if (devnull > 2) close(devnull); }
        execl(exe, exe, static_cast<char*>(nullptr));
        _exit(1);
    }
    return 0;
}

void printUsage() {
    std::puts(
        "end4pC - a light Wayland shell for Hyprland, niri and sway\n"
        "\n"
        "usage: end4pC                      start the shell (foreground; the session does this, see hyprland/end4pC.lua, niri/end4pC.kdl, sway/)\n"
        "       end4pC run                  start it detached (does nothing if it is already running)\n"
        "       end4pC <command> [args]     talk to the running shell\n"
        "       end4pC --version            the installed version\n"
        "       end4pC update [--check|-y|--rollback]   update to the latest published version (code and assets only, never your settings or dotfiles)\n"
        "\n"
        "The shell:\n"
        "  restart                          restart the shell          quit                   stop it\n"
        "  ping                             'pong' when it runs        config                 the settings in use\n"
        "  set <key> <value>                change a setting (the keys of config.json, e.g. set barPosition bottom, set dock 1)\n"
        "  applytheme [image]               the wallpaper and the colors made from it; no path: the one in use\n"
        "  wallpaper <image>                change the wallpaper       collage [args]         the wallpaper collage\n"
        "\n"
        "Panels and popups (no argument: toggle):\n"
        "  launcher [toggle|open|close|clipboard|emoji|query <text>]   the launcher; clipboard/emoji open it in that mode\n"
        "  selector [toggle|open|close]     the wallpaper selector     control [toggle|open|close|wifi|bt]   the control center\n"
        "  media [toggle|open|close]        the media player           panel [toggle|open|close]            the notification panel\n"
        "  power [toggle|open|close]        the power menu             switcher [open|next|prev|commit|close]  the window switcher\n"
        "  clockpanel|weatherpanel|aipanel|respanel|batterypanel [toggle|open|close|tab N]   the popups of the bar's widgets\n"
        "  audiomenu [open|close|tab output|input|apps]                the volume popup\n"
        "  desktopmenu [open [x y]|close]   the desktop's right-click menu\n"
        "  settings                         the Settings window        tools                  the Tools window\n"
        "  osd volume | osd brightness      show the on-screen display\n"
        "\n"
        "Desktop widgets:\n"
        "  weather|player|calendar|resources|image|clock|todo|usercard|text|visualizer [toggle|show|hide]\n"
        "  widget <name> on|off [W H LEFT TOP]   show or hide one, in that size and place\n"
        "  widgets lock|unlock              lock or unlock their edit mode\n"
        "  usercard image [path]            the user card's picture (no path: back to the wallpaper)\n"
        "\n"
        "The dock, the tray, hardware:\n"
        "  dock [on|off|toggle]             the dock                   tray | tray match | tray open|close   the tray's items\n"
        "  brightness [get|up|down|set N]   the screen's brightness    volume                 the volume in use\n"
        "  screenshot [region|screen|window|edit]   a screenshot       lock                   lock the screen\n"
        "  dnd [on|off|toggle]              do not disturb             updates [check]        check the pending updates\n"
        "  quick <n>                        run quick action n         identify [NAME=N ...]  show each screen's number\n"
        "  workspace | music | recording | notifs | shapes             what the shell knows (for scripts)\n"
        "\n"
        "Debug: network <kind>, notifications <n>, periph <pct> <state> <kind> [name] | periph off (fake states until the next real one).\n"
        "Settings -> General has the key binds; the catalogs of the languages are in assets/i18n.");
}

int clientMain(const std::string& cmd) {
    if (cmd == "run") return runShellDetached();
    if (cmd == "help" || cmd == "-h" || cmd == "--help") { printUsage(); return 0; }
    sockaddr_un a;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || unixAddr(a, runtimeDir() + "/end4pC.sock") < 0 ||
        connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
        std::fprintf(stderr, "end4pC is not running\n");
        return 1;
    }
    std::string msg = cmd + "\n";
    (void)!write(fd, msg.data(), msg.size());
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) std::fwrite(buf, 1, n, stdout);
    close(fd);
    return 0;
}

// `end4pC update`: scripts/update.sh of the shell's folder (the binary lives in <folder>/build), with the rest of the arguments.
int runUpdate(int argc, char** argv) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) { std::fprintf(stderr, "end4pC: cannot tell where it is installed\n"); return 1; }
    exe[n] = 0;
    std::string dir = exe;
    for (int up = 0; up < 2; up++) dir = dir.substr(0, dir.find_last_of('/'));  // .../build/end4pC -> the shell's folder
    const std::string script = dir + "/scripts/update.sh";
    std::vector<char*> args = {const_cast<char*>("bash"), const_cast<char*>(script.c_str())};
    for (int i = 0; i < argc; i++) args.push_back(argv[i]);
    args.push_back(nullptr);
    execvp("bash", args.data());
    std::perror("end4pC update");
    return 1;
}

int main(int argc, char** argv) {
    mallopt(M_ARENA_MAX, 1);
    signal(SIGPIPE, SIG_IGN);
    if (argc > 1) {
        const std::string first = argv[1];
        if (first == "--version" || first == "-v" || first == "version") { std::puts("end4pC " END4PC_VERSION); return 0; }
        if (first == "update" || first == "--update") return runUpdate(argc - 2, argv + 2);
        return clientMain(first);
    }
    {
        sockaddr_un a;
        const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const bool running = fd >= 0 && unixAddr(a, runtimeDir() + "/end4pC.sock") == 0 && connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
        if (fd >= 0) close(fd);
        if (running || isatty(STDOUT_FILENO)) {
            printUsage();
            std::puts(running ? "\n(the shell is already running: nothing was started)"
                              : "\n(no command: the shell would start here in the foreground; the session does that for you. To start it by hand: end4pC run)");
            return running ? 0 : 0;
        }
    }
    App app;

    // SIGINT/SIGTERM handled through the event loop instead of async handlers.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sigFd = signalfd(-1, &mask, SFD_CLOEXEC);

    int timerFd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
    timespec nowTs{};
    clock_gettime(CLOCK_REALTIME, &nowTs);
    itimerspec spec{};
    spec.it_value.tv_sec = nowTs.tv_sec - nowTs.tv_sec % 60 + 60;
    spec.it_interval.tv_sec = 60;
    timerfd_settime(timerFd, TFD_TIMER_ABSTIME, &spec, nullptr);

    signal(SIGCHLD, SIG_IGN);
    {
        int wp[2];
        if (pipe2(wp, O_CLOEXEC | O_NONBLOCK) == 0) { g_weatherFd = wp[0]; g_weatherWrite = wp[1]; }
    }
    {
        int wp[2];
        if (pipe2(wp, O_CLOEXEC | O_NONBLOCK) == 0) { g_wpWakeFd = wp[0]; g_wpWakeWrite = wp[1]; }
    }
    std::setlocale(LC_TIME, "");
    migrateConfig();
    loadConfig(app);
    matsym::assetsDir() = assetsDir();
    matsym::enabled() = app.cfg.materialIcons != 0;
    applyShellFont(app.cfg.fontFamily, app.cfg.fontWeightText, app.cfg.fontWeightTitle);
    app.collageShown = collageNow(app);
    i18n::load(app.cfg.language, assetsDir(), configDir() + "/i18n");
    updateClock(app);
    barModulesSync(app);
    updateBattery(app);
    ccRunScript("night-apply");
    readPeripherals(app);
    loadColors(app);
    app.wallpaper = currentWallpaper();

    int inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int colorsWd = -1, configWd = -1;
    if (inoFd >= 0) {
        colorsWd = inotify_add_watch(inoFd, generatedDir().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
        configWd = inotify_add_watch(inoFd, configDir().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);
    }
    if (access(configPath().c_str(), F_OK) != 0) saveConfig(app);

    app.display = wl_display_connect(nullptr);
    if (!app.display) {
        std::fprintf(stderr, "no wayland display\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &kRegistryListener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.layerShell) {
        std::fprintf(stderr, "missing wayland globals (compositor/shm/layer-shell)\n");
        return 1;
    }
    wl_display_roundtrip(app.display);
    app.ready = true;
    if (wmKind() == Wm::Hyprland) ovRecoverGaps();  // (the shell may have ended with the overview open: its gaps go back)
    app.conflictCheckAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    startNotifications(app);
    startPolkit(app);
    if (app.cfg.lockOnStart) {
        const char* rt = std::getenv("XDG_RUNTIME_DIR");
        const std::string marker = std::string(rt ? rt : "/tmp") + "/end4pC-started";
        if (access(marker.c_str(), F_OK) != 0) {
            if (FILE* m = std::fopen(marker.c_str(), "w")) std::fclose(m);
            char exe[4096] = {};
            const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
            if (n > 0) {
                std::string lock(exe, n);
                lock = lock.substr(0, lock.rfind('/')) + "/end4pC-lock";
                runDetached({lock.c_str()});
            }
        }
    }
    if (app.bus) {
        sd_bus_set_method_call_timeout(app.bus, 1500000);
        const int tr = traywatch_start(app.bus, trayChanged, &app);
        if (tr < 0) std::fprintf(stderr, "tray: could not start the tray (%s)\n", std::strerror(-tr));
        app.trayDirty = true;
    }
    for (auto [output, name] : app.pendingOutputs) createBar(app, output, name);
    app.pendingOutputs.clear();
    wl_display_roundtrip(app.display);
    deskInit(app);
    for (DeskWidget* d : deskList(app)) deskLoad(*d);
    deskRestore(app);
    deskVizRestore(app);
    gameNotesGhostSync(app);

    const std::string sockPath = runtimeDir() + "/end4pC.sock";
    int ipcFd = listenSocket(sockPath);
    if (ipcFd < 0) std::fprintf(stderr, "warning: could not open %s\n", sockPath.c_str());
    int wmFd = wmEventSocket();  // -1 when the compositor has no event socket (Hyprland's socket2, niri's event stream)
    kbdRead(app);
    awinRead(app);
    std::string wmPending;

    int audioFd = startAudioWatcher();
    readVolume(app);
    if (quickIsShown(app, kQaMic)) app.micMuted = readMicMuted();
    if (quickIsShown(app, kQaPerf)) app.perfProfile = readPerfProfile();
    int netFd = startNetlink();
    UPowerWatch upower;
    startUPowerWatcher(upower);
    int powerFd = startPowerWatcher();
    readNetwork(app);
    {
        int ap[2];
        if (pipe2(ap, O_CLOEXEC | O_NONBLOCK) == 0) { g_artFetchFd = ap[0]; g_artFetchWrite = ap[1]; }
        int tp[2];
        if (pipe2(tp, O_CLOEXEC | O_NONBLOCK) == 0) { g_thumbFd = tp[0]; g_thumbWrite = tp[1]; }
        int cp[2];
        if (pipe2(cp, O_CLOEXEC | O_NONBLOCK) == 0) { g_clipFd = cp[0]; g_clipWrite = cp[1]; }
        int op[2];
        if (pipe2(op, O_CLOEXEC | O_NONBLOCK) == 0) { g_onlineFd = op[0]; g_onlineWrite = op[1]; }
    }
    app.musicFd = startMusicWatcher(app.cfg.preferredPlayer, &app.musicPid);
    app.watchedPlayer = app.cfg.preferredPlayer;
    std::string musicPending;
    refreshActiveWorkspace(app);
    refreshOccupied(app);
    refreshWsApps(app);
    timersLoad();

    pollfd fds[26] = {
        {wl_display_get_fd(app.display), POLLIN, 0},
        {timerFd, POLLIN, 0},
        {sigFd, POLLIN, 0},
        {ipcFd, POLLIN, 0},
        {wmFd, POLLIN, 0},
        {inoFd, POLLIN, 0},
        {audioFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {g_artFetchFd, POLLIN, 0},
        {netFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {g_thumbFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {g_clipFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {-1, POLLIN, 0},
        {g_wpWakeFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {g_weatherFd, POLLIN, 0},
        {g_onlineFd, POLLIN, 0},
        {-1, POLLIN, 0},
        {-1, POLLIN, 0},
        {-1, POLLIN, 0},
        {powerFd, POLLIN, 0},
        {-1, POLLIN, 0},           // UPower's signals on the system bus (set every loop turn)
        {-1, POLLIN, 0},           // the launcher's file search (fd)
    };
    auto lastTick = std::chrono::steady_clock::now();

    while (app.running) {
        while (wl_display_prepare_read(app.display) != 0) wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);

        const auto before = std::chrono::steady_clock::now();
        if (app.osd.open() && before >= app.osd.hideAt) retractOsd(app);
        int timeoutMs = -1;
        auto wakeIn = [&](std::chrono::steady_clock::time_point when) {
            const int ms = std::max<int>(1, std::chrono::duration_cast<std::chrono::milliseconds>(when - before).count());
            timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
        };
        if (app.conflictCheckAt != std::chrono::steady_clock::time_point{}) {
            if (before >= app.conflictCheckAt) { app.conflictCheckAt = {}; conflictsStartupCheck(app); }
            else wakeIn(app.conflictCheckAt);
        }
        if (app.cfg.wallpaperAuto) {
            const auto every = std::chrono::minutes(app.cfg.wallpaperAutoMin);
            if (app.wallpaperAutoFor != app.cfg.wallpaperAutoMin || app.wallpaperAutoNext == std::chrono::steady_clock::time_point{}) {
                app.wallpaperAutoFor = app.cfg.wallpaperAutoMin;
                app.wallpaperAutoNext = before + every;
            }
            if (before >= app.wallpaperAutoNext) {
                app.wallpaperAutoNext = before + every;
                std::vector<std::string> pool = listWallpapers(expandHome(app.cfg.wallpaperDir));
                if (app.cfg.wallpaperAutoFavs) {
                    std::vector<std::string> favs;
                    for (const std::string& f : readPathList(wpListFile("wallpaper-favorites.txt")))
                        if (std::find(pool.begin(), pool.end(), f) != pool.end()) favs.push_back(f);
                    if (!favs.empty()) pool = std::move(favs);
                }
                pool.erase(std::remove(pool.begin(), pool.end(), app.wallpaper), pool.end());
                if (!pool.empty()) applyTheme(app, pool[std::rand() % pool.size()]);
            }
            wakeIn(app.wallpaperAutoNext);
        } else {
            app.wallpaperAutoNext = {};
        }
        if (app.cfg.updatesEnable) {
            if (app.updatesLast == std::chrono::steady_clock::time_point{}) app.updatesLast = before - std::chrono::minutes(app.cfg.updatesInterval) + std::chrono::seconds(45);
            const auto due = app.updatesLast + std::chrono::minutes(app.cfg.updatesInterval);
            if (before >= due) startUpdatesNow(app);
            else wakeIn(due);
        }
        if (app.updatesRecheckAt != std::chrono::steady_clock::time_point{}) {
            if (before >= app.updatesRecheckAt) { app.updatesRecheckAt = {}; if (app.cfg.updatesEnable) startUpdatesNow(app); }
            else wakeIn(app.updatesRecheckAt);
        }
        if (refreshUpdatesCount(app)) drawAll(app);
        if (app.updatesChecking) {
            if (before >= app.updatesCheckEnd) { app.updatesChecking = false; drawAll(app); }
            else wakeIn(before + std::chrono::seconds(2));
        }
        if ((resourcesInLayout(app) && !resItems(app).empty()) || app.resW.open()) {
            const auto interval = std::chrono::milliseconds(app.cfg.resInterval);
            if (app.resLast == std::chrono::steady_clock::time_point{} || before >= app.resLast + interval) {
                app.resLast = before;
                if (updateResources(app)) { drawAll(app); if (app.resW.open()) resWSync(app); }
            }
            wakeIn(app.resLast + interval);
        }
        if (netSpeedShown(app) && networkInLayout(app)) {
            const auto interval = std::chrono::milliseconds(1000);
            if (app.netPrevAt == std::chrono::steady_clock::time_point{} || before >= app.netPrevAt + interval) {
                if (updateNetSpeed(app)) drawAll(app);
            }
            wakeIn(app.netPrevAt + interval);
        } else app.netPrevAt = std::chrono::steady_clock::time_point{};
        {   // device batteries: asked at the start, and whenever UPower says a device changed (a safety net every 10 minutes, in case a signal is missed);
            const auto none = std::chrono::steady_clock::time_point{};
            const bool signalled = app.periphSignalDue != none && before >= app.periphSignalDue;
            if (app.periphLast == none || signalled || before >= app.periphLast + std::chrono::minutes(10)) {
                app.periphLast = before;
                app.periphSignalDue = none;
                app.periphRead = before + std::chrono::milliseconds(app.periph.empty() && app.periphRead == none && !app.periphEverRead ? 700 : 4000);
                app.periphEverRead = true;
                const std::string script = assetsDir() + "/../scripts/peripherals.sh";
                const std::string lo = std::to_string(app.cfg.peripheralLow), cr = std::to_string(app.cfg.peripheralCritical);
                runDetached({script.c_str(), lo.c_str(), cr.c_str(), app.cfg.peripheralNotify ? "1" : "0"});
            }
            if (app.periphRead != none) {
                if (before >= app.periphRead) { app.periphRead = none; if (readPeripherals(app)) drawAll(app); }
                else wakeIn(app.periphRead);
            }
            wakeIn(app.periphLast + std::chrono::minutes(10));
            if (app.periphSignalDue != none) wakeIn(app.periphSignalDue);
        }
        if (app.launcher.open() && app.launcher.trPending) {
            Launcher& L = app.launcher;
            if (before >= L.trDue) { L.trPending = false; requestTranslate(app, translatePhrase(L)); drawLauncher(app); }
            else wakeIn(L.trDue);
        }
        if ((app.cfg.keepAwake != 0) != (app.idler != nullptr)) setKeepAwake(app, app.cfg.keepAwake != 0);
        {
            const bool want[4] = {app.recording, app.muted, app.netKind == "none", (app.batPresent && !app.batCharging && app.batPct <= app.cfg.batteryLow) || lowPeripheral(app) != nullptr};
            const double dt = app.statusLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(before - app.statusLast).count());
            bool moving = false;
            for (int i = 0; i < 4; i++) {
                const double target = want[i] ? 1.0 : 0.0;
                if (app.statusT[i] == target) continue;
                if (!sideBar(app)) { app.statusT[i] = target; continue; }
                moving = true;
                app.statusT[i] = target > app.statusT[i] ? std::min(target, app.statusT[i] + dt / 0.22) : std::max(target, app.statusT[i] - dt / 0.22);
            }
            app.statusLast = moving ? before : std::chrono::steady_clock::time_point{};
            if (moving) { timeoutMs = 16; drawAll(app); }
        }
        {
            const double target = islandActive(app) && islandShowsRec(app) ? 1.0 : 0.0;
            static double recV = 0;
            static std::chrono::steady_clock::time_point recLast{};
            if (g_islandRecT != target || recV != 0) {
                const double dt = recLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.05, std::chrono::duration<double>(before - recLast).count());
                islandSpring(g_islandRecT, recV, target, dt);
                recLast = before;
                timeoutMs = 16;
                drawAll(app);
            } else {
                recLast = {};
                if (target > 0) {
                    static long lastSec = -1;
                    const long sec = static_cast<long>(std::chrono::duration<double>(before - g_recStartAt).count());
                    if (sec != lastSec) { lastSec = sec; drawAll(app); }
                    timeoutMs = timeoutMs < 0 ? 500 : std::min(timeoutMs, 500);
                }
            }
        }
        {
            const double target = islandActive(app) && islandShowsMusic(app) ? 1.0 : 0.0;
            static double musicV = 0;
            if (app.islandMusicT != target || musicV != 0) {
                const double dt = app.islandLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.05, std::chrono::duration<double>(before - app.islandLast).count());
                islandSpring(app.islandMusicT, musicV, target, dt);
                app.islandLast = before;
                timeoutMs = 16;
                drawAll(app);
            } else app.islandLast = std::chrono::steady_clock::time_point{};
        }
        {
            const double target = app.trayOpen && !app.tray.empty() ? 1.0 : 0.0;
            if (app.trayT != target) {
                const double dt = app.trayLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(before - app.trayLast).count());
                app.trayT = target > app.trayT ? std::min(target, app.trayT + dt / 0.22) : std::max(target, app.trayT - dt / 0.22);
                app.trayLast = before;
                timeoutMs = 16;
                drawAll(app);
            } else app.trayLast = std::chrono::steady_clock::time_point{};
        }
        {
            const bool m3q = !sideBar(app) && widgetStyle(app, "quickactions") == "m3" && inBarLayout(app, "quickactions");
            const Hit over = m3q && app.hoverBar ? hitTest(*app.hoverBar, app.pointerX, app.pointerY) : HitNone;
            const double dt = app.quickGrowLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(before - app.quickGrowLast).count());
            bool moving = false;
            for (int id = 0; id < 8; id++) {
                const double target = m3q && (over == kHitQuick + id || (id == kQaRecord && app.recording)) ? 1.0 : 0.0;
                if (std::abs(app.quickGrow[id] - target) < 0.004) { app.quickGrow[id] = target; continue; }
                app.quickGrow[id] += (target - app.quickGrow[id]) * (1 - std::exp(-dt * 18));
                moving = true;
            }
            app.quickGrowLast = moving ? before : std::chrono::steady_clock::time_point{};
            if (moving) { timeoutMs = 16; drawAll(app); }
        }
        {
            const double target = std::clamp(app.workspace, 1, std::max(1, wsShown(app)));
            if (!(pillStyle(app) || m3Ws(app)) || !app.wsPillSeen || !inBarLayout(app, "workspaces")) { app.wsPill = target; app.wsPillSeen = true; app.wsPillLast = {}; }
            else if (std::abs(app.wsPill - target) > 0.01) {
                const double dt = app.wsPillLast == std::chrono::steady_clock::time_point{} ? 0.016 : std::min(0.1, std::chrono::duration<double>(before - app.wsPillLast).count());
                app.wsPill += (target - app.wsPill) * (1 - std::exp(-dt * 16));
                app.wsPillLast = before;
                timeoutMs = 16;
                drawAll(app);
            } else { app.wsPill = target; app.wsPillLast = {}; }
        }
        if (app.trayDirty) timeoutMs = timeoutMs < 0 ? 300 : std::min(timeoutMs, 300);
        if (app.cc.open()) {
            ControlCenter& C = app.cc;
            if (before >= C.nextPoll) { ccRunScript("status"); C.nextPoll = before + std::chrono::seconds(5); }
            ccReadStatus(app);
            ccRadioTick(app, before, timeoutMs);
            if (C.configured && ccSignature(app) != C.signature) drawControlCenter(app);
            timeoutMs = timeoutMs < 0 ? 400 : std::min(timeoutMs, 400);
            if (C.ticking) { drawControlCenter(app); timeoutMs = 33; }
        }
        if (app.sw.open() && swPoll(app)) timeoutMs = timeoutMs < 0 ? 30 : std::min(timeoutMs, 30);
        if (app.sw.open() && !app.swLive.wins.empty()) timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        if (app.cap.open() && app.cap.configured && app.cap.bubbleMoving) { captureRedraw(app); timeoutMs = 16; }
        if (app.sel.open() && app.sel.configured && app.sel.bubbleMoving) { drawSelector(app); timeoutMs = 16; }
        if (app.sel.open() && app.sel.configured && !app.sel.msgShown.empty()) {
            const double age = std::chrono::duration<double>(before - app.sel.msgAt).count();
            if (age >= 3.4) { drawSelector(app); timeoutMs = timeoutMs < 0 ? 40 : std::min(timeoutMs, 40); }
            else { const int ms = static_cast<int>((3.4 - age) * 1000) + 5; timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms); }
        }
        if (app.dmenu.open() && app.dmenu.configured && (dmStep(app) || app.dmenu.animating)) { drawDesktopMenu(app); timeoutMs = 16; }
        mediaWTick(app, before, timeoutMs);
        barMediaTick(app, before, timeoutMs);
        resWTick(app, before, timeoutMs);
        calWTick(app, before, timeoutMs);
        clockWTick(app, before, timeoutMs);
        dockWTick(app, before, timeoutMs);
        userWTick(app, before, timeoutMs);
        identifyTick(before, timeoutMs);
        if (app.launcher.sliding) timeoutMs = 16;
        if ((app.sel.open() && (app.sel.modeT != app.sel.modeTarget || app.sel.navAnim.moving())) || (app.osd.open() && std::abs(app.osd.shown - app.osd.target) > 0.003)) timeoutMs = 16;
        if (app.osd.open()) {
            if (!popIsOut(app.osd.anim)) wakeIn(app.osd.hideAt);
            if (const auto ow = popWake(app.osd.anim.phase, app.osd.anim.settleAt, std::chrono::steady_clock::now()); ow != std::chrono::steady_clock::time_point::max()) wakeIn(ow);
        }
        if (app.brightDirty && app.brightFd < 0) wakeIn(app.brightSetAt);
        if (app.cwpA.active) timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        for (const auto& b : app.bars)
            if (b->animTo) timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        for (const auto& n : app.notifs)
            if (n.popup && !n.sticky && !n.leaving) wakeIn(n.hideAt);
        if (app.np.open() && notifPopupAnimating(app, before)) timeoutMs = timeoutMs < 0 ? 16 : std::min(timeoutMs, 16);
        if (app.repeatKey) {
            const int ms = std::max<int>(1, std::chrono::duration_cast<std::chrono::milliseconds>(app.repeatAt - before).count());
            timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
        }

        fds[7].fd = app.musicFd;
        fds[17].fd = app.media.cavaFd;
        if (app.media.open()) wakeIn(mediaWake(app));
        if (const auto cw = clockWake(app); cw != std::chrono::steady_clock::time_point::max()) wakeIn(cw);
        if (const auto sw = selectorWake(app); sw != std::chrono::steady_clock::time_point::max()) wakeIn(sw);
        if (const auto ww = controlWake(app); ww != std::chrono::steady_clock::time_point::max()) wakeIn(ww);
        if (const auto aw = audioWake(app); aw != std::chrono::steady_clock::time_point::max()) wakeIn(aw);
        if (const auto npw = notifPanelWake(app); npw != std::chrono::steady_clock::time_point::max()) wakeIn(npw);
        if (const auto lw = launcherWake(app); lw != std::chrono::steady_clock::time_point::max()) wakeIn(lw);
        if (const auto pkw = polkitWake(app); pkw != std::chrono::steady_clock::time_point::max()) wakeIn(pkw);
        if (const auto sww = switcherWake(app); sww != std::chrono::steady_clock::time_point::max()) wakeIn(sww);
        if (const auto pw = powerWake(app); pw != std::chrono::steady_clock::time_point::max()) wakeIn(pw);
        timersCheck(app);
        if (const auto tw = timersWake(app); tw != std::chrono::steady_clock::time_point::max()) wakeIn(tw);
        if (const auto iw = islandCardWake(app); iw != std::chrono::steady_clock::time_point::max()) wakeIn(iw);
        if (const auto gw = gameWake(app); gw != std::chrono::steady_clock::time_point::max()) wakeIn(gw);
        if (const auto ow = ovWake(app); ow != std::chrono::steady_clock::time_point::max()) wakeIn(ow);
        if (const auto pw = privacyWake(app); pw != std::chrono::steady_clock::time_point::max()) wakeIn(pw);
        if (const auto vw = drivesWake(app); vw != std::chrono::steady_clock::time_point::max()) wakeIn(vw);
        if (const auto bw = btWake(app); bw != std::chrono::steady_clock::time_point::max()) wakeIn(bw);
        if (const auto dw = desktopMenuWake(app); dw != std::chrono::steady_clock::time_point::max()) wakeIn(dw);
        fds[14].fd = app.brightFd;
        if (app.bus) {
            fds[15].fd = sd_bus_get_fd(app.bus);
            fds[15].events = static_cast<short>(sd_bus_get_events(app.bus));
            uint64_t usec = 0;
            if (sd_bus_get_timeout(app.bus, &usec) >= 0 && usec != UINT64_MAX) {
                const int ms = static_cast<int>(std::min<uint64_t>(usec / 1000 + 1, 60000));
                timeoutMs = timeoutMs < 0 ? ms : std::min(timeoutMs, ms);
            }
        }
        fds[10].fd = app.recFd;
        fds[12].fd = app.launcher.mathFd;
        fds[25].fd = app.launcher.fileFd;
        if (app.pkBus) {
            int pr;
            while ((pr = sd_bus_process(app.pkBus, nullptr)) > 0) {}
            sd_bus_flush(app.pkBus);
            fds[20].fd = sd_bus_get_fd(app.pkBus);
            fds[20].events = static_cast<short>(sd_bus_get_events(app.pkBus));
        }
        if (upower.bus) {  // UPower's signals: handle the queue, then ask the bus what to wait for
            while (sd_bus_process(upower.bus, nullptr) > 0) {}
            fds[24].fd = sd_bus_get_fd(upower.bus);
            fds[24].events = static_cast<short>(sd_bus_get_events(upower.bus));
        }
        fds[21].fd = app.pk.fd;
        fds[22].fd = app.amenu.job.fd;
        if (poll(fds, 26, timeoutMs) < 0 && errno != EINTR) {
            wl_display_cancel_read(app.display);
            break;
        }

        if (fds[0].revents & POLLIN) wl_display_read_events(app.display);
        else wl_display_cancel_read(app.display);
        wl_display_dispatch_pending(app.display);

        if (fds[1].revents & POLLIN) {
            uint64_t expirations;
            if (read(timerFd, &expirations, sizeof expirations) > 0) {
                static auto lastBattery = std::chrono::steady_clock::time_point{};
                const auto tickNow = std::chrono::steady_clock::now();
                bool bat = false;
                if (tickNow - lastBattery >= std::chrono::seconds(295)) { lastBattery = tickNow; bat = updateBattery(app); }
                const bool clk = updateClock(app);
                ccRunScript("night-apply");
                if (clk || bat) drawAll(app);
            }
        }
        if (fds[2].revents & POLLIN) app.running = false;
        if (app.bus) {
            int r;
            while ((r = sd_bus_process(app.bus, nullptr)) > 0) {}
            sd_bus_flush(app.bus);
        }
        syncCatcher(app);
        dockSync(app);
        if (app.trayDirty && std::chrono::steady_clock::now() - app.trayRead > std::chrono::milliseconds(300)) {
            app.trayDirty = false;
            app.trayRead = std::chrono::steady_clock::now();
            refreshTray(app);
            drawAll(app);
        }
        expireNotifs(app, std::chrono::steady_clock::now());
        if (fds[16].revents & POLLIN) {
            char junk[64];
            while (read(g_wpWakeFd, junk, sizeof junk) > 0) {}
            collectWallpaperJobs(app);
        }
        {
            const auto frameNow = std::chrono::steady_clock::now();
            for (auto& b : app.bars) stepWallpaperAnim(app, *b, frameNow);
            cwpStep(app, frameNow);
        }
        if (fds[14].revents & (POLLIN | POLLHUP)) {
            char buf[256];
            ssize_t n;
            while ((n = read(app.brightFd, buf, sizeof buf)) > 0) app.brightBuf.append(buf, n);
            if (n == 0) brightnessJobDone(app);
        }
        if (app.brightDirty && app.brightFd < 0 && std::chrono::steady_clock::now() >= app.brightSetAt) {
            const std::string out = *app.brightDirtyOuts.begin();
            app.brightDirtyOuts.erase(app.brightDirtyOuts.begin());
            app.brightDirty = !app.brightDirtyOuts.empty();
            startBrightnessJob(app, "set", app.brightVal[out], out);
        }
        if (fds[13].revents & POLLIN) {
            char junk[256];
            while (read(g_clipFd, junk, sizeof junk) > 0) {}
            drawLauncher(app);
        }
        if (fds[25].revents & (POLLIN | POLLHUP)) {  // `fd` has files for the launcher
            Launcher& L = app.launcher;
            char buf[4096];
            ssize_t n;
            while ((n = read(L.fileFd, buf, sizeof buf)) > 0) L.fileBuf.append(buf, n);
            if (n == 0) {
                close(L.fileFd);
                L.fileFd = -1;
                L.filePid = 0;
                std::istringstream lines(L.fileBuf);
                std::vector<std::string> hits;
                for (std::string line; std::getline(lines, line);) if (!line.empty()) hits.push_back(line);
                const std::string needle = lowerCopy(L.fileFor);
                std::stable_sort(hits.begin(), hits.end(), [&](const std::string& a, const std::string& b) {  // a name that has the word before one that only has it in its folders
                    const bool ia = lowerCopy(a.substr(a.rfind('/') + 1)).find(needle) != std::string::npos, ib = lowerCopy(b.substr(b.rfind('/') + 1)).find(needle) != std::string::npos;
                    return ia != ib ? ia : a.size() < b.size();
                });
                L.fileHits = std::move(hits);
                L.fileBuf.clear();
                updateLauncherResults(app, true);
                drawLauncher(app);
            }
        }
        if (fds[12].revents & (POLLIN | POLLHUP)) {
            Launcher& L = app.launcher;
            char buf[512];
            ssize_t n;
            while ((n = read(L.mathFd, buf, sizeof buf)) > 0) L.mathBuf.append(buf, n);
            if (n == 0) {
                close(L.mathFd);
                L.mathFd = -1;
                L.mathPid = 0;
                if (L.mathIsTr) {
                    L.mathIsTr = false;
                    std::string all = L.mathBuf;
                    while (!all.empty() && all.back() == '\n') all.pop_back();
                    const size_t t1 = all.find('\t'), t2 = t1 == std::string::npos ? t1 : all.find('\t', t1 + 1);
                    if (t2 == std::string::npos || t2 + 1 >= all.size()) { L.trFailed = true; L.trOut.clear(); }
                    else { L.trSrc = all.substr(0, t1); L.trTgt = all.substr(t1 + 1, t2 - t1 - 1); L.trOut = all.substr(t2 + 1); L.trFailed = false; }
                    drawLauncher(app);
                } else {
                std::string answer = L.mathBuf.substr(0, L.mathBuf.find('\n'));
                L.mathResult = trimmed(answer);
                updateLauncherResults(app, true);
                drawLauncher(app);
                }
            }
        }
        if (fds[11].revents & POLLIN) {
            char buf[256];
            ssize_t n;
            int done = 0;
            while ((n = read(g_thumbFd, buf, sizeof buf)) > 0)
                for (ssize_t i = 0; i < n; i++) done += buf[i] == '\n';
            app.sel.inflight = std::max(0, app.sel.inflight - done);
            startThumbJobs(app);
            if (done > 0) drawSelector(app);
        }
        if (fds[10].revents & (POLLIN | POLLHUP | POLLERR)) {
            char junk[16];
            const ssize_t n = read(app.recFd, junk, sizeof junk);
            if (n == 0 || (n < 0 && errno != EAGAIN)) {  // recorder exited (stopped, crashed, or never started)
                close(app.recFd);
                app.recFd = -1;
                app.recording = false;
                app.recPid = 0;
                islandLiveRecording(app, false);
                recAudioCleanup();
                drawAll(app);
            }
        }
        if (upower.bus) {
            while (sd_bus_process(upower.bus, nullptr) > 0) {}
            if (upower.battery) { upower.battery = false; if (updateBattery(app)) drawAll(app); }
            if (upower.devices) {  // a mouse, a keyboard, a headset: ask again in a moment (a burst of signals is one question)
                upower.devices = false;
                if (app.periphSignalDue == std::chrono::steady_clock::time_point{}) app.periphSignalDue = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            }
        }
        if (fds[23].revents & POLLIN) {
            if (powerEventsPending(powerFd) && updateBattery(app)) drawAll(app);
        }
        if (fds[9].revents & POLLIN) {
            char junk[4096];
            while (read(netFd, junk, sizeof junk) > 0) {}
            if (readNetwork(app)) drawAll(app);
        }
        {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::min(0.05, std::chrono::duration<double>(now - lastTick).count());
            lastTick = now;
            if (fds[17].revents & (POLLIN | POLLHUP)) { readCava(app); vizFrame(app, now); }
            tickMediaPanel(app, now);
            tickClockPanel(app, now);
            tickControlCenter(app, now);
            tickSelector(app, now);
            tickPowerMenu(app, now);
            tickSwitcher(app, now);
            tickPolkit(app, now);
            tickLauncher(app, now);
            tickOsd(app, now);
            tickNotifPanel(app, now);
            notifPopupTick(app, now);
            tickAudioMenu(app, now);
            tickBtMenu(app, now);
            tickDrivesMenu(app, now);
            tickIslandCard(app, now);
            tickDesktopMenu(app, now);
            btTick(app, now);
            drivesTick(app, now);
            privacyTick(app, now);
            ovTick(app, now);
            popupKeysSync(app);
            tickGame(app, now);
            if (app.launcher.sliding) { if (app.launcher.open()) drawLauncher(app); else app.launcher.sliding = false; }
            if (app.repeatKey && now >= app.repeatAt) {
                DeskWidget* typer = nullptr;
                for (DeskWidget* w : deskList(app)) if (w->open() && w->kbFocus && w->typing && w->typing(*w)) typer = w;
                if (app.pk.open()) polkitKey(app, app.repeatKey);
                else if (app.clockP.open() && app.clockP.adding) clockPanelKey(app, app.repeatKey);
                else if (gameNotesFocused(app)) gameNotesKey(app, app.repeatKey);
                else if (typer && typer->key) typer->key(app, *typer, app.repeatKey);
                else if (app.dock.gridOpen) dockGridKey(app, app.repeatKey);
                else if (app.launcher.open()) launcherKey(app, app.repeatKey);
                else selectorKey(app, app.repeatKey);
                app.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, app.kbRate)));
            }
            if (app.osd.open() && std::abs(app.osd.shown - app.osd.target) > 0.003) {
                app.osd.shown += (app.osd.target - app.osd.shown) * std::min(1.0, dt * 14);
                if (std::abs(app.osd.shown - app.osd.target) <= 0.003) app.osd.shown = app.osd.target;
                drawOsd(app);
            }
            if (app.sel.open() && app.sel.navAnim.moving()) {
                app.sel.navAnim.stepNow(std::chrono::steady_clock::now());
                drawSelector(app);
            }
            if (app.sel.open() && app.sel.modeT != app.sel.modeTarget) {
                const double step = dt / kModeSeconds;
                app.sel.modeT = app.sel.modeT < app.sel.modeTarget ? std::min(app.sel.modeTarget, app.sel.modeT + step)
                                                                   : std::max(app.sel.modeTarget, app.sel.modeT - step);
                drawSelector(app);
            }
        }
        if (fds[19].revents & POLLIN) onlineDrain(app);
        if (fds[21].revents & (POLLIN | POLLHUP | POLLERR)) pkRead(app);
        if (fds[22].revents & (POLLIN | POLLHUP | POLLERR)) audioJobReadable(app);
        if (fds[18].revents & POLLIN) {
            char junk[64];
            while (read(g_weatherFd, junk, sizeof junk) > 0) {}
            weatherApply(app);
            barModulesApply(app);
            mediaWApply(app);
        }
        if (fds[8].revents & POLLIN) {
            char junk[256];
            while (read(g_artFetchFd, junk, sizeof junk) > 0) {}
            const std::string url = app.musicArtUrl;
            updateCover(app, url);
            if (app.musicArt) {
                drawAll(app);
                if (app.media.open()) drawMediaPanel(app);
                if (app.cc.open()) drawControlCenter(app);
                deskRedrawAll(app);
            }
            if (app.ic.open()) drawIslandCard(app);
        }
        if (fds[7].revents & (POLLIN | POLLHUP)) {
            char buf[1024];
            ssize_t n = read(app.musicFd, buf, sizeof buf);
            if (n == 0) {
                close(app.musicFd);
                app.musicFd = -1;
                fds[7].fd = -1;
                if (parseMusicLine(app, "")) { syncCava(app); drawAll(app); if (app.ic.open()) drawIslandCard(app); }
            } else if (n > 0) {
                musicPending.append(buf, n);
                bool changed = false;
                size_t nl;
                while ((nl = musicPending.find('\n')) != std::string::npos) {
                    changed |= parseMusicLine(app, musicPending.substr(0, nl));
                    musicPending.erase(0, nl + 1);
                }
                if (changed) { syncCava(app); drawAll(app); if (app.ic.open()) drawIslandCard(app); if (app.cfg.dockMedia && app.dock.open() && app.dock.configured) dockMediaChanged(app); }
            }
        }
        if (fds[6].revents & POLLIN) {
            char junk[1024];
            std::string audioEvents;
            ssize_t got;
            while ((got = read(audioFd, junk, sizeof junk)) > 0) audioEvents.append(junk, static_cast<size_t>(got));
            if (audioEventsChangeVolume(audioEvents) && readVolume(app)) { showOsd(app, OsdVolume); drawAll(app); }
            if (app.amenu.open()) {
                drawAudioMenu(app);
                if (audioEventsChangeDevices(audioEvents)) startAudioDevicesJob(app);
            }
            if (quickIsShown(app, kQaMic) && audioEventsChangeMic(audioEvents)) { const bool m = readMicMuted(); if (m != app.micMuted) { app.micMuted = m; drawAll(app); } }
        }
        if (fds[5].revents & POLLIN) {
            char ev[4096];
            bool touched = false, cfgTouched = false;
            ssize_t n;
            while ((n = read(inoFd, ev, sizeof ev)) > 0) {
                for (char* p = ev; p < ev + n;) {
                    auto* e = reinterpret_cast<inotify_event*>(p);
                    if (e->len && e->wd == colorsWd && !std::strcmp(e->name, "colors.json")) touched = true;
                    if (e->len && e->wd == configWd && !std::strcmp(e->name, "config.json")) cfgTouched = true;
                    p += sizeof(inotify_event) + e->len;
                }
            }
            if (touched) {
                loadColors(app); redrawFrames(app); applyBorders(app); drawAll(app); drawSelector(app); deskRedrawAll(app);
                if (app.dmenu.open()) drawDesktopMenu(app);
                if (app.media.open()) drawMediaPanel(app);
                if (app.launcher.open()) drawLauncher(app);
                if (app.dock.open() && app.dock.configured) dockRedraw(app);
            }
            if (cfgTouched && loadConfig(app)) applyConfig(app);
        }
        if (fds[3].revents & POLLIN) handleClient(app, ipcFd);
        if (fds[4].revents & (POLLIN | POLLHUP) && !readWmEvents(app, wmFd, wmPending)) {
            close(wmFd);
            wmFd = -1;
            fds[4].fd = -1;
        }
        if (fds[0].revents & (POLLERR | POLLHUP)) break;
    }

    if (app.recording) stopRecording(app);
    closeDock(app);
    if (netFd >= 0) close(netFd);
    if (powerFd >= 0) close(powerFd);
    if (upower.bus) sd_bus_unref(upower.bus);
    if (app.musicFd >= 0) close(app.musicFd);
    if (app.musicArt) cairo_surface_destroy(app.musicArt);
    if (audioFd >= 0) close(audioFd);  // pactl exits on SIGPIPE
    if (ipcFd >= 0) { close(ipcFd); unlink(sockPath.c_str()); }
    if (wmFd >= 0) close(wmFd);
    for (auto& bar : app.bars) destroyBar(*bar);
    app.bars.clear();
    wl_display_disconnect(app.display);
    return 0;
}
