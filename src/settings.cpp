#include <cairo.h>
#include "i18n.h"
#include "bardivider.h"
#include "oled.h"
#include "bubble.h"
#include "elastic.h"
#include "tabanim.h"
#include "clipboard.h"
#include "textsel_pango.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include "shellfont.h"
#include <poll.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "displaymap.h"
#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

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

namespace {
using Clock = std::chrono::steady_clock;

struct Color { double r = 0, g = 0, b = 0; };
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : ""; }
std::string stateDir() {
    const char* d = std::getenv("XDG_STATE_HOME");
    return (d ? std::string(d) : homeDir() + "/.local/state") + "/end4pC";
}
std::string projectDir() {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        std::string p = exe;
        for (int i = 0; i < 2; i++) p = p.substr(0, p.rfind('/'));
        if (access((p + "/assets").c_str(), R_OK) == 0) return p;
    }
    return homeDir() + "/.config/end4pC";
}
std::string assetsDir() { return projectDir() + "/assets"; }
std::string configHome() { const char* x = std::getenv("XDG_CONFIG_HOME"); return x && *x ? std::string(x) : homeDir() + "/.config"; }
std::string dataHome() { const char* x = std::getenv("XDG_DATA_HOME"); return x && *x ? std::string(x) : homeDir() + "/.local/share"; }
std::string configDir() { return dataHome() + "/end4pC"; }
std::string configPath() {
    const std::string now = configDir() + "/config.json";
    if (access(now.c_str(), F_OK) == 0) return now;
    for (const std::string& old : {configHome() + "/end4pC-user/config.json", projectDir() + "/config.json"})
        if (access(old.c_str(), F_OK) == 0) return old;
    return now;
}
std::string pageFile() { return stateDir() + "/settings-page"; }
void savePage();
double searchFlash(int pi, int ri);  // (Search, below)
void commitText();
std::string colorsPath() { return stateDir() + "/generated/colors.json"; }
std::string runtimeDir() { const char* d = std::getenv("XDG_RUNTIME_DIR"); return d ? d : "/tmp"; }

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool widgetEnabled(const std::string& name) {  // The shell writes these files under ~/.local/state; XDG_STATE_HOME is not followed.
    return readFile(homeDir() + "/.local/state/end4pC/" + name + ".txt").find("enabled\t1") != std::string::npos;
}
time_t mtimeOf(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

bool widgetEnabled(const std::string& name);

std::string ipc(const std::string& cmd) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return "";
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    const std::string path = runtimeDir() + "/end4pC.sock";
    std::snprintf(a.sun_path, sizeof a.sun_path, "%s", path.c_str());
    std::string reply;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        timeval tv{1, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        (void)!write(fd, cmd.data(), cmd.size());
        shutdown(fd, SHUT_WR);
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) reply.append(buf, n);
    }
    close(fd);
    return reply;
}

bool jsonValue(const std::string& text, const std::string& key, std::string& out) {
    const size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k);
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n')) i++;
    if (i < text.size() && text[i] == '"') {
        const size_t e = text.find('"', i + 1);
        if (e == std::string::npos) return false;
        out = text.substr(i + 1, e - i - 1);
        return true;
    }
    size_t e = i;
    while (e < text.size() && (std::isdigit(static_cast<unsigned char>(text[e])) || text[e] == '-')) e++;
    out = text.substr(i, e - i);
    return true;
}
bool jsonValueIs(const std::string& t, const char* key, const char* want) {
    std::string v;
    return jsonValue(t, key, v) && v == want;
}
std::string jsonString(const std::string& text, const std::string& key, const std::string& fallback) {
    const size_t k = text.find("\"" + key + "\"");
    if (k == std::string::npos) return fallback;
    const size_t colon = text.find(':', k);
    if (colon == std::string::npos) return fallback;
    size_t i = colon + 1;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
    if (i >= text.size() || text[i] != '"') return fallback;
    std::string out;
    for (i++; i < text.size() && text[i] != '"'; i++) {
        if (text[i] == '\\' && i + 1 < text.size()) i++;
        out += text[i];
    }
    return i < text.size() ? out : fallback;
}
constexpr const char* kCollageDefaultTree = R"({"t":"leaf","id":1,"img":""})";

enum Kind { Toggle, Choice, Slider, Text, Info, Action, Webapp, Chip, Widgets, Gallery, Accent, Swatch, Split, Scheme, WCards, ThemeCards, CollageLayout, IconStyle, PresetCards, DisplayMap, StyleCards };
std::vector<std::pair<std::string, std::string>> cwpShapeOpts();
using Opts = std::vector<std::pair<std::string, std::string>>;

struct Row {
    Kind kind;
    std::string key, title, desc;
    Opts opts;
    int min = 0, max = 100, step = 1;
    std::string unit;
    std::string section;
    double splitW = 0;
};
struct Page { std::string name, icon; std::vector<Row> rows; bool about = false; bool hidden = false; bool placeholder = false; };

using SectionStarts = std::vector<std::pair<std::string, std::string>>;
void sections(Page& pg, const SectionStarts& starts) {
    std::string cur;
    for (auto& r : pg.rows) {
        for (const auto& [key, name] : starts) if (r.key == key) cur = name;
        r.section = cur;
    }
}

std::string compositorName() {
    auto has = [](const char* v) { const char* e = std::getenv(v); return e && *e; };
    if (has("HYPRLAND_INSTANCE_SIGNATURE")) return "Hyprland";
    if (has("NIRI_SOCKET")) return "Niri";
    if (has("SWAYSOCK")) return "Sway";
    if (has("WAYFIRE_SOCKET")) return "Wayfire";
    if (has("RIVER_SOCKET") || has("RIVERWM")) return "River";
    const char* d = std::getenv("XDG_CURRENT_DESKTOP");
    if (d && *d) {
        std::string n = d;
        for (char& c : n) if (c == ':') { n.resize(&c - &n[0]); break; }
        return n;
    }
    return "Compositor";
}

// ------------------------------------------------------------------------------------------------ Hyprland page
// Its rows are keyed "hypr:<hyprland option>". They are not in config.json: the value is read from Hyprland itself
// (scripts/hyprconf.py --dump) and saved by the same script into the file end4pC uses, hypr/hyprland/shellOverrides/main.lua.
pid_t spawn(const std::vector<std::string>& args);
std::string scriptPath(const char* name);
bool isHyprKey(const std::string& k) { return k.compare(0, 5, "hypr:") == 0; }
const std::map<std::string, int>& hyprFactors() {
    static const std::map<std::string, int> f = {
        {"input:sensitivity", 10}, {"input:touchpad:scroll_factor", 10}, {"decoration:active_opacity", 100}, {"decoration:inactive_opacity", 100},
        {"decoration:dim_strength", 100}, {"decoration:blur:noise", 100}, {"decoration:blur:vibrancy", 100},
        {"dwindle:default_split_ratio", 10}, {"master:mfact", 100}, {"scrolling:column_width", 100}, {"scrolling:follow_min_visible", 100}};
    return f;
}
int hyprFactor(const std::string& key) { auto it = hyprFactors().find(key.substr(5)); return it == hyprFactors().end() ? 1 : it->second; }

std::vector<Row> hyprRows() {
    std::vector<Row> r = {
        {DisplayMap, "mon:map", "Arrangement", "Drag a screen to place it. It snaps to the edges of the others; hold Shift to place it freely.", {}},
        {Choice, "mon:select", "Screen", "Which screen the options below are about.", {}},
        {Toggle, "mon:enabled", "Enabled", "Turn this screen on or off. The last screen on cannot be turned off."},
        {Split, "mon:mode", "Resolution and refresh rate", "The modes this screen offers.", {}},
        {Choice, "mon:scale", "Scale", "Makes everything bigger (above 1) or smaller on this screen.", {{"1", "1"}, {"1.25", "1.25"}, {"1.5", "1.5"}, {"1.75", "1.75"}, {"2", "2"}}},
        {Choice, "mon:transform", "Orientation", "Turn the picture of this screen.", {{"0", "Normal"}, {"1", "90°"}, {"2", "180°"}, {"3", "270°"}}},
        {Text, "mon:position", "Position", "Where this screen sits in the layout, as XxY, like 1920x0 for one to the right of a 1920 wide screen."},
        {Choice, "mon:bitdepth", "Bit depth", "10 bit shows smoother colors, if the screen and the cable can do it.", {{"8", "8 bit"}, {"10", "10 bit"}}},
        {Split, "mon:cm", "Color management", "How colors are handled on this screen. Auto is right for most.", {{"auto", "Auto"}, {"srgb", "sRGB"}, {"wide", "Wide"}, {"edid", "EDID"}, {"hdr", "HDR"}, {"hdredid", "HDR (EDID)"}}},
        {Choice, "hypr:general:layout", "Tiling layout", "How windows share the screen.", {{"dwindle", "Dwindle"}, {"master", "Master"}, {"scrolling", "Scrolling"}}},
        {Toggle, "hypr:dwindle:preserve_split", "Preserve split direction", "Dwindle: keep a split's direction when windows come and go."},
        {Toggle, "hypr:dwindle:smart_split", "Smart split", "Dwindle: split on the side of the window the cursor is nearest to."},
        {Choice, "hypr:dwindle:force_split", "Force split side", "Dwindle: where the new window goes.", {{"0", "Auto"}, {"1", "Left / top"}, {"2", "Right / bottom"}}},
        {Slider, "hypr:dwindle:default_split_ratio", "Default split ratio", "Dwindle: the size of the first window of a split (10 is half).", {}, 2, 18, 1, ""},
        {Choice, "hypr:master:new_status", "New windows become", "Master: what a new window is.", {{"master", "Master"}, {"slave", "Slave"}, {"inherit", "Inherit"}}},
        {Choice, "hypr:master:orientation", "Master position", "Master: where the master window sits.", {{"left", "Left"}, {"right", "Right"}, {"top", "Top"}, {"bottom", "Bottom"}, {"center", "Center"}}},
        {Slider, "hypr:master:mfact", "Master size", "Master: how much of the screen the master window takes.", {}, 10, 90, 5, "%"},
        {Choice, "hypr:scrolling:direction", "Scroll direction", "Scrolling: which way the columns run.", {{"right", "Right"}, {"left", "Left"}, {"down", "Down"}, {"up", "Up"}}},
        {Slider, "hypr:scrolling:column_width", "Column width", "Scrolling: how wide a new column is, as a part of the screen.", {}, 10, 100, 5, "%"},
        {Toggle, "hypr:scrolling:fullscreen_on_one_column", "Fullscreen on one column", "Scrolling: a single column takes the whole screen."},
        {Choice, "hypr:scrolling:focus_fit_method", "Focus fit", "Scrolling: how the focused column is brought into view.", {{"0", "Center"}, {"1", "Fit"}}},
        {Toggle, "hypr:scrolling:follow_focus", "Follow focus", "Scrolling: scroll to the column that gets the focus."},
        {Slider, "hypr:scrolling:follow_min_visible", "Minimum visible", "Scrolling: how much of a column must show before it counts as in view.", {}, 0, 100, 5, "%"},
        {Text, "hypr:input:kb_layout", "Keyboard layout", "Layout codes, like us or us,es."},
        {Text, "hypr:input:kb_variant", "Keyboard variant", "For example intl. Empty for the default."},
        {Text, "hypr:input:kb_options", "Keyboard options", "For example caps:escape. Empty for none."},
        {Toggle, "hypr:input:numlock_by_default", "Numlock by default", "Turn numlock on when the session starts."},
        {Slider, "hypr:input:repeat_delay", "Repeat delay", "How long a key is held before it repeats.", {}, 100, 1000, 10, "ms"},
        {Slider, "hypr:input:repeat_rate", "Repeat rate", "How many times per second a held key repeats.", {}, 10, 100, 1, "/s"},
        {Choice, "hypr:input:follow_mouse", "Follow mouse", "Whether the window under the cursor gets the focus.", {{"0", "Disabled"}, {"1", "Full"}, {"2", "Loose"}, {"3", "Explicit"}}},
        {Slider, "hypr:input:sensitivity", "Pointer sensitivity", "-10 is the slowest, 10 the fastest.", {}, -10, 10, 1, ""},
        {Choice, "hypr:input:accel_profile", "Acceleration profile", "How the pointer speeds up with the mouse.", {{"", "Default"}, {"flat", "Flat"}, {"adaptive", "Adaptive"}}},
        {Toggle, "hypr:input:force_no_accel", "Raw input", "Turn the pointer acceleration off completely."},
        {Toggle, "hypr:input:left_handed", "Left-handed buttons", "Swap the left and right buttons."},
        {Toggle, "hypr:misc:middle_click_paste", "Middle click paste", "Paste the selection with the middle button."},
        {Toggle, "hypr:input:touchpad:natural_scroll", "Natural scroll", "Content follows your fingers."},
        {Toggle, "hypr:input:touchpad:disable_while_typing", "Disable while typing", "Ignore the touchpad while you type."},
        {Toggle, "hypr:input:touchpad:clickfinger_behavior", "Clickfinger behavior", "One, two, three fingers click as left, right, middle."},
        {Toggle, "hypr:input:touchpad:tap-to-click", "Tap to click", "A tap on the touchpad is a click."},
        {Toggle, "hypr:input:touchpad:tap-and-drag", "Tap and drag", "Tap, then drag, to move things."},
        {Toggle, "hypr:input:touchpad:middle_button_emulation", "Middle button emulation", "A three-finger tap is a middle click."},
        {Slider, "hypr:input:touchpad:scroll_factor", "Scroll factor", "How fast the touchpad scrolls (10 is normal).", {}, 1, 30, 1, ""},
        {Split, "idle:lock", "Lock the screen", "After this long without use. Never keeps it unlocked.", {{"0", "Never"}, {"60", "1 minute"}, {"120", "2 minutes"}, {"180", "3 minutes"}, {"300", "5 minutes"}, {"600", "10 minutes"}, {"900", "15 minutes"}, {"1200", "20 minutes"}, {"1800", "30 minutes"}, {"2700", "45 minutes"}, {"3600", "1 hour"}, {"7200", "2 hours"}, {"10800", "3 hours"}, {"18000", "5 hours"}, {"36000", "10 hours"}}},
        {Split, "idle:screenOff", "Turn the screen off", "After this long without use. Never keeps it on.", {{"0", "Never"}, {"60", "1 minute"}, {"120", "2 minutes"}, {"180", "3 minutes"}, {"300", "5 minutes"}, {"600", "10 minutes"}, {"900", "15 minutes"}, {"1200", "20 minutes"}, {"1800", "30 minutes"}, {"2700", "45 minutes"}, {"3600", "1 hour"}, {"7200", "2 hours"}, {"10800", "3 hours"}, {"18000", "5 hours"}, {"36000", "10 hours"}}},
        {Split, "idle:suspend", "Suspend", "Put the computer to sleep after this long without use. Never keeps it awake.", {{"0", "Never"}, {"60", "1 minute"}, {"120", "2 minutes"}, {"180", "3 minutes"}, {"300", "5 minutes"}, {"600", "10 minutes"}, {"900", "15 minutes"}, {"1200", "20 minutes"}, {"1800", "30 minutes"}, {"2700", "45 minutes"}, {"3600", "1 hour"}, {"7200", "2 hours"}, {"10800", "3 hours"}, {"18000", "5 hours"}, {"36000", "10 hours"}}},
        {Slider, "hypr:decoration:rounding", "Window rounding", "How round the window corners are.", {}, 0, 40, 1, "px"},
        {Slider, "hypr:general:border_size", "Border size", "Thickness of the window borders.", {}, 0, 10, 1, "px"},
        {Slider, "hypr:general:gaps_in", "Gaps in", "Space between windows.", {}, 0, 30, 1, "px"},
        {Slider, "hypr:general:gaps_out", "Gaps out", "Space between windows and the screen edge.", {}, 0, 60, 1, "px"},
        {Slider, "hypr:decoration:active_opacity", "Active opacity", "Opacity of the focused window.", {}, 10, 100, 5, "%"},
        {Slider, "hypr:decoration:inactive_opacity", "Inactive opacity", "Opacity of the other windows.", {}, 10, 100, 5, "%"},
        {Toggle, "hypr:decoration:blur:enabled", "Blur", "Blur what is behind see-through windows."},
        {Slider, "hypr:decoration:blur:size", "Blur size", "How far the blur reaches.", {}, 1, 20, 1, ""},
        {Slider, "hypr:decoration:blur:passes", "Blur passes", "More passes: smoother blur, more work for the GPU.", {}, 1, 8, 1, ""},
        {Slider, "hypr:decoration:blur:noise", "Blur noise", "A little grain over the blur.", {}, 0, 100, 1, "%"},
        {Slider, "hypr:decoration:blur:vibrancy", "Blur vibrancy", "How saturated the blurred colors are.", {}, 0, 100, 5, "%"},
        {Toggle, "hypr:decoration:shadow:enabled", "Window shadows", "A shadow under the windows."},
        {Slider, "hypr:decoration:shadow:range", "Shadow range", "How far the shadow spreads.", {}, 1, 60, 1, "px"},
        {Toggle, "hypr:decoration:dim_inactive", "Dim inactive windows", "Make the windows without focus darker."},
        {Slider, "hypr:decoration:dim_strength", "Dim strength", "How dark they get.", {}, 0, 100, 5, "%"},
        {Toggle, "borderColor", "Palette border colors", "Color the window borders from the wallpaper palette. They change with it."},
        {Swatch, "borderActive", "Active border", "Color of the focused window's border.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Slider, "borderActiveOpacity", "Active border opacity", "How strong the focused border is.", {}, 0, 100, 5, "%"},
        {Swatch, "borderInactive", "Inactive border", "Color of the other windows' borders.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Slider, "borderInactiveOpacity", "Inactive border opacity", "How strong the other borders are.", {}, 0, 100, 5, "%"},
        {Toggle, "hypr:animations:enabled", "Animations", "Turn the window and workspace animations on or off."},
        {Choice, "hypr:animPreset", "Preset", "A ready-made set of animations. It replaces the animations file.", {{"fast", "Elastic"}, {"normal", "Normal"}, {"niri", "Niri like"}, {"off", "None"}}},
        {Toggle, "hypr:general:resize_on_border", "Resize on border", "Resize windows by dragging their borders."},
        {Slider, "hypr:general:extend_border_grab_area", "Border grab area", "How far outside the border you can grab it.", {}, 0, 60, 1, "px"},
        {Toggle, "hypr:general:allow_tearing", "Allow tearing", "Lower latency in games, with possible tearing."},
        {Choice, "hypr:cursor:no_hardware_cursors", "Hardware cursor", "Turn it off if the cursor flickers or is missing.", {{"2", "Auto"}, {"0", "On"}, {"1", "Off"}}},
        {Slider, "hypr:cursor:inactive_timeout", "Hide cursor after", "Seconds without moving. 0 never hides it.", {}, 0, 60, 1, "s"},
        {Toggle, "hypr:cursor:hide_on_key_press", "Hide cursor while typing", "The cursor goes away when you press a key."},
        {Choice, "hypr:cursor:warp_on_change_workspace", "Cursor follows workspace", "Move the cursor to the new workspace.", {{"0", "Off"}, {"1", "On"}, {"2", "Force"}}},
        {Toggle, "hypr:misc:focus_on_activate", "Focus on activate", "A window that asks for attention gets the focus."},
        {Choice, "hypr:misc:vrr", "Variable refresh rate", "Adaptive sync, if the screen supports it.", {{"0", "Off"}, {"1", "On"}, {"2", "Fullscreen only"}}},
        {Toggle, "hypr:misc:enable_swallow", "Window swallowing", "A terminal hides while the app it launched is open."},
        {Toggle, "hypr:misc:animate_manual_resizes", "Animate manual resizes", "Animate the size while you drag a window's edge."},
        {Toggle, "hypr:misc:close_special_on_empty", "Close empty special workspace", "Hide the scratchpad when its last window closes."},
        {Toggle, "hypr:misc:disable_hyprland_logo", "Hide the Hyprland logo", "No logo on the empty wallpaper."},
        {Toggle, "hypr:misc:disable_splash_rendering", "Hide the splash text", "No random phrase on the empty wallpaper."},
        {Toggle, "hypr:misc:mouse_move_enables_dpms", "Mouse wakes the screen", "Moving the mouse turns the screen back on."},
        {Toggle, "hypr:misc:key_press_enables_dpms", "Keys wake the screen", "A key press turns the screen back on."},
        {Toggle, "hypr:xwayland:force_zero_scaling", "Sharp XWayland apps", "Force zero scaling: sharp, but small on scaled screens."},
        {Toggle, "hypr:group:auto_group", "Auto-group dragged windows", "A window dropped on another one joins its group."},
        {Toggle, "hypr:binds:workspace_back_and_forth", "Workspace back and forth", "Going to the current workspace goes back to the last one."},
        {Toggle, "hypr:binds:allow_workspace_cycles", "Allow workspace cycles", "Let the back-and-forth ping-pong between workspaces."},
        {Choice, "hypr:render:direct_scanout", "Direct scanout", "Send fullscreen apps straight to the screen.", {{"0", "Off"}, {"1", "On"}, {"2", "Auto"}}},
        {Action, "hyprReset", "Reset", "Put every Hyprland option back as it was before you changed anything here.", {}, 0, 100, 1, "Reset all"},
    };
    return r;
}

std::string captureCmd(const std::string& cmd);
bool isNiriKey(const std::string& k) { return k.compare(0, 5, "niri:") == 0; }
std::vector<Row> niriRows() {
    return {
        {Slider, "niri:gaps", "Gaps", "Space between windows.", {}, 0, 40, 1, "px"},
        {Info, "niri:status", "Not saved", ""},
        {Slider, "niri:strut", "Screen margin", "Space between the windows and the edge of the screen, on top of the gaps.", {}, 0, 60, 1, "px"},
        {Choice, "niri:center", "Center the focused column", "When the focused column is put in the middle of the screen.", {{"never", "Never"}, {"always", "Always"}, {"on-overflow", "When it overflows"}}},
        {Slider, "niri:colWidth", "Column width", "How wide a new window is, as a part of the screen.", {}, 10, 100, 5, "%"},
        {Slider, "niri:radius", "Window rounding", "How round the window corners are. The Settings and Tools windows keep their own.", {}, 0, 40, 1, "px"},
        {Toggle, "niri:border", "Window border", "A border around every window. Its colors follow the wallpaper."},
        {Slider, "niri:borderWidth", "Border size", "Thickness of the window borders.", {}, 1, 10, 1, "px"},
        {Toggle, "borderColor", "Palette border colors", "Color the window borders from the wallpaper palette. They change with it."},
        {Swatch, "borderActive", "Active border", "Color of the focused window's border.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Slider, "borderActiveOpacity", "Active border opacity", "How strong the focused border is.", {}, 0, 100, 5, "%"},
        {Swatch, "borderInactive", "Inactive border", "Color of the other windows' borders.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Slider, "borderInactiveOpacity", "Inactive border opacity", "How strong the other borders are.", {}, 0, 100, 5, "%"},
        {Toggle, "niri:ring", "Focus ring", "A ring around the focused window, outside its border."},
        {Slider, "niri:ringWidth", "Focus ring size", "Thickness of the focus ring.", {}, 1, 10, 1, "px"},
        {Toggle, "niri:shadow", "Window shadows", "A shadow under the windows."},
        {Slider, "niri:shadowSoftness", "Shadow softness", "How far the shadow fades out.", {}, 0, 100, 5, "px"},
        {Toggle, "niri:follow", "Focus follows mouse", "The window under the pointer gets the focus."},
        {Choice, "niri:anim", "Animations", "The speed of the window and workspace animations.", {{"normal", "Normal"}, {"fast", "Fast"}, {"slow", "Slow"}, {"off", "Off"}}},
        {Slider, "niri:repeatDelay", "Repeat delay", "How long a key is held before it repeats.", {}, 100, 1000, 10, "ms"},
        {Slider, "niri:repeatRate", "Repeat rate", "How many times per second a held key repeats.", {}, 5, 100, 1, "/s"},
        {Slider, "niri:accel", "Pointer speed", "-10 is the slowest, 10 the fastest. For the mouse and the touchpad.", {}, -10, 10, 1, ""},
        {Action, "niriReset", "Reset", "Put everything on this page back as it was before you changed anything here.", {}, 0, 100, 1, "Reset all"},
    };
}

std::vector<Page> makePages() {
    std::vector<Page> p;
    p.push_back({"Appearance", "palette", {
        {StyleCards, "colorMode", "Color mode", "Light or dark colors for the whole shell.", {{"dark", "Dark"}, {"light", "Light"}}},
        {Scheme, "colorScheme", "Color scheme", "How colors are generated from the wallpaper.",
         {{"scheme-tonal-spot", "Tonal Spot"}, {"scheme-neutral", "Neutral"}, {"scheme-content", "Content"}, {"scheme-expressive", "Expressive"},
          {"scheme-fidelity", "Fidelity"}, {"scheme-fruit-salad", "Fruit Salad"}, {"scheme-monochrome", "Monochrome"},
          {"scheme-rainbow", "Rainbow"}, {"scheme-vibrant", "Vibrant"}, {"scheme-smart", "Smart"}}},
        {Accent, "accent", "Accent color", "Colors taken from your wallpaper. Pick one and the whole palette is built around it, or pick any color on screen. Neutral then uses Tonal Spot."},
        {Choice, "uiBackground", "UI background", "Themed follows the wallpaper colors. OLED turns the surfaces black.", {{"themed", "Themed"}, {"oled", "OLED"}}},
        {Choice, "uiBackground", "UI background", "Themed follows the wallpaper colors. White turns the main surfaces white.", {{"themed", "Themed"}, {"white", "White"}}},
        {Slider, "barOpacity", "Transparency", "How see-through the bar and the panels are. The blur behind them shows through.", {}, 0, 100, 5, "%"},
        {Text, "fontFamily", "Font", "The font of the whole shell, like Google Sans Flex. Type the family name and press Enter; type - for the system default.", {}},
        {Slider, "fontWeightText", "Text weight", "How heavy the normal text is, with a variable font like Google Sans Flex. 450 is the default.", {}, 300, 700, 10, ""},
        {Slider, "fontWeightTitle", "Title weight", "How heavy bold text and titles are. 550 is the default.", {}, 400, 900, 10, ""},
        {Action, "fontWeightReset", "Default weights", "Back to 450 for the text and 550 for the titles.", {}, 0, 100, 1, "Reset"},
        {Toggle, "materialIcons", "Material Symbols", "Use Google's Material Symbols for every icon instead of the built-in set.", {}},
        {Slider, "iconScale", "Icon size", "How big the icons of the shell are, in either icon set. 100% is the original size.", {}, 60, 150, 10, "%"},
        {IconStyle, "iconStyle", "Icon style", "The look of the Solar icons.", {{"broken", "Broken"}, {"outline", "Outline"}, {"line-duotone", "Line duotone"}, {"bold-duotone", "Bold duotone"}}},
        {ThemeCards, "theme", "Themes", "Ready-made palettes instead of colors taken from the wallpaper. Add your own with the last card, or drop a .json in ~/.local/share/end4pC/themes (see schemes/README.md)."},
        {Swatch, "themePrimary", "Primary color", "The main accent of the theme: buttons, highlights, the focus. Pick another of its colors.", {}},
        {Swatch, "themeSecondary", "Secondary color", "The second accent: tonal buttons and chips.", {}},
        {Swatch, "themeTertiary", "Tertiary color", "The third accent: small details and contrast.", {}},
        {Text, "presetSave", "Save the current look", "A name, and after a comma a description. Press Enter to save. The same name overwrites that preset."},
        {PresetCards, "presets", "Presets", "Click one to apply it. Hover it to overwrite it with the current look or to delete it. It keeps the look and the wallpaper, not your personal settings."},
        {Toggle, "templateKitty", "Kitty", "The shell's colors for kitty, in the same file the original end4-pC setup uses. The include line is added to kitty.conf if it is missing, and open windows follow at once.", {}},
        {Toggle, "templateKde", "KDE", "A color scheme named End4pC for Plasma and the KDE apps, made from the shell's colors and applied at once.", {}},
        {Toggle, "templateGtk", "GTK", "The shell's colors for GTK 3 and 4 apps (gtk.css). The old file is kept next to it as gtk.css.end4pC-backup. Some open apps need a restart.", {}},
    }});
    p.push_back({"Bar", "slider-minimalistic-horizontal", {
        {Choice, "barPosition", "Position", "Which edge of the screen the bar sits on.", {{"top", "Top"}, {"bottom", "Bottom"}, {"left", "Left"}, {"right", "Right"}}},
        {Choice, "barStyle", "Style", "Shape of the bar.", {{"panel", "Panel"}, {"float", "Float"}, {"island", "Island"}, {"hug", "Hug"}, {"notch", "Notch"}, {"split", "Split hug"}}},
        {Slider, "notchWidth", "Notch width", "How wide the Notch bar is, as a share of the screen. 0 makes it only as wide as its widgets need.", {}, 0, 100, 5, "%"},
        {Slider, "islandRounding", "Island rounding", "How round the corners of each island are, as a share of its height. 50 makes them fully round.", {}, 0, 50, 1, "%"},
        {Toggle, "dynamicIsland", "Dynamic island", "A small island in the middle of the bar that replaces the avatar, the clock and the media widget: the avatar and the time, or the cover and a visualizer while music plays. Island and Split hug bars only.", {}},
        {Choice, "groupStyle", "Group style", "How the widgets sit on the bar. Free: each widget has its own look, as you set it in Widgets. The others put every widget on a cell of the bar (Pills: one chain per zone; Separated: a pill each; Transparent: no cells). Their widgets use a look without frame, and the look can not be changed from the popups of the bar.", {{"free", "Free"}, {"transparent", "Transparent"}, {"pills", "Pills"}, {"separated", "Separated"}}},
        {Swatch, "groupColor", "Group color", "The color of the cells.", {{"surface", "Layer 0"}, {"surface_container_low", "Layer 1"}, {"surface_container", "Layer 2"}, {"surface_container_high", "Surface container high"}, {"surface_container_highest", "Surface container highest"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}}},
        {Text, "groupColor", "Custom group color", "Or type your own color, like #1e1e2e, and press Enter. Type - for the default."},
        {Toggle, "islandClick", "Expand island on click", "The island opens when you click it instead of when the pointer rests on it.", {}},
        {Choice, "barScreens", "Bar on screens", "Which screens get the bar. The other ones keep their wallpaper but have no bar.", {}},
        {Toggle, "barShadow", "Bar shadow", "A soft shadow under the bar, in every style. With the bar background off, the widgets keep their own shadow.", {}},
        {Toggle, "barBackground", "Bar background", "Turn it off to hide the bar's background: only the widgets stay, floating over the desktop.", {}},
        {Slider, "frame", "Screen frame", "Thickness of the black frame around the screen.", {}, 0, 12, 1, "px"},
        {Swatch, "frameColor", "Frame color", "The color of the screen frame, and of the rounded corners inside it.", {{"black", "Black"}, {"surface", "Surface"}, {"surface_container", "Surface container"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"outline_variant", "Outline variant"}}},
        {Toggle, "barFollowsFrame", "Bar uses the frame color", "Paints the bar in the frame's color (solid), so the bar and the frame look like one piece.", {}},
        {Choice, "fakeRounding", "Rounded screen corners", "Black corners that make the screen look rounded. With the frame on, they sit inside it.", {{"0", "No"}, {"1", "Yes"}, {"2", "When not fullscreen"}}},
        {Slider, "fakeRoundingSize", "Corner size", "The radius of those corners. 0 uses the same as your windows.", {}, 0, 48, 2, "px"},
        {Choice, "layoutFor", "Layout of", "The horizontal bars (top, bottom) and the vertical ones (left, right) each have their own widgets. Pick which one you edit.", {{"h", "Horizontal bar"}, {"v", "Vertical bar"}}},
        {Slider, "barSpacing", "Spacing", "The space between the widgets of the bar. 4 px by default.", {}, 0, 24, 1, "px"},
        {Slider, "groupSpacing", "Spacing", "The space between the cells of the bar. 2 px by default.", {}, 2, 6, 1, "px"},
        {Slider, "sideBarSpacing", "Spacing", "The space between the widgets of the bar. 4 px by default.", {}, 0, 24, 1, "px"},
        {Widgets, "layoutEditor", "Widgets", "Drag a widget to move it between the left, center and right of the bar (top, middle and bottom in a vertical one). Right-click one to hide it, or drop it on Hidden. Click a placed divider to edit it."},
        {Choice, "workspaceStyle", "Workspaces", "Icons, numbered squares, dots, a pill, ticks or numbered rings.", {{"icons", "Icons"}, {"default", "Squares"}, {"gnome", "Dots"}, {"pill", "Pill"}, {"bar", "Bars"}, {"ring", "Rings"}, {"m3", "M3"}}},
        {Choice, "workspaceStyle", "Workspaces", "Dots or M3: the group styles use these two.", {{"gnome", "Dots"}, {"m3", "M3"}}},
        {Choice, "workspaceNumbers", "Numbers style", "How the workspaces are numbered: 1 2 3, Roman numerals (I II III) or Chinese characters (一 二 三). Only the looks that show numbers.", {{"normal", "Normal"}, {"roman", "Roman"}, {"han", "Han chars"}}},
        {Toggle, "workspaceApps", "Show apps in workspaces", "Shows the icon of the biggest window of each workspace instead of its number or icon. Not for the dots and bar styles.", {}},
        {Slider, "workspaces", "Workspace count", "How many workspaces the bar shows.", {}, 1, 10, 1, ""},
        {Toggle, "batteryPercent", "Show the percentage", "The battery level next to the battery icon in the bar.", {}},
        {Toggle, "networkAlways", "Always show the icon", "Show the network icon even when it is connected. Off: it only appears while there is no connection.", {}},
        {Toggle, "networkSpeed", "Show the speed", "Upload and download speed next to the icon, two small lines.", {}},
        {Toggle, "privacyAlways", "Always show the indicators", "Show the privacy icons even when nothing is using them. Off: they only appear while an app uses the microphone, a camera or the screen.", {}},
        {Toggle, "privacyMic", "Microphone", "Show when an app is listening to the microphone.", {}},
        {Toggle, "privacyCam", "Camera", "Show when an app is using a camera.", {}},
        {Toggle, "privacyScreen", "Screen", "Show when the screen is being shared or recorded.", {}},
        {Toggle, "quick:picker", "Color picker", "Pick any color on the screen and copy it to the clipboard. A right click picks the accent color instead."},
        {Toggle, "quick:record", "Screen recording", "Choose the area, the sound and the frame rate, then record. Click again to stop."},
        {Toggle, "quick:screenshot", "Screenshot", "Select an area, draw on it, then copy or save it."},
        {Toggle, "quick:wallpaper", "Wallpaper selector", "Open the wallpaper selector."},
        {Toggle, "quick:mic", "Microphone", "Mute or unmute the microphone. The icon turns red while it is muted."},
        {Toggle, "quick:darkmode", "Light / dark mode", "Switch between the light and the dark colors."},
        {Toggle, "quick:perfprofile", "Power profile", "Go through power saver, balanced and performance (needs power-profiles-daemon)."},
        {Toggle, "quick:osk", "On-screen keyboard", "Opens and closes the on-screen keyboard (SUPER + K does the same)."},
        {Toggle, "trayPinDefault", "Pin tray icons", "Keep the tray icons in the bar. Off: they sit behind the arrow. Right-click an icon, or middle-click it, to send it the other way.", {}},
        {Toggle, "trayTint", "Tint tray icons", "Draw the tray icons in one color, the shell's, instead of their own.", {}},
        {Toggle, "resCpu", "CPU", "The processor's load.", {}},
        {Toggle, "resRam", "RAM", "How much of the memory is in use.", {}},
        {Toggle, "resTemp", "CPU temperature", "The processor's temperature, on a scale up to 100 °C.", {}},
        {Toggle, "resDisk", "Disk", "How full the main disk is.", {}},
        {Toggle, "resSwap", "Swap", "How much of the swap is in use.", {}},
        {Choice, "resStyle", "Style", "A filling circle, a ring, a label over a bar, bars or a tonal pill.", {{"filled", "Filled"}, {"outline", "Outline"}, {"text", "Text"}, {"bars", "Bars"}, {"pill", "Pill"}, {"m3", "M3"}}},
        {Choice, "resStyle", "Style", "A filling circle, a ring, a label over a bar or a tonal pill.", {{"filled", "Filled"}, {"outline", "Outline"}, {"text", "Text"}, {"pill", "Pill"}, {"m3", "M3"}}},
        {Toggle, "resValue", "Show the value", "The percentage next to each one (under it in a vertical bar).", {}},
        {Slider, "resInterval", "Update every", "How often the numbers are read.", {}, 1000, 10000, 500, "ms"},
        {Slider, "resWarnCpu", "CPU warning", "It turns red at this load.", {}, 50, 100, 5, "%"},
        {Slider, "resWarnMem", "RAM warning", "It turns red at this use.", {}, 50, 100, 5, "%"},
        {Slider, "resWarnSwap", "Swap warning", "It turns red at this use.", {}, 50, 100, 5, "%"},
        {Choice, "dividerItem", "Divider", "Choose a divider in the layout you are editing.", {}},
        {Choice, "dividerItemStyle", "Style", "Choose a line, a dot, or a blank space.", {{"line", "Line"}, {"dot", "Dot"}, {"space", "Space"}}},
        {Slider, "dividerItemMargin", "Margin", "The space on each side of this divider.", {}, 0, bardivider::maxMargin, 1, "px"},
        {Toggle, "aiClaude", "Claude Code", "The tokens used in the current 5 hour window, as a percentage. It shows only while Claude Code has left data on this machine.", {}},
        {Toggle, "aiCodex", "Codex", "The usage limit Codex reports. It shows only while Codex has left data on this machine.", {}},
        {Slider, "aiTokenLimit", "Claude limit", "How many thousands of tokens count as 100 % of the window. Anthropic does not publish the number of your plan, so set the one you reach.", {}, 100, 5000, 100, "k"},
    }});
    p.push_back({"Interface", "layers-minimalistic", {
        {Toggle, "animEnabled", "Animations", "Popups, menus, the launcher and the wallpaper picker grow and settle with a motion. Off: they appear and go at once.", {}},
        {Choice, "animStyle", "Animation style", "Boing grows out of its widget with a bounce; Fade only fades; Slide comes in from the bar's side; Zoom grows from its own middle.", {{"boing", "Boing"}, {"fade", "Fade"}, {"slide", "Slide"}, {"zoom", "Zoom"}}},
        {Slider, "animSpeed", "Animation speed", "How fast the animations run. 100 % is the default.", {}, 50, 200, 10, "%"},
        {Toggle, "hotCorners", "Hot corners", "Act when the pointer is in a screen corner. Scrolling always works: brightness in the top-left, volume in the top-right. A click opens what you choose below.", {}},
        {Toggle, "hotCornerHover", "Open on hover", "Open what a corner holds as soon as the pointer enters it, with no click. Off by default.", {}},
        {Toggle, "hotCornerVisible", "Show the corners", "Paints the regions of the corners, to see where they are.", {}},
        {Slider, "hotCornerWidth", "Corner width", "How wide each corner region is.", {}, 4, 240, 2, "px"},
        {Slider, "hotCornerHeight", "Corner height", "How tall each corner region is.", {}, 4, 240, 2, "px"},
        {Split, "cornerTL", "Top left", "What a click in this corner opens.", {{"none", "Nothing"}, {"controlcenter", "Control center"}, {"media", "Player"}, {"wallpapers", "Wallpapers"}, {"launcher", "Launcher"}, {"overview", "Overview"}, {"notifications", "Notifications"}, {"power", "Power menu"}, {"lock", "Lock screen"}, {"settings", "Settings"}}},
        {Split, "cornerTR", "Top right", "What a click in this corner opens.", {{"none", "Nothing"}, {"controlcenter", "Control center"}, {"media", "Player"}, {"wallpapers", "Wallpapers"}, {"launcher", "Launcher"}, {"overview", "Overview"}, {"notifications", "Notifications"}, {"power", "Power menu"}, {"lock", "Lock screen"}, {"settings", "Settings"}}},
        {Split, "cornerBL", "Bottom left", "What a click in this corner opens.", {{"none", "Nothing"}, {"controlcenter", "Control center"}, {"media", "Player"}, {"wallpapers", "Wallpapers"}, {"launcher", "Launcher"}, {"overview", "Overview"}, {"notifications", "Notifications"}, {"power", "Power menu"}, {"lock", "Lock screen"}, {"settings", "Settings"}}},
        {Split, "cornerBR", "Bottom right", "What a click in this corner opens.", {{"none", "Nothing"}, {"controlcenter", "Control center"}, {"media", "Player"}, {"wallpapers", "Wallpapers"}, {"launcher", "Launcher"}, {"overview", "Overview"}, {"notifications", "Notifications"}, {"power", "Power menu"}, {"lock", "Lock screen"}, {"settings", "Settings"}}},
        {Toggle, "lockOnStart", "Lock on startup", "Show the lock screen when the shell starts with the session, so the desktop is not left open after an automatic login. Restarting the shell does not lock again.", {}},
        {Choice, "lockAnimation", "Lock animation", "How the lock screen comes in and goes out. Split: two halves slide in from the top and bottom (or from the sides) and meet in the middle. Shape: the shape of the centered wallpaper grows from the center until it fills the screen, and shrinks back when you unlock.", {{"none", "None"}, {"split_h", "Split top and bottom"}, {"split_v", "Split sides"}, {"shape", "Shape"}}},
        {Toggle, "unlockKeyring", "Unlock the keyring", "When you unlock the lock screen, also unlock the password keyring (gnome-keyring) with the same password, so apps do not ask for it again. Your API keys for the online wallpapers live there.", {}},
        {Toggle, "dock", "Dock", "A floating dock at the edge of the screen with your pinned apps and the ones that are open."},
        {Choice, "dockPosition", "Position", "Which edge of the screen the dock sits on.", {{"bottom", "Bottom"}, {"left", "Left"}, {"right", "Right"}, {"top", "Top"}}},
        {Choice, "dockScreen", "Dock on screen", "The screen where the dock is shown.", {}},
        {Toggle, "dockLauncher", "Apps button", "A button at the right end of the dock. A click pops a grid of every app over it.", {}},
        {Toggle, "dockHug", "Hug the screen", "The dock sits on the screen edge with flared corners, as the bar does in the hug style, instead of floating.", {}},
        {Slider, "dockRounding", "Rounding", "How round the corners of the dock are. The highest values make a full pill.", {}, 8, 40, 1, "px"},
        {Slider, "dockSpacing", "App spacing", "The space between the icons of the dock.", {}, 0, 24, 1, "px"},
        {Slider, "dockBorder", "Border", "The width of a line around the dock and its apps grid. 0 for none.", {}, 0, 6, 1, "px"},
        {Swatch, "dockBorderColor", "Border color", "The color of that line.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Toggle, "dockMedia", "Media Player", "Shows what is playing in the dock, after the pinned apps: the cover, the artist and the title, with play and pause and next buttons. Right click for previous and the full player.", {}},
        {Toggle, "dockPin", "Pin button", "A button at the start of the dock that turns auto-hide on and off.", {}},
        {Toggle, "dockAutoHide", "Auto-hide", "Hide the dock until the pointer touches its edge of the screen. Off: it stays on screen and windows do not go under it."},
        {Toggle, "dockMagnify", "Magnify icons", "The icons under the pointer grow as it passes over them."},
        {Slider, "dockMagnification", "Magnification", "How big the icon under the pointer gets.", {}, 100, 220, 10, "%"},
        {Slider, "dockIconSize", "Icon size", "The size of the dock icons.", {}, 28, 72, 2, "px"},
        {Text, "dockPinned", "Pinned apps", "Desktop ids or window classes, separated by commas. You can also pin from the right click menu of an icon. Type - for none."},
        {Choice, "osdPosition", "OSD position", "Where the volume and brightness bar appears.", {{"right", "Right"}, {"left", "Left"}, {"top", "Top"}, {"bottom", "Bottom"}}},
        {Slider, "osdTimeout", "OSD time", "How long the volume and brightness bar stays on screen.", {}, 500, 6000, 250, "ms"},
        {Toggle, "overviewLive", "Live windows", "The overview shows the windows moving, as they are. Off: a box with the app icon instead, which is lighter.", {}},
        {Toggle, "overviewLauncher", "Include the launcher", "Opening the overview also opens the launcher, a little lower so it does not cover the strip when this is at the top. Closing one closes the other.", {}},
        {Choice, "overviewPosition", "Strip position", "The edge of the screen where the overview shows every workspace, live.", {{"top", "Top"}, {"bottom", "Bottom"}, {"left", "Left"}, {"right", "Right"}}},
        {Choice, "overviewIcon", "App icon position", "Where the app icon sits on every window of the overview.", {{"left", "Left"}, {"center", "Center"}, {"right", "Right"}}},
        {StyleCards, "switcherStyle", "Window switcher", "How Alt + Tab shows the windows: cards in a grid, or in a row across the middle of the screen.", {{"grid", "Grid"}, {"row", "Row"}}},
        {Choice, "magnifier", "Screen magnifier", "Zoom the whole screen around the pointer. SUPER + = and SUPER + - do the same, one step at a time. It needs Hyprland.", {{"1.0", "Off"}, {"1.3", "1.3x"}, {"1.6", "1.6x"}, {"1.9", "1.9x"}, {"2.2", "2.2x"}, {"2.5", "2.5x"}, {"3.0", "3x"}}},
        {Slider, "sys:cursorSize", "Cursor size", "Make the mouse pointer bigger. It is the same setting as System > Cursor. Applies when you let go of the slider.", {}, 16, 64, 4, "px"},
        {Action, "oskToggle", "On-screen keyboard", "A keyboard on the screen that does not take the focus from the app you type into. Drag it by its top bar. It needs wtype.", {}, 0, 100, 1, "Show or hide"},
    }});
    p.push_back({"Services", "cloud-sun", {
        {Choice, "weatherUnit", "Temperature unit", "Used by the weather on the lock screen.", {{"celsius", "Celsius"}, {"fahrenheit", "Fahrenheit"}}},
        {Slider, "weatherInterval", "Update every", "How often the weather is fetched again while the screen is locked. A fresh answer is not fetched twice within this time.", {}, 5, 120, 5, "min"},
        {Toggle, "weatherTheme", "Use the theme colors", "Paints the weather card of the desktop with the colors of your theme instead of the colors of the sky.", {}},
        {Text, "weatherCity", "Weather city", "Leave it empty to find your place by IP address."},
        {Toggle, "updatesEnable", "Check for updates", "Now and then, count the pending pacman updates (and the AUR and Flatpak ones below, when on)."},
        {Toggle, "updatesAur", "Include the AUR", "Also check and install AUR updates (needs yay or paru)."},
        {Toggle, "updatesFlatpak", "Include Flatpak", "Also check and install Flatpak updates."},
        {Slider, "updatesInterval", "Check every", "How long to wait between two checks.", {}, 15, 1440, 15, "min"},
        {Slider, "updatesMinShow", "Show in the bar from", "The bar shows the updates widget only when at least this many updates are pending. 0 keeps it always visible.", {}, 0, 200, 5, ""},
        {Info, "updSummary", "Pending updates", ""},
        {Action, "updCheck", "Check now", "Look for updates right away.", {}, 0, 100, 1, "Check"},
        {Choice, "lyricsProvider", "Lyrics source", "Where synced lyrics come from. Auto tries every free source in turn and keeps the first answer.", {{"auto", "Auto"}, {"lrclib", "LRCLIB"}, {"betterlyrics", "BetterLyrics"}, {"netease", "NetEase"}, {"kugou", "KuGou"}}},
        {Text, "webappAdd", "Add a webapp", "Type a site address and press Enter. It becomes an app in the launcher (needs Chromium)."},
        {Text, "searchEngine", "Web search address", "Where the launcher sends a web search. The text you type goes at the end."},
        {Toggle, "launcherFiles", "Search files", "The launcher also looks for files: the ones you used lately and, from three letters on, those whose name matches under your home folder. Type /recent for the latest ones.", {}},
        {Text, "prefixAction", "Actions", "Type this first to run shell actions like lock or settings."},
        {Text, "prefixApp", "Apps", "Type this first to search only applications."},
        {Text, "prefixClipboard", "Clipboard", "Type this first to search the clipboard history."},
        {Text, "prefixEmoji", "Emoji", "Type this first to search emojis."},
        {Text, "prefixMath", "Math", "Type this first to calculate."},
        {Text, "prefixShell", "Command", "Type this first to run a command."},
        {Text, "prefixWeb", "Web", "Type this first to search the web."},
        {Text, "prefixKeybind", "Keybinds", "Type this first to search keybinds."},
        {Text, "prefixTranslate", "Translator", "Type this first to translate between English and Spanish."},
    }});
    p.push_back({"System", "cpu", {
        {Split, "sys:cursor", "Cursor theme", "The mouse pointer, for every app.", {}},
        {Slider, "sys:cursorSize", "Cursor size", "Applies when you let go of the slider.", {}, 16, 64, 4, "px"},
        {Split, "sys:icons", "Icon theme", "The icons of the apps and the file manager.", {}},
        {Split, "sys:font", "Interface font", "The font of the windows and menus of other apps.", {}},
        {Slider, "sys:fontSize", "Interface font size", "Applies when you let go of the slider.", {}, 8, 20, 1, "pt"},
        {Split, "sys:mono", "Monospace font", "The font of terminals and code editors.", {}},
        {Slider, "sys:monoSize", "Monospace font size", "Applies when you let go of the slider.", {}, 8, 24, 1, "pt"},
        {Split, "sys:app:browser", "Web browser", "Opens links and web pages.", {}},
        {Split, "sys:app:mail", "Email", "Opens mailto: links.", {}},
        {Split, "sys:app:files", "File manager", "Opens folders.", {}},
        {Split, "sys:app:image", "Images", "Opens pictures.", {}},
        {Split, "sys:app:video", "Video player", "Opens videos.", {}},
        {Split, "sys:app:music", "Music player", "Opens audio files.", {}},
        {Split, "sys:app:pdf", "PDF viewer", "Opens PDF documents.", {}},
        {Split, "sys:app:text", "Text editor", "Opens plain text files.", {}},
        {Text, "screenshotDir", "Screenshots folder", "Where screenshots are saved. Empty: Pictures/Screenshots."},
        {Toggle, "screenshotCopy", "Copy screenshots", "Also put each screenshot in the clipboard, ready to paste."},
        {Choice, "recordFps", "Recording frame rate", "Frames per second. Automatic lets the recorder decide. It can also be changed before recording starts.", {{"0", "Auto"}, {"24", "24"}, {"30", "30"}, {"60", "60"}}},
        {Text, "recordDir", "Recordings folder", "Where screen recordings are saved. Empty: Videos."},
        {Toggle, "recordDesktop", "Record the computer's sound", "Include what the speakers play (music, a video, a call) in screen recordings. It can also be switched before recording starts."},
        {Toggle, "recordAudio", "Record the microphone", "Include your voice in screen recordings. With both on, the two are mixed."},
        {Toggle, "doNotDisturb", "Do not disturb", "Keep notifications in the panel without showing popups."},
        {Slider, "notificationTimeout", "Popup time", "How long a popup stays on screen.", {}, 1000, 10000, 500, "ms"},
        {Choice, "notificationPosition", "Popup position", "Where the notification popups appear.",
         {{"top_left", "Top left"}, {"top_center", "Top center"}, {"top_right", "Top right"}, {"bottom_left", "Bottom left"}, {"bottom_center", "Bottom center"}, {"bottom_right", "Bottom right"}}},
        {Toggle, "notificationAppIcons", "App icons", "A notification without a picture shows the icon of the app that sent it, instead of a symbol."},
        {Toggle, "conflictCheck", "Look for conflicting programs", "At the first start of a session, look for other bars, notification daemons or wallpaper programs that fight with end4pC, and offer to close them. You can also run it by hand with: end4pC conflicts", {}},
        {Info, "periphSummary", "Device batteries", "Mouse, keyboard and headset batteries, from upower."},
        {Toggle, "peripheralNotify", "Low battery alerts", "Notify when the battery of a mouse, keyboard or headset runs low, and again when it is critical."},
        {Slider, "peripheralLow", "Low at", "A device is low at this charge. It also shows in the bar.", {}, 5, 50, 5, "%"},
        {Slider, "peripheralCritical", "Critical at", "The second, urgent notification.", {}, 1, 20, 1, "%"},
    }});
    p.push_back({"General", "settings", {
        {Choice, "language", "Language", "The language of the shell, of this window and of the lock screen. System language follows your session (LANG). A language that is not translated yet shows English. You can fix or add one in ~/.local/share/end4pC/i18n.", {{"", "System language"}, {"ar", "العربية"}, {"be", "Беларуская"}, {"ca", "Català"}, {"cs", "Čeština"}, {"de", "Deutsch"}, {"en", "English"}, {"es", "Español"}, {"fr", "Français"}, {"gl-ES", "Galego"}, {"he", "עברית"}, {"hu", "Magyar"}, {"it", "Italiano"}, {"ja", "日本語"}, {"ko", "한국어"}, {"nl", "Nederlands"}, {"nn", "Norsk nynorsk"}, {"pl", "Polski"}, {"pt-BR", "Português (Brasil)"}, {"ru", "Русский"}, {"sv", "Svenska"}, {"tr", "Türkçe"}, {"uk-UA", "Українська"}, {"vi", "Tiếng Việt"}, {"zh-Hans", "简体中文"}, {"zh-Hant", "繁體中文"}}},
        {Action, "langTemplate", "Translate", "Your language is missing or has mistakes? This opens its file in your text editor, ready to translate: the left side of each line is the English text, change only the right side. Save it and pick the language again to see it. With the system language in English the file is called translate-me.json; rename it to your language code (fr.json, ja.json...).", {}, 0, 100, 1, "Open file"},
        {Text, "preferredPlayer", "Preferred player", "The music player the bar and the player card follow first."},
        {Text, "terminal", "Terminal", "Used to open apps and commands that need one."},
        {Choice, "clock24", "Clock format", "24-hour or 12-hour with am/pm, in the bar.", {{"1", "24-hour"}, {"0", "12-hour"}}},
        {Toggle, "clockDate", "Show the date", "The date next to the time in a horizontal bar."},
        {Text, "worldClocks", "World clocks", "Time zones for the World tab of the clock popup, separated by commas: Europe/Madrid, Asia/Tokyo. Type - for none. The names are in /usr/share/zoneinfo."},
        {Slider, "volumeMax", "Volume limit", "The loudest the volume can be set from the shell: the sliders, the wheel and the hot corner.", {}, 30, 100, 5, "%"},
        {Slider, "volumeStep", "Volume step", "How much one wheel notch changes the volume.", {}, 1, 10, 1, "%"},
        {Split, "sys:sink", "Output device", "Where the sound comes out.", {}},
        {Split, "sys:source", "Input device", "The microphone that apps use by default.", {}},
        {Toggle, "sys:micMute", "Mute the microphone", "Silences the default input for every app."},
        {Slider, "batteryLow", "Low battery", "The battery counts as low at this charge: the red icon, and the bar shows it in the side bar.", {}, 5, 50, 5, "%"},
        {Slider, "batteryCritical", "Critical battery", "The second, urgent warning.", {}, 1, 20, 1, "%"},
        {Toggle, "batteryNotify", "Battery notifications", "Notify when the battery is low and when it is critical. Only on a laptop."},
        {Split, "sys:power", "Power profile", "Performance uses more energy, power saver stretches the battery.", {}},
    }});
    p.push_back({"Desktop", "monitor", {
        {Choice, "wallpaperTransition", "Wallpaper transition", "How a new wallpaper replaces the old one.",
         {{"none", "None"}, {"fade", "Fade"}, {"wipe", "Wipe"}, {"circle", "Circle"}, {"slide", "Slide"}, {"blinds", "Blinds"}, {"diamond", "Diamond"}, {"split", "Split"}, {"random", "Random"}}},
        {Slider, "wallpaperTransitionMs", "Transition length", "How long the transition lasts.", {}, 100, 1500, 50, "ms"},
        {Text, "wallpaperDir", "Wallpaper folder", "Where the wallpaper selector looks for images."},
        {Toggle, "wallpaperAuto", "Change automatically", "The wallpaper changes by itself to a random one of the folder, with the transition above."},
        {Toggle, "wallpaperAutoFavs", "Only favorites", "Pick only from your favorite wallpapers. With none (or none left in the folder) it picks from all of them."},
        {Slider, "wallpaperAutoMin", "Change every", "How long a wallpaper stays before the next one.", {}, 1, 180, 1, "min"},
        {Toggle, "centeredWallpaper", "Centered wallpaper", "The wallpaper rests as a small picture inside a shape, in the middle of a solid color."},
        {Swatch, "centeredWallpaperShape", "Shape", "The shape that holds the picture.", cwpShapeOpts()},
        {Swatch, "centeredWallpaperColor", "Background color", "The color around the shape.", {{"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"outline_variant", "Outline variant"}}},
        {Slider, "centeredWallpaperSize", "Size", "How big the shape is.", {}, 400, 800, 20, "px"},
        {Toggle, "collage", "Multiple wallpapers", "Several pictures as rounded tiles, laid out as you choose, instead of one wallpaper."},
        {CollageLayout, "collageLayout", "", "Pick a layout. The pictures you chose stay, in order."},
        {Slider, "collageGap", "Gap", "Space between two tiles.", {}, 0, 80, 2, "px"},
        {Slider, "collageMargin", "Margin", "Space around the whole area.", {}, 0, 120, 2, "px"},
        {Slider, "collageRadius", "Corner radius", "How round each tile is.", {}, 0, 80, 2, "px"},
        {Slider, "collageBlur", "Backdrop blur", "How much the picture behind the tiles is blurred. 0 keeps it sharp.", {}, 0, 100, 5, "%"},
        {Action, "collageReset", "Reset layout", "Back to one tile with the normal wallpaper.", {}, 0, 100, 1, "Reset"},
        {WCards, "wcards", "Widgets", "The widgets of the desktop. Click one to show or hide it; right click the wallpaper for the same.", {{"todo", "To-Do"}, {"weather", "Weather"}, {"player", "Player"}, {"resources", "Resources"}, {"calendar", "Calendar"}, {"clock", "Clock"}, {"usercard", "User card"}, {"image", "Image"}, {"visualizer", "Visualizer"}, {"text", "Text"}}},
        {Choice, "widgetScreen", "Widgets on screen", "The screen where the desktop widgets (clock, calendar, weather...) live.", {}},
        {Toggle, "widgetsLocked", "Lock widgets", "Keeps the desktop widgets where they are: they cannot be resized or moved while this is on."},
        {Toggle, "widgetShadow", "Widget shadow", "A soft shadow under the desktop widgets."},
        {Text, "customImagePath", "Custom image", "The picture of the image widget (a file path). Clicking the empty widget opens a file chooser."},
        {Choice, "clockStyle", "Clock style", "The face of the clock widget.", {{"cookie", "Cookie"}, {"digital", "Digital"}, {"daily", "Daily"}, {"pixel", "Pixel"}}},
        {Choice, "clockDaily", "Daily layout", "How the daily clock is laid out.", {{"badge", "Badge"}, {"morning", "Morning"}, {"script", "Script"}, {"bold", "Bold"}}},
        {Choice, "clockPixel", "Pixel layout", "The four big digits in a row, or two by two.", {{"horizontal", "Horizontal"}, {"vertical", "Vertical"}}},
        {Choice, "calendarMonday", "Calendar week starts on", "The first day of the week in the calendar widget.", {{"1", "Monday"}, {"0", "Sunday"}}},
        {Slider, "widgetGap", "Screen margin", "How far a desktop widget stays from the edges of the screen. Widgets can overlap each other freely.", {}, 0, 48, 2, "px"},
        {Slider, "cookieSides", "Cookie sides", "How many bumps the cookie has.", {}, 6, 20, 1, ""},
        {Choice, "cookieDial", "Dial", "The marks round the face.", {{"full", "Lines"}, {"dots", "Dots"}, {"numbers", "Numbers"}, {"none", "None"}}},
        {Choice, "cookieHour", "Hour hand", "Solid, only an outline, or a thin classic one.", {{"fill", "Fill"}, {"hollow", "Hollow"}, {"classic", "Classic"}, {"hide", "Hidden"}}},
        {Choice, "cookieMinute", "Minute hand", "How thick the minute hand is.", {{"thin", "Thin"}, {"medium", "Medium"}, {"bold", "Bold"}, {"classic", "Classic"}, {"hide", "Hidden"}}},
        {Choice, "cookieSecond", "Second hand", "A dot, a line or both going round once a minute. Hidden by default.", {{"hide", "Hidden"}, {"dot", "Dot"}, {"line", "Line"}, {"classic", "Classic"}}},
        {Choice, "cookieDate", "Date", "The day in a pentagon and the month in a pill, or just the day in a small box.", {{"bubble", "Bubble"}, {"rect", "Box"}, {"hide", "Hidden"}}},
        {Toggle, "cookieTime", "Time in the middle", "The hour and the minutes in big numbers behind the hands."},
        {Choice, "deskVizStyle", "Design", "How the music is drawn on the desktop, behind the widgets.", {{"bars", "Bars"}, {"reflect", "Reflection"}, {"wave", "Wave"}, {"dots", "Dots"}, {"winamp", "Winamp"}, {"ring", "Ring"}}},
        {Choice, "deskVizPos", "Position", "Where it sits on the screen.", {{"bottom", "Bottom"}, {"top", "Top"}, {"center", "Center"}, {"left", "Left"}, {"right", "Right"}}},
        {Choice, "deskVizColor", "Color", "The gradient of your theme, or a single color.", {{"gradient", "Gradient"}, {"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"on_surface", "Text"}}},
        {Slider, "deskVizSize", "Size", "How tall it is (how wide, on the sides; the diameter of the ring).", {}, 80, 600, 10, "px"},
        {Slider, "deskVizBars", "Bars", "How many bars the music is split into.", {}, 12, 96, 2, ""},
        {Choice, "customTextFont", "Font", "The letters of the text on the desktop. Click the text to write; right click it to resize it.", {{"Display", "Display"}, {"Sans", "Sans"}, {"Serif", "Serif"}, {"Mono", "Mono"}, {"Caveat", "Caveat"}, {"Dancing Script", "Dancing Script"}, {"Pacifico", "Pacifico"}, {"Permanent Marker", "Permanent Marker"}, {"Indie Flower", "Indie Flower"}, {"Great Vibes", "Great Vibes"}, {"League Gothic", "League Gothic"}}},
        {Text, "customTextFontName", "Custom font", "The name of any font installed on your system. When it is filled, it is used instead of the one above."},
        {Swatch, "customTextColor", "Text color", "A color of your theme, or plain white or black.", {{"on_surface", "Text"}, {"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"primary_container", "Primary container"}, {"secondary_container", "Secondary container"}, {"tertiary_container", "Tertiary container"}, {"outline", "Outline"}, {"white", "White"}, {"black", "Black"}}},
        {Choice, "customTextAlign", "Alignment", "How the lines sit in the box.", {{"left", "Left"}, {"center", "Center"}, {"right", "Right"}}},
        {Toggle, "customTextBold", "Bold", "Heavier letters."},
        {Toggle, "customTextItalic", "Italic", "Slanted letters."},
        {Toggle, "customTextShadow", "Shadow", "A soft shadow under the letters, so they read over any wallpaper."},
        {Toggle, "customTextFit", "Fit to the box", "The letters grow or shrink with the box. Turn it off to pick a size."},
        {Slider, "customTextSize", "Letter size", "The size of the letters when they do not fit the box.", {}, 12, 400, 2, "px"},
    }});
    if (compositorName() == "Hyprland") p.push_back({"Hyprland", "widget", hyprRows()});
    else if (compositorName() == "Niri") p.push_back({"Niri", "widget", niriRows()});
    else p.push_back({compositorName(), "widget", {}, false, false, true});
    p.push_back({"About", "info-circle", {}, true});
    p.push_back({"Profile", "user-circle", {
        {Text, "avatarPath", "Avatar folder", "A folder with your pictures. Pick one below."},
        {Gallery, "avatarPicture", "Avatar", "The picture shown in the bar, the control center and here."},
        {Action, "avatarReset", "Default avatar", "Go back to the picture of your Quickshell config, or ~/.face.", {}, 0, 100, 1, "Reset"},
    }, false, true});
    for (auto& pg : p) {
        if (pg.name == "General") sections(pg, {{"language", "Language"}, {"preferredPlayer", "Apps"}, {"clock24", "Time"}, {"volumeMax", "Audio"}, {"batteryLow", "Battery"}});
        else if (pg.name == "Appearance") sections(pg, {{"colorMode", "Colors"}, {"fontFamily", "Font & Icons"}, {"theme", "Themes"}, {"presetSave", "Presets"}, {"templateKitty", "Templates"}});
        else if (pg.name == "Bar") sections(pg, {{"barPosition", "Position & Style"}, {"layoutFor", "Widgets"}, {"workspaceStyle", "Workspaces"}, {"networkAlways", "Network"}, {"privacyAlways", "Privacy"}, {"quick:picker", "Quick actions"}, {"trayPinDefault", "Tray"}, {"batteryPercent", "Battery"}, {"resCpu", "Resources"}, {"dividerItem", "Divider"}, {"aiClaude", "AI usage"}});
        else if (pg.name == "Hyprland") sections(pg, {{"mon:map", "Displays"}, {"hypr:general:layout", "Layout"}, {"hypr:input:kb_layout", "Keyboard"}, {"hypr:input:sensitivity", "Mouse"}, {"hypr:input:touchpad:natural_scroll", "Touchpad"}, {"idle:lock", "Idle"}, {"hypr:decoration:rounding", "Look"}, {"borderColor", "Borders"}, {"hypr:animations:enabled", "Animations"}, {"hypr:general:resize_on_border", "Windows"}, {"hypr:misc:focus_on_activate", "Misc"}});
        else if (pg.name == "Niri") sections(pg, {{"niri:gaps", "Layout"}, {"niri:radius", "Windows"}, {"niri:follow", "Behavior"}, {"niri:repeatDelay", "Input"}, {"niriReset", "Reset"}});
        else if (pg.name == "Desktop") sections(pg, {{"wallpaperTransition", "Wallpaper"}, {"centeredWallpaper", "Centered wallpaper"}, {"collage", "Multiple wallpapers"}, {"wcards", "Widgets"}, {"cookieSides", "Cookie clock"}, {"deskVizStyle", "Visualizer"}, {"customTextFont", "Text"}});
        else if (pg.name == "System") sections(pg, {{"sys:cursor", "Cursor"}, {"sys:icons", "Icons"}, {"sys:font", "Fonts"}, {"sys:app:browser", "Default Apps"}, {"screenshotDir", "Capture"}, {"doNotDisturb", "Notifications"}, {"periphSummary", "Devices"}});
        else if (pg.name == "Interface") sections(pg, {{"animEnabled", "Animations"}, {"hotCorners", "Hot Corners"}, {"lockOnStart", "Lock screen"}, {"dock", "Dock"}, {"osdPosition", "OSD"}, {"overviewLive", "Overview"}, {"switcherStyle", "Window switcher"}, {"magnifier", "Accessibility"}});
        else if (pg.name == "Services") sections(pg, {{"weatherUnit", "Weather"}, {"updatesEnable", "Updates"}, {"lyricsProvider", "Lyrics"}, {"webappAdd", "Webapps"}, {"searchEngine", "Search"}});
        if (pg.name == "System")
            for (auto& r : pg.rows) if (r.key.compare(0, 8, "sys:app:") == 0) r.splitW = 280;
    }
    if (compositorName() != "Hyprland" && compositorName() != "Niri")
        for (auto& pg : p) std::erase_if(pg.rows, [](const Row& r) { return r.key == "borderColor"; });
    // Sidebar order follows makePages(): the compositor's own page is the Hyprland slot; Profile is hidden and reached from the profile card.
    const std::vector<std::string> order = {"Appearance", "Bar", "Interface", "Desktop", "Hyprland", "System", "General", "Services", "About"};
    const auto rank = [&](const Page& pg) {
        const auto it = std::find(order.begin(), order.end(), pg.name);
        if (it != order.end()) return static_cast<int>(it - order.begin());
        return pg.name == "Profile" ? 100 : 4;
    };
    std::stable_sort(p.begin(), p.end(), [&](const Page& a, const Page& b) { return rank(a) < rank(b); });
    return p;
}

struct ModInfo { const char* id; const char* label; const char* icon; };
const ModInfo kMods[] = {{"workspaces", "Workspaces", "widget-4"}, {"media", "Player", "music-note"}, {"quickactions", "Quick actions", "bolt"},
                         {"network", "Network", "wi-fi-router"}, {"volume", "Volume", "volume-loud"}, {"bluetooth", "Bluetooth", "bluetooth"}, {"drives", "Removable drives", "flash-drive"}, {"privacy", "Privacy indicators", "shield-check"}, {"clock", "Clock", "clock-circle"},
                         {"notifications", "Notifications", "bell"}, {"settings", "Settings", "settings"}, {"avatar", "Avatar", "user-circle"}, {"battery", "Battery", "battery-full"}, {"systray", "System tray", "widget-2"}, {"resources", "Resources", "cpu"}, {"launcher", "Launcher", "magnifer"}, {"power", "Power", "power"}, {"tools", "Tools", "ruler-pen/list"}, {"updates", "Updates", "download-square"},
                         {"weather", "Weather", "cloud-sun"}, {"aiusage", "AI usage", "magic-stick-3"}, {"keyboard", "Keyboard layout", "keyboard"}, {"activewindow", "Active window", "window-frame"}, {"divider", "Divider", "menu-dots"},
                         {"visualizer", "Visualizer", "soundwave"}, {"docktopanel", "Dock to panel", "layers-minimalistic"}};
const ModInfo* modInfo(const std::string& id) {
    const std::string_view widget = bardivider::isDivider(id) ? "divider" : std::string_view(id);
    for (const auto& m : kMods) if (widget == m.id) return &m;
    return nullptr;
}
struct EdChip { Rect r; std::string id; int lane = 0, idx = 0; bool gap = false; };
struct EdGeom { Rect lane[4]; std::vector<EdChip> chips; double h = 0; };
struct EdDrag { double pressX = 0, pressY = 0; bool on = false, moved = false; std::string id; int from = 0, fromIdx = 0; int hoverLane = -1, hoverIdx = 0; double w = 90; };

struct Hit {
    Rect r;
    int row = -1;
    int opt = -1;
    Kind kind = Toggle;
};
struct RowLayout { double y = 0, h = 0, ctrlX = 0, ctrlW = 0, textH = 0; std::vector<Rect> chips; Rect ctrl; double titleH = 0; };
struct Layout { std::vector<RowLayout> rows; double contentH = 0; };

constexpr double kMargin = 16, kSideW = 248, kGap = 16, kRadius = 28, kHeader = 88, kPadX = 36;
constexpr int kDefaultW = 1000, kDefaultH = 720;

struct WebappItem { std::string id, name, url, icon; };

struct App {
    std::vector<WebappItem> webapps;
    std::vector<int> secSel;
    double pillA = 1, pillTarget = 1;
    TabAnim secAnim, sideAnim;
    int secForce = -1;
    double secPrevScroll = 0;
    pid_t createPid = 0;
    std::set<pid_t> webPids;           // helper processes to reap; the list reloads when one ends
    bool updChecking = false;
    Clock::time_point updCheckStart;
    time_t updStamp = 0;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;

    bool running = true, configured = false, dirty = true;
    int width = kDefaultW, height = kDefaultH;

    std::map<std::string, Color> colors;
    std::map<std::string, std::string> val;
    std::vector<Page> pages = makePages();
    int page = 0, prevPage = 0;
    double animT = 1;
    int animDir = 1;
    Clock::time_point animStart;
    double scroll = 0;
    time_t colorsStamp = 0, configStamp = 0;
    std::string iconStyle = "broken";

    double px = 0, py = 0;
    bool inside = false;
    int hoverSide = -1;
    bool hoverClose = false;
    int hoverHit = -1;
    std::vector<Hit> hits;
    int dragHit = -1;
    std::vector<std::string> lay[3];
    std::vector<std::pair<int, size_t>> dividerLocations;
    std::string otherLayout;
    EdDrag drag;
    EdGeom edGeom;
    Clock::time_point lastSend;

    bool searchOpen = false, hoverSearch = false, hoverSearchClear = false, hoverResult = false;
    std::string searchQ;
    size_t searchCaret = 0, searchAnchor = 0;
    int searchSel = 0;
    double searchScroll = 0, searchTextX = 0, searchPointerX = -1, searchPointerY = -1;
    int flashPage = -1, flashRow = -1;
    Clock::time_point flashAt;
    int focusRow = -1;
    std::string edit;
    size_t caret = 0, anchor = 0;
    textsel::Clicks clicks;
    bool selDrag = false;
    textsel::Drag dragInfo;
    double fieldTx = 0;
    Rect fieldR{};
    Clock::time_point dragTick = Clock::now();
    Clock::time_point caretReset = Clock::now();

    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    int kbRate = 25, kbDelay = 500;
    uint32_t repeatKey = 0;
    Clock::time_point repeatAt;

    Color color(const char* n) const {
        auto it = colors.find(n);
        return it == colors.end() ? Color{} : it->second;
    }
};

App g;
PangoLayout* g_measure = nullptr;

void loadColors() {
    g.colors.clear();
    std::ifstream f(colorsPath());
    std::string line;
    while (std::getline(f, line)) {
        const size_t q1 = line.find('"');
        const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        const size_t h = line.find('#', q2 == std::string::npos ? 0 : q2);
        if (q2 == std::string::npos || h == std::string::npos || line.size() < h + 7) continue;
        const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(h + 1, 6).c_str(), nullptr, 16));
        g.colors[line.substr(q1 + 1, q2 - q1 - 1)] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
    }
    g.colorsStamp = mtimeOf(colorsPath());
    const bool lightMode = jsonValueIs(readFile(configPath()), "colorMode", "light");
    if (lightMode && jsonValueIs(readFile(configPath()), "uiBackground", "white"))
        for (const char* n : {"background", "surface", "surface_container_lowest"}) g.colors[n] = Color{1, 1, 1};
    if (jsonValueIs(readFile(configPath()), "uiBackground", "oled") && !lightMode) {
        for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) g.colors[n] = Color{0, 0, 0};
        for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
            auto it = g.colors.find(n);
            if (it != g.colors.end()) it->second = Color{it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
        }
    }
}

std::string g_profPic;
void loadProfile();
bool loadWidgetStates() {
    bool changed = false;
    for (const char* n : {"todo", "weather", "player", "resources", "calendar", "clock", "usercard", "image", "visualizer", "text"}) {
        const std::string v = widgetEnabled(n) ? "1" : "0", k = std::string("w:") + n;
        if (g.val[k] != v) { g.val[k] = v; changed = true; }
    }
    return changed;
}

const char* layoutLaneKey(int lane) {
    static constexpr const char* horizontal[] = {"left", "center", "right"};
    static constexpr const char* vertical[] = {"sideLeft", "sideCenter", "sideRight"};
    return (g.val["layoutFor"] == "v" ? vertical : horizontal)[lane];
}

bardivider::Options readDividerOptions(const std::string& entry) {
    return bardivider::parse(entry).value_or(bardivider::Options{
        bardivider::parseStyle(g.val["dividerStyle"]).value_or(bardivider::Style::Line),
        std::clamp(std::atoi(g.val["dividerSpacing"].c_str()), 0, bardivider::maxMargin)});
}

void refreshDividerChoices() {
    static constexpr const char* horizontal[] = {"Left", "Center", "Right"};
    static constexpr const char* vertical[] = {"Top", "Middle", "Bottom"};
    const auto names = g.val["layoutFor"] == "v" ? vertical : horizontal;
    g.dividerLocations.clear();
    Opts choices;
    for (int lane = 0; lane < 3; lane++) {
        int number = 0;
        for (size_t index = 0; index < g.lay[lane].size(); index++) {
            if (!bardivider::isDivider(g.lay[lane][index])) continue;
            choices.emplace_back(std::to_string(choices.size()), std::string(i18n::tr(names[lane])) + " " + std::to_string(++number));
            g.dividerLocations.emplace_back(lane, index);
        }
    }
    for (auto& page : g.pages)
        for (auto& row : page.rows)
            if (row.key == "dividerItem") row.opts = choices;
    if (choices.empty()) return;
    const size_t selected = std::min<size_t>(std::max(0, std::atoi(g.val["dividerItem"].c_str())), choices.size() - 1);
    g.val["dividerItem"] = choices[selected].first;
    const auto [lane, index] = g.dividerLocations[selected];
    const auto options = readDividerOptions(g.lay[lane][index]);
    g.val["dividerItemStyle"] = bardivider::styleName(options.style);
    g.val["dividerItemMargin"] = std::to_string(options.margin);
}

void loadConfig() {
    const std::string t = readFile(configPath());
    for (const auto& d : std::vector<std::pair<const char*, const char*>>{{"dividerStyle", "line"}, {"overviewLive", "1"}, {"switcherStyle", "grid"}, {"overviewLauncher", "0"}, {"overviewIcon", "right"}, {"overviewPosition", "top"}, {"privacyMic", "1"}, {"privacyCam", "1"}, {"privacyScreen", "1"}, {"weatherTheme", "0"}, {"materialIcons", "0"}, {"iconScale", "100"}, {"fontWeightText", "450"}, {"fontWeightTitle", "550"}, {"launcherFiles", "0"}, {"batteryPercent", "1"}, {"templateKitty", "0"}, {"templateGtk", "0"}, {"templateKde", "0"}, {"aiClaude", "1"}, {"aiCodex", "1"}, {"aiTokenLimit", "1000"}, {"weatherInterval", "10"}, {"resCpu", "1"}, {"resRam", "1"}, {"resTemp", "0"}, {"resDisk", "0"}, {"resSwap", "0"}, {"resValue", "0"}, {"resInterval", "3000"}, {"resWarnCpu", "90"}, {"resWarnMem", "95"}, {"resWarnSwap", "85"}, {"resStyle", "filled"}, {"trayPinDefault", "0"}, {"trayTint", "0"}, {"notificationPosition", "top_right"}, {"notificationAppIcons", "0"}, {"conflictCheck", "1"}, {"clock24", "1"}, {"clockDate", "1"}, {"workspaceApps", "0"}, {"volumeMax", "100"}, {"volumeStep", "2"}, {"batteryLow", "20"}, {"batteryCritical", "5"}, {"batteryNotify", "1"}, {"widgetsLocked", "0"}, {"widgetShadow", "1"}, {"widgetGap", "12"}, {"calendarMonday", "1"}})
        g.val.insert(d);
    if (const std::string zoom = ipc("magnifier get"); !zoom.empty() && std::isdigit(static_cast<unsigned char>(zoom[0]))) g.val["magnifier"] = zoom.substr(0, 3);
    g.val.insert({"lyricsProvider", "auto"});
    g.val.insert({"updatesEnable", "1"});
    for (const auto& d : std::vector<std::pair<const char*, const char*>>{{"hotCornerWidth", "6"}, {"hotCornerHeight", "6"}, {"hotCornerVisible", "0"}, {"hotCornerHover", "0"}}) g.val.insert(d);
    g.val.insert({"cornerTL", "none"});
    g.val.insert({"cornerTR", "controlcenter"});
    g.val.insert({"cornerBL", "media"});
    g.val.insert({"cornerBR", "wallpapers"});
    for (const char* n : {"picker", "record", "screenshot", "wallpaper"}) g.val.insert({std::string("quick:") + n, "1"});
    for (const char* n : {"mic", "darkmode", "perfprofile", "osk"}) g.val.insert({std::string("quick:") + n, "0"});
    g.val.insert({"recordFps", "0"});
    g.val.insert({"recordDesktop", "0"});
    g.val.insert({"cookieSides", "14"});
    g.val.insert({"fakeRounding", "0"});
    g.val.insert({"unlockKeyring", "1"});
    g.val.insert({"frameColor", "black"});
    g.val.insert({"barFollowsFrame", "0"});
    g.val.insert({"lockOnStart", "0"});
    g.val.insert({"lockAnimation", "none"});
    g.val.insert({"barSpacing", "4"});
    g.val.insert({"barShadow", "0"});
    g.val.insert({"dynamicIsland", "0"});
    g.val.insert({"islandClick", "0"});
    g.val.insert({"groupStyle", "free"});
    g.val.insert({"groupSpacing", "2"});
    g.val.insert({"groupColor", "surface_container_high"});
    g.val.insert({"dividerSpacing", "0"});
    jsonValue(t, "dividerStyle", g.val["dividerStyle"]);
    jsonValue(t, "dividerSpacing", g.val["dividerSpacing"]);
    g.val.insert({"workspaceNumbers", "normal"});
    g.val.insert({"sideBarSpacing", "4"});
    g.val.insert({"animEnabled", "1"});
    g.val.insert({"animStyle", "boing"});
    g.val.insert({"animSpeed", "100"});
    g.val.insert({"osdPosition", "right"});
    g.val.insert({"osdTimeout", "2000"});
    g.val.insert({"dock", "0"});
    g.val.insert({"dockAutoHide", "0"});
    g.val.insert({"dockPin", "0"});
    g.val.insert({"dockMedia", "0"});
    g.val.insert({"dockLauncher", "0"});
    g.val.insert({"dockHug", "0"});
    g.val.insert({"dockPosition", "bottom"});
    g.val.insert({"dockScreen", ""});
    g.val.insert({"wallpaperAuto", "0"});
    g.val.insert({"wallpaperAutoMin", "30"});
    g.val.insert({"wallpaperAutoFavs", "0"});
    g.val.insert({"dockRounding", "40"});
    g.val.insert({"dockSpacing", "6"});
    g.val.insert({"dockBorder", "0"});
    g.val.insert({"dockBorderColor", "primary"});
    g.val.insert({"dockMagnify", "1"});
    g.val.insert({"dockMagnification", "140"});
    g.val.insert({"dockIconSize", "44"});
    g.val.insert({"dockPinned", "kitty,firefox,org.kde.dolphin"});
    g.val.insert({"fakeRoundingSize", "0"});
    g.val.insert({"centeredWallpaper", "0"});
    g.val.insert({"centeredWallpaperShape", "Cookie7Sided"});
    g.val.insert({"centeredWallpaperSize", "400"});
    g.val.insert({"centeredWallpaperColor", "primary_container"});
    g.val.insert({"collage", "0"});
    g.val.insert({"collageGap", "30"});
    g.val.insert({"collageMargin", "30"});
    g.val.insert({"collageRadius", "30"});
    g.val.insert({"collageBlur", "50"});
    g.val.insert({"collagePrimary", "1"});
    g.val.insert({"collageTree", kCollageDefaultTree});
    g.val.insert({"cookieTime", "1"});
    g.val.insert({"cookieDial", "full"});
    g.val.insert({"cookieHour", "fill"});
    g.val.insert({"cookieMinute", "medium"});
    g.val.insert({"cookieSecond", "hide"});
    g.val.insert({"cookieDate", "bubble"});
    g.val.insert({"theme", ""});
    g.val.insert({"themePrimary", ""});
    g.val.insert({"themeSecondary", ""});
    g.val.insert({"themeTertiary", ""});
    g.val.insert({"clockDaily", "badge"});
    g.val.insert({"clockPixel", "horizontal"});
    for (const auto& d : std::vector<std::pair<const char*, const char*>>{{"deskVizStyle", "bars"}, {"deskVizPos", "bottom"}, {"deskVizColor", "gradient"}, {"deskVizSize", "220"}, {"deskVizBars", "48"}, {"customTextFont", "Display"}, {"customTextFontName", ""}, {"customTextColor", "on_surface"}, {"customTextAlign", "center"}, {"customTextFit", "1"}, {"customTextSize", "56"}, {"customTextBold", "0"}, {"customTextItalic", "0"}, {"customTextShadow", "1"}}) g.val.insert(d);
    g.val.insert({"language", ""});
    g.val.insert({"updatesAur", "0"});
    g.val.insert({"updatesFlatpak", "0"});
    g.val.insert({"updatesInterval", "60"});
    g.val.insert({"updatesMinShow", "0"});
    g.val.insert({"peripheralNotify", "1"});
    g.val.insert({"peripheralLow", "20"});
    g.val.insert({"peripheralCritical", "5"});
    for (const auto& pg : g.pages)
        for (const auto& r : pg.rows) {
            std::string v;
            if (jsonValue(t, r.key, v)) g.val[r.key] = v;
        }
    g.val["collageTree"] = jsonString(t, "collageTree", kCollageDefaultTree);
    {
        std::string pos = "top", list;
        jsonValue(t, "barPosition", pos);
        jsonValue(t, "sideWidgetStyles", list);
        if (pos == "left" || pos == "right") {
            struct Look { const char* mod; const char* key; std::vector<std::string> allowed; };
            static const std::vector<Look> looks = {{"workspaces", "workspaceStyle", {"icons", "default", "gnome", "pill", "bar", "ring", "m3"}},
                                                    {"resources", "resStyle", {"filled", "outline", "text", "m3"}}, {"clock", "barClock", {"pill", "outline", "m3"}}};
            for (const Look& l : looks) {
                std::string chosen;
                const std::string tag = std::string(l.mod) + "=";
                for (size_t at = 0; at < list.size();) {
                    size_t end = list.find(',', at);
                    if (end == std::string::npos) end = list.size();
                    if (list.compare(at, tag.size(), tag) == 0) chosen = list.substr(at + tag.size(), end - at - tag.size());
                    at = end + 1;
                }
                const auto ok = [&](const std::string& v) { return std::find(l.allowed.begin(), l.allowed.end(), v) != l.allowed.end(); };
                if (ok(chosen)) g.val[l.key] = chosen;
                else if (!ok(g.val[l.key])) g.val[l.key] = l.allowed.front();
            }
        }
    }
    {
        static std::string lastLang = "\x01";
        const std::string lang = g.val["language"];
        if (lang != lastLang) { lastLang = lang; i18n::load(lang, assetsDir(), configDir() + "/i18n"); }
    }
    {
        std::string q = "picker,record,screenshot,wallpaper";
        jsonValue(t, "quickActions", q);
        std::set<std::string> on;
        size_t from = 0;
        while (from <= q.size()) {
            const size_t comma = q.find(',', from);
            on.insert(q.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
        for (const char* n : {"picker", "record", "screenshot", "wallpaper", "mic", "darkmode", "perfprofile", "osk"}) g.val[std::string("quick:") + n] = on.count(n) ? "1" : "0";
    }
    if (!g.val.count("layoutFor")) {
        std::string pos = "top";
        jsonValue(t, "barPosition", pos);
        g.val["layoutFor"] = pos == "left" || pos == "right" ? "v" : "h";
    }
    if (!g.drag.on) {
        const bool side = g.val["layoutFor"] == "v";
        const char* names[3] = {"left", "center", "right"};
        const size_t base = t.find(side ? "\"sideLayout\"" : "\"layout\"");
        for (int k = 0; k < 3; k++) {
            g.lay[k].clear();
            if (base == std::string::npos && side) {
                static const std::vector<std::string> classic[3] = {{"avatar", "workspaces"}, {}, {"systray", "battery", "notifications", "settings", "clock"}};
                g.lay[k] = classic[k];
                continue;
            }
            const size_t at = base == std::string::npos ? base : t.find(std::string("\"") + names[k] + "\"", base);
            const size_t lb = at == std::string::npos ? at : t.find('[', at), rb = lb == std::string::npos ? lb : t.find(']', lb);
            if (rb == std::string::npos) continue;
            size_t q = lb;
            while ((q = t.find('"', q + 1)) != std::string::npos && q < rb) {
                const size_t e = t.find('"', q + 1);
                if (e == std::string::npos || e > rb) break;
                g.lay[k].push_back(t.substr(q + 1, e - q - 1));
                q = e;
            }
        }
    }
    {
        const std::string key = g.val["layoutFor"] == "v" ? "\"layout\"" : "\"sideLayout\"";
        const size_t b = t.find(key), e = b == std::string::npos ? b : t.find('}', b);
        g.otherLayout = e == std::string::npos ? "" : t.substr(b, e - b);
    }
    refreshDividerChoices();
    loadWidgetStates();
    if (g.focusRow < 0) g.configStamp = mtimeOf(configPath());
    const std::string st = g.val.count("iconStyle") ? g.val["iconStyle"] : "broken";
    g.iconStyle = st;
    matsym::enabled() = g.val.count("materialIcons") && g.val["materialIcons"] == "1";
    loadColors();
    if (g_profPic != (g.val.count("avatarPicture") ? g.val["avatarPicture"] : "")) loadProfile();
}

std::mutex g_preMx;
std::map<std::string, std::string> g_pre;
std::thread g_preThread;
std::string runCapture(const std::string& cmd) {
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        pclose(f);
    }
    return out;
}
std::string captureCmd(const std::string& cmd) {
    if (g_preThread.joinable()) g_preThread.join();
    {
        std::lock_guard<std::mutex> lk(g_preMx);
        auto it = g_pre.find(cmd);
        if (it != g_pre.end()) { std::string v = std::move(it->second); g_pre.erase(it); return v; }
    }
    return runCapture(cmd);
}
std::string monitorsCmd() { return "python3 '" + scriptPath("hyprconf.py") + "' --monitors 2>/dev/null"; }
std::string idleCmd() { return "python3 '" + scriptPath("hyprconf.py") + "' --idle-get 2>/dev/null"; }
std::string dumpCmd() {
    std::string cmd = "python3 '" + scriptPath("hyprconf.py") + "' --dump";
    for (const auto& pg : g.pages)
        for (const auto& r : pg.rows)
            if (isHyprKey(r.key) && r.key != "hypr:animPreset") cmd += " " + r.key.substr(5);
    return cmd + " 2>/dev/null";
}
void preloadHypr() {
    const std::vector<std::string> cmds = {monitorsCmd(), idleCmd(), dumpCmd()};
    g_preThread = std::thread([cmds] {
        for (const auto& c : cmds) {
            std::string out = runCapture(c);
            std::lock_guard<std::mutex> lk(g_preMx);
            g_pre[c] = std::move(out);
        }
    });
}
std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        out.push_back(l);
    }
    return out;
}

struct Mon { std::string name, mode, bitdepth, cm, desc; int x = 0, y = 0, transform = 0; std::string scale; bool disabled = false; std::vector<std::string> modes; };
std::vector<Mon> g_mons;
size_t g_monSel = 0;

constexpr double kMapH = 240, kMapBtnH = 40;
struct MapView { double scale = 0.1, tx = 0, ty = 0; };
struct MapDrag {
    bool on = false, moved = false;
    size_t idx = 0;
    double pressX = 0, pressY = 0;
    int origX = 0, origY = 0, curX = 0, curY = 0;
    MapView view;
} g_mapDrag;
MapView g_mapView;
int g_mapCount = 0;
dm::Screen monScreen(const Mon& m) {
    int pw = 1920, ph = 1080, w = 0, h = 0;
    dm::parseMode(m.mode, pw, ph);
    dm::logicalSize(pw, ph, std::atof(m.scale.c_str()), m.transform, w, h);
    return dm::Screen{m.name, dm::Rect{m.x, m.y, w, h}, m.disabled};
}
std::vector<dm::Screen> monScreens() {
    std::vector<dm::Screen> v;
    for (size_t i = 0; i < g_mons.size(); i++) {
        v.push_back(monScreen(g_mons[i]));
        if (g_mapDrag.on && g_mapDrag.idx == i) { v.back().r.x = g_mapDrag.curX; v.back().r.y = g_mapDrag.curY; }
    }
    return v;
}
MapView mapViewFor(const Rect& canvas, const std::vector<dm::Screen>& screens) {
    MapView v;
    if (screens.empty()) return v;
    const dm::Rect b = dm::bounds(screens);
    int big = 1;
    for (const auto& s : screens) big = std::max({big, s.r.w, s.r.h});
    const double extra = 0.16 * big, pad = 16;
    const double worldW = b.w + 2 * extra, worldH = b.h + 2 * extra;
    v.scale = std::min((canvas.w - 2 * pad) / worldW, (canvas.h - 2 * pad) / worldH);
    v.tx = canvas.x + canvas.w / 2 - (b.x + b.w / 2.0) * v.scale;
    v.ty = canvas.y + canvas.h / 2 - (b.y + b.h / 2.0) * v.scale;
    return v;
}
Row* hyprRow(const std::string& key) {
    for (auto& pg : g.pages)
        for (auto& r : pg.rows) if (r.key == key) return &r;
    return nullptr;
}
std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
void showMonitor() {
    if (g_mons.empty()) return;
    const Mon& m = g_mons[std::min(g_monSel, g_mons.size() - 1)];
    g.val["mon:select"] = m.name;
    g.val["mon:enabled"] = m.disabled ? "0" : "1";
    g.val["mon:mode"] = m.mode;
    g.val["mon:scale"] = m.scale;
    g.val["mon:transform"] = std::to_string(m.transform);
    g.val["mon:position"] = std::to_string(m.x) + "x" + std::to_string(m.y);
    g.val["mon:bitdepth"] = m.bitdepth;
    g.val["mon:cm"] = m.cm;
    if (Row* r = hyprRow("mon:mode")) {
        r->opts.clear();
        for (const auto& md : m.modes) r->opts.push_back({md, md});
    }
}
void loadMonitors() {
    g_mons.clear();
    for (const std::string& l : linesOf(captureCmd(monitorsCmd()))) {
        const auto p = splitOn(l, '|');
        if (p.size() < 10) continue;
        Mon m;
        m.name = p[0]; m.mode = p[1]; m.x = std::atoi(p[2].c_str()); m.y = std::atoi(p[3].c_str());
        m.scale = p[4]; m.transform = std::atoi(p[5].c_str()); m.disabled = p[6] == "1"; m.bitdepth = p[7]; m.cm = p[8];
        m.modes = splitOn(p[9], ',');
        if (p.size() > 10) m.desc = p[10];
        g_mons.push_back(m);
    }
    if (g_monSel >= g_mons.size()) g_monSel = 0;
    if (Row* r = hyprRow("mon:select")) {
        r->opts.clear();
        for (const auto& m : g_mons) r->opts.push_back({m.name, m.name});
    }
    showMonitor();
}
std::vector<std::string> g_screens;
void loadScreens() {
    g_screens.clear();
    const std::string screenList = compositorName() == "Niri" ? "niri msg --json outputs 2>/dev/null | jq -r 'keys[]'"
                                 : compositorName() == "Sway" ? "swaymsg -r -t get_outputs 2>/dev/null | jq -r '.[].name' 2>/dev/null"
                                                              : "hyprctl monitors -j 2>/dev/null | jq -r '.[].name' 2>/dev/null";
    for (const std::string& l : linesOf(captureCmd(screenList)))
        if (!l.empty()) g_screens.push_back(l);
    const size_t n = g_screens.size();
    if (Row* r = hyprRow("barScreens")) {
        r->opts.clear();
        r->opts.push_back({"", "All screens"});
        for (unsigned mask = 1; mask < (1u << n) - 1 && n <= 20; mask++) {
            if (n > 3 && (mask & (mask - 1))) continue;
            std::string value, label;
            for (size_t i = 0; i < n; i++)
                if (mask & (1u << i)) { value += (value.empty() ? "" : ",") + g_screens[i]; label += (label.empty() ? "" : " + ") + g_screens[i]; }
            r->opts.push_back({value, label});
        }
    }
    if (Row* r = hyprRow("dockScreen")) {
        r->opts.clear();
        r->opts.push_back({"", "First screen"});
        for (const auto& name : g_screens) r->opts.push_back({name, name});
    }
    if (Row* r = hyprRow("widgetScreen")) {
        r->opts.clear();
        r->opts.push_back({"", "First screen"});
        for (const auto& name : g_screens) r->opts.push_back({name, name});
    }
}
void monitorSend(const std::string& key, const std::string& value) {
    if (g_mons.empty()) return;
    if (key == "mon:select") {
        for (size_t i = 0; i < g_mons.size(); i++) if (g_mons[i].name == value) g_monSel = i;
        showMonitor();
        return;
    }
    Mon& m = g_mons[std::min(g_monSel, g_mons.size() - 1)];
    std::string field = key.substr(4), v = value;
    if (key == "mon:enabled") {
        int on = 0;
        for (const auto& o : g_mons) on += o.disabled ? 0 : 1;
        if (value == "0" && on <= 1 && !m.disabled) return;
        field = "disabled";
        v = value == "0" ? "1" : "0";
        m.disabled = v == "1";
    } else if (key == "mon:position") {
        int px = 0, py = 0;
        char tail = 0;
        if (std::sscanf(value.c_str(), "%dx%d%c", &px, &py, &tail) != 2) return;
        m.x = px; m.y = py;
    } else if (key == "mon:mode") m.mode = value;
    else if (key == "mon:scale") m.scale = value;
    else if (key == "mon:transform") m.transform = std::atoi(value.c_str());
    else if (key == "mon:bitdepth") m.bitdepth = value;
    else if (key == "mon:cm") m.cm = value;
    g.val[key] = value;
    spawn({"python3", scriptPath("hyprconf.py"), "--monitor", m.name, "--set", field, v});
}

std::string g_armed;
std::string g_splitOpen;
double g_splitT = 1, g_splitScroll = 0;
std::chrono::steady_clock::time_point g_splitStart;
Rect g_splitRect;
bool g_hyprLoaded = false;
std::string idleLabel(long sec) {
    if (sec <= 0) return "Never";
    if (sec % 3600 == 0) return std::to_string(sec / 3600) + (sec == 3600 ? " hour" : " hours");
    if (sec % 60 == 0) return std::to_string(sec / 60) + (sec == 60 ? " minute" : " minutes");
    return std::to_string(sec) + " seconds";
}
void loadIdle() {
    for (const std::string& l : linesOf(captureCmd(idleCmd()))) {
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = "idle:" + l.substr(0, eq), sec = l.substr(eq + 1);
        g.val[key] = sec;
        if (Row* r = hyprRow(key)) {
            bool known = false;
            for (const auto& o : r->opts) if (o.first == sec) known = true;
            if (!known) r->opts.push_back({sec, idleLabel(std::atol(sec.c_str()))});
        }
    }
}
void idleSend(const std::string& key, const std::string& value) {
    g.val[key] = value;
    spawn({"python3", scriptPath("hyprconf.py"), "--idle", key.substr(5), value});
}

void loadHypr() {
    if (g_hyprLoaded) return;
    g_hyprLoaded = true;
    loadMonitors();
    loadIdle();
    for (const std::string& l : linesOf(captureCmd(dumpCmd()))) {
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = "hypr:" + l.substr(0, eq), v = l.substr(eq + 1);
        const int fac = hyprFactor(key);
        g.val[key] = fac > 1 ? std::to_string(static_cast<int>(std::lround(std::atof(v.c_str()) * fac))) : v;
    }
    const char* home = std::getenv("HOME");
    const std::string a = readFile(std::string(home ? home : "") + "/.config/hypr/hyprland/shellOverrides/animations.lua");
    g.val["hypr:animPreset"] = a.find("pc_wobble") != std::string::npos ? "fast" : a.find("emphasizedDecel") != std::string::npos ? "normal" : a.find("niri_wobble") != std::string::npos ? "niri" : (a.empty() && g.val["hypr:animations:enabled"] == "0") ? "off" : "";
}

void hyprSend(const std::string& key, const std::string& ui) {
    g.val[key] = ui;
    if (key == "hypr:animPreset") { spawn({"python3", scriptPath("hyprconf.py"), "--anim-preset", ui}); g.val["hypr:animations:enabled"] = ui == "off" ? "0" : "1"; return; }
    const int fac = hyprFactor(key);
    std::string v = ui;
    if (fac > 1) { char b[32]; std::snprintf(b, sizeof b, "%g", std::atoi(ui.c_str()) / double(fac)); v = b; }
    spawn({"python3", scriptPath("hyprconf.py"), "--set", key.substr(5), v});
}

bool g_niriLoaded = false;
std::time_t g_niriStatusStamp = 0;
std::string niriCmd(const std::string& args) { return "python3 '" + scriptPath("niriconf.py") + "' " + args + " 2>/dev/null"; }
std::string niriStatusFile() {
    const char* st = std::getenv("XDG_STATE_HOME");
    const char* home = std::getenv("HOME");
    return (st && *st ? std::string(st) : std::string(home ? home : "") + "/.local/state") + "/end4pC/niri-status.txt";
}
void niriShowStatus() {
    std::string t = readFile(niriStatusFile());
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    g.val["niri:status"] = t;
    if (Row* r = hyprRow("niri:status")) r->desc = t.compare(0, 7, "error: ") == 0 ? t.substr(7) : t;
    g_niriStatusStamp = mtimeOf(niriStatusFile());
}
void loadNiri() {
    if (g_niriLoaded) return;
    g_niriLoaded = true;
    std::string cmd = "--dump";
    for (const auto& pg : g.pages)
        for (const auto& r : pg.rows)
            if (isNiriKey(r.key) && r.key != "niri:status") cmd += " " + r.key.substr(5);
    for (const std::string& l : linesOf(captureCmd(niriCmd(cmd)))) {
        const size_t eq = l.find('=');
        if (eq != std::string::npos) g.val["niri:" + l.substr(0, eq)] = l.substr(eq + 1);
    }
    niriShowStatus();
}
void niriSend(const std::string& key, const std::string& ui) {
    g.val[key] = ui;
    spawn({"python3", scriptPath("niriconf.py"), "--set", key.substr(5), ui});
}
bool niriPoll() {
    const std::time_t m = mtimeOf(niriStatusFile());
    if (m == g_niriStatusStamp) return false;
    niriShowStatus();
    if (g.val["niri:status"].compare(0, 5, "error") == 0) { g_niriLoaded = false; loadNiri(); }
    return true;
}

bool g_sysLoaded = false;
bool g_sysHasPower = false;
std::string sysCmd(const std::string& script, const std::string& args) { return "python3 -I '" + scriptPath(script.c_str()) + "' " + args + " 2>/dev/null"; }
std::string g_sysBundle;
std::thread g_sysThread;
bool g_sysStarted = false;
void sysPreload() {
    if (g_sysStarted) return;
    g_sysStarted = true;
    g_sysThread = std::thread([] { g_sysBundle = runCapture(sysCmd("system_session.py", "--bundle")); });
}
std::string sysPretty(const std::string& key, const std::string& v) {
    if (key == "sys:power") return v == "power-saver" ? "Power saver" : v == "balanced" ? "Balanced" : v == "performance" ? "Performance" : v;
    return v;
}
void loadSystem() {
    if (g_sysLoaded) return;
    g_sysLoaded = true;
    sysPreload();
    if (g_sysThread.joinable()) g_sysThread.join();
    std::map<std::string, std::vector<std::string>> sec;
    std::string cur;
    for (const std::string& l : linesOf(g_sysBundle)) {
        if (l.compare(0, 2, "@@") == 0) { cur = l.substr(2); sec[cur]; }
        else if (!cur.empty() && !l.empty()) sec[cur].push_back(l);
    }
    for (auto& pg : g.pages)
        for (auto& r : pg.rows) {
            if (r.kind != Split || r.key.compare(0, 4, "sys:") != 0) continue;
            r.opts.clear();
            for (const std::string& line : sec[r.key]) {
                const size_t bar = line.find('|');
                const std::string val = bar == std::string::npos ? line : line.substr(0, bar);
                r.opts.push_back({val, bar == std::string::npos ? sysPretty(r.key, val) : line.substr(bar + 1)});
            }
        }
    for (const char* name : {"get-appearance", "get-apps", "get-audio"})
        for (const std::string& l : sec[name]) {
            const size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            g.val[std::string(name) == "get-apps" ? "sys:app:" + l.substr(0, eq) : "sys:" + l.substr(0, eq)] = l.substr(eq + 1);
        }
    const std::string power = sec["get-power"].empty() ? "" : sec["get-power"][0];
    g.val["sys:power"] = power;
    g_sysHasPower = !power.empty();
    g.dirty = true;
}
std::string shq(const std::string& v);

struct ThemeInfo { std::string id, name, source; std::string cols[7]; };
void loadThemeAccents();
std::vector<ThemeInfo> g_themes;
std::string g_themesMode;
time_t g_themesStamp = 0;
struct PresetInfo { std::string name, desc, wall, mode, scheme, theme; };
std::vector<PresetInfo> g_presets;
std::string g_presetConfirm;
std::map<std::string, cairo_surface_t*> g_presetThumbs;
void loadPresets() {
    namespace fs = std::filesystem;
    g_presets.clear();
    std::vector<std::pair<time_t, PresetInfo>> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(configDir() + "/presets", ec)) {
        if (entry.path().extension() != ".json") continue;
        const std::string text = readFile(entry.path().string());
        if (text.empty()) continue;
        PresetInfo p;
        p.name = entry.path().stem().string();
        p.desc = jsonString(text, "description", "");
        p.wall = jsonString(text, "wallpaper", "");
        p.mode = jsonString(text, "colorMode", "");
        p.scheme = jsonString(text, "colorScheme", "");
        p.theme = jsonString(text, "theme", "");
        found.emplace_back(mtimeOf(entry.path().string()), std::move(p));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& f : found) g_presets.push_back(std::move(f.second));
    if (Row* r = hyprRow("presets")) {
        r->opts.clear();
        for (const auto& p : g_presets) r->opts.push_back({p.name, p.name});
    }
    g.dirty = true;
}

cairo_surface_t* coverSurface(const std::string& path, int w, int h);
constexpr int kPresetThumbW = 320, kPresetThumbH = 180;
cairo_surface_t* presetThumb(const std::string& wall) {
    if (auto it = g_presetThumbs.find(wall); it != g_presetThumbs.end()) return it->second;
    const char* xc = std::getenv("XDG_CACHE_HOME");
    const std::string dir = (xc && *xc ? std::string(xc) : homeDir() + "/.cache") + "/end4pC/preset-thumbs";
    char key[40];
    std::snprintf(key, sizeof key, "%016zx", std::hash<std::string>{}(wall + "|" + std::to_string(mtimeOf(wall))));
    const std::string png = dir + "/" + key + ".png";
    cairo_surface_t* pic = cairo_image_surface_create_from_png(png.c_str());
    if (cairo_surface_status(pic) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(pic);
        pic = coverSurface(wall, kPresetThumbW, kPresetThumbH);
        if (pic) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            cairo_surface_write_to_png(pic, png.c_str());
        }
    }
    return g_presetThumbs[wall] = pic;
}
std::string presetSchemeName(std::string s) {
    if (s.compare(0, 7, "scheme-") == 0) s.erase(0, 7);
    bool up = true;
    for (char& c : s) {
        if (c == '-') { c = ' '; up = true; }
        else if (up) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); up = false; }
    }
    return s;
}
std::string themesDir() { return configDir() + "/themes"; }
void loadThemes() {
    g_themesMode = g.val.count("colorMode") && g.val["colorMode"] == "light" ? "light" : "dark";
    g_themesStamp = mtimeOf(themesDir());
    g_themes.clear();
    for (const std::string& l : linesOf(runCapture(sysCmd("../matugen/named_scheme.py", "--list " + g_themesMode)))) {
        const auto f = splitOn(l, '\t');
        if (f.size() < 10) continue;
        ThemeInfo t;
        t.id = f[0]; t.name = f[1]; t.source = f[2];
        for (int k = 0; k < 7; k++) t.cols[k] = f[3 + k];
        g_themes.push_back(t);
    }
    if (Row* r = hyprRow("theme")) {
        r->opts.clear();
        r->opts.push_back({"", "Wallpaper colors"});
        for (const auto& t : g_themes) r->opts.push_back({t.id, t.name});
        r->opts.push_back({"+", "Add theme"});
    }
    loadThemeAccents();
    g.dirty = true;
}
std::map<std::string, std::string> g_themeAcc;
std::string g_themeDef[3];
std::string g_accFor;
void loadThemeAccents() {
    g_themeAcc.clear();
    for (auto& d : g_themeDef) d.clear();
    const std::string id = g.val["theme"];
    g_accFor = id;
    std::vector<std::pair<std::string, std::string>> opts;
    if (!id.empty()) {
        for (const std::string& l : linesOf(runCapture(sysCmd("../matugen/named_scheme.py", "--accents " + shq(id) + " " + g_themesMode)))) {
            const auto f = splitOn(l, '\t');
            if (f.size() == 4 && f[0] == "@defaults") { for (int k = 0; k < 3; k++) g_themeDef[k] = f[1 + k]; }
            else if (f.size() == 2) {
                std::string label = f[0];
                if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                opts.push_back({f[0], label});
                g_themeAcc[f[0]] = f[1];
            }
        }
    }
    for (const char* key : {"themePrimary", "themeSecondary", "themeTertiary"})
        if (Row* r = hyprRow(key)) r->opts = opts;
    g.dirty = true;
}
void importTheme() {
    const std::string script = scriptPath("../matugen/named_scheme.py");
    const std::string cmd = "p=$(zenity --file-selection --title='Choose a theme (.json)' --file-filter='JSON | *.json') && [ -n \"$p\" ] && { out=$(python3 -I " + shq(script) +
                            " --import \"$p\") || notify-send -a end4pC 'Theme not added' \"$out\"; }";
    spawn({"bash", "-c", cmd});
}

std::string shq(const std::string& v) {
    std::string o = "'";
    for (char c : v) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}
void sysSend(const std::string& key, const std::string& value) {
    g.val[key] = value;
    auto v = [&](const char* k, const char* def) { const auto it = g.val.find(std::string("sys:") + k); return it != g.val.end() && !it->second.empty() ? it->second : std::string(def); };
    std::string cmd;
    if (key == "sys:cursor" || key == "sys:cursorSize") cmd = sysCmd("system_appearance.py", "--set cursor " + shq(v("cursor", "Adwaita")) + " " + v("cursorSize", "24"));
    else if (key == "sys:icons") cmd = sysCmd("system_appearance.py", "--set icons " + shq(v("icons", "Adwaita")));
    else if (key == "sys:font" || key == "sys:fontSize") cmd = sysCmd("system_appearance.py", "--set font ui " + shq(v("font", "Sans")) + " " + v("fontSize", "11"));
    else if (key == "sys:mono" || key == "sys:monoSize") cmd = sysCmd("system_appearance.py", "--set font mono " + shq(v("mono", "Monospace")) + " " + v("monoSize", "11"));
    else if (key.compare(0, 8, "sys:app:") == 0) cmd = sysCmd("system_session.py", "--apps-set " + key.substr(8) + " " + shq(value));
    else if (key == "sys:sink" || key == "sys:source") cmd = sysCmd("system_session.py", "--audio-set " + shq(value));
    else if (key == "sys:micMute") cmd = sysCmd("system_session.py", "--mic-mute " + std::string(value == "1" ? "1" : "0"));
    else if (key == "sys:power") cmd = sysCmd("system_session.py", "--power-set " + shq(value));
    if (!cmd.empty()) spawn({"bash", "-c", cmd});
}

void openTranslation() {
    const std::string chosen = g.val.count("language") ? g.val["language"] : "";
    std::string tag = chosen.empty() ? i18n::systemLanguage() : i18n::normalize(chosen);
    if (tag.empty() || tag == "en" || tag.rfind("en-", 0) == 0) tag = "translate-me";
    const std::string dir = configDir() + "/i18n", file = dir + "/" + tag + ".json";
    if (!i18n::exists(file)) {
        std::string src = i18n::findFile(assetsDir() + "/i18n", tag);
        if (src.empty()) src = assetsDir() + "/i18n/en.json";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::filesystem::copy_file(src, file, ec);
        if (ec) return;
    }
    spawn({"bash", "-c", "d=$(xdg-mime query default text/plain); { [ -n \"$d\" ] && command -v gtk-launch >/dev/null && gtk-launch \"${d%.desktop}\" \"$1\"; } || xdg-open \"$1\"", "sh", file});
}

bool edCenterLocked();
void sendSet(const std::string& key, const std::string& value) {
    if (key.compare(0, 6, "quick:") == 0) {
        g.val[key] = value;
        std::string list;
        for (const char* n : {"picker", "record", "screenshot", "wallpaper", "mic", "darkmode", "perfprofile", "osk"})
            if (g.val[std::string("quick:") + n] == "1") list += (list.empty() ? "" : ",") + std::string(n);
        sendSet("quickActions", list);
        g.dirty = true;
        return;
    }
    if (key == "magnifier") { g.val[key] = value; ipc("magnifier set " + value); g.dirty = true; return; }
    if (key.compare(0, 4, "sys:") == 0) { sysSend(key, value); g.dirty = true; return; }
    if (key.compare(0, 5, "idle:") == 0) { idleSend(key, value); g.dirty = true; return; }
    if (key.compare(0, 4, "mon:") == 0) { monitorSend(key, value); g.dirty = true; return; }
    if (key == "layoutFor") { g.val[key] = value; g.val.erase("dividerItem"); loadConfig(); g.dirty = true; return; }
    if (key == "dividerItem") {
        g.val[key] = value;
        refreshDividerChoices();
        g.dirty = true;
        return;
    }
    if (key == "dividerItemStyle" || key == "dividerItemMargin") {
        const size_t selected = std::atoi(g.val["dividerItem"].c_str());
        if (selected >= g.dividerLocations.size()) return;
        const auto [lane, index] = g.dividerLocations[selected];
        const auto& entries = g.lay[lane];
        if (index >= entries.size() || !bardivider::isDivider(entries[index])) return;
        auto options = readDividerOptions(entries[index]);
        if (key == "dividerItemStyle") options.style = bardivider::parseStyle(value).value_or(options.style);
        else options.margin = std::clamp(std::atoi(value.c_str()), 0, bardivider::maxMargin);
        auto changed = entries;
        changed[index] = bardivider::encode(options);
        std::string list;
        for (const auto& entry : changed) list += (list.empty() ? "" : ",") + entry;
        if (ipc(std::string("set ") + layoutLaneKey(lane) + " " + list) == "ok\n") g.lay[lane] = std::move(changed);
        refreshDividerChoices();
        g.dirty = true;
        return;
    }
    if (isHyprKey(key)) { hyprSend(key, value); g.dirty = true; return; }
    if (isNiriKey(key)) { niriSend(key, value); g.dirty = true; return; }
    g.val[key] = value;
    ipc("set " + key + " " + (value.empty() ? "-" : value));
    if ((key == "dynamicIsland" || key == "barStyle") && edCenterLocked()) g.lay[1].clear();
    if (key == "fontFamily" || key == "fontWeightText" || key == "fontWeightTitle") {  // live: this window changes at once, the shell gets the same setting
        const std::string fam = key == "fontFamily" ? (value == "-" ? "" : value) : (g.val["fontFamily"] == "-" ? "" : g.val["fontFamily"]);
        applyShellFont(fam, std::atoi(g.val["fontWeightText"].c_str()), std::atoi(g.val["fontWeightTitle"].c_str()));
    }
    if (key == "materialIcons") matsym::enabled() = value == "1";
    g.configStamp = 0;
}

void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}
// A clip to the current path for a stack of layers, cut once (same idea as in the shell, src/parts/04_theme_config.inc): clipping each
// layer separately leaves a fringe of the layers underneath along the anti-aliased edge of a curve.
struct ClipGroup { cairo_path_t* path = nullptr; };
inline void beginClipGroup(cairo_t* cr, ClipGroup& g) {
    g.path = cairo_copy_path(cr);
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    cairo_path_extents(cr, &x1, &y1, &x2, &y2);
    cairo_new_path(cr);
    cairo_rectangle(cr, std::floor(x1), std::floor(y1), std::ceil(x2) - std::floor(x1), std::ceil(y2) - std::floor(y1));
    cairo_clip(cr);
    cairo_push_group(cr);
}
inline void endClipGroup(cairo_t* cr, ClipGroup& g) {
    cairo_pop_group_to_source(cr);
    cairo_new_path(cr);
    cairo_append_path(cr, g.path);
    cairo_fill(cr);
    cairo_path_destroy(g.path);
    g.path = nullptr;
}
void src(cairo_t* cr, const Color& c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
Color mix(const Color& a, const Color& b, double p) { return {a.r * p + b.r * (1 - p), a.g * p + b.g * (1 - p), a.b * p + b.b * (1 - p)}; }

std::string g_face;  // when set, text is drawn in this family (the font choices of the text widget show each name in its own letters)
std::string faceOf(const std::string& rowKey, const std::string& opt) {
    if (rowKey != "customTextFont") return "";
    return opt == "Display" ? "Google Sans Flex" : opt == "Mono" ? "Monospace" : opt;
}
void setFont(PangoLayout* l, double px, bool bold) {
    PangoFontDescription* d = pango_font_description_from_string(g_face.empty() ? (bold ? "Sans Bold" : "Sans") : (g_face + (bold ? " Bold" : "")).c_str());
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
}
void measure(const std::string& s, double px, bool bold, double wrap, double& w, double& h) {
    setFont(g_measure, px, bold);
    pango_layout_set_width(g_measure, wrap > 0 ? static_cast<int>(wrap * PANGO_SCALE) : -1);
    pango_layout_set_wrap(g_measure, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(g_measure, s.c_str(), -1);
    int iw = 0, ih = 0;
    pango_layout_get_pixel_size(g_measure, &iw, &ih);
    w = iw;
    h = ih;
}
double text(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double wrap = 0, double alpha = 1) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    if (wrap > 0) { pango_layout_set_width(l, static_cast<int>(wrap * PANGO_SCALE)); pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR); }
    pango_layout_set_text(l, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    return h;
}

double fieldTextX(double fx, double fw, const std::string& s, size_t caret) {
    double x = fx + 14, cw = 0, ch = 0;
    measure(s.substr(0, std::min(caret, s.size())), 14, false, 0, cw, ch);
    if (cw > fw - 30) x -= cw - (fw - 30);
    return x;
}
size_t fieldIndexAt(const std::string& s, double x) {
    setFont(g_measure, 14, false);
    pango_layout_set_width(g_measure, -1);
    pango_layout_set_text(g_measure, s.c_str(), -1);
    return textsel::indexAtX(g_measure, s, x);
}

cairo_surface_t* iconMaskIn(const std::string& style, const std::string& name, int size) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = style + "/" + name + "/" + std::to_string(size);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = nullptr;
    const std::string path = assetsDir() + "/icons/" + style + "/" + name + ".svg";
    if (access(path.c_str(), R_OK) == 0) {
        if (RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr)) {
            mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
            cairo_t* cr = mkcr(mask);
            RsvgRectangle vp = {0, 0, double(size), double(size)};
            rsvg_handle_render_document(h, cr, &vp, nullptr);
            cairo_destroy(cr);
            g_object_unref(h);
        }
    }
    cache[key] = mask;
    return mask;
}

cairo_surface_t* iconMask(const std::string& name, int size, bool bold = false) {
    static std::map<std::string, cairo_surface_t*> cache;
    const std::string key = (bold ? "bold!" : g.iconStyle) + "/" + name + "/" + std::to_string(size) + (matsym::enabled() ? "|m" : "");
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = matsym::makeMask(name, bold, size);
    for (const std::string& style : {bold ? std::string("bold") : g.iconStyle, std::string("outline"), std::string("bold")}) {
        if (mask) break;
        const std::string path = assetsDir() + "/icons/" + style + "/" + (name == "widget" ? "widget" : matsym::baseName(name)) + ".svg";
        if (access(path.c_str(), R_OK) != 0) continue;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr);
        if (!h) continue;
        mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(mask);
        RsvgRectangle vp = {0, 0, double(size), double(size)};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
        break;
    }
    cache[key] = mask;
    return mask;
}
void icon(cairo_t* cr, const std::string& name, double x, double y, int size, const Color& c, bool bold = false) {
    cairo_surface_t* m = iconMask(name, size, bold);
    if (!m) return;
    src(cr, c);
    cairo_mask_surface(cr, m, x, y);
}

void shapePath(cairo_t* cr, const std::string& name, double x, double y, double size);

void rgbToHsl(const Color& c, double& h, double& s, double& l) {
    const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
    l = (mx + mn) / 2;
    s = d < 1e-9 ? 0 : d / (1 - std::fabs(2 * l - 1));
    if (d < 1e-9) h = 0;
    else if (mx == c.r) h = std::fmod((c.g - c.b) / d + 6, 6);
    else if (mx == c.g) h = (c.b - c.r) / d + 2;
    else h = (c.r - c.g) / d + 4;
    h /= 6;
}
Color hslToRgb(double h, double s, double l) {
    const double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    auto f = [&](double t) {
        t = t - std::floor(t);
        if (t < 1.0 / 6) return p + (q - p) * 6 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
        return p;
    };
    return {f(h + 1.0 / 3), f(h), f(h - 1.0 / 3)};
}

// The small fake picture of a style card: a window in the dark or light colors, or the shape of the window switcher.
void drawStyleMock(cairo_t* cr, const std::string& key, const std::string& value, double x, double y, double w, double h) {
    const Color prim = g.color("primary"), onS = g.color("on_surface");
    auto bar = [&](double bx, double by, double bw, double bh, const Color& c, double a) { rrect(cr, bx, by, bw, bh, bh / 2); src(cr, c, a); cairo_fill(cr); };
    cairo_save(cr);
    if (key == "colorMode") {
        const bool dark = value == "dark";
        // The mode in use shows the real palette; the other one is the same palette (the same hues) turned light or dark.
        const bool inUse = value == g.val["colorMode"];
        Color bg = g.color("background"), side = g.color("surface_container"), ink = g.color("on_surface"), acc = prim;
        if (!inUse) {
            double hb, sb, lb, hp, sp, lp;
            rgbToHsl(bg, hb, sb, lb);
            rgbToHsl(prim, hp, sp, lp);
            sb = std::max(sb, 0.06);
            bg = hslToRgb(hb, sb, dark ? 0.08 : 0.975);
            side = hslToRgb(hb, sb, dark ? 0.14 : 0.92);
            ink = hslToRgb(hb, sb * 0.6, dark ? 0.90 : 0.12);
            acc = hslToRgb(hp, sp, dark ? 0.78 : 0.40);
        }
        rrect(cr, x, y, w, h, 10); src(cr, bg); cairo_fill(cr);
        cairo_save(cr);
        rrect(cr, x, y, w, h, 10); cairo_clip(cr);
        cairo_rectangle(cr, x, y, w * 0.3, h); src(cr, side); cairo_fill(cr);
        cairo_restore(cr);
        for (int k = 0; k < 3; k++) { cairo_new_sub_path(cr); cairo_arc(cr, x + 8 + k * 7, y + 7, 2.2, 0, 2 * M_PI); src(cr, ink, 0.45); cairo_fill(cr); }
        bar(x + w * 0.38, y + h * 0.28, w * 0.45, 5, ink, 0.8);
        bar(x + w * 0.38, y + h * 0.28 + 11, w * 0.3, 5, ink, 0.4);
        bar(x + w * 0.38, y + h - 18, w * 0.28, 10, acc, 1);
        bar(x + w * 0.06, y + h * 0.36, w * 0.18, 5, ink, 0.5);
        bar(x + w * 0.06, y + h * 0.36 + 11, w * 0.14, 5, ink, 0.3);
    } else {  // the window switcher: a panel of fake cards over a dimmed screen
        rrect(cr, x, y, w, h, 10); src(cr, g.color("surface_container_lowest")); cairo_fill(cr);
        const bool row = value == "row";
        auto card = [&](double cx, double cy, double cw, double ch, bool on) {
            rrect(cr, cx, cy, cw, ch, 4); src(cr, on ? mix(prim, g.color("surface_container_high"), 0.3) : g.color("surface_container_high")); cairo_fill(cr);
            bar(cx + 3, cy + 3, cw * 0.5, 2.5, onS, 0.7);
            rrect(cr, cx + 3, cy + 8, cw - 6, ch - 11, 2); src(cr, g.color("surface_container_highest")); cairo_fill(cr);
        };
        if (row) {
            const double pw = w * 0.9, ph = h * 0.58, px = x + (w - pw) / 2, py = y + (h - ph) / 2;
            rrect(cr, px, py, pw, ph, 6); src(cr, g.color("surface_container")); cairo_fill(cr);
            const double cw = (pw - 8 - 3 * 4) / 4;
            for (int k = 0; k < 4; k++) card(px + 4 + k * (cw + 4), py + 4, cw, ph - 8, k == 1);
        } else {
            const double pw = w * 0.5, ph = h * 0.86, px = x + (w - pw) / 2, py = y + (h - ph) / 2;
            rrect(cr, px, py, pw, ph, 6); src(cr, g.color("surface_container")); cairo_fill(cr);
            const double cw = (pw - 8 - 4) / 2, ch = (ph - 8 - 4) / 2;
            for (int k = 0; k < 4; k++) card(px + 4 + (k % 2) * (cw + 4), py + 4 + (k / 2) * (ch + 4), cw, ch, k == 1);
        }
    }
    cairo_restore(cr);
}

void drawWidgetPreview(cairo_t* cr, const std::string& id, double x, double y, double w, double h) {
    auto bar = [&](double bx, double by, double bw, double bh, const Color& c, double a) { rrect(cr, bx, by, bw, bh, bh / 2); src(cr, c, a); cairo_fill(cr); };
    auto disc = [&](double cx, double cy, double r, const Color& c, double a) { cairo_new_sub_path(cr); cairo_arc(cr, cx, cy, r, 0, 2 * M_PI); src(cr, c, a); cairo_fill(cr); };
    const Color onS = g.color("on_surface"), prim = g.color("primary"), outl = g.color("outline");
    cairo_save(cr);
    rrect(cr, x, y, w, h, 16);
    ClipGroup cutCard;
    beginClipGroup(cr, cutCard);
    if (id == "todo") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        disc(x + 16, y + 17, 6, prim, 1);
        bar(x + 28, y + 13, 30, 8, onS, 0.8);
        bar(x + w - 52, y + 11, 40, 12, g.color("primary_container"), 1);
        for (int k = 0; k < 2; k++) {
            const double ry = y + 36 + k * 18;
            cairo_new_sub_path(cr); cairo_arc(cr, x + 18, ry + 5, 5, 0, 2 * M_PI); src(cr, k == 1 ? prim : outl, 1); cairo_set_line_width(cr, 1.6);
            if (k == 1) cairo_fill(cr); else cairo_stroke(cr);
            bar(x + 30, ry + 1, w * (k == 0 ? 0.5 : 0.38), 8, onS, k == 1 ? 0.35 : 0.7);
        }
        bar(x + 10, y + h - 20, w - 50, 12, g.color("surface_container_high"), 1);
        disc(x + w - 22, y + h - 14, 8, prim, 1);
    } else if (id == "weather") {
        cairo_pattern_t* gr = cairo_pattern_create_linear(0, y, 0, y + h);
        cairo_pattern_add_color_stop_rgb(gr, 0, 0.31, 0.55, 0.96);
        cairo_pattern_add_color_stop_rgb(gr, 1, 0.18, 0.37, 0.82);
        cairo_set_source(cr, gr); cairo_paint(cr); cairo_pattern_destroy(gr);
        bar(x + 12, y + 10, 24, 5, Color{1, 1, 1}, 0.8);
        disc(x + 18, y + 30, 7, Color{1, 0.92, 0.6}, 1);
        bar(x + 30, y + 26, 34, 8, Color{1, 1, 1}, 0.95);
        bar(x + 12, y + 46, 40, 5, Color{1, 1, 1}, 0.6);
        bar(x + w - 58, y + 12, 46, 22, Color{1, 1, 1}, 0.95);
        for (int k = 0; k < 3; k++) {
            const double cx = x + w - 50 + k * 18;
            bar(cx - 4, y + h - 30, 8, 4, Color{1, 1, 1}, 0.7);
            disc(cx, y + h - 18, 4.5, Color{1, 1, 1}, 0.9);
        }
    } else if (id == "player") {
        src(cr, g.color("primary_container")); cairo_paint(cr);
        cairo_pattern_t* veil = cairo_pattern_create_linear(0, y, 0, y + h);
        cairo_pattern_add_color_stop_rgba(veil, 0, 0, 0, 0, 0.15);
        cairo_pattern_add_color_stop_rgba(veil, 1, 0, 0, 0, 0.55);
        cairo_set_source(cr, veil); cairo_paint(cr); cairo_pattern_destroy(veil);
        rrect(cr, x + 12, y + 11, 26, 26, 8); src(cr, Color{1, 1, 1}, 0.3); cairo_fill(cr);
        bar(x + 46, y + 14, 44, 6, Color{1, 1, 1}, 0.95);
        bar(x + 46, y + 26, 28, 5, Color{1, 1, 1}, 0.5);
        bar(x + 12, y + h * 0.56, w * 0.36, 5, Color{1, 1, 1}, 0.3);
        bar(x + 12, y + h * 0.56 + 12, w * 0.62, 7, Color{1, 1, 1}, 1.0);
        bar(x + 12, y + h - 12, w - 24, 3, Color{1, 1, 1}, 0.25);
        bar(x + 12, y + h - 12, (w - 24) * 0.4, 3, Color{1, 1, 1}, 0.95);
    } else if (id == "text") {  // a big word on the plain card, as it looks written on the wallpaper
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        PangoLayout* tl = pango_cairo_create_layout(cr);
        PangoFontDescription* fd = pango_font_description_from_string("Serif Italic Bold");
        pango_font_description_set_absolute_size(fd, h * 0.5 * PANGO_SCALE);
        pango_layout_set_font_description(tl, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(tl, "Aa", -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(tl, &tw, &th);
        src(cr, prim);
        cairo_move_to(cr, x + (w - tw) / 2.0, y + (h - th) / 2.0 - 6);
        pango_cairo_show_layout(cr, tl);
        g_object_unref(tl);
        bar(x + w * 0.3, y + h - 22, w * 0.4, 4, onS, 0.35);
    } else if (id == "visualizer") {  // a few bars of different heights, with the gradient of the theme
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        constexpr int n = 9;
        const double lv[n] = {0.30, 0.55, 0.42, 0.85, 0.62, 1.0, 0.5, 0.72, 0.36}, pad = 14, base = y + h - 12, reach = h - 30, pitch = (w - 2 * pad) / n, bw = pitch * 0.58;
        for (int k = 0; k < n; k++) {
            const double bh = std::max(bw, lv[k] * reach), bx = x + pad + k * pitch + (pitch - bw) / 2;
            rrect(cr, bx, base - bh, bw, bh, bw / 2);
            const Color c = g.color("primary"), d = g.color("tertiary"), m{c.r + (d.r - c.r) * k / (n - 1.0), c.g + (d.g - c.g) * k / (n - 1.0), c.b + (d.b - c.b) * k / (n - 1.0)};
            src(cr, m, 1);
            cairo_fill(cr);
        }
    } else if (id == "image") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        const double bw = w - 20, bh = h - 20;
        rrect(cr, x + 10, y + 10, bw, bh, 12); src(cr, g.color("primary_container")); cairo_fill(cr);
        cairo_save(cr); rrect(cr, x + 10, y + 10, bw, bh, 12);
        ClipGroup cutPic;
        beginClipGroup(cr, cutPic);
        disc(x + 10 + bw * 0.7, y + 10 + bh * 0.3, bh * 0.13, g.color("tertiary"), 0.95);
        cairo_move_to(cr, x + 10, y + h); cairo_line_to(cr, x + 10, y + 10 + bh * 0.65); cairo_curve_to(cr, x + 10 + bw * 0.25, y + 10 + bh * 0.3, x + 10 + bw * 0.4, y + 10 + bh * 0.55, x + 10 + bw * 0.55, y + 10 + bh * 0.7);
        cairo_line_to(cr, x + 10 + bw * 0.55, y + h); cairo_close_path(cr); src(cr, prim, 1); cairo_fill(cr);
        cairo_move_to(cr, x + w, y + h); cairo_line_to(cr, x + w, y + 10 + bh * 0.6); cairo_curve_to(cr, x + 10 + bw * 0.85, y + 10 + bh * 0.45, x + 10 + bw * 0.6, y + 10 + bh * 0.7, x + 10 + bw * 0.4, y + 10 + bh * 0.85);
        cairo_line_to(cr, x + 10 + bw * 0.4, y + h); cairo_close_path(cr); src(cr, g.color("secondary"), 1); cairo_fill(cr);
        endClipGroup(cr, cutPic);
        cairo_restore(cr);
    } else if (id == "usercard") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        cairo_pattern_t* gr = cairo_pattern_create_linear(0, y, 0, y + h * 0.62);
        cairo_pattern_add_color_stop_rgba(gr, 0, 0.35, 0.5, 0.6, 1);
        cairo_pattern_add_color_stop_rgba(gr, 1, 0.35, 0.5, 0.6, 0);
        cairo_set_source(cr, gr); cairo_rectangle(cr, x, y, w, h * 0.62); cairo_fill(cr); cairo_pattern_destroy(gr);
        disc(x + 26, y + h * 0.55, 13, g.color("surface_container_highest"), 1);
        disc(x + 26, y + h * 0.55, 11, prim, 1);
        bar(x + 46, y + h * 0.58, 40, 6, onS, 0.85);
        bar(x + 46, y + h * 0.58 + 11, 26, 4, onS, 0.5);
        bar(x + w - 52, y + h - 18, 40, 11, onS, 0.9);
    } else if (id == "clock") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        const double cx = x + w / 2, cy = y + h / 2, r = std::min(w, h) / 2 - 6;
        shapePath(cr, "Cookie12Sided", cx - r, cy - r, 2 * r);
        src(cr, g.color("primary_container")); cairo_fill(cr);
        for (int k = 0; k < 12; k++) {
            const double a = k * M_PI / 6, rr = r - 6;
            disc(cx + std::cos(a) * rr, cy + std::sin(a) * rr, 1.4, g.color("secondary"), 0.8);
        }
        cairo_set_line_width(cr, 5); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        src(cr, g.color("tertiary")); cairo_move_to(cr, cx, cy); cairo_line_to(cr, cx + 4, cy - r * 0.62); cairo_stroke(cr);
        cairo_set_line_width(cr, 7); src(cr, prim); cairo_move_to(cr, cx, cy); cairo_line_to(cr, cx + r * 0.38, cy + r * 0.12); cairo_stroke(cr);
    } else if (id == "calendar") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        bar(x + 12, y + 9, 40, 8, onS, 0.85);
        disc(x + w - 22, y + 13, 5, outl, 0.5); disc(x + w - 38, y + 13, 5, outl, 0.5);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 7; c++) {
                const double cx = x + 14 + c * ((w - 28) / 6.0), cy = y + 30 + r * 13;
                if (r == 1 && c == 3) disc(cx, cy, 5.5, prim, 1);
                else disc(cx, cy, 2, onS, r == 3 && c > 3 ? 0.2 : 0.55);
            }
    } else if (id == "resources") {
        src(cr, g.color("surface_container_highest")); cairo_paint(cr);
        const char* names[4] = {"primary_container", "secondary_container", "tertiary_container", "primary_fixed"};
        const double lv[4] = {0.35, 0.55, 0.25, 0.7};
        const double tw = (w - 8 * 3) / 2, th = (h - 8 * 3) / 2;
        for (int k = 0; k < 4; k++) {
            const double tx = x + 8 + (k % 2) * (tw + 8), ty = y + 8 + (k / 2) * (th + 8);
            cairo_save(cr);
            rrect(cr, tx, ty, tw, th, 10);
            ClipGroup cutTile;
            beginClipGroup(cr, cutTile);
            src(cr, g.color(names[k]), 0.35); cairo_paint(cr);
            src(cr, g.color(names[k])); cairo_rectangle(cr, tx, ty + th * (1 - lv[k]), tw, th * lv[k]); cairo_fill(cr);
            endClipGroup(cr, cutTile);
            cairo_restore(cr);
            disc(tx + 9, ty + 9, 4, onS, 0.55);
            bar(tx + 6, ty + th - 11, tw * 0.45, 6, onS, 0.85);
        }
    }
    endClipGroup(cr, cutCard);
    cairo_restore(cr);
}

struct Profile {
    std::string name, avatarPath;
    cairo_surface_t* avatar = nullptr;
    int uptimeMinute = -1;
    std::string uptime;
};
Profile g_profile;

std::vector<std::pair<std::string, std::string>> cwpShapeOpts() {
    std::vector<std::pair<std::string, std::string>> v;
    for (const char* n : {"Circle", "Square", "Slanted", "Arch", "Arrow", "SemiCircle", "Oval", "Pill", "Triangle", "Diamond", "ClamShell", "Pentagon", "Gem", "Sunny", "VerySunny", "Cookie4Sided", "Cookie6Sided", "Cookie7Sided", "Cookie9Sided", "Cookie12Sided", "Ghostish", "Clover4Leaf", "Clover8Leaf", "Burst", "SoftBurst", "Flower", "Puffy", "PuffyDiamond", "PixelCircle", "Bun", "Heart"})
        v.push_back({n, n});
    return v;
}
std::map<std::string, std::vector<double>> loadShapes() {
    std::map<std::string, std::vector<double>> out;
    std::ifstream f(assetsDir() + "/shapes.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string name;
        ss >> name;
        std::vector<double> nums;
        for (double v; ss >> v;) nums.push_back(v);
        if (!name.empty() && nums.size() >= 8 && (nums.size() - 2) % 6 == 0) out[name] = std::move(nums);
    }
    return out;
}
void shapePath(cairo_t* cr, const std::string& name, double x, double y, double size) {
    static const auto all = loadShapes();
    auto it = all.find(name);
    if (it == all.end()) return;
    const auto& n = it->second;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x + n[0] * size, y + n[1] * size);
    for (size_t i = 2; i + 5 < n.size(); i += 6)
        cairo_curve_to(cr, x + n[i] * size, y + n[i + 1] * size, x + n[i + 2] * size, y + n[i + 3] * size, x + n[i + 4] * size, y + n[i + 5] * size);
    cairo_close_path(cr);
}

cairo_surface_t* squareSurface(const std::string& path, int size) {
    int iw = 0, ih = 0;
    if (!gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return nullptr;
    const double scale = std::max(double(size) / iw, double(size) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(size, int(iw * scale + 0.5)), std::max(size, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return nullptr;
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < ph; y++) {
        const guchar* sp = px + y * rs;
        uint32_t* dp = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < pw; x++, sp += nch) {
            const uint32_t a = nch == 4 ? sp[3] : 255;
            dp[x] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(img);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = mkcr(out);
    cairo_set_source_surface(cr, img, (size - pw) / 2.0, (size - ph) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    g_object_unref(pb);
    return out;
}

void loadProfile() {
    Profile& p = g_profile;
    const std::string ii = readFile(homeDir() + "/.config/illogical-impulse/config.json");
    std::string pic, display;
    jsonValue(ii, "avatarPicture", pic);
    jsonValue(ii, "displayName", display);
    if (display.empty()) {
        const passwd* pw = getpwuid(getuid());
        display = pw ? pw->pw_name : "user";
    }
    p.name = display;
    if (p.avatar) { cairo_surface_destroy(p.avatar); p.avatar = nullptr; }
    p.avatarPath.clear();
    const std::string mine = g.val.count("avatarPicture") ? g.val["avatarPicture"] : "";
    g_profPic = mine;
    for (const std::string& c : {mine, pic, homeDir() + "/.face", homeDir() + "/.face.icon"})
        if (!c.empty() && access(c.c_str(), R_OK) == 0) { p.avatarPath = c; break; }
    if (p.avatarPath.empty()) return;
    p.avatar = squareSurface(p.avatarPath, 60);
}

bool refreshUptime() {
    double secs = 0;
    std::ifstream f("/proc/uptime");
    f >> secs;
    const int total = static_cast<int>(secs) / 60;
    if (total == g_profile.uptimeMinute) return false;
    g_profile.uptimeMinute = total;
    const int d = total / 1440, h = (total % 1440) / 60, m = total % 60;
    std::string t;
    if (d > 0) t += std::to_string(d) + "d";
    if (h > 0) t += (t.empty() ? "" : ", ") + std::to_string(h) + "h";
    if (m > 0 || t.empty()) t += (t.empty() ? "" : ", ") + std::to_string(m) + "m";
    g_profile.uptime = "Up • " + t;
    return true;
}

std::string cachePath() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : homeDir() + "/.cache") + "/end4pC";
}

struct SchemeColors { Color c[3]; };
Bubble g_bubble;
bool g_bubbleMoving = false;

struct SpringAnim { double x = 0, v = 0, target = 0; };
std::map<std::string, SpringAnim> g_springs;
bool g_springsMoving = false;
double springValue(const std::string& key, double target) {
    auto it = g_springs.find(key);
    if (it == g_springs.end()) { g_springs[key] = {target, 0, target}; return target; }
    if (it->second.target != target) { it->second.target = target; g_springsMoving = true; }
    return it->second.x;
}
bool stepSprings(double dt) {
    constexpr double kStiffness = 900, kDamping = 46;
    dt = std::min(dt, 1.0 / 30);
    bool moving = false;
    for (auto& [key, a] : g_springs) {
        if (std::fabs(a.target - a.x) < 0.0008 && std::fabs(a.v) < 0.01) { a.x = a.target; a.v = 0; continue; }
        a.v += (kStiffness * (a.target - a.x) - kDamping * a.v) * dt;
        a.x += a.v * dt;
        moving = true;
    }
    g_springsMoving = moving;
    return moving;
}
std::map<std::string, SchemeColors> g_schemes;
time_t g_schemesStamp = -1;
std::string schemesFile() { return cachePath() + "/schemes.txt"; }
void loadSchemes() {
    g_schemes.clear();
    std::ifstream f(schemesFile());
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::vector<std::string> p;
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, '|')) p.push_back(t);
        if (p.size() < 4) continue;
        SchemeColors sc;
        for (int k = 0; k < 3; k++) {
            unsigned v = 0;
            if (std::sscanf(p[1 + k].c_str(), "#%x", &v) == 1) sc.c[k] = Color{((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
        }
        g_schemes[p[0]] = sc;
    }
}
void ensureSchemes(const std::string& mode, const std::string& accent) {
    static std::string last;
    const std::string key = mode + "|" + accent;
    if (key == last) return;
    last = key;
    spawn({"python3", projectDir() + "/matugen/schemes.py", mode, accent});
}
std::string updatesPath() { return cachePath() + "/updates.json"; }
std::string scriptPath(const char* name) { return assetsDir() + "/../scripts/" + name; }

void loadUpdates() {
    const std::string t = readFile(updatesPath());
    std::string a, b, c, when;
    if (!jsonValue(t, "pacman", a) || !jsonValue(t, "aur", b) || !jsonValue(t, "flatpak", c)) { g.val["updSummary"] = "Not checked yet"; g.val["updTime"] = "0"; }
    else {
        g.val["updSummary"] = a + " pacman · " + b + " AUR · " + c + " Flatpak";
        jsonValue(t, "time", when);
        g.val["updTime"] = when.empty() ? "0" : when;
    }
    g.updStamp = mtimeOf(updatesPath());
    g.updChecking = false;
}

void loadPeripherals() {
    const std::string t = readFile(cachePath() + "/peripherals.txt");
    std::string out;
    std::stringstream ss(t);
    std::string line;
    while (std::getline(ss, line)) {
        std::vector<std::string> p;
        std::stringstream ls(line);
        std::string f;
        while (std::getline(ls, f, '|')) p.push_back(f);
        if (p.size() < 3) continue;
        std::string name = p[0];
        if (name.rfind("Logitech ", 0) == 0) name = name.substr(9);
        out += (out.empty() ? "" : " · ") + name + " " + p[1] + "%" + (p[2] == "charging" ? " (charging)" : "");
    }
    g.val["periphSummary"] = out.empty() ? "No devices" : out;
}

std::string ago(time_t then) {
    if (then <= 0) return "";
    const long m = std::max(0L, static_cast<long>(time(nullptr) - then) / 60);
    if (m < 1) return "just now";
    if (m < 60) return std::to_string(m) + " min ago";
    if (m < 1440) return std::to_string(m / 60) + " h ago";
    return std::to_string(m / 1440) + " d ago";
}

std::string descOf(const Row& r) {
    if (r.key == "webappAdd" && g.createPid > 0) return "Creating the webapp, getting its icon…";
    if (r.key == "updSummary") {
        if (g.updChecking) return "Checking…";
        const time_t t = static_cast<time_t>(std::atol(g.val.count("updTime") ? g.val["updTime"].c_str() : "0"));
        return t > 0 ? "Last checked " + ago(t) : "Turn the checks on, or press Check now.";
    }
    return r.desc;
}

pid_t spawn(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();
        const int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}

void loadWebapps() {
    std::vector<WebappItem> items;
    if (FILE* f = popen(("bash '" + scriptPath("webapp.sh") + "' list 2>/dev/null").c_str(), "r")) {
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        pclose(f);
        for (size_t pos = 0; (pos = out.find('{', pos)) != std::string::npos;) {
            const size_t end = out.find('}', pos);
            if (end == std::string::npos) break;
            const std::string obj = out.substr(pos, end - pos + 1);
            WebappItem w;
            jsonValue(obj, "id", w.id); jsonValue(obj, "name", w.name); jsonValue(obj, "url", w.url); jsonValue(obj, "icon", w.icon);
            if (!w.id.empty()) items.push_back(w);
            pos = end + 1;
        }
    }
    g.webapps = items;
    for (auto& pg : g.pages) {
        if (pg.name != "Services") continue;
        while (!pg.rows.empty() && pg.rows.back().kind == Webapp) pg.rows.pop_back();
        for (const auto& w : items) {
            Row r{Webapp, w.id, w.name, w.url};
            r.unit = w.icon;
            r.section = "Webapps";
            pg.rows.push_back(r);
        }
    }
    g.dirty = true;
}

void createWebapp(const std::string& raw) {
    std::string url;
    for (char c : raw) if (!std::isspace(static_cast<unsigned char>(c))) url += c;
    if (url.empty() || g.createPid > 0) return;
    g.createPid = spawn({"bash", scriptPath("webapp.sh"), "create", url});
    if (g.createPid > 0) g.webPids.insert(g.createPid);
    else g.createPid = 0;
}

cairo_surface_t* webIcon(const std::string& path) {
    static std::map<std::string, cairo_surface_t*> cache;
    if (path.empty() || path[0] != '/') return nullptr;
    if (auto it = cache.find(path); it != cache.end()) return it->second;
    cairo_surface_t* out = nullptr;
    if (GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), 32, 32, TRUE, nullptr)) {
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
        out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
        unsigned char* dst = cairo_image_surface_get_data(out);
        const int drs = cairo_image_surface_get_stride(out);
        const guchar* px = gdk_pixbuf_get_pixels(pb);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const guchar* sp = px + y * rs + x * nch;
                const uint32_t a = nch == 4 ? sp[3] : 255;
                reinterpret_cast<uint32_t*>(dst + (y + (32 - h) / 2) * drs)[x + (32 - w) / 2] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
            }
        cairo_surface_mark_dirty(out);
        g_object_unref(pb);
    }
    cache[path] = out;
    return out;
}

double cardW();

struct SysInfo {
    bool loaded = false;
    std::string distro = "Linux", distroId = "arch", host, user, kernel, cpu, gpu, shell, packages, hyprland, session;
    std::string memText, diskText;
    double memFrac = 0, diskFrac = 0;
    Clock::time_point stamp;
};
SysInfo g_sys;

std::string runLine(const std::string& cmd) {
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[1024];
        if (std::fgets(buf, sizeof buf, f)) out = buf;
        pclose(f);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}
std::string replaceAll(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
    return s;
}
std::string humanBytes(double b) {
    char buf[32];
    if (b >= 1099511627776.0) std::snprintf(buf, sizeof buf, "%.1f TiB", b / 1099511627776.0);
    else std::snprintf(buf, sizeof buf, "%.1f GiB", b / 1073741824.0);
    return buf;
}

void refreshSysLive() {
    double total = 0, avail = 0;
    std::ifstream f("/proc/meminfo");
    std::string k, unit;
    double v = 0;
    while (f >> k >> v >> unit) {
        if (k == "MemTotal:") total = v * 1024;
        else if (k == "MemAvailable:") avail = v * 1024;
    }
    if (total > 0) { g_sys.memText = humanBytes(total - avail) + " / " + humanBytes(total); g_sys.memFrac = (total - avail) / total; }
    struct statvfs sv {};
    if (statvfs("/", &sv) == 0) {
        const double all = double(sv.f_blocks) * sv.f_frsize, used = all - double(sv.f_bfree) * sv.f_frsize;
        g_sys.diskText = humanBytes(used) + " / " + humanBytes(all);
        g_sys.diskFrac = all > 0 ? used / all : 0;
    }
    g_sys.stamp = Clock::now();
}

void loadSysInfo() {
    SysInfo& s = g_sys;
    s.loaded = true;
    const std::string osr = readFile("/etc/os-release");
    auto field = [&](const std::string& key) {
        const size_t p = osr.find("\n" + key + "=");
        if (p == std::string::npos) return std::string();
        size_t b = p + key.size() + 2, e = osr.find('\n', b);
        std::string v = osr.substr(b, e == std::string::npos ? std::string::npos : e - b);
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        return v;
    };
    if (const std::string v = field("PRETTY_NAME"); !v.empty()) s.distro = v;
    if (const std::string v = field("ID"); !v.empty()) s.distroId = v;
    s.host = runLine("cat /etc/hostname 2>/dev/null");
    if (const passwd* pw = getpwuid(getuid())) s.user = pw->pw_name;
    utsname u {};
    if (uname(&u) == 0) s.kernel = u.release;
    std::string cpu = runLine("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-");
    for (const char* junk : {"(R)", "(TM)", "CPU ", "  "}) cpu = replaceAll(cpu, junk, junk[0] == ' ' ? " " : "");
    while (!cpu.empty() && cpu.front() == ' ') cpu.erase(0, 1);
    s.cpu = cpu.empty() ? "Unknown" : cpu;
    std::string gpu = runLine("lspci 2>/dev/null | grep -iE 'vga|3d|display' | grep -vi 'Intel Corporation' | head -1");
    if (gpu.empty()) gpu = runLine("lspci 2>/dev/null | grep -iE 'vga|3d|display' | head -1");
    if (const size_t c = gpu.find(": "); c != std::string::npos) gpu = gpu.substr(c + 2);
    if (const size_t r = gpu.find(" (rev"); r != std::string::npos) gpu.resize(r);
    if (gpu.find('[') != std::string::npos && gpu.back() == ']') gpu = gpu.substr(gpu.rfind('[') + 1, gpu.size() - gpu.rfind('[') - 2);
    for (const char* junk : {"Intel Corporation ", "NVIDIA Corporation ", "Advanced Micro Devices, Inc. [AMD/ATI] "}) gpu = replaceAll(gpu, junk, "");
    s.gpu = gpu.empty() ? "Not found" : gpu;
    const char* sh = std::getenv("SHELL");
    s.shell = sh ? std::string(sh).substr(std::string(sh).rfind('/') + 1) : "sh";
    std::string pk = runLine("pacman -Qq 2>/dev/null | wc -l");
    const std::string fp = runLine("flatpak list --app 2>/dev/null | wc -l");
    s.packages = pk + " pacman" + (fp != "0" && !fp.empty() ? " · " + fp + " Flatpak" : "");
    const std::string hv = runLine("hyprctl version 2>/dev/null | head -1");
    char ver[64] = "";
    s.hyprland = std::sscanf(hv.c_str(), "Hyprland %63s", ver) == 1 ? std::string("Hyprland ") + ver : "";
    if (compositorName() == "Niri") s.hyprland = runLine("niri --version 2>/dev/null | head -1");
    if (compositorName() == "Sway") s.hyprland = runLine("swaymsg -r -t get_version 2>/dev/null | jq -r '\"SwayFX/sway \" + .human_readable' 2>/dev/null | head -1");
    s.session = std::getenv("WAYLAND_DISPLAY") ? "Wayland" : "X11";
    refreshSysLive();
}

cairo_surface_t* distroMask(int size) {
    static std::map<int, cairo_surface_t*> cache;
    if (auto it = cache.find(size); it != cache.end()) return it->second;
    cairo_surface_t* mask = nullptr;
    for (const std::string& id : {g_sys.distroId, std::string("arch")}) {
        const std::string path = assetsDir() + "/distro/" + id + "-symbolic.svg";
        if (access(path.c_str(), R_OK) != 0) continue;
        if (RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr)) {
            mask = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
            cairo_t* cr = mkcr(mask);
            RsvgRectangle vp = {0, 0, double(size), double(size)};
            rsvg_handle_render_document(h, cr, &vp, nullptr);
            cairo_destroy(cr);
            g_object_unref(h);
            break;
        }
    }
    cache[size] = mask;
    return mask;
}

std::string updatesText() {
    int a = 0, b = 0, c = 0;
    if (!g.val.count("updSummary") || std::sscanf(g.val["updSummary"].c_str(), "%d pacman · %d AUR · %d Flatpak", &a, &b, &c) != 3) return "Not checked yet";
    return a + b + c == 0 ? "Up to date" : std::to_string(a + b + c) + " pending";
}

struct AboutTile { const char* icon; const char* label; std::string value; double bar; bool full; Rect r; };
constexpr double kHeroH = 176, kTileGap = 12, kTilePad = 18, kTileIcon = 44;

double aboutLayout(std::vector<AboutTile>& t, double& heroY) {
    if (!g_sys.loaded) loadSysInfo();
    t = {
        {"cpu", "Processor", g_sys.cpu, -1, true, {}},
        {"monitor", "Graphics", g_sys.gpu, -1, true, {}},
        {"server-square", "Memory", g_sys.memText, g_sys.memFrac, false, {}},
        {"database", "Disk", g_sys.diskText, g_sys.diskFrac, false, {}},
        {"box", "Packages", g_sys.packages, -1, false, {}},
        {"download-square", "Updates", updatesText(), -1, false, {}},
        {"code", "Shell", g_sys.shell, -1, false, {}},
        {"clock-circle", "Uptime", g_profile.uptime.size() > 5 ? g_profile.uptime.substr(g_profile.uptime.find("• ") + 4) : "", -1, false, {}},
    };
    const double inner = cardW() - 2 * kPadX, colW = (inner - kTileGap) / 2;
    heroY = kHeader;
    double y = kHeader + kHeroH + 14;
    for (size_t i = 0; i < t.size();) {
        const bool pair = !t[i].full && i + 1 < t.size();
        const size_t n = pair ? 2 : 1;
        double rowH = 0;
        for (size_t k = 0; k < n; k++) {
            AboutTile& a = t[i + k];
            const double w = (a.full ? inner : colW) - 2 * kTilePad - kTileIcon - 14;
            double vw = 0, vh = 0, lw = 0, lh = 0;
            measure(a.value, 16, false, w, vw, vh);
            measure(a.label, 12.5, false, w, lw, lh);
            rowH = std::max(rowH, 2 * kTilePad + std::max<double>(kTileIcon, lh + 2 + vh + (a.bar >= 0 ? 16 : 0)));
        }
        for (size_t k = 0; k < n; k++) {
            AboutTile& a = t[i + k];
            a.r = a.full ? Rect{kPadX, y, inner, rowH} : Rect{kPadX + k * (colW + kTileGap), y, colW, rowH};
        }
        y += rowH + kTileGap;
        i += n;
    }
    return y + 12;
}

double cardH();
std::string compositorVersionLine(std::string& second) {
    const std::string name = compositorName();
    if (name == "Sway") {
        const std::string fx = runLine("swaymsg -r -t get_version 2>/dev/null | jq -r .human_readable 2>/dev/null | head -1");
        const std::string base = runLine("sway --version 2>/dev/null | head -1");  // "swayfx version 0.6 (based on sway 1.12.0)"
        const size_t at = base.find("based on sway ");
        std::string sv = at == std::string::npos ? "" : base.substr(at + 14);
        if (!sv.empty() && sv.back() == ')') sv.pop_back();
        second = (sv.empty() ? "" : "sway " + sv + " · ") + (std::getenv("WAYLAND_DISPLAY") ? "Wayland" : "X11");
        return "SwayFX" + (fx.empty() ? "" : " " + fx);
    }
    second = std::getenv("WAYLAND_DISPLAY") ? "Wayland" : "X11";
    return name;
}
void drawCompositorPlaceholder(cairo_t* cr) {
    static std::string line1, line2;
    if (line1.empty()) line1 = compositorVersionLine(line2);
    const std::string name = compositorName();
    const std::string initial = name.empty() ? "?" : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    constexpr double kShape = 170, kTop = 110;
    const double cx = cardW() / 2, top = kTop + (cardH() - kTop - (kShape + 28 + 62)) / 2;
    shapePath(cr, "Cookie12Sided", cx - kShape / 2, top, kShape);
    src(cr, g.color("primary_container"));
    cairo_fill(cr);
    double iw = 0, ih = 0;
    measure(initial, 88, true, 0, iw, ih);
    text(cr, initial, cx - iw / 2, top + (kShape - ih) / 2, 88, true, g.color("on_primary_container"));
    double w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    measure(line1, 26, true, 0, w1, h1);
    measure(line2, 15, false, 0, w2, h2);
    text(cr, line1, cx - w1 / 2, top + kShape + 28, 26, true, g.color("on_surface"));
    text(cr, line2, cx - w2 / 2, top + kShape + 28 + h1 + 4, 15, false, g.color("on_surface_variant"));
}

constexpr int kAboutDotsOpt = 7;  // the "Update Dots" button's hit (the Updates tile has none)
void drawAbout(cairo_t* cr, double scroll, bool live, bool hotTile, bool hotDots) {
    std::vector<AboutTile> tiles;
    double heroY = 0;
    aboutLayout(tiles, heroY);
    const Color onSurface = g.color("on_surface"), variant = g.color("on_surface_variant"), primary = g.color("primary");
    const double oy = -scroll, inner = cardW() - 2 * kPadX;
    {
        const double hy = heroY + oy;
        rrect(cr, kPadX, hy, inner, kHeroH, 28);
        src(cr, g.color("surface_container_high"));
        cairo_fill(cr);
        cairo_save(cr);
        rrect(cr, kPadX, hy, inner, kHeroH, 28);
        cairo_clip(cr);
        shapePath(cr, "Cookie12Sided", kPadX + inner - 190, hy - 60, 280);
        src(cr, primary, 0.08);
        cairo_fill(cr);
        cairo_restore(cr);
        const double cs = 96, cx = kPadX + 32, cy = hy + (kHeroH - cs) / 2;
        if (cairo_surface_t* m = distroMask(96)) {
            src(cr, primary);
            cairo_mask_surface(cr, m, cx, cy);
        }
        const double tx = cx + cs + 26;
        double th = text(cr, g_sys.distro, tx, hy + 30, 28, true, onSurface);
        {
            static const char* kRoles[6] = {"primary", "primary_fixed_dim", "tertiary", "secondary", "primary_container", "secondary_container"};
            static const char* kShapes[6] = {"Cookie12Sided", "Cookie9Sided", "Cookie7Sided", "Cookie6Sided", "Cookie4Sided", "Circle"};
            constexpr double kSize = 34, kStep = 26;
            const double titleBottom = hy + 30 + th * 0.78, chipsTop = hy + kHeroH - 30 - 32;
            const double sy = titleBottom + (chipsTop - titleBottom - kSize) / 2;
            for (int k = 0; k < 6; k++) {
                const double sx = tx + k * kStep;
                shapePath(cr, kShapes[k], sx - 3, sy - 3, kSize + 6);
                src(cr, g.color("surface_container_high"));
                cairo_fill(cr);
                shapePath(cr, kShapes[k], sx, sy, kSize);
                src(cr, g.color(kRoles[k]));
                cairo_fill(cr);
            }
        }
        double chipX = tx;
        const double chipY = hy + kHeroH - 30 - 32;
        for (const std::string& c : {"Linux " + g_sys.kernel}) {
            if (c.empty() || c == "Linux ") continue;
            double cw = 0, ch = 0;
            measure(c, 13, false, 0, cw, ch);
            if (chipX + cw + 28 > kPadX + inner - 16) break;
            rrect(cr, chipX, chipY, cw + 28, 32, 16);
            src(cr, g.color("surface_container_highest"));
            cairo_fill(cr);
            text(cr, c, chipX + 14, chipY + (32 - ch) / 2, 13, false, onSurface);
            chipX += cw + 28 + 8;
        }
        {  // updates the shell (the dots' own files, the keybinds and the compositor config stay as they are)
            const char* label = "Update Dots";
            double lw = 0, lh = 0;
            measure(label, 14, true, 0, lw, lh);
            const double bw = 26 + 8 + lw + 28, bx = kPadX + inner - 20 - bw;
            const Rect b{bx, chipY, bw, 32};
            if (live) g.hits.push_back({{b.x, b.y - oy, b.w, b.h}, -1, kAboutDotsOpt, Action});
            rrect(cr, b.x, b.y, b.w, b.h, 16);
            src(cr, hotDots ? mix(g.color("on_primary"), primary, 0.14) : primary);
            cairo_fill(cr);
            icon(cr, "refresh", b.x + 14, b.y + 6, 20, g.color("on_primary"));
            text(cr, label, b.x + 14 + 20 + 8, b.y + (32 - lh) / 2, 14, true, g.color("on_primary"));
        }
    }
    for (const AboutTile& a : tiles) {
        const Rect r = {a.r.x, a.r.y + oy, a.r.w, a.r.h};
        const bool click = std::string(a.label) == "Updates";
        const bool hot = click && hotTile;
        if (click && live) g.hits.push_back({a.r, -1, -1, Action});
        rrect(cr, r.x, r.y, r.w, r.h, 22);
        src(cr, hot ? mix(g.color("primary"), g.color("surface_container_high"), 0.14) : g.color("surface_container_high"));
        cairo_fill(cr);
        if (click) icon(cr, "arrow-right-up", r.x + r.w - kTilePad - 20, r.y + kTilePad, 20, hot ? g.color("primary") : variant);
        const double ix = r.x + kTilePad, iy = r.y + kTilePad;
        cairo_new_sub_path(cr);
        cairo_arc(cr, ix + kTileIcon / 2, iy + kTileIcon / 2, kTileIcon / 2, 0, 2 * M_PI);
        src(cr, g.color("secondary_container"));
        cairo_fill(cr);
        icon(cr, a.icon, ix + (kTileIcon - 24) / 2, iy + (kTileIcon - 24) / 2, 24, g.color("on_secondary_container"));
        const double tx = ix + kTileIcon + 14, tw = r.w - 2 * kTilePad - kTileIcon - 14;
        const double lh = text(cr, a.label, tx, iy - 1, 12.5, false, variant);
        const double vh = text(cr, a.value, tx, iy + lh + 1, 16, false, onSurface, tw);
        if (a.bar >= 0) {
            const double by = iy + lh + 1 + vh + 8;
            rrect(cr, tx, by, tw, 6, 3);
            src(cr, g.color("secondary_container"), 0.7);
            cairo_fill(cr);
            rrect(cr, tx, by, std::max(6.0, tw * std::clamp(a.bar, 0.0, 1.0)), 6, 3);
            src(cr, primary);
            cairo_fill(cr);
        }
    }
}

bool moduleUsed(const std::string& id) {
    for (const auto& l : g.lay) if (std::find(l.begin(), l.end(), id) != l.end()) return true;
    return g.otherLayout.find("\"" + id + "\"") != std::string::npos;
}
bool rowAvailable(const Row& r) {
    if (r.key == "niri:status") return g.val["niri:status"].compare(0, 5, "error") == 0;
    if (r.key == "niri:borderWidth") return g.val["niri:border"] == "1";
    if (r.key == "niri:ringWidth") return g.val["niri:ring"] == "1";
    if (r.key == "niri:shadowSoftness") return g.val["niri:shadow"] == "1";
    if (r.key == "mon:enabled") return g_mons.size() > 1;
    if (r.key == "magnifier") return compositorName() == "Hyprland";
    if (r.key == "dockAutoHide" || r.key == "dockPin" || r.key == "dockMedia" || r.key == "dockLauncher" || r.key == "dockMagnify" || r.key == "dockIconSize" || r.key == "dockPinned") return g.val["dock"] == "1";
    if (r.key == "groupSpacing") return g.val["layoutFor"] != "v" && (g.val["groupStyle"] == "pills" || g.val["groupStyle"] == "separated");
    if (r.key == "barSpacing") return g.val["layoutFor"] != "v" && g.val["groupStyle"] != "pills" && g.val["groupStyle"] != "separated";  // (the cells set the gap)
    if (r.key == "sideBarSpacing") return g.val["layoutFor"] == "v";
    if (r.key == "animStyle" || r.key == "animSpeed") return g.val["animEnabled"] == "1";
    if (r.key == "wallpaperAutoMin" || r.key == "wallpaperAutoFavs") return g.val["wallpaperAuto"] == "1";
    if (r.key == "dockPosition" || r.key == "dockHug" || r.key == "dockRounding" || r.key == "dockSpacing" || r.key == "dockBorder") return g.val["dock"] == "1";
    if (r.key == "dockBorderColor") return g.val["dock"] == "1" && g.val["dockBorder"] != "0";
    if (r.key == "dockMagnification") return g.val["dock"] == "1" && g.val["dockMagnify"] == "1";
    if (r.key == "dockScreen") return g.val["dock"] == "1" && g_screens.size() > 1;
    if (r.key == "barScreens" || r.key == "widgetScreen") return g_screens.size() > 1;
    if (r.key == "themePrimary" || r.key == "themeSecondary" || r.key == "themeTertiary") return !g.val["theme"].empty();
    if (r.key == "uiBackground") return (g.val["colorMode"] == "light") == (r.opts.size() == 2 && r.opts[1].first == "white");  // (OLED in the dark mode, White in the light one)
    if (r.key == "colorScheme" || r.key == "accent") return g.val["theme"].empty();
    if (r.key.compare(0, 9, "hotCorner") == 0 && r.key != "hotCorners") return g.val["hotCorners"] == "1";
    if (r.key == "customTextSize") return g.val["w:text"] == "1" && g.val["customTextFit"] != "1";
    if (r.key.compare(0, 10, "customText") == 0) return g.val["w:text"] == "1";
    if (r.key == "deskVizPos") return g.val["w:visualizer"] == "1" && g.val["deskVizStyle"] != "ring";  // (the ring is moved by hand)
    if (r.key.compare(0, 7, "deskViz") == 0) return g.val["w:visualizer"] == "1";
    if (r.key.compare(0, 6, "cookie") == 0) return g.val["clockStyle"] == "cookie";
    if (r.key == "clockDaily") return g.val["clockStyle"] == "daily";
    if (r.key == "clockPixel") return g.val["clockStyle"] == "pixel";
    if (r.key == "sys:power") return g_sysHasPower;
    if (r.key == "centeredWallpaperShape" || r.key == "centeredWallpaperColor" || r.key == "centeredWallpaperSize") return g.val["centeredWallpaper"] == "1";
    if (r.key.compare(0, 7, "collage") == 0 && r.key != "collage") return g.val["collage"] == "1";
    if (r.key == "frameColor" || r.key == "barFollowsFrame") return g.val["frame"] != "0";
    if (r.key == "fakeRoundingSize") return g.val["fakeRounding"] != "0";
    if (r.key == "notchWidth") return g.val["barStyle"] == "notch";
    if (r.key == "groupStyle") return g.val["barPosition"] == "top" || g.val["barPosition"] == "bottom";
    if (r.key == "groupColor") return (g.val["barPosition"] == "top" || g.val["barPosition"] == "bottom") && g.val["groupStyle"] != "free" && g.val["groupStyle"] != "transparent";
    if (r.key == "islandClick") return g.val["dynamicIsland"] == "1" && (g.val["barStyle"] == "island" || g.val["barStyle"] == "split");
    if (r.key == "dynamicIsland") return g.val["barStyle"] == "island" || g.val["barStyle"] == "split";
    if (r.key == "islandRounding") return g.val["barStyle"] == "island";
    if (r.key == "iconStyle") return g.val["materialIcons"] != "1";
    if (r.key == "workspaces") return moduleUsed("workspaces") && compositorName() != "Niri";  // niri's workspaces are dynamic: the bar shows the ones that exist
    if (r.key == "workspaceStyle") {  // the full list, or Dots / M3 only in the group styles
        const bool grp = (g.val["barPosition"] == "top" || g.val["barPosition"] == "bottom") && g.val["groupStyle"] != "free";
        return moduleUsed("workspaces") && (r.opts.size() == 2) == grp;
    }
    if (r.key == "workspaceNumbers") return moduleUsed("workspaces") && (g.val["workspaceStyle"] == "default" || g.val["workspaceStyle"] == "pill" || g.val["workspaceStyle"] == "ring" || g.val["workspaceStyle"] == "m3");
    if (r.key == "batteryPercent") return moduleUsed("battery");
    if (r.key == "workspaceApps") return moduleUsed("workspaces") && g.val["workspaceStyle"] != "gnome" && g.val["workspaceStyle"] != "bar";
    if (r.key == "trayPinDefault" || r.key == "trayTint") return moduleUsed("systray");
    if (r.key == "networkAlways" || r.key == "networkSpeed") return moduleUsed("network");
    if (r.key == "overviewLive" || r.key == "overviewLauncher" || r.key == "overviewIcon" || r.key == "overviewPosition") return compositorName() == "Hyprland";
    if (r.key.compare(0, 7, "privacy") == 0) return moduleUsed("privacy");
    if (r.key.compare(0, 6, "quick:") == 0) return moduleUsed("quickactions");
    if (r.key == "resStyle") {  // the full list, or without Bars in the group styles
        const bool grp = (g.val["barPosition"] == "top" || g.val["barPosition"] == "bottom") && g.val["groupStyle"] != "free";
        return moduleUsed("resources") && (r.opts.size() == 5) == grp;
    }
    for (const char* k : {"resCpu", "resRam", "resTemp", "resDisk", "resSwap", "resValue", "resInterval", "resWarnCpu", "resWarnMem", "resWarnSwap"})
        if (r.key == k) return moduleUsed("resources");
    if (r.key == "dividerItem" || r.key == "dividerItemStyle" || r.key == "dividerItemMargin") return !g.dividerLocations.empty();
    if (r.key == "aiClaude" || r.key == "aiCodex" || r.key == "aiTokenLimit") return moduleUsed("aiusage");
    return true;
}
std::vector<std::string> sectionNames(const Page& pg) {
    std::vector<std::string> out;
    for (const auto& r : pg.rows)
        if (!r.section.empty() && rowAvailable(r) && std::find(out.begin(), out.end(), r.section) == out.end()) out.push_back(r.section);
    return out;
}
bool hasChips(const Page& pg) { return sectionNames(pg).size() > 1; }
std::string activeSection(int pi) {
    const auto names = sectionNames(g.pages[pi]);
    if (names.size() < 2) return "";
    const int chosen = g.secForce >= 0 && pi == g.page ? g.secForce : g.secSel[pi];
    return names[std::clamp(chosen, 0, static_cast<int>(names.size()) - 1)];
}
bool rowShown(int pi, const Row& r) {
    const std::string a = activeSection(pi);
    return rowAvailable(r) && (a.empty() || r.section == a);
}
constexpr double kChipsY = 76, kChipH = 32, kChipGap = 8;
constexpr bool kChipFillSlides = false;
constexpr double kMenuRadius = 4;
constexpr double kChoiceBtnH = 40, kChipRadius = 8;
double cardW();
std::vector<Rect> chipRects(int pi) {
    std::vector<Rect> out;
    double cx = kPadX, cy = kChipsY;
    for (const auto& n : sectionNames(g.pages[pi])) {
        double tw = 0, th = 0;
        measure(n, 14, false, 0, tw, th);
        const double w = tw + 32;
        if (cx > kPadX && cx + w > cardW() - kPadX) { cx = kPadX; cy += kChipH + kChipGap; }
        out.push_back({cx, cy, w, kChipH});
        cx += w + kChipGap;
    }
    return out;
}
double pageTop(int pi) {
    if (!hasChips(g.pages[pi])) return kHeader;
    const auto rc = chipRects(pi);
    return (rc.empty() ? kChipsY : rc.back().y) + kChipH + 20;
}

double cardW() { return g.width - kMargin - kSideW - kGap - kMargin; }
double cardH() { return g.height - 2 * kMargin; }

bool repeatableMod(const std::string& id) { return bardivider::isDivider(id) || id == "visualizer"; }
bool edCenterLocked() { return g.val["layoutFor"] != "v" && g.val["dynamicIsland"] == "1" && (g.val["barStyle"] == "island" || g.val["barStyle"] == "split"); }
void edLists(std::vector<std::string> out[4]) {
    for (int k = 0; k < 3; k++) out[k] = g.lay[k];
    for (const auto& m : kMods) {
        bool used = false;
        for (int k = 0; k < 3; k++) if (std::find(g.lay[k].begin(), g.lay[k].end(), m.id) != g.lay[k].end()) used = true;
        if (!used || repeatableMod(m.id)) out[3].push_back(m.id);
    }
    if (g.drag.on) {
        auto& src = out[g.drag.from];
        if (!repeatableMod(g.drag.id)) src.erase(std::remove(src.begin(), src.end(), g.drag.id), src.end());
        else if (g.drag.from < 3 && g.drag.fromIdx < static_cast<int>(src.size())) src.erase(src.begin() + g.drag.fromIdx);
        if (g.drag.hoverLane >= 0) {
            auto& l = out[g.drag.hoverLane];
            l.insert(l.begin() + std::min<size_t>(l.size(), g.drag.hoverIdx), "");
        }
    }
}
constexpr double kEdChipH = 34, kEdChipGap = 6, kEdPad = 12, kEdHead = 34;
bool edClearShown() { return !g.lay[0].empty() || !g.lay[1].empty() || !g.lay[2].empty(); }
constexpr double kEdClearGapTop = 8, kEdClearH = 30, kEdClearGapBelow = 16;
double edTop() { return edClearShown() ? kEdClearGapTop + kEdClearH + kEdClearGapBelow : 16; }
double edChipW(const std::string& id) {
    if (id.empty()) return g.drag.w;
    const ModInfo* m = modInfo(id);
    double tw = 0, th = 0;
    measure(m ? m->label : id, 14, false, 0, tw, th);
    return tw + 52;
}
void drawDisplayMap(cairo_t* cr, int rowIdx, const Rect& canvas, double oy, bool live) {
    const Color primary = g.color("primary"), onSurface = g.color("on_surface"), variant = g.color("on_surface_variant");
    rrect(cr, canvas.x, canvas.y + oy, canvas.w, canvas.h, 20);
    src(cr, g.color("surface_container"));
    cairo_fill(cr);
    if (g_mons.empty()) {
        double w = 0, h = 0;
        measure("No screens found.", 14, false, 0, w, h);
        text(cr, "No screens found.", canvas.x + (canvas.w - w) / 2, canvas.y + oy + (canvas.h - h) / 2, 14, false, variant);
    } else {
        const std::vector<dm::Screen> screens = monScreens();
        const MapView v = g_mapDrag.on ? g_mapDrag.view : mapViewFor(canvas, screens);
        if (live) { g_mapView = v; g_mapCount = static_cast<int>(screens.size()); }
        std::vector<size_t> order;
        for (size_t i = 0; i < screens.size(); i++) if (i != g_monSel && !(g_mapDrag.on && g_mapDrag.idx == i)) order.push_back(i);
        if (g_monSel < screens.size() && !(g_mapDrag.on && g_mapDrag.idx == g_monSel)) order.push_back(g_monSel);
        if (g_mapDrag.on && g_mapDrag.idx < screens.size()) order.push_back(g_mapDrag.idx);
        for (size_t i : order) {
            const dm::Screen& sc = screens[i];
            const double x = sc.r.x * v.scale + v.tx, y = sc.r.y * v.scale + v.ty + oy, w = sc.r.w * v.scale, h = sc.r.h * v.scale;
            const bool sel = i == g_monSel, off = sc.disabled;
            bool hot = false;
            if (live) {
                g.hits.push_back({Rect{x, y - oy, w, h}, rowIdx, static_cast<int>(i), DisplayMap});
                hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
            }
            const double rad = std::min(12.0, std::min(w, h) * 0.18);
            rrect(cr, x, y, w, h, rad);
            src(cr, sel ? g.color("primary_container") : hot ? g.color("secondary_container") : g.color("surface_container_highest"), off ? 0.45 : 1);
            cairo_fill_preserve(cr);
            src(cr, sel ? primary : g.color("outline"), sel ? 1 : 0.7);
            cairo_set_line_width(cr, sel ? 2 : 1);
            cairo_stroke(cr);
            cairo_save(cr);
            rrect(cr, x, y, w, h, rad);
            cairo_clip(cr);
            const Color ink = sel ? g.color("on_primary_container") : onSurface;
            const double alpha = off ? 0.55 : 1;
            const std::string num = std::to_string(i + 1);
            const double npx = std::clamp(h * 0.34, 14.0, 40.0);
            double nw = 0, nh = 0;
            measure(num, npx, true, 0, nw, nh);
            const bool roomName = h >= 70 && w >= 80, roomInfo = h >= 100 && w >= 110;
            const double block = nh + (roomName ? 15 : 0) + (roomInfo ? 14 : 0);
            double ty = y + (h - block) / 2;
            text(cr, num, x + (w - nw) / 2, ty, npx, true, ink, 0, alpha);
            ty += nh;
            auto line = [&](const std::string& t, double px, double a) {
                double lw = 0, lh = 0;
                measure(t, px, false, 0, lw, lh);
                if (lw > w - 8) return;
                text(cr, t, x + (w - lw) / 2, ty, px, false, ink, 0, a * alpha);
                ty += lh;
            };
            if (roomName) line(sc.name, 11.5, 0.85);
            if (roomInfo) {
                const Mon& m = g_mons[i];
                std::string mode = m.mode;
                const size_t at = mode.find('@');
                if (at != std::string::npos) {
                    const double hz = std::atof(mode.c_str() + at + 1);
                    mode = mode.substr(0, at) + "@" + std::to_string(static_cast<int>(std::lround(hz)));
                }
                const double scl = std::atof(m.scale.c_str());
                line(mode + (scl > 1.001 || scl < 0.999 ? "  x" + m.scale : ""), 10.5, 0.7);
            }
            cairo_restore(cr);
        }
        if (g_mapDrag.on && g_mapDrag.moved && g_mapDrag.idx < g_mons.size()) {
            const std::string t = g_mons[g_mapDrag.idx].name + "   " + std::to_string(g_mapDrag.curX) + ", " + std::to_string(g_mapDrag.curY);
            double tw = 0, th = 0;
            measure(t, 12, true, 0, tw, th);
            rrect(cr, canvas.x + 10, canvas.y + oy + 10, tw + 20, th + 10, (th + 10) / 2);
            src(cr, g.color("secondary_container"));
            cairo_fill(cr);
            text(cr, t, canvas.x + 20, canvas.y + oy + 15, 12, true, g.color("on_secondary_container"));
        }
    }
    static const char* labels[2] = {"Identify", "Reset layout"};
    double bx = canvas.x;
    const double by = canvas.y + canvas.h + 12;
    for (int k = 0; k < 2; k++) {
        double tw = 0, th = 0;
        measure(labels[k], 14, true, 0, tw, th);
        const Rect b{bx, by, tw + 36, kMapBtnH};
        bool hot = false;
        if (live) {
            g.hits.push_back({b, rowIdx, 100 + k, DisplayMap});
            hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
        }
        const bool disabled = k == 1 && g_mons.size() < 2;
        rrect(cr, b.x, b.y + oy, b.w, b.h, b.h / 2);
        src(cr, hot && !disabled ? mix(primary, g.color("secondary_container"), 0.14) : g.color("secondary_container"), disabled ? 0.5 : 1);
        cairo_fill(cr);
        text(cr, labels[k], b.x + 18, b.y + oy + (b.h - th) / 2, 14, true, g.color("on_secondary_container"), 0, disabled ? 0.5 : 1);
        bx += b.w + 10;
    }
}

EdGeom edLayout(double x0, double y0, double w) {
    std::vector<std::string> L[4];
    edLists(L);
    EdGeom G;
    const double lw = (w - 20) / 3;
    double laneH = 0;
    auto flow = [&](int lane, double bx, double by, double bw) {
        double x = bx + kEdPad, y = by + kEdHead;
        for (size_t i = 0; i < L[lane].size(); i++) {
            const double cw = edChipW(L[lane][i]);
            if (x > bx + kEdPad && x + cw > bx + bw - kEdPad) { x = bx + kEdPad; y += kEdChipH + kEdChipGap; }
            G.chips.push_back({{x, y, cw, kEdChipH}, L[lane][i], lane, static_cast<int>(i), L[lane][i].empty()});
            x += cw + kEdChipGap;
        }
        return y + kEdChipH + kEdPad - by;
    };
    for (int k = 0; k < 3; k++) laneH = std::max(laneH, flow(k, x0 + k * (lw + 10), y0, lw));
    laneH = std::max(laneH, 116.0);
    for (int k = 0; k < 3; k++) G.lane[k] = {x0 + k * (lw + 10), y0, lw, laneH};
    const double ty = y0 + laneH + 10;
    const double th = std::max(flow(3, x0, ty, w), 82.0);
    G.lane[3] = {x0, ty, w, th};
    G.h = laneH + 10 + th;
    return G;
}

std::string expandHome(const std::string& p) { return !p.empty() && p[0] == '~' ? homeDir() + p.substr(1) : p; }
std::string avatarFolder() {
    std::string f = g.val.count("avatarPath") ? g.val["avatarPath"] : "";
    if (f.empty()) jsonValue(readFile(homeDir() + "/.config/illogical-impulse/config.json"), "avatarPath", f);
    return f.empty() ? homeDir() + "/Pictures" : expandHome(f);
}
std::string avatarCurrent() {
    std::string c = g.val.count("avatarPicture") ? g.val["avatarPicture"] : "";
    if (c.empty()) jsonValue(readFile(homeDir() + "/.config/illogical-impulse/config.json"), "avatarPicture", c);
    return c;
}
std::vector<std::string> g_gal;
std::string g_galDir;
time_t g_galStamp = -1;
void scanGallery() {
    namespace fs = std::filesystem;
    const std::string dir = avatarFolder();
    std::error_code ec;
    const time_t stamp = mtimeOf(dir);
    if (dir == g_galDir && stamp == g_galStamp) return;
    g_galDir = dir;
    g_galStamp = stamp;
    g_gal.clear();
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".svg" || ext == ".gif") g_gal.push_back(e.path().string());
    }
    std::sort(g_gal.begin(), g_gal.end());
    if (g_gal.size() > 120) g_gal.resize(120);
}
constexpr double kSw = 46, kSwGap = 14;
constexpr double kWcW = 146, kWcH = 126, kWcGap = 12;
constexpr double kThW = 204, kThH = 150, kThGap = 12;
std::vector<std::string> g_accColors;
std::string g_accWall;
void rgb2hsv(double r, double g_, double b, double& h, double& s, double& v) {
    const double mx = std::max({r, g_, b}), mn = std::min({r, g_, b}), d = mx - mn;
    v = mx; s = mx <= 0 ? 0 : d / mx; h = 0;
    if (d > 0) {
        if (mx == r) h = std::fmod((g_ - b) / d, 6.0);
        else if (mx == g_) h = (b - r) / d + 2;
        else h = (r - g_) / d + 4;
        h *= 60; if (h < 0) h += 360;
    }
}
void hsv2rgb(double h, double s, double v, double& r, double& g_, double& b) {
    const double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2.0) - 1)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
    r = rr + m; g_ = gg + m; b = bb + m;
}
Color hexColor(const std::string& h);
void scanAccents() {
    const std::string wall = [] { std::string w = readFile(stateDir() + "/wallpaper.txt"); while (!w.empty() && (w.back() == '\n' || w.back() == '\r')) w.pop_back(); return w; }();
    if (wall == g_accWall && !g_accColors.empty()) return;
    g_accWall = wall;
    g_accColors.clear();
    GdkPixbuf* pb = wall.empty() ? nullptr : gdk_pixbuf_new_from_file_at_scale(wall.c_str(), 96, 96, TRUE, nullptr);
    if (!pb) return;
    constexpr int kBins = 36;
    struct Bin { double w = 0, r = 0, g = 0, b = 0; } bins[kBins];
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), nch = gdk_pixbuf_get_n_channels(pb), rs = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const guchar* q = px + y * rs + x * nch;
            double h, sat, v;
            rgb2hsv(q[0] / 255.0, q[1] / 255.0, q[2] / 255.0, h, sat, v);
            if (sat < 0.12 || v < 0.15) continue;
            const double wgt = sat * (0.4 + v);
            Bin& bn = bins[std::min(kBins - 1, static_cast<int>(h / 10))];
            bn.w += wgt; bn.r += q[0] * wgt; bn.g += q[1] * wgt; bn.b += q[2] * wgt;
        }
    g_object_unref(pb);
    std::vector<int> order(kBins);
    for (int i = 0; i < kBins; i++) order[i] = i;
    auto score = [&](int i) { return bins[i].w + 0.5 * (bins[(i + 1) % kBins].w + bins[(i + kBins - 1) % kBins].w); };
    std::sort(order.begin(), order.end(), [&](int a, int b) { return score(a) > score(b); });
    std::vector<double> picked;
    for (int i : order) {
        if (g_accColors.size() >= 8 || bins[i].w <= 0) break;
        const double hue = i * 10 + 5;
        bool near = false;
        for (double ph2 : picked) { double d = std::fabs(hue - ph2); d = std::min(d, 360 - d); if (d < 28) near = true; }
        if (near) continue;
        picked.push_back(hue);
        double h, sat, v;
        rgb2hsv(bins[i].r / bins[i].w / 255.0, bins[i].g / bins[i].w / 255.0, bins[i].b / bins[i].w / 255.0, h, sat, v);
        sat = std::clamp(sat, 0.45, 0.9); v = std::clamp(v, 0.6, 0.95);
        double r, g2, b;
        hsv2rgb(h, sat, v, r, g2, b);
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%02x%02x%02x", int(r * 255 + 0.5), int(g2 * 255 + 0.5), int(b * 255 + 0.5));
        g_accColors.push_back(hex);
    }
    if (!picked.empty()) {
        const double base = picked[0];
        for (double off : {40.0, 180.0, -40.0, 90.0, -90.0}) {
            if (g_accColors.size() >= 6) break;
            double r, g2, b;
            hsv2rgb(std::fmod(base + off + 360, 360.0), 0.5, 0.85, r, g2, b);
            char hex[16];
            std::snprintf(hex, sizeof hex, "#%02x%02x%02x", int(r * 255 + 0.5), int(g2 * 255 + 0.5), int(b * 255 + 0.5));
            g_accColors.push_back(hex);
        }
    }
    bool violet = false;
    for (const std::string& c : g_accColors) {
        const Color k = hexColor(c);
        double h, sat, v;
        rgb2hsv(k.r, k.g, k.b, h, sat, v);
        if (h >= 250 && h <= 320) violet = true;
    }
    if (!violet) {
        double r, g2, b;
        hsv2rgb(268, 0.45, 0.88, r, g2, b);
        char hex[16];
        std::snprintf(hex, sizeof hex, "#%02x%02x%02x", int(r * 255 + 0.5), int(g2 * 255 + 0.5), int(b * 255 + 0.5));
        g_accColors.push_back(hex);
    }
}
int accCount() { return 2 + static_cast<int>(g_accColors.size()); }
Color hexColor(const std::string& h) {
    unsigned v = 0;
    if (h.size() == 7) std::sscanf(h.c_str() + 1, "%x", &v);
    return {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
}

constexpr double kThumb = 76, kThumbGap = 14;
int galCols(double w) { return std::max(1, static_cast<int>((w + kThumbGap) / (kThumb + kThumbGap))); }
double galHeight(double w) {
    scanGallery();
    if (g_gal.empty()) return 56;
    const int cols = galCols(w), rows = (static_cast<int>(g_gal.size()) + cols - 1) / cols;
    return rows * (kThumb + kThumbGap) - kThumbGap;
}
bool g_galPending = false;
int g_galMade = 0;
cairo_surface_t* galThumb(const std::string& path) {
    static std::map<std::string, cairo_surface_t*> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    struct stat st {};
    stat(path.c_str(), &st);
    char name[64];
    std::snprintf(name, sizeof name, "%016zx.png", std::hash<std::string>{}(path + "|" + std::to_string(st.st_mtime) + "|" + std::to_string(st.st_size)));
    const std::string dir = cachePath() + "/avatars", file = dir + "/" + name;
    if (access(file.c_str(), R_OK) == 0) {
        cairo_surface_t* im = cairo_image_surface_create_from_png(file.c_str());
        if (cairo_surface_status(im) == CAIRO_STATUS_SUCCESS) return cache[path] = im;
        cairo_surface_destroy(im);
    }
    if (g_galMade >= 3) { g_galPending = true; return nullptr; }
    g_galMade++;
    cairo_surface_t* im = squareSurface(path, static_cast<int>(kThumb));
    if (im) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        cairo_surface_write_to_png(im, file.c_str());
    }
    return cache[path] = im;
}

#include "settings_collage.inc"

Layout layoutPage(int pi) {
    Layout L;
    const Page& pg = g.pages[pi];
    if (pg.about) {
        std::vector<AboutTile> t;
        double hy = 0;
        L.contentH = aboutLayout(t, hy);
        return L;
    }
    const double cw = cardW(), textW = std::floor((cw - 2 * kPadX) * 0.40);
    const double ctrlX = kPadX + textW + 28, ctrlW = cw - kPadX - ctrlX;
    double y = pageTop(pi);
    for (const Row& r : pg.rows) {
        RowLayout rl;
        if (!rowShown(pi, r)) { rl.y = y; L.rows.push_back(rl); continue; }
        if (r.kind == Accent) {
            double w = 0, th = 0, dh = 0;
            scanAccents();
            measure(r.title, 16, false, cw - 2 * kPadX, w, th);
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = th + 3 + dh;
            rl.y = y;
            const int cols = std::max(1, static_cast<int>((cw - 2 * kPadX + kSwGap) / (kSw + kSwGap)));
            const int rows = (accCount() + cols - 1) / cols;
            rl.h = 22 + rl.textH + 16 + rows * (kSw + kSwGap) - kSwGap + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == Swatch) {
            double w = 0, th = 0, dh = 0;
            measure(r.title, 16, false, cw - 2 * kPadX, w, th);
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = th + 3 + dh;
            rl.y = y;
            const int cols = std::max(1, static_cast<int>((cw - 2 * kPadX + kSwGap) / (kSw + kSwGap)));
            const int rows = (static_cast<int>(r.opts.size()) + cols - 1) / cols;
            rl.h = 22 + rl.textH + 16 + rows * (kSw + kSwGap) - kSwGap + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == ThemeCards) {
            double w = 0, dh = 0;
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = dh;
            rl.y = y;
            const double availW = cw - 2 * kPadX;
            const int cols = std::max(1, static_cast<int>((availW + kThGap) / (kThW + kThGap)));
            const double tcw = (availW - (cols - 1) * kThGap) / cols, tch = (tcw - 20) * 0.51 + 56;
            const int rows = (static_cast<int>(r.opts.size()) + cols - 1) / cols;
            rl.h = 22 + rl.textH + 16 + rows * (tch + kThGap) - kThGap + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == PresetCards) {
            double w = 0, dh = 0;
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = dh;
            rl.y = y;
            const double availW = cw - 2 * kPadX;
            const int cols = std::max(1, static_cast<int>((availW + kThGap) / (kThW + kThGap)));
            const double tcw = (availW - (cols - 1) * kThGap) / cols, tch = (tcw - 20) * 0.5625 + 20 + 52;
            const int rows = (static_cast<int>(r.opts.size()) + cols - 1) / cols;
            rl.h = 22 + rl.textH + 16 + (rows ? rows * (tch + kThGap) - kThGap : 64) + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == WCards) {
            double w = 0, th = 0, dh = 0;
            th = 0;
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = dh;
            rl.y = y;
            const int cols = std::max(1, static_cast<int>((cw - 2 * kPadX + kWcGap) / (kWcW + kWcGap)));
            const int rows = (static_cast<int>(r.opts.size()) + cols - 1) / cols;
            rl.h = 22 + rl.textH + 16 + rows * (kWcH + kWcGap) - kWcGap + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == CollageLayout) {
            const CollageDims d = collageDims(cw - 2 * kPadX, descOf(r));
            rl.y = y;
            rl.textH = d.textH;
            rl.h = d.h;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == Gallery) {
            double w = 0, th = 0, dh = 0;
            measure(r.title, 16, false, cw - 2 * kPadX, w, th);
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.textH = th + 3 + dh;
            rl.y = y;
            rl.h = 22 + rl.textH + 16 + galHeight(cw - 2 * kPadX) + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == DisplayMap) {
            double w = 0, th = 0, dh = 0;
            measure(r.title, 16, false, cw - 2 * kPadX, w, th);
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.titleH = th;
            rl.textH = th + 3 + dh;
            rl.ctrlX = kPadX;
            rl.ctrlW = cw - 2 * kPadX;
            rl.y = y;
            rl.h = 22 + rl.textH + 16 + kMapH + 12 + kMapBtnH + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        if (r.kind == Widgets) {
            double w = 0, th = 0, dh = 0;
            measure(r.title, 16, false, cw - 2 * kPadX, w, th);
            measure(descOf(r), 12.5, false, cw - 2 * kPadX, w, dh);
            rl.titleH = th;
            rl.textH = th + 3 + dh;
            rl.ctrlX = kPadX;
            rl.ctrlW = cw - 2 * kPadX;
            rl.y = y;
            rl.h = 22 + rl.textH + edTop() + edLayout(0, 0, cw - 2 * kPadX).h + 22;
            y += rl.h;
            L.rows.push_back(rl);
            continue;
        }
        double w = 0, th = 0, dh = 0;
        measure(r.title, 16, false, textW, w, th);
        const std::string desc = descOf(r);
        measure(desc, 12.5, false, textW, w, dh);
        if (desc.empty()) dh = -3;
        rl.titleH = th;
        rl.textH = th + 3 + dh;
        double ch = 28;
        rl.ctrlX = ctrlX;
        rl.ctrlW = ctrlW;
        std::vector<Rect> chips;
        if (r.kind == Scheme) {
            double x = 0, ly = 0;
            for (size_t o = 0; o < r.opts.size(); o++) {
                const double cwid = 74;
                if (x > 0 && x + cwid > ctrlW) { x = 0; ly += 40 + 8; }
                chips.push_back({x, ly, cwid, 40});
                x += cwid + 8;
            }
            ch = ly + 40;
        } else if (r.kind == StyleCards) {  // small fake cards that share the width of the control
            const double gap = 10;
            const int n = std::max<int>(1, r.opts.size());
            const double cwid = std::min(190.0, (ctrlW - (n - 1) * gap) / n), chh = std::round(cwid * 0.56) + 30;
            for (int o = 0; o < n; o++) chips.push_back({o * (cwid + gap), 0, cwid, chh});
            ch = chh;
        } else if (r.kind == IconStyle) {
            const double cwid = (ctrlW - 8) / 2;
            for (size_t o = 0; o < r.opts.size(); o++) chips.push_back({(o % 2) * (cwid + 8), (o / 2) * (74.0 + 8), cwid, 74});
            ch = ((r.opts.size() + 1) / 2) * (74.0 + 8) - 8;
        } else if (r.kind == Choice) {
            double x = 0, ly = 0;
            for (const auto& o : r.opts) {
                double tw = 0, h2 = 0;
                g_face = faceOf(r.key, o.first);
                measure(o.second, 14, false, 0, tw, h2);
                g_face.clear();
                const double cwid = tw + 32;
                if (x > 0 && x + cwid > ctrlW) { x = 0; ly += kChoiceBtnH + 8; }
                chips.push_back({x, ly, cwid, kChoiceBtnH});
                x += cwid + 8;
            }
            ch = ly + kChoiceBtnH;
        } else if (r.kind == Split) {
            double widest = 0;
            for (const auto& o : r.opts) { double tw = 0, h2 = 0; measure(o.second, 14, false, 0, tw, h2); widest = std::max(widest, tw); }
            const double bw = r.splitW > 0 ? std::min(r.splitW, ctrlW) : std::clamp(widest + 36 + 56, 200.0, ctrlW);
            const bool open = g_splitOpen == r.key;
            chips.push_back({ctrlW - bw, 0, bw, 40});
            ch = 40;
        } else if (r.kind == Text || r.kind == Action) ch = 38;
        else if (r.kind == Webapp) ch = 34;
        rl.h = std::max(rl.textH, ch) + 30;
        const double cy = y + (rl.h - ch) / 2;
        rl.ctrl = {ctrlX, cy, ctrlW, ch};
        for (auto& c : chips) { c.x += ctrlX; c.y += cy; }
        rl.chips = chips;
        rl.y = y;
        y += rl.h;
        L.rows.push_back(rl);
    }
    L.contentH = y + 12;
    return L;
}

double maxScroll(const Layout& L) { return std::max(0.0, L.contentH - cardH()); }

int valInt(const Row& r) { return g.val.count(r.key) ? std::atoi(g.val[r.key].c_str()) : r.min; }
std::string valStr(const Row& r) {
    const std::string v = g.val.count(r.key) ? g.val[r.key] : "";
    return r.kind == Text && r.key == "groupColor" && (v.empty() || v[0] != '#') ? "" : v;  // (the text field only shows a color of your own)
}

Rect sliderTrack(const RowLayout& rl) { return {rl.ctrlX, rl.ctrl.y, std::max(40.0, rl.ctrlW - 76), rl.ctrl.h}; }

void roundedRectCornersS(cairo_t* cr, double x, double y, double w, double h, double tl, double tr, double br, double bl) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - tr, y + tr, tr, -M_PI / 2, 0);
    cairo_arc(cr, x + w - br, y + h - br, br, 0, M_PI / 2);
    cairo_arc(cr, x + bl, y + h - bl, bl, M_PI / 2, M_PI);
    cairo_arc(cr, x + tl, y + tl, tl, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

Elastic g_scrollEl, g_searchEl;  // the rubber band at the ends of a page / of the search results (g.scroll stays the real position)

void drawPageBody(cairo_t* cr, int pi, double scroll, bool live) {
    const Page& pg = g.pages[pi];
    const Layout L = layoutPage(pi);
    const auto hitCopy = [](int i) { Hit h; h.row = -2; if (i >= 0 && i < static_cast<int>(g.hits.size())) h = g.hits[i]; return h; };
    const Hit hv = hitCopy(g.hoverHit), dv = hitCopy(g.dragHit);
    if (live) g.hits.clear();
    const Color onSurface = g.color("on_surface"), variant = g.color("on_surface_variant"), primary = g.color("primary");
    const Color onPrimary = g.color("on_primary"), chip = g.color("surface_container_highest"), outline = g.color("outline_variant");
    if (pg.about) { drawAbout(cr, scroll, live, live && hv.row == -1 && hv.opt != kAboutDotsOpt, live && hv.row == -1 && hv.opt == kAboutDotsOpt); return; }
    if (pg.placeholder) { drawCompositorPlaceholder(cr); return; }
    bool firstShown = true;
    for (size_t i = 0; i < pg.rows.size(); i++) {
        const Row& r = pg.rows[i];
        if (!rowShown(pi, r)) continue;
        const RowLayout& rl = L.rows[i];
        const double oy = -scroll;
        if (const double lit = live ? searchFlash(pi, static_cast<int>(i)) : 0; lit > 0.01) {  // the row the search led to
            rrect(cr, kPadX - 14, rl.y + oy + 2, cardW() - 2 * kPadX + 28, rl.h - 4, 18);
            src(cr, primary, 0.28 * lit);
            cairo_fill(cr);
        }
        if (!firstShown) {
            src(cr, outline, 0.45);
            cairo_rectangle(cr, kPadX, rl.y + oy, cardW() - 2 * kPadX, 1);
            cairo_fill(cr);
        }
        firstShown = false;
        if (r.kind == Accent) {
            const double tw0 = cardW() - 2 * kPadX;
            const double th0 = text(cr, r.title, kPadX, rl.y + oy + 22, 16, false, onSurface, tw0);
            text(cr, descOf(r), kPadX, rl.y + oy + 22 + th0 + 3, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            const int cols = std::max(1, static_cast<int>((tw0 + kSwGap) / (kSw + kSwGap)));
            const std::string cur = g.val.count("accent") ? g.val["accent"] : "";
            for (int k = 0; k < accCount(); k++) {
                const double x = kPadX + (k % cols) * (kSw + kSwGap), y = gy + (k / cols) * (kSw + kSwGap);
                const int opt = k == 0 ? -1 : k == accCount() - 1 ? 100 : k - 1;
                const bool sel = opt == -1 ? cur.empty() : opt == 100 ? (!cur.empty() && std::find(g_accColors.begin(), g_accColors.end(), cur) == g_accColors.end()) : g_accColors[opt] == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({{x, y, kSw, kSw}, static_cast<int>(i), opt, Accent});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                const double cxm = x + kSw / 2, cym = y + oy + kSw / 2;
                const double rad = kSw / 2 - (hot && !sel ? 0 : 2);
                cairo_new_sub_path(cr);
                cairo_arc(cr, cxm, cym, rad, 0, 2 * M_PI);
                if (opt == -1) src(cr, chip);
                else if (opt == 100) src(cr, !cur.empty() && sel ? hexColor(cur) : chip);
                else src(cr, hexColor(g_accColors[opt]));
                cairo_fill(cr);
                if (opt == -1) icon(cr, "magic-stick-3", cxm - 11, cym - 11, 22, variant);
                else if (opt == 100) icon(cr, "pipette", cxm - 11, cym - 11, 22, sel ? g.color("on_primary") : variant);
                else if (sel) icon(cr, "check-circle", cxm - 11, cym - 11, 22, hexColor(g_accColors[opt]).r * 0.3 + hexColor(g_accColors[opt]).g * 0.6 + hexColor(g_accColors[opt]).b * 0.1 > 0.55 ? Color{0.08, 0.08, 0.08} : Color{1, 1, 1});
                if (sel || hot) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cxm, cym, kSw / 2 - 0.5, 0, 2 * M_PI);
                    src(cr, sel ? primary : onSurface, sel ? 1 : 0.35);
                    cairo_set_line_width(cr, 2.5);
                    cairo_stroke(cr);
                }
            }
            continue;
        }
        if (r.kind == WCards) {
            const double tw0 = cardW() - 2 * kPadX;
            text(cr, descOf(r), kPadX, rl.y + oy + 22, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            const int cols = std::max(1, static_cast<int>((tw0 + kWcGap) / (kWcW + kWcGap)));
            for (int k = 0; k < static_cast<int>(r.opts.size()); k++) {
                const double x = kPadX + (k % cols) * (kWcW + kWcGap), y0 = gy + (k / cols) * (kWcH + kWcGap);
                const bool on = g.val["w:" + r.opts[k].first] == "1";
                bool hot = false;
                if (live) {
                    g.hits.push_back({{x, y0, kWcW, kWcH}, static_cast<int>(i), k, WCards});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                const double cy0 = y0 + oy;
                rrect(cr, x, cy0, kWcW, kWcH, 24);
                src(cr, on ? mix(g.color("secondary_container"), chip, 0.55) : hot ? mix(primary, chip, 0.12) : chip);
                cairo_fill(cr);
                if (on) { rrect(cr, x + 1, cy0 + 1, kWcW - 2, kWcH - 2, 23); src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
                drawWidgetPreview(cr, r.opts[k].first, x + 10, cy0 + 10, kWcW - 20, kWcH - 54);
                text(cr, r.opts[k].second, x + 14, cy0 + kWcH - 34, 14, true, onSurface);
                const double sx = x + kWcW - 16 - 36, sy = cy0 + kWcH - 36;
                rrect(cr, sx, sy, 36, 20, 10);
                if (on) { src(cr, primary); cairo_fill(cr); }
                else { src(cr, mix(g.color("outline"), chip, 0.2)); cairo_fill_preserve(cr); src(cr, g.color("outline")); cairo_set_line_width(cr, 1.8); cairo_stroke(cr); }
                cairo_new_sub_path(cr);
                cairo_arc(cr, on ? sx + 36 - 10 : sx + 10, sy + 10, on ? 7.5 : 5.5, 0, 2 * M_PI);
                src(cr, on ? onPrimary : g.color("outline"));
                cairo_fill(cr);
            }
            continue;
        }
        if (r.kind == ThemeCards) {
            const double tw0 = cardW() - 2 * kPadX;
            text(cr, descOf(r), kPadX, rl.y + oy + 22, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            const int cols = std::max(1, static_cast<int>((tw0 + kThGap) / (kThW + kThGap)));
            const double tcwBase = (tw0 - (cols - 1) * kThGap) / cols, tch = (tcwBase - 20) * 0.51 + 56;
            const std::string cur = g.val["theme"];
            for (int k = 0; k < static_cast<int>(r.opts.size()); k++) {
                const std::string& id = r.opts[k].first;
                const double x = kPadX + (k % cols) * (tcwBase + kThGap), y0 = gy + (k / cols) * (tch + kThGap), cy0 = y0 + oy;
                const bool lastCard = k == static_cast<int>(r.opts.size()) - 1;
                const int free = lastCard ? cols - 1 - (k % cols) : 0;
                const double tcw = tcwBase + free * (tcwBase + kThGap);
                const bool on = id != "+" && id == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({{x, y0, tcw, tch}, static_cast<int>(i), k, ThemeCards});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                rrect(cr, x, cy0, tcw, tch, 24);
                src(cr, on ? mix(g.color("secondary_container"), chip, 0.55) : hot ? mix(primary, chip, 0.12) : chip);
                cairo_fill(cr);
                if (on) { rrect(cr, x + 1, cy0 + 1, tcw - 2, tch - 2, 23); src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
                const double px = x + 10, py = cy0 + 10, pw = tcw - 20, ph = tch - 56;
                const ThemeInfo* th = nullptr;
                for (const auto& t : g_themes) if (t.id == id) th = &t;
                cairo_save(cr);
                rrect(cr, px, py, pw, ph, 16);
                ClipGroup cutP;
                beginClipGroup(cr, cutP);
                if (th) {
                    const double u = pw / 184.0, phh = ph / u;
                    cairo_save(cr);
                    cairo_translate(cr, px, py);
                    cairo_scale(cr, u, u);
                    const double px = 0, py = 0, pw = 184, ph = phh;
                    const Color sf = hexColor(th->cols[0]), cn = hexColor(th->cols[1]), ch = hexColor(th->cols[2]), os = hexColor(th->cols[3]);
                    const Color pr = hexColor(th->cols[4]), se = hexColor(th->cols[5]), te = hexColor(th->cols[6]);
                    src(cr, sf); cairo_paint(cr);
                    rrect(cr, px, py, pw, 22, 0); src(cr, cn); cairo_fill(cr);
                    const Color dots[3] = {pr, se, te};
                    for (int d = 0; d < 3; d++) { cairo_new_sub_path(cr); cairo_arc(cr, px + 14 + d * 13, py + 11, 4, 0, 2 * M_PI); src(cr, dots[d]); cairo_fill(cr); }
                    rrect(cr, px + pw - 44, py + 6, 32, 10, 5); src(cr, ch); cairo_fill(cr);
                    rrect(cr, px + 8, py + 30, 34, ph - 38, 8); src(cr, cn); cairo_fill(cr);
                    rrect(cr, px + 14, py + 38, 22, 6, 3); src(cr, pr); cairo_fill(cr);
                    rrect(cr, px + 14, py + 50, 22, 6, 3); src(cr, os, 0.45); cairo_fill(cr);
                    rrect(cr, px + 52, py + 32, pw - 72, 8, 4); src(cr, os, 0.9); cairo_fill(cr);
                    rrect(cr, px + 52, py + 46, (pw - 72) * 0.62, 7, 3.5); src(cr, os, 0.4); cairo_fill(cr);
                    rrect(cr, px + 52, py + ph - 24, 40, 14, 7); src(cr, pr); cairo_fill(cr);
                    rrect(cr, px + 98, py + ph - 24, 26, 14, 7); src(cr, se); cairo_fill(cr);
                    rrect(cr, px + 130, py + ph - 24, 18, 14, 7); src(cr, te); cairo_fill(cr);
                    cairo_restore(cr);
                } else if (id.empty()) {
                    src(cr, g.color("primary_container")); cairo_paint(cr);
                    icon(cr, "gallery", px + (pw - 34) / 2, py + (ph - 34) / 2, 34, g.color("on_primary_container"), true);
                } else {
                    src(cr, g.color("surface_container_high")); cairo_paint(cr);
                    icon(cr, "add-circle", px + (pw - 36) / 2, py + (ph - 36) / 2, 36, variant);
                }
                endClipGroup(cr, cutP);
                cairo_restore(cr);
                if (!th && id == "+") {
                    rrect(cr, px + 0.75, py + 0.75, pw - 1.5, ph - 1.5, 15.5);
                    const double dash[2] = {5, 4};
                    cairo_set_dash(cr, dash, 2, 0);
                    src(cr, variant, 0.55); cairo_set_line_width(cr, 1.5); cairo_stroke(cr);
                    cairo_set_dash(cr, nullptr, 0, 0);
                }
                text(cr, r.opts[k].second, x + 14, cy0 + tch - 36, 14, true, onSurface, tcw - 60);
                if (on) icon(cr, "check-circle", x + tcw - 14 - 22, cy0 + tch - 38, 22, primary, true);
                else if (th && th->source == "user") text(cr, "yours", x + tcw - 14 - 40, cy0 + tch - 32, 10, false, variant);
            }
            continue;
        }
        if (r.kind == PresetCards) {
            const double tw0 = cardW() - 2 * kPadX;
            text(cr, descOf(r), kPadX, rl.y + oy + 22, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            if (r.opts.empty()) {
                rrect(cr, kPadX, gy + oy, tw0, 64, 20);
                src(cr, chip);
                cairo_fill(cr);
                text(cr, "No presets yet. Save the current look above.", kPadX + 20, gy + oy + 22, 14, false, variant, tw0 - 40);
                continue;
            }
            const int cols = std::max(1, static_cast<int>((tw0 + kThGap) / (kThW + kThGap)));
            const double tcw = (tw0 - (cols - 1) * kThGap) / cols, tch = (tcw - 20) * 0.5625 + 20 + 52;
            for (int k = 0; k < static_cast<int>(g_presets.size()) && k < static_cast<int>(r.opts.size()); k++) {
                const PresetInfo& pr = g_presets[k];
                const double x = kPadX + (k % cols) * (tcw + kThGap), y0 = gy + (k / cols) * (tch + kThGap), cy0 = y0 + oy;
                const double bd = 30, bx2 = x + tcw - 14 - bd, bx1 = bx2 - 6 - bd, by = cy0 + tch - 14 - bd;
                const bool confO = g_presetConfirm == "o|" + pr.name, confD = g_presetConfirm == "d|" + pr.name;
                double wO = bd, wD = bd;
                if (confO) { double tw1 = 0, th1 = 0; measure("Overwrite?", 12, true, 0, tw1, th1); wO = tw1 + 26; }
                if (confD) { double tw1 = 0, th1 = 0; measure("Delete?", 12, true, 0, tw1, th1); wD = tw1 + 26; }
                const double rxO = bx1 + bd - wO, rxD = bx2 + bd - wD;
                bool hot = false, hotO = false, hotD = false;
                if (live) {
                    const int card = static_cast<int>(g.hits.size());
                    g.hits.push_back({{x, y0, tcw, tch}, static_cast<int>(i), k * 4, PresetCards});
                    g.hits.push_back({{rxO, y0 + tch - 14 - bd, wO, bd}, static_cast<int>(i), k * 4 + 1, PresetCards});
                    g.hits.push_back({{rxD, y0 + tch - 14 - bd, wD, bd}, static_cast<int>(i), k * 4 + 2, PresetCards});
                    hot = g.hoverHit >= card && g.hoverHit <= card + 2;
                    hotO = g.hoverHit == card + 1;
                    hotD = g.hoverHit == card + 2;
                }
                rrect(cr, x, cy0, tcw, tch, 24);
                src(cr, hot ? mix(primary, chip, 0.12) : chip);
                cairo_fill(cr);
                const double px = x + 10, py = cy0 + 10, pw = tcw - 20, ph = tch - 20 - 52;
                cairo_save(cr);
                rrect(cr, px, py, pw, ph, 16);
                ClipGroup cutP;
                beginClipGroup(cr, cutP);
                cairo_surface_t* pic = nullptr;
                if (!pr.wall.empty()) pic = presetThumb(pr.wall);
                if (pic) {
                    const double sc = std::max(pw / cairo_image_surface_get_width(pic), ph / cairo_image_surface_get_height(pic));
                    cairo_save(cr);
                    cairo_translate(cr, px + (pw - cairo_image_surface_get_width(pic) * sc) / 2, py + (ph - cairo_image_surface_get_height(pic) * sc) / 2);
                    cairo_scale(cr, sc, sc);
                    cairo_set_source_surface(cr, pic, 0, 0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                    cairo_paint(cr);
                    cairo_restore(cr);
                } else {
                    src(cr, g.color("surface_container_high")); cairo_paint(cr);
                    icon(cr, "gallery", px + (pw - 34) / 2, py + (ph - 34) / 2, 34, variant);
                }
                endClipGroup(cr, cutP);
                cairo_restore(cr);
                text(cr, pr.name, x + 14, cy0 + tch - 52, 14, true, onSurface, tcw - 28);
                std::string sub = pr.desc;
                if (sub.empty()) sub = (pr.mode == "light" ? "Light" : "Dark") + std::string(" · ") + (pr.theme.empty() ? presetSchemeName(pr.scheme) : pr.theme);
                text(cr, sub, x + 14, cy0 + tch - 30, 12, false, variant, tcw - 28 - (hot ? 86 : 0));
                if (hot) {
                    const struct { double bx, bw; const char* ic; const char* label; bool on, conf; bool danger; } btn[2] = {
                        {rxO, wO, "diskette", "Overwrite?", hotO, confO, false}, {rxD, wD, "trash-bin-minimalistic", "Delete?", hotD, confD, true}};
                    for (const auto& b : btn) {
                        rrect(cr, b.bx, by, b.bw, bd, bd / 2);
                        const Color fillc = b.conf ? g.color(b.danger ? "error" : "primary") : mix(g.color("surface"), Color{0, 0, 0}, 0.85);
                        src(cr, fillc, b.conf ? 1.0 : b.on ? 0.95 : 0.78);
                        cairo_fill(cr);
                        if (b.conf) {
                            text(cr, b.label, b.bx + 13, by + (bd - 15) / 2, 12, true, g.color(b.danger ? "on_error" : "on_primary"));
                        } else {
                            icon(cr, b.ic, b.bx + (bd - 18) / 2, by + (bd - 18) / 2, 18, b.danger && b.on ? g.color("error") : onSurface);
                        }
                    }
                }
            }
            continue;
        }
        if (r.kind == Swatch) {
            const double tw0 = cardW() - 2 * kPadX;
            const double th0 = text(cr, r.title, kPadX, rl.y + oy + 22, 16, false, onSurface, tw0);
            text(cr, descOf(r), kPadX, rl.y + oy + 22 + th0 + 3, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            const int cols = std::max(1, static_cast<int>((tw0 + kSwGap) / (kSw + kSwGap)));
            std::string cur = valStr(r);
            const int slot = r.key == "themePrimary" ? 0 : r.key == "themeSecondary" ? 1 : r.key == "themeTertiary" ? 2 : -1;
            if (slot >= 0 && cur.empty()) cur = g_themeDef[slot];
            std::string shown;
            for (int k = 0; k < static_cast<int>(r.opts.size()); k++) {
                const double x = kPadX + (k % cols) * (kSw + kSwGap), y = gy + (k / cols) * (kSw + kSwGap);
                const bool sel = r.opts[k].first == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({{x, y, kSw, kSw}, static_cast<int>(i), k, Swatch});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                if (sel && shown.empty()) shown = r.opts[k].second;
                if (r.key == "centeredWallpaperShape") {
                    const double cxm = x + kSw / 2, cym = y + oy + kSw / 2;
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cxm, cym, kSw / 2 - 1, 0, 2 * M_PI);
                    src(cr, sel ? primary : g.color("primary_container"), sel ? 0.25 : (hot ? 1 : 0.75));
                    cairo_fill(cr);
                    shapePath(cr, r.opts[k].first, x + 9, y + oy + 9, kSw - 18);
                    src(cr, primary);
                    cairo_fill(cr);
                    if (sel) {
                        cairo_new_sub_path(cr);
                        cairo_arc(cr, cxm, cym, kSw / 2 - 1, 0, 2 * M_PI);
                        src(cr, primary);
                        cairo_set_line_width(cr, 2.5);
                        cairo_stroke(cr);
                    }
                    continue;
                }
                const Color col = slot >= 0 && g_themeAcc.count(r.opts[k].first) ? hexColor(g_themeAcc[r.opts[k].first]) : r.opts[k].first == "white" ? Color{1, 1, 1} : r.opts[k].first == "black" ? Color{0, 0, 0} : g.color(r.opts[k].first.c_str());
                const double cxm = x + kSw / 2, cym = y + oy + kSw / 2;
                cairo_new_sub_path(cr);
                cairo_arc(cr, cxm, cym, kSw / 2 - (hot && !sel ? 0 : 2), 0, 2 * M_PI);
                src(cr, col);
                cairo_fill(cr);
                if (sel) icon(cr, "check-circle", cxm - 11, cym - 11, 22, col.r * 0.3 + col.g * 0.6 + col.b * 0.1 > 0.55 ? Color{0.08, 0.08, 0.08} : Color{1, 1, 1});
                if (sel || hot) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cxm, cym, kSw / 2 - 0.5, 0, 2 * M_PI);
                    src(cr, sel ? primary : onSurface, sel ? 1 : 0.35);
                    cairo_set_line_width(cr, 2.5);
                    cairo_stroke(cr);
                }
            }
            if (!shown.empty()) {
                double sw = 0, sh = 0;
                measure(shown, 14, false, 0, sw, sh);
                text(cr, shown, kPadX + tw0 - sw, rl.y + oy + 22, 14, false, variant);
            }
            continue;
        }
        if (r.kind == CollageLayout) {
            drawCollageRow(cr, static_cast<int>(i), r, rl, oy, live, cardW() - 2 * kPadX);
            continue;
        }
        if (r.kind == Gallery) {
            const double tw0 = cardW() - 2 * kPadX;
            const double th0 = text(cr, r.title, kPadX, rl.y + oy + 22, 16, false, onSurface, tw0);
            text(cr, descOf(r), kPadX, rl.y + oy + 22 + th0 + 3, 12.5, false, variant, tw0);
            const double gy = rl.y + 22 + rl.textH + 16;
            if (g_gal.empty()) {
                text(cr, "No pictures in this folder.", kPadX, gy + oy + 14, 14, false, variant);
                continue;
            }
            const int cols = galCols(tw0);
            const std::string cur = avatarCurrent();
            for (size_t k = 0; k < g_gal.size(); k++) {
                const double x = kPadX + (k % cols) * (kThumb + kThumbGap), y = gy + (k / cols) * (kThumb + kThumbGap);
                const bool sel = g_gal[k] == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({{x, y, kThumb, kThumb}, static_cast<int>(i), static_cast<int>(k), Gallery});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                const double cxm = x + kThumb / 2, cym = y + oy + kThumb / 2;
                cairo_save(cr);
                cairo_new_sub_path(cr);
                cairo_arc(cr, cxm, cym, kThumb / 2 - (sel ? 4 : 0), 0, 2 * M_PI);
                cairo_clip(cr);
                if (cairo_surface_t* im = galThumb(g_gal[k])) {
                    cairo_translate(cr, x, y + oy);
                    cairo_set_source_surface(cr, im, 0, 0);
                    cairo_paint(cr);
                } else { src(cr, chip); cairo_paint(cr); }
                cairo_restore(cr);
                if (sel || hot) {
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cxm, cym, kThumb / 2 - 1.5, 0, 2 * M_PI);
                    src(cr, sel ? primary : onSurface, sel ? 1 : 0.35);
                    cairo_set_line_width(cr, 3);
                    cairo_stroke(cr);
                }
            }
            continue;
        }
        if (r.kind == DisplayMap) {
            const double tw0 = cardW() - 2 * kPadX;
            const double th0 = text(cr, r.title, kPadX, rl.y + oy + 22, 16, false, onSurface, tw0);
            text(cr, descOf(r), kPadX, rl.y + oy + 22 + th0 + 3, 12.5, false, variant, tw0);
            drawDisplayMap(cr, static_cast<int>(i), Rect{kPadX, rl.y + 22 + rl.textH + 16, tw0, kMapH}, oy, live);
            continue;
        }
        if (r.kind == Widgets) {
            const double tw0 = cardW() - 2 * kPadX;
            const double th0 = text(cr, r.title, kPadX, rl.y + oy + 22, 16, false, onSurface, tw0);
            text(cr, descOf(r), kPadX, rl.y + oy + 22 + th0 + 3, 12.5, false, variant, tw0);
            if (edClearShown()) {
                const bool hotNow = live && hv.row == static_cast<int>(i) && hv.kind == Action;
                if (g_armed == "layoutClear" && live && !hotNow) g_armed.clear();
                const bool armed = g_armed == "layoutClear";
                const std::string label = armed ? "Confirm" : "Remove all";
                double bw = 0, bh = 0;
                measure(label, 13, true, 0, bw, bh);
                const double w = bw + 28 + 22, h = kEdClearH;
                const Rect b = {kPadX + tw0 - w, rl.y + 22 + rl.textH + kEdClearGapTop, w, h};
                if (live) g.hits.push_back({b, static_cast<int>(i), -1, Action});
                rrect(cr, b.x, b.y + oy, b.w, b.h, h / 2);
                src(cr, armed ? g.color("error_container") : hotNow ? mix(primary, g.color("secondary_container"), 0.18) : g.color("secondary_container"));
                cairo_fill(cr);
                const Color ink = g.color(armed ? "on_error_container" : "on_secondary_container");
                icon(cr, armed ? "check-circle" : "trash-bin-minimalistic", b.x + 12, b.y + oy + (h - 18) / 2, 18, ink);
                text(cr, label, b.x + 12 + 18 + 6, b.y + oy + (h - bh) / 2, 13, true, ink);
            }
            const EdGeom G = edLayout(kPadX, rl.y + 22 + rl.textH + edTop(), tw0);
            if (live) g.edGeom = G;
            static const char* hLaneNames[4] = {"Left", "Center", "Right", "Hidden"};
            static const char* vLaneNames[4] = {"Top", "Middle", "Bottom", "Hidden"};
            const char* const* laneNames = g.val["layoutFor"] == "v" ? vLaneNames : hLaneNames;
            for (int k = 0; k < 4; k++) {
                const Rect b = G.lane[k];
                const bool hotLane = g.drag.on && g.drag.hoverLane == k;
                rrect(cr, b.x, b.y + oy, b.w, b.h, 20);
                src(cr, k == 3 ? g.color("surface_container") : g.color("surface_container_high"));
                cairo_fill(cr);
                if (hotLane) { rrect(cr, b.x + 1, b.y + oy + 1, b.w - 2, b.h - 2, 19); src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
                if (k == 1 && edCenterLocked()) {
                    rrect(cr, b.x, b.y + oy, b.w, b.h, 20);
                    src(cr, g.color("surface"), 0.55);
                    cairo_fill(cr);
                    const double sz = std::clamp(std::min(b.w - 40, b.h - 40), 40.0, 84.0);
                    const double sx = b.x + (b.w - sz) / 2, sy = b.y + oy + (b.h - sz) / 2;
                    shapePath(cr, "Cookie7Sided", sx, sy, sz);
                    src(cr, g.color("primary_container"));
                    cairo_fill(cr);
                    icon(cr, "smartphone-2", sx + sz * 0.27, sy + sz * 0.27, sz * 0.46, g.color("on_primary_container"));
                    continue;
                }
                text(cr, laneNames[k], b.x + kEdPad + 4, b.y + oy + 9, 13, true, variant);
            }
            for (const EdChip& c : G.chips) {
                const double cy = c.r.y + oy;
                if (c.gap) {
                    rrect(cr, c.r.x + 1, cy + 1, c.r.w - 2, c.r.h - 2, 16);
                    src(cr, primary, 0.7); cairo_set_line_width(cr, 2); cairo_stroke(cr);
                    continue;
                }
                const ModInfo* m = modInfo(c.id);
                bool hot = false;
                if (live) {
                    g.hits.push_back({c.r, static_cast<int>(i), c.lane * 100 + c.idx, Widgets});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                rrect(cr, c.r.x, cy, c.r.w, c.r.h, 17);
                const Color base = c.lane == 3 ? chip : g.color("secondary_container");
                src(cr, hot ? mix(primary, base, 0.14) : base);
                cairo_fill(cr);
                const Color ink = c.lane == 3 ? onSurface : g.color("on_secondary_container");
                icon(cr, m ? m->icon : "widget-4", c.r.x + 14, cy + 9, 16, ink);
                text(cr, m ? m->label : c.id, c.r.x + 36, cy + (c.r.h - 17) / 2, 14, false, ink);
            }
            continue;
        }
        const double ty = rl.y + oy + (rl.h - rl.textH) / 2;
        const double tx0 = r.kind == Webapp ? kPadX + 46 : kPadX;
        const double th = text(cr, r.title, tx0, ty, 16, false, onSurface, std::floor((cardW() - 2 * kPadX) * 0.40));
        text(cr, descOf(r), tx0, ty + th + 3, 12.5, false, variant, std::floor((cardW() - 2 * kPadX) * 0.40));

        if (r.kind == Toggle) {
            const bool on = valInt(r) != 0;
            const Rect t = {rl.ctrlX + rl.ctrlW - 52, rl.ctrl.y + oy - 2, 52, 32};
            const bool hot = live && hv.row == static_cast<int>(i);
            const double p = springValue("sw:" + r.key, on ? 1.0 : 0.0), pc = std::clamp(p, 0.0, 1.0);
            const Color track = mix(primary, g.color("surface_container_highest"), pc), edge = g.color("outline");
            rrect(cr, t.x, t.y, t.w, t.h, t.h / 2);
            src(cr, track);
            cairo_fill(cr);
            if (pc < 1) {
                rrect(cr, t.x + 1, t.y + 1, t.w - 2, t.h - 2, t.h / 2 - 1);
                src(cr, edge, 1 - pc);
                cairo_set_line_width(cr, 2);
                cairo_stroke(cr);
            }
            const double kx = t.x + 16 + 20 * p, ky = t.y + t.h / 2, kr = (16 + 8 * p) / 2;
            if (hot) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, kx, ky, 20, 0, 2 * M_PI);
                src(cr, on ? primary : onSurface, 0.08);
                cairo_fill(cr);
            }
            cairo_new_sub_path(cr);
            cairo_arc(cr, kx, ky, std::max(1.0, kr), 0, 2 * M_PI);
            src(cr, mix(onPrimary, edge, pc));
            cairo_fill(cr);
            if (live) g.hits.push_back({{t.x, rl.ctrl.y - 2, t.w, t.h}, static_cast<int>(i), -1, Toggle});
        } else if (r.kind == Info) {
            const std::string v = valStr(r);
            double vw = 0, vh = 0;
            measure(v, 15, false, 0, vw, vh);
            text(cr, v, rl.ctrlX + rl.ctrlW - vw, rl.ctrl.y + oy + (rl.ctrl.h - vh) / 2, 15, false, onSurface);
        } else if (r.kind == Action) {
            double bw = 0, bh = 0;
            const bool hotNow = live && hv.row == static_cast<int>(i);
            if (g_armed == r.key && live && !hotNow) g_armed.clear();
            const bool armed = g_armed == r.key;
            const std::string label = armed ? "Confirm" : r.unit;
            measure(label, 14, true, 0, bw, bh);
            const double w = bw + 40 + (armed ? 26 : 0);
            const Rect b = {rl.ctrlX + rl.ctrlW - w, rl.ctrl.y + oy, w, 38};
            if (live) g.hits.push_back({{b.x, rl.ctrl.y, b.w, b.h}, static_cast<int>(i), -1, Action});
            const bool hot = live && hv.row == static_cast<int>(i);
            rrect(cr, b.x, b.y, b.w, b.h, 19);
            src(cr, armed ? g.color("error_container") : hot ? mix(primary, g.color("secondary_container"), 0.18) : g.color("secondary_container"));
            cairo_fill(cr);
            const Color ink = g.color(armed ? "on_error_container" : "on_secondary_container");
            if (armed) icon(cr, "check-circle", b.x + 14, b.y + (b.h - 20) / 2, 20, ink);
            text(cr, label, b.x + 20 + (armed ? 26 : 0), b.y + (b.h - bh) / 2, 14, true, ink);
        } else if (r.kind == Webapp) {
            const double iy = rl.y + oy + (rl.h - 32) / 2;
            if (cairo_surface_t* im = webIcon(r.unit)) {
                cairo_save(cr);
                rrect(cr, kPadX, iy, 32, 32, 8);
                cairo_clip(cr);
                cairo_set_source_surface(cr, im, kPadX, iy);
                cairo_paint(cr);
                cairo_restore(cr);
            } else {
                rrect(cr, kPadX, iy, 32, 32, 16);
                src(cr, chip);
                cairo_fill(cr);
                icon(cr, "global", kPadX + 6, iy + 6, 20, variant);
            }
            const char* names[2] = {"square-arrow-right-up", "trash-bin-minimalistic"};
            for (int o = 0; o < 2; o++) {
                const Rect b = {rl.ctrlX + rl.ctrlW - 34 - o * 42.0, rl.ctrl.y + oy, 34, 34};
                const int opt = o == 0 ? 1 : 0;
                bool hot = false;
                if (live) {
                    g.hits.push_back({{b.x, rl.ctrl.y, b.w, b.h}, static_cast<int>(i), opt, Webapp});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                const bool danger = opt == 1;
                rrect(cr, b.x, b.y, b.w, b.h, 17);
                src(cr, hot ? mix(danger ? g.color("error") : primary, chip, 0.22) : chip);
                cairo_fill(cr);
                icon(cr, names[opt], b.x + 7, b.y + 7, 20, hot ? (danger ? g.color("error") : primary) : variant);
            }
        } else if (r.kind == IconStyle) {
            const std::string cur = valStr(r);
            static const char* kSample[4] = {"home-2", "music-note", "heart", "settings"};
            for (size_t o = 0; o < r.opts.size(); o++) {
                Rect c = rl.chips[o];
                const bool active = r.opts[o].first == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({c, static_cast<int>(i), static_cast<int>(o), IconStyle});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                c.y += oy;
                rrect(cr, c.x, c.y, c.w, c.h, 22);
                src(cr, active ? g.color("secondary_container") : hot ? mix(primary, chip, 0.14) : chip);
                cairo_fill(cr);
                if (active) {
                    rrect(cr, c.x + 1, c.y + 1, c.w - 2, c.h - 2, 21);
                    src(cr, primary);
                    cairo_set_line_width(cr, 2);
                    cairo_stroke(cr);
                }
                const double cell = 28, gap = (c.w - 24 - 4 * cell) / 3;
                for (int k = 0; k < 4; k++) {
                    if (cairo_surface_t* m = iconMaskIn(r.opts[o].first, kSample[k], static_cast<int>(cell))) {
                        src(cr, active ? g.color("on_secondary_container") : onSurface);
                        cairo_mask_surface(cr, m, c.x + 12 + k * (cell + gap), c.y + 12);
                    }
                }
                const double tw = text(cr, r.opts[o].second, c.x + 14, c.y + c.h - 24, 13, active, active ? g.color("on_secondary_container") : variant);
                (void)tw;
            }
        } else if (r.kind == Scheme) {
            const std::string cur = valStr(r);
            for (size_t o = 0; o < r.opts.size(); o++) {
                Rect c = rl.chips[o];
                const bool active = r.opts[o].first == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({c, static_cast<int>(i), static_cast<int>(o), Scheme});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                c.y += oy;
                rrect(cr, c.x, c.y, c.w, c.h, 20);
                src(cr, active ? g.color("secondary_container") : hot ? mix(primary, chip, 0.14) : chip);
                cairo_fill(cr);
                if (active) {
                    rrect(cr, c.x + 1, c.y + 1, c.w - 2, c.h - 2, 19);
                    src(cr, primary);
                    cairo_set_line_width(cr, 2);
                    cairo_stroke(cr);
                }
                const auto it = g_schemes.find(r.opts[o].first);
                for (int k = 0; k < 3; k++) {
                    const double cx = c.x + c.w / 2 + (k - 1) * 15, cy = c.y + c.h / 2;
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, cx, cy, 9, 0, 2 * M_PI);
                    if (it != g_schemes.end()) { src(cr, it->second.c[k]); cairo_fill_preserve(cr); }
                    else { src(cr, onSurface, 0.15); cairo_fill_preserve(cr); }
                    src(cr, active ? g.color("secondary_container") : chip);
                    cairo_set_line_width(cr, 1.8);
                    cairo_stroke(cr);
                }
            }
        } else if (r.kind == Choice) {
            const std::string cur = valStr(r);
            for (size_t o = 0; o < r.opts.size(); o++) {
                Rect c = rl.chips[o];
                const bool active = r.opts[o].first == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({c, static_cast<int>(i), static_cast<int>(o), Choice});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                c.y += oy;
                const double sp = springValue("ch:" + r.key + ":" + r.opts[o].first, active ? 1.0 : 0.0), spc = std::clamp(sp, 0.0, 1.0);
                const double radius = std::max(4.0, c.h / 2 - (c.h / 2 - 12) * sp);
                const Color fill = mix(primary, g.color("secondary_container"), spc), ink = mix(onPrimary, g.color("on_secondary_container"), spc);
                rrect(cr, c.x, c.y, c.w, c.h, radius);
                src(cr, fill);
                cairo_fill(cr);
                if (hot) { rrect(cr, c.x, c.y, c.w, c.h, radius); src(cr, ink, 0.08); cairo_fill(cr); }
                double tw = 0, h2 = 0;
                g_face = faceOf(r.key, r.opts[o].first);
                measure(r.opts[o].second, 14, false, 0, tw, h2);
                text(cr, r.opts[o].second, c.x + (c.w - tw) / 2, c.y + (c.h - h2) / 2, 14, false, ink);
                g_face.clear();
            }
        } else if (r.kind == StyleCards) {
            const std::string cur = valStr(r);
            for (size_t o = 0; o < r.opts.size(); o++) {
                Rect c = rl.chips[o];
                const bool active = r.opts[o].first == cur;
                bool hot = false;
                if (live) {
                    g.hits.push_back({c, static_cast<int>(i), static_cast<int>(o), StyleCards});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                c.y += oy;
                rrect(cr, c.x, c.y, c.w, c.h, 16);
                src(cr, active ? mix(g.color("secondary_container"), chip, 0.55) : hot ? mix(primary, chip, 0.12) : chip);
                cairo_fill(cr);
                if (active) { rrect(cr, c.x + 1, c.y + 1, c.w - 2, c.h - 2, 15); src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); }
                drawStyleMock(cr, r.key, r.opts[o].first, c.x + 8, c.y + 8, c.w - 16, c.h - 38);
                double tw = 0, h2 = 0;
                measure(r.opts[o].second, 13, active, 0, tw, h2);
                text(cr, r.opts[o].second, c.x + (c.w - tw) / 2, c.y + c.h - 15 - h2 / 2, 13, active, onSurface);
            }
        } else if (r.kind == Split) {
            const bool open = g_splitOpen == r.key;
            const std::string cur = valStr(r);
            std::string shown = cur;
            for (const auto& o : r.opts) if (o.first == cur) shown = o.second;
            Rect b = rl.chips[0];
            b.y += oy;
            const double gap = 2, tw2 = 52, mw = b.w - tw2 - gap;
            bool hotMain = false, hotArrow = false;
            if (live) {
                g.hits.push_back({{b.x, rl.chips[0].y, mw, b.h}, static_cast<int>(i), -1, Split});
                hotMain = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                g.hits.push_back({{b.x + mw + gap, rl.chips[0].y, tw2, b.h}, static_cast<int>(i), -1, Split});
                hotArrow = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
            }
            const Color tonal = g.color("secondary_container"), onTonal = g.color("on_secondary_container");
            auto seg = [&](double x, double w, double rl2, double rr, bool hot, const Color& c) {
                roundedRectCornersS(cr, x, b.y, w, b.h, rl2, rr, rr, rl2);
                src(cr, c);
                cairo_fill(cr);
                if (hot) { roundedRectCornersS(cr, x, b.y, w, b.h, rl2, rr, rr, rl2); src(cr, onTonal, 0.08); cairo_fill(cr); }
            };
            seg(b.x, mw, 20, 4, hotMain, tonal);
            const double ar = open ? 20 : 4;
            roundedRectCornersS(cr, b.x + mw + gap, b.y, tw2, b.h, open ? 20 : 4, 20, 20, open ? 20 : 4);
            src(cr, open ? primary : tonal);
            cairo_fill(cr);
            if (hotArrow) { roundedRectCornersS(cr, b.x + mw + gap, b.y, tw2, b.h, open ? 20 : 4, 20, 20, open ? 20 : 4); src(cr, open ? onPrimary : onTonal, 0.08); cairo_fill(cr); }
            (void)ar;
            double sw = 0, sh = 0;
            measure(shown, 14, false, 0, sw, sh);
            text(cr, shown, b.x + 20, b.y + (b.h - sh) / 2, 14, true, onTonal);
            icon(cr, open ? "alt-arrow-up" : "alt-arrow-down", b.x + mw + gap + (tw2 - 20) / 2, b.y + (b.h - 20) / 2, 20, open ? onPrimary : onTonal);
        } else if (r.kind == Slider) {
            const Rect tr = sliderTrack(rl);
            const int v = valInt(r);
            const double f = std::clamp(double(v - r.min) / std::max(1, r.max - r.min), 0.0, 1.0);
            const double mid = rl.ctrl.y + oy + 14, inner = tr.w - 8, hx = tr.x + 4 + f * inner;
            const bool active = live && dv.row == static_cast<int>(i);
            const bool hot = live && hv.row == static_cast<int>(i);
            const double hw = active ? 3 : 4;
            rrect(cr, tr.x, mid - 3, std::max(0.0, hx - hw - 4 - tr.x), 6, 3);
            src(cr, primary);
            cairo_fill(cr);
            rrect(cr, hx + hw + 4, mid - 3, std::max(0.0, tr.x + tr.w - hx - hw - 4), 6, 3);
            src(cr, g.color("secondary_container"));
            cairo_fill(cr);
            rrect(cr, hx - hw / 2, mid - (hot || active ? 14 : 12), hw, hot || active ? 28 : 24, hw / 2);
            src(cr, primary);
            cairo_fill(cr);
            const std::string label = std::to_string(v) + (r.unit.empty() ? "" : (r.unit == "%" ? "%" : " " + r.unit));
            double lw = 0, lh = 0;
            measure(label, 14, false, 0, lw, lh);
            text(cr, label, rl.ctrlX + rl.ctrlW - lw, mid - lh / 2, 14, false, variant);
            if (live) g.hits.push_back({{tr.x - 6, rl.ctrl.y, tr.w + 12, rl.ctrl.h}, static_cast<int>(i), -1, Slider});
        } else {
            const bool focus = live && g.focusRow == static_cast<int>(i);
            const Rect f = {rl.ctrlX, rl.ctrl.y + oy, rl.ctrlW, 38};
            rrect(cr, f.x, f.y, f.w, f.h, 12);
            src(cr, chip);
            cairo_fill_preserve(cr);
            if (focus) { src(cr, primary); cairo_set_line_width(cr, 2); cairo_stroke(cr); } else cairo_new_path(cr);
            const std::string shown = focus ? g.edit : valStr(r);
            double tw = 0, h2 = 0;
            measure(shown, 14, false, 0, tw, h2);
            const double tx = fieldTextX(f.x, f.w, shown, focus ? g.caret : 0);
            if (focus) { g.fieldTx = tx; g.fieldR = f; }
            cairo_save(cr);
            cairo_rectangle(cr, f.x + 8, f.y, f.w - 16, f.h);
            cairo_clip(cr);
            if (focus && g.caret != g.anchor) {
                double x0 = 0, x1 = 0, hh = 0;
                measure(g.edit.substr(0, std::min(g.caret, g.anchor)), 14, false, 0, x0, hh);
                measure(g.edit.substr(0, std::max(g.caret, g.anchor)), 14, false, 0, x1, hh);
                rrect(cr, tx + x0 - 1, f.y + 7, x1 - x0 + 2, f.h - 14, 5);
                src(cr, primary, 0.35);
                cairo_fill(cr);
            }
            text(cr, shown, tx, f.y + (f.h - h2) / 2, 14, false, onSurface);
            if (focus) {
                const bool blink = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.caretReset).count() % 1000 < 600;
                double cw2 = 0, ch2 = 0;
                measure(g.edit.substr(0, g.caret), 14, false, 0, cw2, ch2);
                if (blink) { src(cr, primary); cairo_rectangle(cr, tx + cw2, f.y + 9, 2, f.h - 18); cairo_fill(cr); }
            }
            cairo_restore(cr);
            if (live) g.hits.push_back({{f.x, rl.ctrl.y, f.w, f.h}, static_cast<int>(i), -1, Text});
        }
    }

    if (!g_splitOpen.empty()) {
        for (size_t i = 0; i < pg.rows.size(); i++) {
            const Row& r = pg.rows[i];
            if (r.kind != Split || r.key != g_splitOpen || !rowShown(pi, r)) continue;
            const RowLayout& rl = L.rows[i];
            const double oy = -scroll;
            const Rect bt = {rl.chips[0].x, rl.chips[0].y + oy, rl.chips[0].w, 40};
            const double itemH = 48, fullH = r.opts.size() * itemH + 16;
            const double below = cardH() - (bt.y + bt.h + 6) - 12, above = bt.y - 6 - 12;
            const bool down = below >= std::min(fullH, 180.0) || below >= above;
            const double h = std::min({fullH, 320.0, std::max(80.0, down ? below : above)});
            const double ly = down ? bt.y + bt.h + 6 : bt.y - 6 - h;
            g_splitScroll = std::clamp(g_splitScroll, 0.0, std::max(0.0, fullH - h));
            const double e = 1 - std::pow(1 - g_splitT, 3), sc = 0.85 + 0.15 * e;
            if (live) g_splitRect = {bt.x, ly, bt.w, h};
            cairo_push_group(cr);
            cairo_save(cr);
            const double ax = bt.x + bt.w / 2, ay = down ? ly : ly + h;
            cairo_translate(cr, ax, ay);
            cairo_scale(cr, sc, sc);
            cairo_translate(cr, -ax, -ay);
            rrect(cr, bt.x + 1, ly + 3, bt.w, h, kMenuRadius);
            src(cr, Color{0, 0, 0}, 0.22);
            cairo_fill(cr);
            rrect(cr, bt.x, ly, bt.w, h, kMenuRadius);
            src(cr, g.color("surface_container"));
            cairo_fill(cr);
            cairo_save(cr);
            rrect(cr, bt.x, ly, bt.w, h, kMenuRadius);
            cairo_clip(cr);
            const std::string cur = valStr(r);
            const Color tonal = g.color("secondary_container"), onTonal = g.color("on_secondary_container");
            if (live) g.hits.push_back({{bt.x, ly - oy, bt.w, h}, static_cast<int>(i), -3, Split});
            for (size_t o = 0; o < r.opts.size(); o++) {
                const double iy = ly + 8 + o * itemH - g_splitScroll;
                if (iy + itemH < ly || iy > ly + h) continue;
                const bool active = r.opts[o].first == cur;
                bool hot = false;
                if (live && iy >= ly - 1 && iy + itemH <= ly + h + 1) {
                    g.hits.push_back({{bt.x, iy - oy, bt.w, itemH}, static_cast<int>(i), static_cast<int>(o), Split});
                    hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
                }
                if (active) { cairo_rectangle(cr, bt.x, iy, bt.w, itemH); src(cr, tonal); cairo_fill(cr); }
                if (hot) { cairo_rectangle(cr, bt.x, iy, bt.w, itemH); src(cr, active ? onTonal : onSurface, 0.08); cairo_fill(cr); }
                double lw = 0, lh = 0;
                measure(r.opts[o].second, 16, false, 0, lw, lh);
                text(cr, r.opts[o].second, bt.x + 12, iy + (itemH - lh) / 2, 16, false, active ? onTonal : onSurface, bt.w - 12 - (active ? 48 : 24));
                if (active) icon(cr, "check-circle", bt.x + bt.w - 36, iy + (itemH - 20) / 2, 20, onTonal, true);
            }
            cairo_restore(cr);
            if (fullH > h) {
                const double bh = std::max(24.0, h * h / fullH), by2 = ly + 6 + (h - 12 - bh) * (g_splitScroll / (fullH - h));
                rrect(cr, bt.x + bt.w - 7, by2, 3, bh, 1.5);
                src(cr, onSurface, 0.3);
                cairo_fill(cr);
            }
            cairo_restore(cr);
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, e);
        }
    }

    if (live) {
        bool shown = false;
        if (g.hoverHit >= 0 && g.hoverHit < static_cast<int>(g.hits.size()) && (g.hits[g.hoverHit].kind == Scheme || g.hits[g.hoverHit].kind == Swatch)) {
            const Hit& hh = g.hits[g.hoverHit];
            const Row& r = pg.rows[hh.row];
            const double plx = g.px - (kMargin + kSideW + kGap), ply = g.py - kMargin;
            if (hh.opt >= 0 && hh.opt < static_cast<int>(r.opts.size()) && Rect{hh.r.x, hh.r.y - scroll, hh.r.w, hh.r.h}.contains(plx, ply)) {
                bubbleShow(g_bubble, r.key + ":" + r.opts[hh.opt].first, r.opts[hh.opt].second, hh.r.x, hh.r.y - scroll, hh.r.w, hh.r.h, cardW(), cardH());
                shown = true;
            }
        }
        if (!shown) bubbleHide(g_bubble);
        const Color bg = g.color("inverse_surface"), fg = g.color("inverse_on_surface");
        g_bubbleMoving = bubbleDraw(cr, g_bubble, {bg.r, bg.g, bg.b}, {fg.r, fg.g, fg.b});
    }
}

void drawPageHeader(cairo_t* cr, int pi, double scroll, bool live) {
    const Page& pg = g.pages[pi];
    const auto hitCopy = [](int i) { Hit h; h.row = -2; if (i >= 0 && i < static_cast<int>(g.hits.size())) h = g.hits[i]; return h; };
    const Hit hv = hitCopy(g.hoverHit);
    (void)hv;
    const Color onSurface = g.color("on_surface"), variant = g.color("on_surface_variant"), outline = g.color("outline_variant");
    const double top = pageTop(pi), w = cardW();
    const Color base = g.color("surface_container_low");
    if (scroll > 0.5) {
        cairo_save(cr);
        cairo_rectangle(cr, 0, 0, w, top);
        src(cr, base);
        cairo_fill(cr);
        cairo_pattern_t* fade = cairo_pattern_create_linear(0, top, 0, top + 14);
        cairo_pattern_add_color_stop_rgba(fade, 0, base.r, base.g, base.b, 1);
        cairo_pattern_add_color_stop_rgba(fade, 1, base.r, base.g, base.b, 0);
        cairo_rectangle(cr, 0, top, w, 14);
        cairo_set_source(cr, fade);
        cairo_fill(cr);
        cairo_pattern_destroy(fade);
        cairo_restore(cr);
    }
    {
        double tw = 0, th = 0;
        measure(pg.name, 30, true, 0, tw, th);
        text(cr, pg.name, kPadX, 40 - th / 2, 30, true, onSurface);
    }
    if (hasChips(pg)) {
        const auto names = sectionNames(pg);
        const std::string active = activeSection(pi);
        const auto crs = chipRects(pi);
        const bool sliding = kChipFillSlides && pi == g.page && g.secAnim.moving() && !crs.empty();
        if (sliding) {
            constexpr double kOver = 14;
            const int n = static_cast<int>(crs.size());
            const double pos = g.secAnim.drawPos(n, crs[0].w, kOver);
            const auto fill = [&](double x, double y, double w, double alpha) {
                rrect(cr, x, y, w, kChipH, kChipRadius);
                src(cr, g.color("secondary_container"), alpha);
                cairo_fill(cr);
            };
            const auto stepOf = [&](int a, int b) { return std::fabs(crs[b].y - crs[a].y) < 1 ? crs[b].x - crs[a].x : crs[a].w + kChipGap; };
            if (pos < 0) {
                fill(crs[0].x + pos * (n > 1 ? stepOf(0, 1) : crs[0].w + kChipGap), crs[0].y, crs[0].w, 1);
            } else if (pos > n - 1) {
                fill(crs[n - 1].x + (pos - (n - 1)) * (n > 1 ? stepOf(n - 2, n - 1) : crs[0].w + kChipGap), crs[n - 1].y, crs[n - 1].w, 1);
            } else {
                const int i0 = std::min(static_cast<int>(std::floor(pos)), n - 1), i1 = std::min(i0 + 1, n - 1);
                const double f = std::clamp(pos - i0, 0.0, 1.0);
                if (std::fabs(crs[i0].y - crs[i1].y) < 1) {
                    fill(crs[i0].x + (crs[i1].x - crs[i0].x) * f, crs[i0].y, crs[i0].w + (crs[i1].w - crs[i0].w) * f, 1);
                } else {
                    fill(crs[i0].x, crs[i0].y, crs[i0].w, 1 - f);
                    fill(crs[i1].x, crs[i1].y, crs[i1].w, f);
                }
            }
        }
        for (size_t k = 0; k < names.size(); k++) {
            double tw = 0, th = 0;
            measure(names[k], 14, false, 0, tw, th);
            const Rect c = {crs[k].x, crs[k].y, crs[k].w, kChipH};
            const bool on = sliding ? std::fabs(g.secAnim.pos - static_cast<double>(k)) < 0.5 : names[k] == active;
            bool hot = false;
            if (live) {
                g.hits.push_back({{c.x, crs[k].y + scroll, c.w, c.h}, -2, static_cast<int>(k), Chip});
                hot = g.hoverHit == static_cast<int>(g.hits.size()) - 1;
            }
            if (on && !sliding) { rrect(cr, c.x, c.y, c.w, c.h, kChipRadius); src(cr, g.color("secondary_container")); cairo_fill(cr); }
            else if (!on) {
                rrect(cr, c.x + 0.5, c.y + 0.5, c.w - 1, c.h - 1, kChipRadius - 0.5);
                src(cr, outline); cairo_set_line_width(cr, 1); cairo_stroke(cr);
            }
            if (hot) { rrect(cr, c.x, c.y, c.w, c.h, kChipRadius); src(cr, onSurface, 0.08); cairo_fill(cr); }
            text(cr, names[k], c.x + 16, c.y + (c.h - th) / 2, 14, false, on ? g.color("on_secondary_container") : variant);
        }
    }
}

void drawPage(cairo_t* cr, int pi, double scroll, bool live) {
    drawPageBody(cr, pi, scroll, live);
    drawPageHeader(cr, pi, scroll, live);
}

void drawDragGhost(cairo_t* cr) {
    if (!g.drag.on || !g.drag.moved) return;
    const double lx = g.px - (kMargin + kSideW + kGap), ly = g.py - kMargin;
    const ModInfo* m = modInfo(g.drag.id);
    const double w = g.drag.w, x = lx - w / 2, y = ly - kEdChipH / 2;
    rrect(cr, x + 1, y + 3, w, kEdChipH, 17);
    src(cr, Color{0, 0, 0}, 0.25);
    cairo_fill(cr);
    rrect(cr, x, y, w, kEdChipH, 17);
    src(cr, g.color("primary"));
    cairo_fill(cr);
    icon(cr, m ? m->icon : "widget-4", x + 14, y + 9, 16, g.color("on_primary"));
    text(cr, m ? m->label : g.drag.id, x + 36, y + (kEdChipH - 17) / 2, 14, false, g.color("on_primary"));
}

Rect closeRect() { return {kMargin + kSideW + kGap + cardW() - 20 - 40, kMargin + 20, 40, 40}; }

int profilePage();
std::vector<Rect> sideRects() {
    std::vector<Rect> v;
    int shown = 0;
    for (const auto& pg : g.pages) shown += pg.hidden ? 0 : 1;
    const double titleBottom = kMargin + 54, navBottom = g.height - kMargin - 104.0 - 12;
    const double items = shown * 46.0 + std::max(0, shown - 1) * 8.0;
    double y = titleBottom + std::clamp((navBottom - titleBottom - items) / 2, 8.0, 24.0);  // (centred in a short window, but close under the title in a tall one)
    for (size_t i = 0; i < g.pages.size(); i++) {
        if (g.pages[i].hidden) { v.push_back({}); continue; }
        v.push_back({kMargin + 14, y, kSideW - 28, 46});
        y += 54;
    }
    v.push_back({kMargin, g.height - kMargin - 104.0, kSideW, 104});
    return v;
}

int sideOrdinal(int page) {
    int n = 0;
    for (int i = 0; i < page && i < static_cast<int>(g.pages.size()); i++) n += g.pages[i].hidden ? 0 : 1;
    return n;
}
int chipIndex(int pi) {
    const auto names = sectionNames(g.pages[pi]);
    return names.size() < 2 ? 0 : std::clamp(g.secSel[pi], 0, static_cast<int>(names.size()) - 1);
}
void syncTabAnims() {
    const bool noRow = g.pages[g.page].hidden;
    if (!noRow && !g.sideAnim.moving() && g.sideAnim.tab != sideOrdinal(g.page)) g.sideAnim.snap(sideOrdinal(g.page));
    if (g.pillA == g.pillTarget && g.pillTarget != (noRow ? 0.0 : 1.0)) g.pillA = g.pillTarget = noRow ? 0.0 : 1.0;
    if (!g.secAnim.fading && g.secAnim.tab != chipIndex(g.page)) g.secAnim.snap(chipIndex(g.page));
}

void wlBufferRelease(void*, wl_buffer* b) { wl_buffer_destroy(b); }
const wl_buffer_listener kBufferListener = {wlBufferRelease};

// ------------------------------------------------------------------------------------------------ Search
// A search icon beside the close button. It opens into a pill, and while it has text the card shows the matches (every row of every page, by its
// title, description, chip, page and config key) in place of the page; Up and Down choose, Enter goes to that row's page and chip and pulses the row.
struct SearchHit { int page = 0, row = 0, secIdx = -1; std::string title, crumb, desc; double score = 0; };
std::vector<SearchHit> g_searchHits;
constexpr double kSearchW = 320, kSearchItemH = 68, kSearchGap = 10;
void switchPage(int to);

std::string sNorm(const std::string& in) {  // lower case, without the accents of the Latin languages
    std::string o;
    for (size_t i = 0; i < in.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == 0xC3 && i + 1 < in.size()) {
            const unsigned char d = static_cast<unsigned char>(in[++i]);
            const char* from = "\xA0\xA1\xA2\xA3\xA4\xA5\xA7\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF\xB1\xB2\xB3\xB4\xB5\xB6\xB9\xBA\xBB\xBC";
            const char* to = "aaaaaaceeeeiiiinooooouuuu";
            const char* p = std::strchr(from, d);
            o += p && d ? to[p - from] : '?';
        } else o += static_cast<char>(std::tolower(c));
    }
    return o;
}
bool sWordPrefix(const std::string& hay, const std::string& tok) {
    for (size_t p = hay.find(tok); p != std::string::npos; p = hay.find(tok, p + 1))
        if (p == 0 || !std::isalnum(static_cast<unsigned char>(hay[p - 1]))) return true;
    return false;
}
int sEdit(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); i++) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); j++) cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}
bool sSubsequence(const std::string& hay, const std::string& tok) {
    size_t at = 0;
    for (const char c : hay) if (at < tok.size() && c == tok[at]) at++;
    return at == tok.size();
}
// Words people use that the settings do not: each also looks for these.
const std::vector<std::pair<const char*, const char*>>& sSynonyms() {
    static const std::vector<std::pair<const char*, const char*>> v = {
        {"wifi", "network"}, {"internet", "network"}, {"red", "network"}, {"sound", "volume"}, {"audio", "volume"}, {"sonido", "volume"},
        {"theme", "themes"}, {"tema", "themes"}, {"colour", "color"}, {"colors", "color"}, {"transparency", "opacity"}, {"transparencia", "opacity"},
        {"font", "font"}, {"letra", "font"}, {"fuente", "font"}, {"icon", "icons"}, {"icono", "icons"}, {"panel", "bar"}, {"barra", "bar"},
        {"notification", "notifications"}, {"aviso", "notifications"}, {"keyboard", "keyboard"}, {"teclado", "keyboard"}};
    return v;
}
double sScoreToken(const std::string& tok, const std::string& title1, const std::string& title2, const std::string& where1, const std::string& where2,
                   const std::string& desc1, const std::string& desc2, const std::string& key) {
    double best = 0;
    const auto consider = [&](const std::string& t, double k) {
        double s = 0;
        for (const std::string* f : {&title1, &title2}) {
            if (f->empty()) continue;
            if (sWordPrefix(*f, t)) s = std::max(s, 100.0);
            else if (f->find(t) != std::string::npos) s = std::max(s, 70.0);
            else if (t.size() >= 4) {
                std::istringstream words(*f);
                for (std::string w; words >> w;) {
                    const int cap = t.size() >= 7 ? 2 : 1;
                    if (std::abs(static_cast<int>(w.size()) - static_cast<int>(t.size())) <= cap && sEdit(w, t) <= cap) s = std::max(s, 50.0);
                    else if (w.size() > t.size() && sEdit(w.substr(0, t.size()), t) <= 1) s = std::max(s, 45.0);
                }
            }
            if (s == 0 && t.size() >= 3 && sSubsequence(*f, t)) s = 25;
        }
        if (!key.empty() && key.find(t) != std::string::npos) s = std::max(s, 60.0);
        for (const std::string* f : {&where1, &where2}) {
            if (f->empty()) continue;
            if (sWordPrefix(*f, t)) s = std::max(s, 50.0);
            else if (f->find(t) != std::string::npos) s = std::max(s, 40.0);
        }
        for (const std::string* f : {&desc1, &desc2}) {
            if (f->empty()) continue;
            if (sWordPrefix(*f, t)) s = std::max(s, 35.0);
            else if (f->find(t) != std::string::npos) s = std::max(s, 30.0);
        }
        best = std::max(best, s * k);
    };
    consider(tok, 1.0);
    for (const auto& [from, to] : sSynonyms()) if (tok == from) consider(to, 0.8);
    return best;
}

void searchRun() {
    g_searchHits.clear();
    std::vector<std::string> tokens;
    {
        std::istringstream in(sNorm(g.searchQ));
        for (std::string t; in >> t;) tokens.push_back(t);
    }
    if (tokens.empty()) return;
    const std::string whole = sNorm(g.searchQ);
    for (size_t pi = 0; pi < g.pages.size(); pi++) {
        const Page& pg = g.pages[pi];
        if (pg.hidden || pg.about || pg.placeholder) continue;
        const std::vector<std::string> names = sectionNames(pg);
        for (size_t ri = 0; ri < pg.rows.size(); ri++) {
            const Row& r = pg.rows[ri];
            if (r.title.empty() || !rowAvailable(r)) continue;
            int secIdx = -1;
            if (names.size() >= 2) {
                const auto at = std::find(names.begin(), names.end(), r.section);
                if (at == names.end()) continue;
                secIdx = static_cast<int>(at - names.begin());
            }
            const std::string desc = descOf(r), titleTr = i18n::tr(r.title.c_str()), descTr = i18n::tr(desc.c_str());
            const std::string pageTr = i18n::tr(pg.name.c_str()), secTr = r.section.empty() ? "" : i18n::tr(r.section.c_str());
            const std::string t1 = sNorm(r.title), t2 = sNorm(titleTr), w1 = sNorm(pg.name + " " + r.section), w2 = sNorm(pageTr + " " + secTr);
            const std::string d1 = sNorm(desc), d2 = sNorm(descTr), key = sNorm(r.key);
            double total = 0;
            bool all = true;
            for (const std::string& tok : tokens) {
                const double s = sScoreToken(tok, t1, t2, w1, w2, d1, d2, key);
                if (s <= 0) { all = false; break; }
                total += s;
            }
            if (!all) continue;
            if (t1 == whole || t2 == whole) total += 40;
            SearchHit h;
            h.page = static_cast<int>(pi);
            h.row = static_cast<int>(ri);
            h.secIdx = secIdx;
            h.title = titleTr;
            h.crumb = pageTr + (secTr.empty() || names.size() < 2 ? "" : "  \xE2\x80\xBA  " + secTr);
            h.desc = descTr;
            h.score = total;
            g_searchHits.push_back(std::move(h));
        }
    }
    std::stable_sort(g_searchHits.begin(), g_searchHits.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
    if (g_searchHits.size() > 60) g_searchHits.resize(60);
}

bool searchResultsShown() { return g.searchOpen && !g.searchQ.empty(); }
double searchT() { return std::max(0.0, springValue("search", g.searchOpen ? 1.0 : 0.0)); }
Rect searchRect() {
    const Rect c = closeRect();
    const double room = cardW() - 2 * kPadX - 40 - kSearchGap - 230;
    const double w = 40 + (std::min(kSearchW, std::max(180.0, room)) - 40) * std::min(1.05, searchT());
    return {c.x - kSearchGap - w, c.y, w, 40};
}
Rect searchClearRect() { const Rect s = searchRect(); return {s.x + s.w - 38, s.y + 2, 36, 36}; }
void searchOpenNow() {
    commitText();
    g.searchOpen = true;
    g.caretReset = Clock::now();
    g.dirty = true;
}
void searchClose() {
    g.searchOpen = false;
    g.searchQ.clear();
    g.searchCaret = g.searchAnchor = 0;
    g.searchSel = 0;
    g.searchScroll = 0;
    g_searchHits.clear();
    g.dirty = true;
}
double searchViewH() { return cardH() - kHeader - 12; }
double searchMaxScroll() { return std::max(0.0, g_searchHits.size() * kSearchItemH - searchViewH()); }
void searchKeepVisible() {
    const double top = g.searchSel * kSearchItemH, bottom = top + kSearchItemH;
    if (top < g.searchScroll) g.searchScroll = top;
    else if (bottom > g.searchScroll + searchViewH()) g.searchScroll = bottom - searchViewH();
    g.searchScroll = std::clamp(g.searchScroll, 0.0, searchMaxScroll());
}
int searchItemAt(double lx, double ly) {
    if (!searchResultsShown() || lx < 16 || lx > cardW() - 16 || ly < kHeader || ly > cardH() - 12) return -1;
    const int i = static_cast<int>(std::floor((ly - kHeader + g.searchScroll) / kSearchItemH));
    return i >= 0 && i < static_cast<int>(g_searchHits.size()) ? i : -1;
}

// Goes to the page and chip of a result, scrolls its row to the top and makes it pulse.
void searchGo(int index) {
    if (index < 0 || index >= static_cast<int>(g_searchHits.size())) return;
    const SearchHit h = g_searchHits[index];
    searchClose();
    if (h.secIdx >= 0) { g.secSel[h.page] = h.secIdx; savePage(); }
    if (h.page != g.page) switchPage(h.page);
    else { g.secAnim.snap(chipIndex(h.page)); g_splitOpen.clear(); }
    const Layout L = layoutPage(h.page);
    if (h.row < static_cast<int>(L.rows.size())) g.scroll = std::clamp(L.rows[h.row].y - pageTop(h.page) - 8, 0.0, maxScroll(L));
    g.flashPage = h.page;
    g.flashRow = h.row;
    g.flashAt = Clock::now();
    g.hoverHit = -1;
    g.dirty = true;
}
double searchFlash(int pi, int ri) {  // 0..1: how strongly the row found by the search is lit
    if (pi != g.flashPage || ri != g.flashRow) return 0;
    const double t = std::chrono::duration<double>(Clock::now() - g.flashAt).count();  // three blinks, on and off, then gone
    if (t >= 1.2) return 0;
    return std::clamp(1.5 * std::cos(2 * M_PI * t / 0.4), 0.0, 1.0);
}
bool searchFlashing() { return g.flashRow >= 0 && std::chrono::duration<double>(Clock::now() - g.flashAt).count() < 1.3; }

double textRaw(cairo_t* cr, const std::string& s, double x, double y, double px, bool bold, const Color& c, double alpha = 1) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    pango_layout_set_text(l, s.c_str(), static_cast<int>(s.size()));  // (with a length: not looked up in the language catalog)
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    return h;
}
void textFit(cairo_t* cr, const std::string& s, double x, double y, double w, double px, bool bold, const Color& c, double alpha = 1) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    setFont(l, px, bold);
    pango_layout_set_width(l, static_cast<int>(w * PANGO_SCALE));
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_text(l, s.c_str(), static_cast<int>(s.size()));
    src(cr, c, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

// The matches, in the card in place of the page (card coordinates).
void drawSearchResults(cairo_t* cr) {
    const Color onSurface = g.color("on_surface"), variant = g.color("on_surface_variant");
    text(cr, "Search", kPadX, 40 - 18, 30, true, onSurface);
    if (g_searchHits.empty()) {
        icon(cr, "magnifer", cardW() / 2 - 20, 190, 40, variant);
        double w = 0, h = 0;
        measure("No results", 18, true, 0, w, h);
        text(cr, "No results", (cardW() - w) / 2, 244, 18, true, onSurface);
        return;
    }
    cairo_save(cr);
    cairo_rectangle(cr, 0, kHeader, cardW(), searchViewH());
    cairo_clip(cr);
    for (size_t i = 0; i < g_searchHits.size(); i++) {
        const double y = kHeader + i * kSearchItemH - (g.searchScroll + g_searchEl.over);
        if (y + kSearchItemH < kHeader || y > cardH()) continue;
        const SearchHit& h = g_searchHits[i];
        const bool on = static_cast<int>(i) == g.searchSel;
        if (on) { rrect(cr, 16, y + 2, cardW() - 32, kSearchItemH - 4, 20); src(cr, g.color("secondary_container")); cairo_fill(cr); }
        const Color ink = on ? g.color("on_secondary_container") : onSurface, sub = on ? g.color("on_secondary_container") : variant;
        icon(cr, g.pages[h.page].icon, 38, y + (kSearchItemH - 22) / 2, 22, sub);
        const double tx = 80, tw = cardW() - tx - 36;
        const double block = 19 + 2 + 15 + (h.desc.empty() ? 0 : 2 + 14), top = y + 2 + (kSearchItemH - 4 - block) / 2;  // the lines centred in the row
        textFit(cr, h.title, tx, top, tw, 16, false, ink);
        textFit(cr, h.crumb, tx, top + 21, tw, 12.5, false, sub, on ? 0.9 : 0.8);
        if (!h.desc.empty()) textFit(cr, h.desc, tx, top + 38, tw, 11.5, false, sub, 0.6);
    }
    cairo_restore(cr);
}

// The search icon, and the pill it grows into (window coordinates).
void drawSearchPill(cairo_t* cr) {
    const Rect s = searchRect();
    const double t = std::min(1.0, searchT());
    const Color onSurface = g.color("on_surface"), variant = g.color("on_surface_variant"), primary = g.color("primary");
    const bool hot = g.hoverSearch && !g.searchOpen;
    // closed: the same filled circle as the close button; it opens into an outlined pill
    rrect(cr, s.x, s.y, s.w, s.h, s.h / 2);
    const Color closed = hot ? mix(g.color("on_surface"), g.color("surface_container_highest"), 0.08) : g.color("surface_container_highest");
    src(cr, mix(g.color("surface_container_low"), closed, t));
    cairo_fill(cr);
    if (t > 0.01) {
        rrect(cr, s.x + 0.75, s.y + 0.75, s.w - 1.5, s.h - 1.5, s.h / 2 - 0.75);
        src(cr, primary, std::min(1.0, t));
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    }
    icon(cr, "magnifer", s.x + 9, s.y + 9, 22, g.searchOpen ? primary : (hot ? onSurface : variant));
    if (t < 0.35) return;
    const double fa = std::clamp((t - 0.35) / 0.4, 0.0, 1.0);
    const double tx = s.x + 44, tw = s.w - 44 - 40;
    cairo_save(cr);
    cairo_rectangle(cr, tx, s.y, tw, s.h);
    cairo_clip(cr);
    if (g.searchQ.empty()) {
        text(cr, "Search settings", tx, s.y + 12, 14, false, variant, 0, 0.7 * fa);
    } else {
        double qw = 0, qh = 0;
        measure(g.searchQ, 14, false, 0, qw, qh);
        double cw = 0, ch = 0;
        measure(g.searchQ.substr(0, g.searchCaret), 14, false, 0, cw, ch);
        const double shift = std::max(0.0, cw + 3 - tw);
        if (g.searchCaret != g.searchAnchor) {
            double x0 = 0, x1 = 0, hh = 0;
            measure(g.searchQ.substr(0, std::min(g.searchCaret, g.searchAnchor)), 14, false, 0, x0, hh);
            measure(g.searchQ.substr(0, std::max(g.searchCaret, g.searchAnchor)), 14, false, 0, x1, hh);
            rrect(cr, tx - shift + x0 - 1, s.y + 9, x1 - x0 + 2, 22, 5);
            src(cr, primary, 0.35);
            cairo_fill(cr);
        }
        textRaw(cr, g.searchQ, tx - shift, s.y + 20 - qh / 2, 14, false, onSurface, fa);
        g.searchTextX = tx - shift;
        if (g.searchOpen && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.caretReset).count() % 1000 < 600) {
            src(cr, primary, fa);
            cairo_rectangle(cr, tx - shift + cw, s.y + 10, 2, 20);
            cairo_fill(cr);
        }
    }
    if (g.searchQ.empty() && g.searchOpen && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g.caretReset).count() % 1000 < 600) {
        src(cr, primary, fa);
        cairo_rectangle(cr, tx, s.y + 10, 2, 20);
        cairo_fill(cr);
    }
    cairo_restore(cr);
    if (g.searchOpen) {
        const Rect c = searchClearRect();
        icon(cr, "close-circle", c.x + 8, c.y + 8, 20, g.hoverSearchClear ? onSurface : variant);
    }
}

void motion();
void render() {
    if (!g.configured || !g.surface) return;
    g_galMade = 0;
    syncTabAnims();
    const int W = g.width, H = g.height, stride = W * 4, size = stride * H;
    const int fd = memfd_create("end4pC-settings", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surf = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surf);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    rrect(cr, 0, 0, W, H, 36);
    src(cr, g.color("background"));
    cairo_fill(cr);

    const double profileH = 104, profileY = H - kMargin - profileH, navH = profileY - 12 - kMargin;
    rrect(cr, kMargin, kMargin, kSideW, navH, kRadius);
    src(cr, g.color("surface_container"));
    cairo_fill(cr);
    rrect(cr, kMargin, profileY, kSideW, profileH, kRadius);
    src(cr, g.page == profilePage() || g.hoverSide == static_cast<int>(g.pages.size()) ? g.color("surface_container_high") : g.color("surface_container"));
    cairo_fill(cr);
    {
        double tw = 0, th = 0;
        measure("Settings", 30, true, 0, tw, th);
        text(cr, "Settings", kMargin + 28, kMargin + 40 - th / 2, 30, true, g.color("on_surface"));
    }
    const auto sr = sideRects();
    std::vector<int> ordinal(g.pages.size(), -1);
    std::vector<Rect> shown;
    for (size_t i = 0; i < g.pages.size(); i++)
        if (!g.pages[i].hidden) { ordinal[i] = static_cast<int>(shown.size()); shown.push_back(sr[i]); }
    const bool sliding = (g.sideAnim.moving() || g.pillA < 0.999) && g.pillA > 0.001 && !shown.empty();
    if (sliding) {
        const double pos = g.sideAnim.drawPos(static_cast<int>(shown.size()), 54, 4);
        const size_t i0 = static_cast<size_t>(std::clamp(static_cast<int>(std::floor(pos)), 0, static_cast<int>(shown.size()) - 1));
        const size_t i1 = std::min(i0 + 1, shown.size() - 1);
        const double f = std::clamp(pos - static_cast<double>(i0), 0.0, 1.0);
        const double y = shown[i0].y + (shown[i1].y - shown[i0].y) * f + (pos < 0 ? pos * 54 : pos > shown.size() - 1 ? (pos - static_cast<double>(shown.size() - 1)) * 54 : 0);
        rrect(cr, shown[i0].x, y, shown[i0].w, shown[i0].h, 23);
        src(cr, g.color("secondary_container"), g.pillA);
        cairo_fill(cr);
    }
    for (size_t i = 0; i < g.pages.size(); i++) {
        if (g.pages[i].hidden) continue;
        const bool chosen = static_cast<int>(i) == g.page, hot = static_cast<int>(i) == g.hoverSide;
        const bool active = sliding ? std::fabs(g.sideAnim.pos - ordinal[i]) < 0.5 : chosen;
        rrect(cr, sr[i].x, sr[i].y, sr[i].w, sr[i].h, 23);
        if (active && !sliding) { src(cr, g.color("secondary_container")); cairo_fill(cr); }
        else if (hot && !active) { src(cr, g.color("surface_container_highest"), 0.8); cairo_fill(cr); }
        else cairo_new_path(cr);
        const Color c = active ? g.color("on_secondary_container") : g.color("on_surface_variant");
        icon(cr, g.pages[i].icon, sr[i].x + 16, sr[i].y + 12, 22, c, active);
        text(cr, g.pages[i].name, sr[i].x + 50, sr[i].y + 12, 16, active, c);
    }
    {
        const double sx = kMargin + 20, sy = profileY + (profileH - 76) / 2, shape = 76, av = 60;
        shapePath(cr, "Cookie9Sided", sx, sy, shape);
        src(cr, g.color("primary"));
        cairo_fill(cr);
        const double ax = sx + (shape - av) / 2, ay = sy + (shape - av) / 2;
        if (g_profile.avatar) {
            cairo_save(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, ax + av / 2, ay + av / 2, av / 2, 0, 2 * M_PI);
            cairo_clip(cr);
            cairo_set_source_surface(cr, g_profile.avatar, ax, ay);
            cairo_paint(cr);
            cairo_restore(cr);
        } else icon(cr, "user-circle", ax + 12, ay + 12, 36, g.color("on_primary"));
        const double tx = sx + shape + 12, tw = kMargin + kSideW - 16 - tx;
        PangoLayout* l = pango_cairo_create_layout(cr);
        pango_layout_set_width(l, static_cast<int>(tw * PANGO_SCALE));
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        setFont(l, 18, true);
        pango_layout_set_text(l, g_profile.name.c_str(), -1);
        int nw = 0, nh = 0;
        pango_layout_get_pixel_size(l, &nw, &nh);
        setFont(l, 12.5, false);
        pango_layout_set_text(l, g_profile.uptime.c_str(), -1);
        int uw = 0, uh = 0;
        pango_layout_get_pixel_size(l, &uw, &uh);
        const double top = sy + (shape - (nh + 2 + uh)) / 2;
        setFont(l, 18, true);
        pango_layout_set_text(l, g_profile.name.c_str(), -1);
        src(cr, g.color("on_surface"));
        cairo_move_to(cr, tx, top);
        pango_cairo_show_layout(cr, l);
        setFont(l, 12.5, false);
        pango_layout_set_text(l, g_profile.uptime.c_str(), -1);
        src(cr, g.color("on_surface_variant"));
        cairo_move_to(cr, tx, top + nh + 2);
        pango_cairo_show_layout(cr, l);
        g_object_unref(l);
    }

    const double cx = kMargin + kSideW + kGap, cy = kMargin, cw = cardW(), ch = cardH();
    rrect(cr, cx, cy, cw, ch, kRadius);
    src(cr, g.color("surface_container_low"));
    cairo_fill_preserve(cr);
    cairo_clip(cr);
    cairo_translate(cr, cx, cy);
    auto eased = [](double t) { return 1 - std::pow(1 - t, 3); };
    if (searchResultsShown()) {
        drawSearchResults(cr);
    } else if (g.animT < 1) {
        const double e = eased(g.animT), slide = 64;
        cairo_push_group(cr);
        drawPage(cr, g.prevPage, 0, false);
        cairo_pop_group_to_source(cr);
        cairo_save(cr);
        cairo_translate(cr, -g.animDir * slide * e, 0);
        cairo_paint_with_alpha(cr, 1 - e);
        cairo_restore(cr);
        cairo_push_group(cr);
        drawPage(cr, g.page, 0, true);
        cairo_pop_group_to_source(cr);
        cairo_save(cr);
        cairo_translate(cr, g.animDir * slide * (1 - e), 0);
        cairo_paint_with_alpha(cr, e);
        cairo_restore(cr);
    } else if (g.secAnim.fading) {
        cairo_push_group(cr);
        g.secForce = g.secAnim.prevTab;
        drawPageBody(cr, g.page, g.secPrevScroll, false);
        g.secForce = -1;
        cairo_pop_group_to_source(cr);
        cairo_save(cr);
        cairo_translate(cr, g.secAnim.outDx(), 0);
        cairo_paint_with_alpha(cr, g.secAnim.outAlpha());
        cairo_restore(cr);
        cairo_push_group(cr);
        drawPageBody(cr, g.page, g.scroll + g_scrollEl.over, true);
        cairo_pop_group_to_source(cr);
        cairo_save(cr);
        cairo_translate(cr, g.secAnim.inDx(), 0);
        cairo_paint_with_alpha(cr, g.secAnim.inAlpha());
        cairo_restore(cr);
        drawPageHeader(cr, g.page, g.scroll + g_scrollEl.over, true);
    } else {
        drawPage(cr, g.page, g.scroll + g_scrollEl.over, true);
    }
    drawDragGhost(cr);

    cairo_reset_clip(cr);
    cairo_identity_matrix(cr);
    {
        const Rect c = closeRect();
        cairo_new_sub_path(cr);
        cairo_arc(cr, c.x + c.w / 2, c.y + c.h / 2, c.w / 2, 0, 2 * M_PI);
        src(cr, g.hoverClose ? mix(g.color("error"), g.color("surface_container_highest"), 0.3) : g.color("surface_container_highest"));
        cairo_fill(cr);
        icon(cr, "close-circle", c.x + 9, c.y + 9, 22, g.hoverClose ? g.color("error") : g.color("on_surface_variant"));
    }
    drawSearchPill(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    wl_shm_pool* pool = wl_shm_create_pool(g.shm, fd, size);
    wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buf, &kBufferListener, nullptr);
    wl_surface_attach(g.surface, buf, 0, 0);
    wl_surface_damage_buffer(g.surface, 0, 0, W, H);
    wl_surface_commit(g.surface);
    g.dirty = false;
    if (!g.drag.on && g.dragHit < 0 && g.animT >= 1) motion();
}

void updateCursor() {
    if (!g.cursorMgr || !g.pointer) return;
    if (!g.cursorDev) g.cursorDev = wp_cursor_shape_manager_v1_get_pointer(g.cursorMgr, g.pointer);
    int shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    if ((g.drag.on && g.drag.moved) || (g_mapDrag.on && g_mapDrag.moved)) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
    else if (g.hoverSide >= 0 || g.hoverClose || g.hoverSearch || g.hoverResult) shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    else if (g.hoverHit >= 0 && g.hoverHit < static_cast<int>(g.hits.size()))
        shape = g.hits[g.hoverHit].kind == Text ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT : (g.hits[g.hoverHit].kind == Widgets || (g.hits[g.hoverHit].kind == DisplayMap && g.hits[g.hoverHit].opt < 100)) ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    if (shape == g.cursorShape) return;
    g.cursorShape = shape;
    wp_cursor_shape_device_v1_set_shape(g.cursorDev, g.enterSerial, shape);
}

void commitText() {
    if (g.focusRow < 0) return;
    const Row& r = g.pages[g.page].rows[g.focusRow];
    const std::string old = valStr(r);
    g.focusRow = -1;
    if (r.key == "webappAdd") { createWebapp(g.edit); g.edit.clear(); }
    else if (r.key == "presetSave") {
        if (!g.edit.empty()) { runCapture(sysCmd("presets.py", "--save " + shq(g.edit))); loadPresets(); }
        g.edit.clear();
    }
    else if (g.edit != old) sendSet(r.key, g.edit);
    g.dirty = true;
}

int profilePage() {
    for (size_t i = 0; i < g.pages.size(); i++) if (g.pages[i].name == "Profile") return static_cast<int>(i);
    return 0;
}

void switchPage(int to) {
    if (to == g.page) return;
    commitText();
    g_splitOpen.clear();
    g.prevPage = g.page;
    g.animDir = to > g.page ? 1 : -1;
    g.page = to;
    if (g.pages[to].hidden) {
        g.pillTarget = 0;
    } else {
        if (g.pages[g.prevPage].hidden) g.sideAnim.snap(sideOrdinal(to));
        else g.sideAnim.select(sideOrdinal(to));
        g.pillTarget = 1;
    }
    g.secAnim.snap(chipIndex(to));
    if (g.pages[to].name == "Hyprland") loadHypr();
    if (g.pages[to].name == "Niri") loadNiri();
    if (g.pages[to].name == "System" || g.pages[to].name == "General" || g.pages[to].name == "Interface") loadSystem();
    if (g.pages[to].name == "Appearance") { loadThemes(); loadPresets(); }
    savePage();
    g.scroll = 0;
    g_scrollEl.over = 0;
    g.animT = 0;
    g.animStart = Clock::now();
    g.hoverHit = -1;
    g.dirty = true;
}

void setSliderFrom(int hitIdx, bool force) {
    const Hit& h = g.hits[hitIdx];
    const Row& r = g.pages[g.page].rows[h.row];
    const double lx = g.px - (kMargin + kSideW + kGap);
    const double f = std::clamp((lx - h.r.x - 10) / std::max(1.0, h.r.w - 20), 0.0, 1.0);
    int v = r.min + static_cast<int>(std::lround(f * (r.max - r.min) / double(r.step))) * r.step;
    v = std::clamp(v, r.min, r.max);
    if (std::to_string(v) == g.val[r.key] && !force) return;
    g.val[r.key] = std::to_string(v);
    g.dirty = true;
    const auto now = Clock::now();
    const bool live = r.key.compare(0, 4, "sys:") != 0;
    if (force || (live && now - g.lastSend > std::chrono::milliseconds(120))) {
        g.lastSend = now;
        sendSet(r.key, std::to_string(v));
    }
}

void dragMotion() {
    const double lx = g.px - (kMargin + kSideW + kGap), ly = g.py - kMargin + g.scroll;
    if (!g.drag.moved && std::hypot(g.px - g.drag.pressX, g.py - g.drag.pressY) > 4) g.drag.moved = true;
    if (g.drag.moved) {
        int lane = -1;
        for (int k = 0; k < 4; k++) if (g.edGeom.lane[k].contains(lx, ly)) { lane = k; break; }
        if (lane == 1 && edCenterLocked()) lane = -1;
        int idx = 0;
        if (lane >= 0)
            for (const EdChip& c : g.edGeom.chips) {
                if (c.lane != lane || c.gap) continue;
                const double cx = c.r.x + c.r.w / 2, cyy = c.r.y + c.r.h / 2, half = c.r.h / 2;
                if (cyy < ly - half || (std::abs(cyy - ly) <= half && cx < lx)) idx++;
            }
        g.drag.hoverLane = lane;
        g.drag.hoverIdx = idx;
    }
    g.dirty = true;
    updateCursor();
}

void finishDrag() {
    const EdDrag d = g.drag;
    g.drag = EdDrag{};
    g.dirty = true;
    if (!d.moved && d.from < 3 && bardivider::isDivider(d.id)) {
        refreshDividerChoices();
        const auto selected = std::find(g.dividerLocations.begin(), g.dividerLocations.end(), std::pair{d.from, static_cast<size_t>(d.fromIdx)});
        if (selected != g.dividerLocations.end()) {
            g.val["dividerItem"] = std::to_string(selected - g.dividerLocations.begin());
            refreshDividerChoices();
            const auto sections = sectionNames(g.pages[g.page]);
            const auto divider = std::find(sections.begin(), sections.end(), "Divider");
            if (divider != sections.end()) {
                g.secSel[g.page] = static_cast<int>(divider - sections.begin());
                g.scroll = 0;
                g.hoverHit = -1;
            }
        }
    }
    if (!d.moved || d.hoverLane < 0) return;
    std::vector<std::string> lists[3] = {g.lay[0], g.lay[1], g.lay[2]};
    if (!repeatableMod(d.id)) for (auto& l : lists) l.erase(std::remove(l.begin(), l.end(), d.id), l.end());
    else if (d.from < 3 && d.fromIdx < static_cast<int>(lists[d.from].size())) lists[d.from].erase(lists[d.from].begin() + d.fromIdx);
    if (d.hoverLane < 3) lists[d.hoverLane].insert(lists[d.hoverLane].begin() + std::min<size_t>(lists[d.hoverLane].size(), d.hoverIdx), d.id);
    static const char* hNames[3] = {"left", "center", "right"};
    static const char* vNames[3] = {"sideLeft", "sideCenter", "sideRight"};
    const char* const* names = g.val["layoutFor"] == "v" ? vNames : hNames;
    for (int k = 0; k < 3; k++) {
        if (lists[k] == g.lay[k]) continue;
        std::string v;
        for (const auto& m : lists[k]) v += (v.empty() ? "" : ",") + m;
        g.lay[k] = lists[k];
        sendSet(names[k], v);
    }
    g.val.erase("dividerItem");
    refreshDividerChoices();
    updateCursor();
}

void clearLayout() {
    static const char* hNames[3] = {"left", "center", "right"};
    static const char* vNames[3] = {"sideLeft", "sideCenter", "sideRight"};
    const char* const* names = g.val["layoutFor"] == "v" ? vNames : hNames;
    for (int k = 0; k < 3; k++) {
        if (g.lay[k].empty()) continue;
        g.lay[k].clear();
        sendSet(names[k], "");
    }
    g.val.erase("dividerItem");
    refreshDividerChoices();
    g.hoverHit = -1;
    g.dirty = true;
}

void hideWidget() {
    if (g.hoverHit < 0 || g.hoverHit >= static_cast<int>(g.hits.size()) || g.hits[g.hoverHit].kind != Widgets) return;
    const int lane = g.hits[g.hoverHit].opt / 100, idx = g.hits[g.hoverHit].opt % 100;
    if (lane > 2 || idx >= static_cast<int>(g.lay[lane].size())) return;
    static const char* hNames[3] = {"left", "center", "right"};
    static const char* vNames[3] = {"sideLeft", "sideCenter", "sideRight"};
    const char* const* names = g.val["layoutFor"] == "v" ? vNames : hNames;
    g.lay[lane].erase(g.lay[lane].begin() + idx);
    std::string v;
    for (const auto& m : g.lay[lane]) v += (v.empty() ? "" : ",") + m;
    sendSet(names[lane], v);
    g.val.erase("dividerItem");
    refreshDividerChoices();
    g.hoverHit = -1;
    g.dirty = true;
}

void dragSelect();
bool shiftHeld();
double mapPointerX() { return g.px - (kMargin + kSideW + kGap); }
double mapPointerY() { return g.py - kMargin + g.scroll; }
std::string monitorPosCmd(const Mon& m) {
    if (m.name.empty()) return "";
    for (char c : m.name) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return "";
    return "python3 '" + scriptPath("hyprconf.py") + "' --monitor " + m.name + " --set position " + std::to_string(m.x) + "x" + std::to_string(m.y);
}
void monitorsApply(size_t first) {
    std::string cmd;
    for (size_t i = 0; i < g_mons.size(); i++) {
        if (i == first || g_mons[i].disabled) continue;
        const std::string c = monitorPosCmd(g_mons[i]);
        if (!c.empty()) cmd += (cmd.empty() ? "" : " ; ") + c;
    }
    if (first < g_mons.size()) {
        const std::string c = monitorPosCmd(g_mons[first]);
        if (!c.empty()) cmd += (cmd.empty() ? "" : " ; ") + c;
    }
    if (!cmd.empty()) spawn({"sh", "-c", cmd});
}
void monitorPlace(size_t idx, int x, int y) {
    if (idx >= g_mons.size()) return;
    g_mons[idx].x = x;
    g_mons[idx].y = y;
    if (idx == g_monSel) showMonitor();
    monitorsApply(idx);
}
void mapDragMotion() {
    MapDrag& d = g_mapDrag;
    if (!d.moved && std::hypot(mapPointerX() - d.pressX, mapPointerY() - d.pressY) < 4) return;
    d.moved = true;
    const double s = std::max(1e-6, d.view.scale);
    const int wantX = d.origX + static_cast<int>(std::lround((mapPointerX() - d.pressX) / s)), wantY = d.origY + static_cast<int>(std::lround((mapPointerY() - d.pressY) / s));
    std::vector<dm::Screen> screens;
    for (const Mon& m : g_mons) screens.push_back(monScreen(m));
    const int threshold = std::max(4, static_cast<int>(std::lround(12 / s)));
    dm::place(screens, d.idx, wantX, wantY, threshold, !shiftHeld(), d.curX, d.curY);
    g.dirty = true;
    updateCursor();
}
void mapDragFinish() {
    MapDrag d = g_mapDrag;
    g_mapDrag = MapDrag{};
    if (d.moved && (d.curX != d.origX || d.curY != d.origY)) monitorPlace(d.idx, d.curX, d.curY);
    g.dirty = true;
    updateCursor();
}
void mapClick(int opt) {
    if (opt >= 0 && opt < static_cast<int>(g_mons.size())) {
        g_monSel = static_cast<size_t>(opt);
        showMonitor();
        const Mon& m = g_mons[g_monSel];
        if (g_mons.size() > 1 && !m.disabled) {
            const dm::Screen sc = monScreen(m);
            g_mapDrag = MapDrag{true, false, g_monSel, mapPointerX(), mapPointerY(), sc.r.x, sc.r.y, sc.r.x, sc.r.y, g_mapView};
        }
        g.dirty = true;
        return;
    }
    if (opt == 100) {
        char exe[4096];
        const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n <= 0) return;
        std::string bin(exe, static_cast<size_t>(n));
        bin = bin.substr(0, bin.rfind('/')) + "/end4pC";
        std::string cmd = "identify";
        for (size_t i = 0; i < g_mons.size(); i++) cmd += " " + g_mons[i].name + "=" + std::to_string(i + 1);
        spawn({bin, cmd});
    } else if (opt == 101 && g_mons.size() > 1) {
        std::vector<dm::Screen> screens;
        for (const Mon& m : g_mons) screens.push_back(monScreen(m));
        dm::autoLayout(screens);
        for (size_t i = 0; i < g_mons.size(); i++) {
            if (g_mons[i].disabled) continue;
            g_mons[i].x = screens[i].r.x;
            g_mons[i].y = screens[i].r.y;
        }
        monitorsApply(g_mons.size());
        showMonitor();
        g.dirty = true;
    }
}
void motion() {
    if (g_mapDrag.on) { mapDragMotion(); return; }
    if (g.selDrag) { dragSelect(); return; }
    if (g.drag.on) { dragMotion(); return; }
    const double lx = g.px - (kMargin + kSideW + kGap), ly = g.py - kMargin;
    if (g.dragHit >= 0) { setSliderFrom(g.dragHit, false); return; }
    int side = -1, hit = -1;
    const bool onClose = closeRect().contains(g.px, g.py);
    const bool onSearch = searchRect().contains(g.px, g.py), onSearchClear = g.searchOpen && searchClearRect().contains(g.px, g.py);
    if (g.px != g.searchPointerX || g.py != g.searchPointerY) {  // the mouse chooses only when it moves: the keys keep the choice while it rests (a redraw asks again)
        g.searchPointerX = g.px;
        g.searchPointerY = g.py;
        if (const int over = searchItemAt(lx, ly); over >= 0 && over != g.searchSel) { g.searchSel = over; g.dirty = true; }
    }
    const bool overResult = searchItemAt(lx, ly) >= 0;
    const auto sr = sideRects();
    for (size_t i = 0; i < sr.size(); i++)
        if (sr[i].contains(g.px, g.py)) side = static_cast<int>(i);
    if (side < 0 && g.animT >= 1 && !searchResultsShown())
        for (size_t i = 0; i < g.hits.size(); i++) {
            const Rect& r = g.hits[i].r;
            if (Rect{r.x, r.y - g.scroll, r.w, r.h}.contains(lx, ly) && ly >= 0 && ly < cardH() && (g.hits[i].row == -2 || ly >= pageTop(g.page))) hit = static_cast<int>(i);
        }
    if (!g_presetConfirm.empty()) {
        std::string over;
        if (hit >= 0 && g.hits[hit].kind == PresetCards) {
            const Row& pr = g.pages[g.page].rows[g.hits[hit].row];
            const int k = g.hits[hit].opt / 4;
            if (k >= 0 && k < static_cast<int>(pr.opts.size())) over = pr.opts[k].first;
        }
        if (over.empty() || g_presetConfirm.compare(2, std::string::npos, over) != 0) { g_presetConfirm.clear(); g.dirty = true; }
    }
    if (onSearch != g.hoverSearch || onSearchClear != g.hoverSearchClear || overResult != g.hoverResult) {
        g.hoverSearch = onSearch;
        g.hoverSearchClear = onSearchClear;
        g.hoverResult = overResult;
        g.dirty = true;
        updateCursor();
    }
    if (side != g.hoverSide || hit != g.hoverHit || onClose != g.hoverClose) {
        g.hoverClose = onClose;
        g.hoverSide = side;
        g.hoverHit = hit;
        g.dirty = true;
        updateCursor();
    }
}

// Closes Settings and runs `update.sh -y` in the user's terminal, so the output is there to read; the shell restart is offered at the end.
void runDotsUpdate() {
    const std::string term = g.val.count("terminal") && !g.val["terminal"].empty() ? g.val["terminal"] : "kitty";
    static const char* kScript =
        "root=\"$(dirname \"$(dirname \"$1\")\")\"; old=$(git -C \"$root\" rev-parse HEAD 2>/dev/null); bash \"$1\" -y; rc=$?; echo; new=$(git -C \"$root\" rev-parse HEAD 2>/dev/null); "
        "if [ $rc -ne 0 ]; then read -rp 'The update failed. Press Enter to close. ' _; "
        "elif [ \"$old\" = \"$new\" ]; then read -rp 'Nothing to update. Press Enter to close. ' _; "
        "else read -rp 'Restart the shell now? [y/N] ' a; case \"$a\" in y|Y) setsid -f bash \"$(dirname \"$1\")/restart.sh\" >/dev/null 2>&1 ;; esac; fi";
    spawn({term, "-e", "bash", "-c", kScript, "bash", scriptPath("update.sh")});
    g.running = false;
}

void runSystemUpdate() {
    const std::string term = g.val.count("terminal") && !g.val["terminal"].empty() ? g.val["terminal"] : "kitty";
    const bool aur = g.val["updatesAur"] == "1", flatpak = g.val["updatesFlatpak"] == "1";
    spawn({term, "-e", "bash", scriptPath("system-update.sh"), aur ? "1" : "0", flatpak ? "1" : "0"});
    g.running = false;
}

bool searchClick() {
    const Rect sr = searchRect();
    if (sr.contains(g.px, g.py)) {
        if (!g.searchOpen) { searchOpenNow(); return true; }
        if (searchClearRect().contains(g.px, g.py)) {
            if (g.searchQ.empty()) searchClose();
            else { g.searchQ.clear(); g.searchCaret = g.searchAnchor = 0; searchRun(); g.dirty = true; }
            return true;
        }
        textsel::Field fld(g.searchQ, g.searchCaret, g.searchAnchor);
        const int n = g.clicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), g.px, g.py);
        textsel::applyClick(fld, fieldIndexAt(g.searchQ, g.px - g.searchTextX), n, shiftHeld());
        g.caretReset = Clock::now();
        g.dirty = true;
        return true;
    }
    if (!g.searchOpen) return false;
    const double lx = g.px - (kMargin + kSideW + kGap), ly = g.py - kMargin;
    if (searchResultsShown() && lx >= 0 && ly >= kHeader) {
        if (const int i = searchItemAt(lx, ly); i >= 0) searchGo(i);
        return true;
    }
    searchClose();  // a click anywhere else folds it back (and does what it was aimed at)
    return false;
}

void click() {
    if (searchClick()) return;
    if (closeRect().contains(g.px, g.py)) { commitText(); g.running = false; return; }
    const auto sr = sideRects();
    for (size_t i = 0; i < sr.size(); i++)
        if (sr[i].contains(g.px, g.py)) { switchPage(i < g.pages.size() ? static_cast<int>(i) : profilePage()); return; }
    if (g.hoverHit < 0 || g.hoverHit >= static_cast<int>(g.hits.size())) { commitText(); if (!g_splitOpen.empty()) { g_splitOpen.clear(); g.dirty = true; } return; }
    const Hit h = g.hits[g.hoverHit];
    if (h.kind != Split && !g_splitOpen.empty()) { g_splitOpen.clear(); g.dirty = true; }
    if (h.kind == Chip) {
        commitText();
        if (h.opt != g.secSel[g.page] && g.animT >= 1) {
            g.secAnim.snap(chipIndex(g.page));
            g.secAnim.select(h.opt);
            g.secPrevScroll = g.scroll;
        }
        g.secSel[g.page] = h.opt;
        savePage();
        g.scroll = 0;
    g_scrollEl.over = 0;
        g.hoverHit = -1;
        g.dirty = true;
        motion();
        return;
    }
    if (h.kind == Accent) {
        commitText();
        scanAccents();
        if (h.opt == -1) sendSet("accent", "");
        else if (h.opt == 100) ipc("accentpick");
        else if (h.opt >= 0 && h.opt < static_cast<int>(g_accColors.size())) sendSet("accent", g_accColors[h.opt]);
        g.dirty = true;
        return;
    }
    if (h.kind == ThemeCards) {
        commitText();
        const Row& tr = g.pages[g.page].rows[h.row];
        if (h.opt >= 0 && h.opt < static_cast<int>(tr.opts.size())) {
            const std::string id = tr.opts[h.opt].first;
            if (id == "+") importTheme();
            else { sendSet("theme", id); loadThemeAccents(); }
        }
        g.dirty = true;
        return;
    }
    if (h.kind == PresetCards) {
        commitText();
        const Row& pr = g.pages[g.page].rows[h.row];
        const int k = h.opt / 4, sub = h.opt % 4;
        if (k >= 0 && k < static_cast<int>(pr.opts.size())) {
            const std::string name = pr.opts[k].first;
            if (sub == 0) {
                g_presetConfirm.clear();
                spawn({"python3", scriptPath("presets.py"), "--apply", name});
            } else {
                const std::string want = std::string(sub == 1 ? "o|" : "d|") + name;
                if (g_presetConfirm != want) g_presetConfirm = want;
                else {
                    g_presetConfirm.clear();
                    runCapture(sysCmd("presets.py", std::string(sub == 1 ? "--overwrite " : "--remove ") + shq(name)));
                    if (sub == 1 && k < static_cast<int>(g_presets.size())) {
                        if (auto it = g_presetThumbs.find(g_presets[k].wall); it != g_presetThumbs.end()) { if (it->second) cairo_surface_destroy(it->second); g_presetThumbs.erase(it); }
                    }
                    loadPresets();
                }
            }
        }
        g.dirty = true;
        return;
    }
    if (h.kind == WCards) {
        commitText();
        const Row& wr = g.pages[g.page].rows[h.row];
        if (h.opt >= 0 && h.opt < static_cast<int>(wr.opts.size())) {
            const std::string name = wr.opts[h.opt].first, reply = ipc(name + " toggle");
            if (!reply.empty()) g.val["w:" + name] = reply.find("shown") != std::string::npos ? "1" : "0";
        }
        g.dirty = true;
        return;
    }
    if (h.kind == Swatch) {
        commitText();
        const Row& sr = g.pages[g.page].rows[h.row];
        if (h.opt >= 0 && h.opt < static_cast<int>(sr.opts.size())) sendSet(sr.key, sr.opts[h.opt].first);
        g.dirty = true;
        return;
    }
    if (h.kind == CollageLayout) { commitText(); clickCollage(h.opt); g.dirty = true; return; }
    if (h.kind == Gallery) {
        commitText();
        if (h.opt >= 0 && h.opt < static_cast<int>(g_gal.size())) sendSet("avatarPicture", g_gal[h.opt]);
        g.dirty = true;
        return;
    }
    if (h.kind == Split) {
        commitText();
        const Row& sr = g.pages[g.page].rows[h.row];
        if (h.opt == -3) return;
        if (h.opt < 0) {
            if (g_splitOpen == sr.key) g_splitOpen.clear();
            else { g_splitOpen = sr.key; g_splitT = 0; g_splitStart = Clock::now(); g_splitScroll = 0; }
        } else { if (h.opt < static_cast<int>(sr.opts.size())) sendSet(sr.key, sr.opts[h.opt].first); g_splitOpen.clear(); }
        g.hoverHit = -1;
        g.dirty = true;
        return;
    }
    if (h.kind == DisplayMap) { commitText(); mapClick(h.opt); return; }
    if (h.kind == Widgets) {
        commitText();
        for (const EdChip& c : g.edGeom.chips)
            if (!c.gap && c.lane * 100 + c.idx == h.opt) {
                g.drag = EdDrag{g.px, g.py, true, false, c.id, c.lane, c.idx, c.lane, c.idx, c.r.w};
                break;
            }
        g.dirty = true;
        return;
    }
    if (h.row < 0) { if (h.opt == kAboutDotsOpt) runDotsUpdate(); else runSystemUpdate(); return; }
    const Row& r = g.pages[g.page].rows[h.row];
    if (h.kind != Text && g.focusRow >= 0) commitText();
    if (h.kind == Toggle) sendSet(r.key, valInt(r) ? "0" : "1");
    else if (h.kind == Choice || h.kind == Scheme || h.kind == IconStyle || h.kind == StyleCards) sendSet(r.key, r.opts[h.opt].first);
    else if (h.kind == Slider) { g.dragHit = g.hoverHit; setSliderFrom(g.dragHit, false); }
    else if (h.kind == Action) {
        if (r.key == "avatarReset") sendSet("avatarPicture", "");
        if (r.key == "fontWeightReset") { sendSet("fontWeightText", "450"); sendSet("fontWeightTitle", "550"); }
        else if (r.key == "langTemplate") openTranslation();
        else if (r.key == "collageReset") resetCollage();
        else if (r.key == "hyprReset" && g_armed != r.key) { g_armed = r.key; g.dirty = true; }
        else if (r.key == "hyprReset") {
            g_armed.clear();
            ipc("set borderColor 0");
            g.val["borderColor"] = "0";
            std::system(("python3 '" + scriptPath("hyprconf.py") + "' --restore >/dev/null 2>&1").c_str());
            g_hyprLoaded = false;
            loadHypr();
        }
        else if (r.key == "niriReset" && g_armed != r.key) { g_armed = r.key; g.dirty = true; }
        else if (r.key == "niriReset") {
            g_armed.clear();
            std::system((niriCmd("--restore") + " >/dev/null").c_str());
            g_niriLoaded = false;
            loadNiri();
        }
        else if (r.key == "layoutEditor" && g_armed != "layoutClear") { g_armed = "layoutClear"; g.dirty = true; }
        else if (r.key == "layoutEditor") { g_armed.clear(); clearLayout(); }
        else if (r.key == "oskToggle") ipc("osk toggle");
        else if (r.key == "updCheck") { ipc("updates check"); g.updChecking = true; g.updCheckStart = Clock::now(); }
    } else if (h.kind == Webapp) {
        const WebappItem* w = nullptr;
        for (const auto& it : g.webapps) if (it.id == r.key) w = &it;
        if (w && h.opt == 0) spawn({"bash", scriptPath("webapp.sh"), "open", w->id});
        else if (w) { const pid_t p = spawn({"bash", scriptPath("webapp.sh"), "remove", w->id}); if (p > 0) g.webPids.insert(p); }
    } else if (h.kind == Text) {
        const bool fresh = g.focusRow != h.row;
        if (fresh && g.focusRow >= 0) commitText();
        double tx = g.fieldTx;
        if (fresh) {
            g.focusRow = h.row;
            g.edit = valStr(r);
            g.caret = g.anchor = g.edit.size();
            tx = h.r.x + 14;
        }
        textsel::Field fld(g.edit, g.caret, g.anchor);
        const int n = g.clicks.press(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), g.px, g.py);
        textsel::applyClick(fld, fieldIndexAt(g.edit, g.px - (kMargin + kSideW + kGap) - tx), n, shiftHeld());
        g.dragInfo = textsel::beginDrag(fld, n);
        g.selDrag = true;
        g.caretReset = Clock::now();
    }
    g.dirty = true;
}

bool shiftHeld() { return g.xkbState && xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0; }

void dragSelect() {
    const double left = g.fieldR.x + 14, right = g.fieldR.x + g.fieldR.w - 16;
    const double lx = std::clamp(g.px - (kMargin + kSideW + kGap), left, right);
    textsel::Field f(g.edit, g.caret, g.anchor);
    textsel::drag(f, fieldIndexAt(g.edit, lx - g.fieldTx), g.dragInfo);
    g.caretReset = Clock::now();
    g.dirty = true;
}
void dragScrollTick() {
    const double lx = g.px - (kMargin + kSideW + kGap);
    const double left = g.fieldR.x + 14, right = g.fieldR.x + g.fieldR.w - 16;
    if (lx >= left && lx <= right) return;
    textsel::Field f(g.edit, g.caret, g.anchor);
    textsel::drag(f, lx < left ? textsel::prevChar(g.edit, g.caret) : textsel::nextChar(g.edit, g.caret), g.dragInfo);
    g.dirty = true;
}

bool editKey(std::string& text, size_t& caret, size_t& anchor, xkb_keysym_t sym, uint32_t key, bool ctrl) {
    textsel::Key k = textsel::Key::None;
    const xkb_keysym_t low = xkb_keysym_to_lower(sym);
    if (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left) k = textsel::Key::Left;
    else if (sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right) k = textsel::Key::Right;
    else if (sym == XKB_KEY_Home || sym == XKB_KEY_KP_Home) k = textsel::Key::Home;
    else if (sym == XKB_KEY_End || sym == XKB_KEY_KP_End) k = textsel::Key::End;
    else if (sym == XKB_KEY_BackSpace) k = textsel::Key::Backspace;
    else if (sym == XKB_KEY_Delete || sym == XKB_KEY_KP_Delete) k = textsel::Key::Delete;
    else if (ctrl && low == XKB_KEY_a) k = textsel::Key::SelectAll;
    else if (ctrl && low == XKB_KEY_c) k = textsel::Key::Copy;
    else if (ctrl && low == XKB_KEY_x) k = textsel::Key::Cut;
    else if (ctrl && low == XKB_KEY_v) k = textsel::Key::Paste;
    textsel::Field fld(text, caret, anchor);
    if (k != textsel::Key::None) {
        const textsel::Result r = textsel::handleKey(fld, k, ctrl, shiftHeld());
        if (r.wantCopy) clipboard::copy(r.copyText);
        if (r.wantPaste) {
            std::string clip;
            if (clipboard::paste(clip)) fld.insert(clip);
        }
        return r.repeat;
    }
    if (ctrl) return false;
    char buf[16];
    const int n = xkb_state_key_get_utf8(g.xkbState, key + 8, buf, sizeof buf);
    if (n > 0 && static_cast<unsigned char>(buf[0]) >= 32 && buf[0] != 127) {
        fld.insert(std::string(buf, static_cast<size_t>(n)));
        return true;
    }
    return false;
}

bool searchKey(xkb_keysym_t sym, uint32_t key, bool ctrl) {
    g.caretReset = Clock::now();
    g.dirty = true;
    const auto step = [](int d) {
        if (g_searchHits.empty()) return;
        g.searchSel = std::clamp(g.searchSel + d, 0, static_cast<int>(g_searchHits.size()) - 1);
        searchKeepVisible();
    };
    if (sym == XKB_KEY_Escape) { searchClose(); return false; }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) { searchGo(g.searchSel); return false; }
    if (sym == XKB_KEY_Down || sym == XKB_KEY_KP_Down) { step(1); return true; }
    if (sym == XKB_KEY_Up || sym == XKB_KEY_KP_Up) { step(-1); return true; }
    if (sym == XKB_KEY_Page_Down) { step(6); return true; }
    if (sym == XKB_KEY_Page_Up) { step(-6); return true; }
    const std::string before = g.searchQ;
    const bool repeat = editKey(g.searchQ, g.searchCaret, g.searchAnchor, sym, key, ctrl);
    if (g.searchQ != before) {
        searchRun();
        g.searchSel = 0;
        g.searchScroll = 0;
    }
    return repeat;
}

bool handleKey(uint32_t key) {
    if (!g.xkbState) return false;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(g.xkbState, key + 8);
    const bool ctrl = xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (ctrl && (sym == XKB_KEY_w || sym == XKB_KEY_q)) { g.running = false; return false; }
    if (g.searchOpen) return searchKey(sym, key, ctrl);
    if (g.focusRow < 0) {  // typing, with nothing in focus, starts a search
        const xkb_keysym_t lowSym = xkb_keysym_to_lower(sym);
        const bool alt = xkb_state_mod_name_is_active(g.xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
        if (ctrl && lowSym == XKB_KEY_f) { searchOpenNow(); return false; }
        if (ctrl || alt) return false;
        if (sym == XKB_KEY_slash) { searchOpenNow(); return false; }
        char first[16];
        const int fn = xkb_state_key_get_utf8(g.xkbState, key + 8, first, sizeof first);
        if (fn > 0 && static_cast<unsigned char>(first[0]) > 32 && first[0] != 127) {
            searchOpenNow();
            textsel::Field fld(g.searchQ, g.searchCaret, g.searchAnchor);
            fld.insert(std::string(first, static_cast<size_t>(fn)));
            searchRun();
            g.searchSel = 0;
            g.searchScroll = 0;
        }
        return false;
    }
    g.caretReset = Clock::now();
    g.dirty = true;
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) { commitText(); return false; }
    if (sym == XKB_KEY_Escape) { g.focusRow = -1; return false; }
    return editKey(g.edit, g.caret, g.anchor, sym, key, ctrl);
}

void onWmPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWmListener = {onWmPing};

void onXdgConfigure(void*, xdg_surface* s, uint32_t serial) {
    xdg_surface_ack_configure(s, serial);
    g.configured = true;
    g.dirty = true;
}
const xdg_surface_listener kXdgSurfaceListener = {onXdgConfigure};
void onTopConfigure(void*, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    if (w > 0 && h > 0) { g.width = std::max(720, w); g.height = std::max(480, h); }
    g.dirty = true;
}
void onTopClose(void*, xdg_toplevel*) { g.running = false; }
const xdg_toplevel_listener kToplevelListener = {onTopConfigure, onTopClose, nullptr, nullptr};

void onPEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
    g.enterSerial = serial;
    g.cursorShape = -1;
    g.inside = true;
    g.px = wl_fixed_to_double(x);
    g.py = wl_fixed_to_double(y);
    motion();
    updateCursor();
}
void onPLeave(void*, wl_pointer*, uint32_t, wl_surface*) {
    g.inside = false;
    if (g.dragHit < 0) { g.hoverSide = g.hoverHit = -1; g.dirty = true; }
}
void onPMotion(void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    g.px = wl_fixed_to_double(x);
    g.py = wl_fixed_to_double(y);
    motion();
}
void onPButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    if (button == 0x111) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED && !g.drag.on) hideWidget();
        return;
    }
    if (button != 0x110) return;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED && g_mapDrag.on) { mapDragFinish(); return; }
    if (state != WL_POINTER_BUTTON_STATE_PRESSED && g.selDrag) { g.selDrag = false; g.dirty = true; return; }
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) click();
    else if (g.drag.on) finishDrag();
    else if (g.dragHit >= 0) {
        setSliderFrom(g.dragHit, true);
        g.dragHit = -1;
        g.dirty = true;
        motion();
    }
}
void onPAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || g.animT < 1) return;
    // Scroll by the amount the compositor sent, like the Tools pages do: a wheel notch is ~15, so about 45 px,
    // and a touchpad sends many small values, so it follows the fingers (and the touchpad scroll factor).
    const double d = wl_fixed_to_double(value) * 3;
    if (!g_splitOpen.empty() && g_splitRect.contains(g.px - (kMargin + kSideW + kGap), g.py - kMargin)) {
        g_splitScroll += d;
        g.dirty = true;
        return;
    }
    if (searchResultsShown()) {
        g_searchEl.pos = g.searchScroll;
        g_searchEl.max = searchMaxScroll();
        g_searchEl.by(d);
        g.searchScroll = g_searchEl.pos;
        motion();
        g.dirty = true;
        return;
    }
    const Layout L = layoutPage(g.page);
    g_scrollEl.pos = g.scroll;
    g_scrollEl.max = maxScroll(L);
    g_scrollEl.by(d);
    g.scroll = g_scrollEl.pos;
    motion();
    g.dirty = true;
}
void onPFrame(void*, wl_pointer*) {}
void onPAxisSource(void*, wl_pointer*, uint32_t) {}
void onPAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void onPAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
const wl_pointer_listener kPointerListener = {onPEnter, onPLeave, onPMotion, onPButton, onPAxis, onPFrame, onPAxisSource, onPAxisStop, onPAxisDiscrete};

void onKKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (!g.xkbCtx) g.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_keymap* km = xkb_keymap_new_from_string(g.xkbCtx, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
            if (km) {
                if (g.xkbState) xkb_state_unref(g.xkbState);
                if (g.xkbMap) xkb_keymap_unref(g.xkbMap);
                g.xkbMap = km;
                g.xkbState = xkb_state_new(km);
            }
        }
    }
    close(fd);
}
void onKEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void onKLeave(void*, wl_keyboard*, uint32_t, wl_surface*) { g.repeatKey = 0; }
void onKKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (key == g.repeatKey) g.repeatKey = 0;
        return;
    }
    if (handleKey(key) && g.kbRate > 0) {
        g.repeatKey = key;
        g.repeatAt = Clock::now() + std::chrono::milliseconds(g.kbDelay);
    } else g.repeatKey = 0;
}
void onKMods(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
    if (g.xkbState) xkb_state_update_mask(g.xkbState, dep, lat, lock, 0, 0, group);
}
void onKRepeat(void*, wl_keyboard*, int32_t rate, int32_t delay) { g.kbRate = rate; g.kbDelay = delay; }
const wl_keyboard_listener kKeyboardListener = {onKKeymap, onKEnter, onKLeave, onKKey, onKMods, onKRepeat};

void onSeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) {
        g.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g.pointer, &kPointerListener, nullptr);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) {
        g.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g.keyboard, &kKeyboardListener, nullptr);
    }
}
void onSeatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {onSeatCaps, onSeatName};

void onGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
    if (!std::strcmp(iface, wl_compositor_interface.name)) g.compositor = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
    else if (!std::strcmp(iface, wl_shm_interface.name)) g.shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
    else if (!std::strcmp(iface, wl_seat_interface.name)) {
        g.seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(g.seat, &kSeatListener, nullptr);
    } else if (!std::strcmp(iface, xdg_wm_base_interface.name)) {
        g.wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(g.wm, &kWmListener, nullptr);
    } else if (!std::strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        g.cursorMgr = static_cast<wp_cursor_shape_manager_v1*>(wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, 1));
}
void onGlobalRemove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistryListener = {onGlobal, onGlobalRemove};

void savePage() {  // the page, then the chip each page was on
    if (FILE* f = std::fopen(pageFile().c_str(), "w")) {
        std::fprintf(f, "%d\n", g.page);
        for (int c : g.secSel) std::fprintf(f, "%d ", c);
        std::fprintf(f, "\n");
        std::fclose(f);
    }
}
}

int main() {
    {
        const int lock = open((runtimeDir() + "/end4pC-settings.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) != 0) return 0;
    }
    std::vector<int> savedChips;
    if (FILE* f = std::fopen(pageFile().c_str(), "r")) {
        int p = 0;
        if (std::fscanf(f, "%d", &p) == 1 && p >= 0 && p < static_cast<int>(g.pages.size())) g.page = g.prevPage = p;
        for (int c = 0; std::fscanf(f, "%d", &c) == 1;) savedChips.push_back(std::max(0, c));
        std::fclose(f);
    }
    matsym::assetsDir() = assetsDir();
    applyShellFont(fontFamilyFromConfig(configDir() + "/config.json"), fontWeightFromConfig(configDir() + "/config.json", "fontWeightText", 450), fontWeightFromConfig(configDir() + "/config.json", "fontWeightTitle", 550));
    g.display = wl_display_connect(nullptr);
    if (!g.display) { std::fprintf(stderr, "end4pC-settings: no Wayland display\n"); return 1; }
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(g.display);
    wl_display_roundtrip(g.display);
    if (!g.compositor || !g.shm || !g.wm) { std::fprintf(stderr, "end4pC-settings: missing Wayland globals\n"); return 1; }

    cairo_surface_t* dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* dcr = mkcr(dummy);
    g_measure = pango_cairo_create_layout(dcr);

    loadColors();
    loadConfig();
    loadProfile();
    refreshUptime();
    g.secSel.assign(g.pages.size(), 0);
    for (size_t i = 0; i < savedChips.size() && i < g.secSel.size(); i++) g.secSel[i] = savedChips[i];  // (chipIndex keeps it inside the page's chips)
    loadUpdates();
    loadPeripherals();
    loadWebapps();
    loadScreens();
    preloadHypr();
    if (g.page < static_cast<int>(g.pages.size()) && g.pages[g.page].name == "Hyprland") loadHypr();
    if (g.page < static_cast<int>(g.pages.size()) && g.pages[g.page].name == "Niri") loadNiri();
    if (g.page < static_cast<int>(g.pages.size()) && (g.pages[g.page].name == "System" || g.pages[g.page].name == "General")) loadSystem();
    if (g.page < static_cast<int>(g.pages.size()) && g.pages[g.page].name == "Appearance") { loadThemes(); loadPresets(); }

    g.surface = wl_compositor_create_surface(g.compositor);
    g.xsurface = xdg_wm_base_get_xdg_surface(g.wm, g.surface);
    xdg_surface_add_listener(g.xsurface, &kXdgSurfaceListener, nullptr);
    g.toplevel = xdg_surface_get_toplevel(g.xsurface);
    xdg_toplevel_add_listener(g.toplevel, &kToplevelListener, nullptr);
    xdg_toplevel_set_title(g.toplevel, "Settings");
    xdg_toplevel_set_app_id(g.toplevel, "end4pC-settings");
    xdg_toplevel_set_min_size(g.toplevel, 720, 480);
    wl_surface_commit(g.surface);

    while (g.running) {
        while (wl_display_prepare_read(g.display) != 0) wl_display_dispatch_pending(g.display);
        wl_display_flush(g.display);

        const auto before = Clock::now();
        int timeout = 1000;
        if (g.animT < 1 || g.dragHit >= 0 || g_bubbleMoving || g_springsMoving || searchFlashing() || g.secAnim.moving() || g.sideAnim.moving() || g.pillA != g.pillTarget || g.selDrag || g_scrollEl.over != 0 || g_searchEl.over != 0 || (!g_splitOpen.empty() && g_splitT < 1)) timeout = 16;
        else if (g.focusRow >= 0 || g.searchOpen || g.createPid > 0 || g.updChecking || collagePickBusy()) timeout = 200;
        if (g.repeatKey) timeout = std::min<long>(timeout, std::max<long>(1, std::chrono::duration_cast<std::chrono::milliseconds>(g.repeatAt - before).count()));

        pollfd pfd = {wl_display_get_fd(g.display), POLLIN, 0};
        if (poll(&pfd, 1, timeout) < 0 && errno != EINTR) { wl_display_cancel_read(g.display); break; }
        if (pfd.revents & POLLIN) wl_display_read_events(g.display);
        else wl_display_cancel_read(g.display);
        wl_display_dispatch_pending(g.display);
        if (pfd.revents & (POLLERR | POLLHUP)) break;

        const auto now = Clock::now();
        if (g.configured && g.animT >= 1 && !g_sysStarted) sysPreload();
        if (g_bubbleMoving) g.dirty = true;
        if (g.selDrag && std::chrono::duration<double>(now - g.dragTick).count() >= 0.04) {
            g.dragTick = now;
            dragScrollTick();
        }
        if (g.pillA != g.pillTarget) {
            static Clock::time_point lastPill = before;
            const double step = std::min(0.1, std::chrono::duration<double>(before - lastPill).count()) / 0.2;
            lastPill = before;
            g.pillA = g.pillA < g.pillTarget ? std::min(g.pillTarget, g.pillA + std::max(step, 0.016 / 0.2)) : std::max(g.pillTarget, g.pillA - std::max(step, 0.016 / 0.2));
            g.dirty = true;
        }
        if (g.secAnim.moving() || g.sideAnim.moving()) {
            g.secAnim.stepNow(before);
            g.sideAnim.stepNow(before);
            g.dirty = true;
        }
        if (g_springsMoving) {
            static Clock::time_point lastSpring = before;
            stepSprings(std::chrono::duration<double>(before - lastSpring).count());
            lastSpring = before;
            g.dirty = true;
        }
        if (!g_splitOpen.empty() && g_splitT < 1) {
            g_splitT = std::min(1.0, std::chrono::duration<double>(now - g_splitStart).count() / 0.18);
            g.dirty = true;
        }
        if (g.animT < 1) {
            g.animT = std::min(1.0, std::chrono::duration<double>(now - g.animStart).count() / 0.26);
            g.dirty = true;
            if (g.animT >= 1) motion();
        }
        if (g.repeatKey && now >= g.repeatAt) {
            if (!handleKey(g.repeatKey)) g.repeatKey = 0;
            g.repeatAt = now + std::chrono::milliseconds(std::max(1, 1000 / std::max(1, g.kbRate)));
        }
        if (g.focusRow >= 0 || g.searchOpen || g_galPending) { g.dirty = true; g_galPending = false; }
        {
            static bool wasFlashing = false;
            const bool flashing = searchFlashing();
            if (flashing || wasFlashing) g.dirty = true;  // (once more when it ends, so the last frame is clean)
            wasFlashing = flashing;
        }
        if (mtimeOf(colorsPath()) != g.colorsStamp) { loadColors(); g.dirty = true; }
        if (g.pages[g.page].name == "Appearance") {
            if (mtimeOf(themesDir()) != g_themesStamp || g.val["colorMode"] != g_themesMode) loadThemes();
            else if (g.val["theme"] != g_accFor) loadThemeAccents();
        }
        if (g.focusRow < 0 && g.dragHit < 0 && !g.drag.on && mtimeOf(configPath()) != g.configStamp) { loadConfig(); g.dirty = true; }
        if (loadWidgetStates()) g.dirty = true;
        if (collagePickPoll()) g.dirty = true;
        if (refreshUptime()) g.dirty = true;
        if (g.pages[g.page].about && g_sys.loaded && now - g_sys.stamp > std::chrono::seconds(3)) { refreshSysLive(); g.dirty = true; }
        if (mtimeOf(updatesPath()) != g.updStamp) { loadUpdates(); g.dirty = true; }
        if (g.pages[g.page].name == "Niri" && niriPoll()) g.dirty = true;
        {
            std::string accent = g.val.count("accent") ? g.val["accent"] : "";
            ensureSchemes(g.val.count("colorMode") ? g.val["colorMode"] : "dark", accent);
            const time_t m = mtimeOf(schemesFile());
            if (m != g_schemesStamp) { g_schemesStamp = m; loadSchemes(); g.dirty = true; }
        }
        { static time_t ps = -1; const time_t m = mtimeOf(cachePath() + "/peripherals.txt"); if (m != ps) { ps = m; loadPeripherals(); g.dirty = true; } }
        if (g.updChecking && now - g.updCheckStart > std::chrono::seconds(120)) { g.updChecking = false; g.dirty = true; }
        for (int st; !g.webPids.empty();) {
            const pid_t p = waitpid(-1, &st, WNOHANG);
            if (p <= 0) break;
            if (g.webPids.erase(p)) {
                if (p == g.createPid) g.createPid = 0;
                loadWebapps();
                g.scroll = std::clamp(g.scroll, 0.0, maxScroll(layoutPage(g.page)));
            }
        }
        if (g_scrollEl.step() | g_searchEl.step()) g.dirty = true;
        if (g.dirty) render();
    }

    if (g.focusRow >= 0) commitText();
    wl_display_disconnect(g.display);
    return 0;
}
