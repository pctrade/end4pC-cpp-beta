// The bar's layout engine, and the small readers of the system: volume, network, the Hyprland socket, the music player.
// Part of main.cpp: included there, in order, inside its anonymous namespace (what comes first is used below).

std::vector<int> quickShown(const App& app) {
    std::vector<int> v;
    const std::string& list = app.cfg.quickActions;
    for (int i = 0; i < kQaCount; i++) {
        const std::string name = kQuickNames[i];
        size_t at = 0;
        while ((at = list.find(name, at)) != std::string::npos) {
            const bool startOk = at == 0 || list[at - 1] == ',', endOk = at + name.size() == list.size() || list[at + name.size()] == ',';
            if (startOk && endOk) { v.push_back(i); break; }
            at += name.size();
        }
    }
    return v;
}
bool quickIsShown(const App& app, int id) {
    for (int i : quickShown(app)) if (i == id) return true;
    return false;
}
const char* quickIcon(const App& app, int id, bool& live, bool& alert) {
    live = alert = false;
    switch (id) {
    case kQaPicker: return "colorize";
    case kQaRecord: live = app.recording; return app.recording ? "stop_circle" : "screen_record";
    case kQaScreenshot: return "screenshot_region";
    case kQaWallpaper: return "wallpaper";
    case kQaMic: alert = app.micMuted; return app.micMuted ? "mic_off" : "mic";
    case kQaDark: return app.cfg.colorMode == "dark" ? "light_mode" : "dark_mode";
    case kQaPerf: return app.perfProfile == "power-saver" ? "leaf" : app.perfProfile == "performance" ? "bolt" : "battery-half";
    case kQaOsk: return "keyboard";  // (the keyboard itself shows it is open: no lit state, that pink is the recording)
    default: return "wallpaper";
    }
}
constexpr double kQuickGrowX = 28;
double quickItemW(const App& app, int id) {
    const bool grows = id >= 0 && id < 8 && !sideBar(app) && widgetStyle(app, "quickactions") == "m3";
    return quickBase(app) + (grows ? app.quickGrow[id] * kQuickGrowX : 0) + softExtra(app, "quickactions") + (!sideBar(app) && widgetStyle(app, "quickactions") == "split" ? kQuickSplitExtra : 0);
}
double quickItemX(const App& app, size_t i) {
    const std::vector<int> shown = quickShown(app);
    const double gap = quickPitch(app) - quickBase(app);
    double x = 0;
    for (size_t j = 0; j < i && j < shown.size(); j++) x += quickItemW(app, shown[j]) + gap;
    return x;
}
double quickW(const App& app) {
    const std::vector<int> shown = quickShown(app);
    return shown.empty() ? 0 : quickItemX(app, shown.size() - 1) + quickItemW(app, shown.back()) + 2 * quickPadX(app);
}

constexpr double kEdgePad = 12;
constexpr double kModuleGap = 14;
constexpr double kEdgeGap = 8;
constexpr double kTightGap = 4;

constexpr double kTrayItem = 26;
constexpr double kBatW = 48;
constexpr double kBatRingW = 26, kBatBarW = 64, kBatTextW = 40;
constexpr double kBatPad = 6, kBatIcon = 20, kBatGap = 4, kBatInkPad = 7;
double batteryTextW(int pct) {
    static std::map<int, double> cache;
    const auto hit = cache.find(pct);
    if (hit != cache.end()) return hit->second;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, (std::to_string(pct) + "%").c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    pango_font_description_free(f);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return cache[pct] = w;
}
const App::Periph* lowPeripheral(const App& app);
double dtpWidth(const App& app);  // (14s_dock_panel.inc)
Hit dtpHit(const App& app, const Bar& bar, double x);
double iconInkInset(const std::string& name, int size, bool right);
std::string batteryIconName(const App& app) {
    const App::Periph* dev = app.batPresent ? nullptr : lowPeripheral(app);
    if (dev) return dev->kind == "keyboard" ? "keyboard" : dev->kind == "headset" || dev->kind == "headphones" ? "headphones-round" : dev->kind == "mouse" ? "mouse" : "battery-low";
    const int pct = app.batPct;
    return app.batCharging ? "battery-charge" : pct >= 70 ? "battery-full" : pct >= 35 ? "battery-half" : "battery-low";
}
double batteryW(const App& app) {
    const App::Periph* dev = app.batPresent ? nullptr : lowPeripheral(app);
    const int level = std::clamp(dev ? dev->pct : app.batPct, 0, 100);
    const std::string s = widgetStyle(app, "battery");
    if (s == "ring") return kBatRingW;
    if (s == "bar") return kBatBarW;
    if (s == "m3") return kBatW;
    if (s == "text") return batteryTextW(level) + 2 * kBatPad;
    const std::string icon = batteryIconName(app);
    const double inkW = kBatIcon - iconInkInset(icon, static_cast<int>(kBatIcon), false) - iconInkInset(icon, static_cast<int>(kBatIcon), true);
    return app.cfg.batteryPercent ? kBatInkPad + inkW + kBatGap + batteryTextW(level) + kBatInkPad : 2 * kBatInkPad + inkW;
}
std::string paintStyle(const App& app, const char* mod) {
    const std::string s = widgetStyle(app, mod);
    return s == "m3" && sideBar(app) && !sideStyleAllowed(mod, s) ? "plain" : s;
}
constexpr double kBarAvatar = 28;
constexpr double kM3AvatarHover = 1.1;

constexpr int kVizBars = 20;
constexpr double kVizDot = 3, kVizGap = 3;
struct ResItem { int kind; const char* label; const char* icon; double pct; int warn; };
std::vector<ResItem> resItems(const App& app) {
    std::vector<ResItem> v;
    if (app.cfg.resCpu) v.push_back({1, "CPU", "cpu", app.res.cpu, app.cfg.resWarnCpu});
    if (app.cfg.resRam) v.push_back({0, "RAM", "server-square", app.res.ram, app.cfg.resWarnMem});
    if (app.cfg.resTemp) v.push_back({2, "TMP", "bonfire", app.res.temp, 100});
    if (app.cfg.resDisk) v.push_back({3, "DSK", "database", app.res.disk, 100});
    if (app.cfg.resSwap) v.push_back({4, "SWP", "transfer-horizontal", app.res.swap, app.cfg.resWarnSwap});
    return v;
}
bool resourcesInLayout(const App& app) {
    for (const auto* l : {&app.cfg.layoutLeft, &app.cfg.layoutCenter, &app.cfg.layoutRight, &app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight})
        if (std::find(l->begin(), l->end(), "resources") != l->end()) return true;
    return false;
}

bool inBarLayout(const App& app, const char* m) {
    const bool side = sideBar(app);
    for (const auto* l : {side ? &app.cfg.sideLeft : &app.cfg.layoutLeft, side ? &app.cfg.sideCenter : &app.cfg.layoutCenter, side ? &app.cfg.sideRight : &app.cfg.layoutRight})
        if (std::find(l->begin(), l->end(), m) != l->end()) return true;
    return false;
}
bool isDivider(const std::string& m) { return bardivider::isDivider(m); }
bool repeatable(const std::string& m) { return isDivider(m) || m == "visualizer"; }

bardivider::Options dividerOptions(const Config& cfg, const std::string& entry) {
    return bardivider::parse(entry).value_or(bardivider::Options{
        bardivider::parseStyle(cfg.dividerStyle).value_or(bardivider::Style::Line), cfg.dividerSpacing});
}

int dividerGap(const Config& cfg, const std::string& a, const std::string& b, int fallback) {
    // Two legacy dividers shared one gap. Explicit margins belong to each divider.
    if (a == "divider" && b == "divider") return cfg.dividerSpacing;
    const bool first = isDivider(a), second = isDivider(b);
    if (!first && !second) return fallback;
    return (first ? dividerOptions(cfg, a).margin : 0) + (second ? dividerOptions(cfg, b).margin : 0);
}
std::vector<int> aiShown(const App& app) {
    std::vector<int> v;
    if (app.cfg.aiClaude && app.barAi[0].seen) v.push_back(0);
    if (app.cfg.aiCodex && app.barAi[1].seen) v.push_back(1);
    return v;
}

bool networkInLayout(const App& app) {
    for (const auto* l : {&app.cfg.layoutLeft, &app.cfg.layoutCenter, &app.cfg.layoutRight, &app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight})
        if (std::find(l->begin(), l->end(), "network") != l->end()) return true;
    return false;
}
bool netIconShown(const App& app) { return app.cfg.networkAlways || app.netKind == "none"; }
bool netSpeedShown(const App& app) { return app.cfg.networkSpeed != 0; }
bool netVisible(const App& app) { return netIconShown(app) || netSpeedShown(app); }
double netIconPart(const App& app, bool side) {
    if (side) return netIconShown(app) ? 40 : 0;
    if (widgetStyle(app, "network") == "label") return kNetLabelW;
    return netIconShown(app) ? kNetW : 0;
}
double netFramePad(const App& app, bool side) {
    if (side || !netSpeedShown(app)) return 0;
    const std::string s = widgetStyle(app, "network");
    return s == "pill" || s == "outline" || s == "badge" || s == "tonal" || s == "square" || m3ButtonLook(s) ? 8 : 0;
}
double netExtent(const App& app, bool side) {
    return netIconPart(app, side) + (netSpeedShown(app) ? (side ? kNetSpeedH : kNetSpeedW) : 0) + netFramePad(app, side) + (netSpeedShown(app) ? 0 : softExtra(app, "network"));
}

bool trayPinned(const App& app, const TrayIcon& t) {
    const bool exception = std::find(app.cfg.trayExceptions.begin(), app.cfg.trayExceptions.end(), t.id) != app.cfg.trayExceptions.end();
    return app.cfg.trayPinDefault ? !exception : exception;
}
void traySplit(const App& app, std::vector<int>& pinned, std::vector<int>& others) {
    for (size_t i = 0; i < app.tray.size(); i++) (trayPinned(app, app.tray[i]) ? pinned : others).push_back(static_cast<int>(i));
}
bool trayForward(const App& app, bool side) {
    const auto has = [](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), "systray") != v.end(); };
    return side ? !has(app.cfg.sideRight) : has(app.cfg.layoutLeft);
}
void drawTrayGlyph(cairo_t* cr, const App& app, cairo_surface_t* surf, double x, double y, double alpha) {
    if (!surf) return;
    if (app.cfg.trayTint) {
        const Color c = app.color("on_surface");
        cairo_set_source_surface(cr, surf, x, y);
        cairo_paint_with_alpha(cr, alpha);
        cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha * 0.35);
        cairo_mask_surface(cr, surf, x, y);
    } else {
        cairo_set_source_surface(cr, surf, x, y);
        cairo_paint_with_alpha(cr, alpha);
    }
}

const App::Periph* lowPeripheral(const App& app);
// The size of the circles, pills and bars of the resources: on a group cell they are smaller, so they leave the same air around them above, below
// and at the sides.
constexpr double kResCellBox = 20;
double resBox(const App& app) { return hasGroupCell(app.cfg, "resources") ? kResCellBox : kQuickBox; }
double resOneW(const App& app) { const std::string s = widgetStyle(app, "resources"); return s == "text" ? 30 : resBox(app); }  // (circles and pills are as big as the quick action buttons)
double resValW(const App& app) { return widgetStyle(app, "resources") == "bars" ? 0 : app.cfg.resValue ? 18 : 0; }
double launcherM3W(const App& app);
double updatesBarW(const App& app);
double btBarW(const App& app);
bool updatesShown(const App& app) { return app.cfg.updatesEnable && (app.cfg.updatesMinShow <= 0 || app.updatesTotal >= app.cfg.updatesMinShow); }
double launcherWidth(const App& app) { return !sideBar(app) && widgetStyle(app, "launcher") == "m3" ? launcherM3W(app) : roundBtnWide(app, "launcher"); }
constexpr double kVizM3Pad = 5;
constexpr double kVizCellPad = 8;  // (air between the bars of a visualizer and the sides of its group cell)
constexpr double kCellTextInset = 6, kCellTitleInset = 4;  // (the active window title already has 6 px of its own)  // (air between the text of AI usage / resources and the sides of their cell)

constexpr double kIslandItem = kBarAvatar;
constexpr double kIslandPadIn = (kBarHeight - kIslandItem) / 2.0 - kIslandPad;
constexpr double kIslandGap = 50;
constexpr int kIslandViz = 7;
constexpr double kIslandVizW = kIslandViz * (kVizDot + kVizGap) - kVizGap;
constexpr double kIslandEndInset = 6;
bool islandActive(const App& app) {
    return app.cfg.dynamicIsland && (app.cfg.barStyle == "island" || app.cfg.barStyle == "split") && !sideBar(app);
}
bool islandShowsMusic(const App& app) { return !app.musicStatus.empty() && app.cfg.islandView == "media"; }
bool g_icRecShown = false;
double g_islandRecT = 0;
std::chrono::steady_clock::time_point g_recStartAt{};
bool islandShowsRec(const App& app) { return app.recording && g_icRecShown; }
std::string recElapsed() {
    const long s = static_cast<long>(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_recStartAt).count());
    char b[24];
    if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    else std::snprintf(b, sizeof b, "%02ld:%02ld", s / 60, s % 60);
    return b;
}
double islandSmooth(double a, double b, double x) { x = std::clamp((x - a) / (b - a), 0.0, 1.0); return x * x * (3 - 2 * x); }
void islandSpring(double& x, double& v, double target, double dt) {
    if (!g_animEnabled) { x = target; v = 0; return; }
    const double speed = g_animSpeed / 100.0, w = 21 * speed, zeta = g_animStyle == "boing" ? 0.62 : 1.0;
    for (int i = 0; i < 4; i++) {
        const double h = dt / 4;
        v += (w * w * (target - x) - 2 * zeta * w * v) * h;
        x += v * h;
    }
    if (std::fabs(target - x) < 0.0008 && std::fabs(v) < 0.01) { x = target; v = 0; }
}
double islandEase(double t) { return 1 - std::pow(1 - t, 3); }
constexpr double kIslandRecDot = 12, kIslandRecTextW = 30, kIslandRecTimeW = 60;
double islandW(const App& app) {
    const double idle = kIslandItem + kIslandGap + app.islandTimeW + kIslandEndInset, music = kIslandItem + kIslandGap + kIslandVizW + kIslandEndInset;
    const double rec = kIslandItem + kIslandRecTextW + kIslandGap + kIslandRecTimeW + kIslandEndInset;
    const double m = std::clamp(app.islandMusicT, -0.2, 1.2), r = std::clamp(g_islandRecT, -0.2, 1.2);
    const double base = idle + (music - idle) * m;
    return 2 * kIslandPadIn + base + (rec - base) * r;
}
std::vector<std::string> centerModules(const App& app) {
    if (islandActive(app)) return {"island"};
    return app.cfg.layoutCenter;
}
double moduleWidth(const App& app, const std::string& m) {
    if (m == "island") return islandActive(app) ? islandW(app) : 0;
    if (m == "workspaces") return wsTotalW(app);
    if (m == "media") return musicW(app);
    if (m == "quickactions") return quickW(app);
    if (m == "network") return netVisible(app) ? netExtent(app, false) : 0;
    if (m == "volume") return volumeW(app);
    if (m == "bluetooth") return btBarW(app);
    if (m == "drives") return drivesVisible(app) ? kQuickBox + softExtra(app, "drives") : 0;
    if (m == "privacy") return privacyBarW(app);
    if (m == "docktopanel") return dtpWidth(app);
    if (m == "clock") return app.clockW;
    if (m == "notifications") return notifW(app);
    if (m == "launcher") return launcherWidth(app);
    if (m == "settings" || m == "power" || m == "tools") return roundBtnWide(app, m.c_str());
    if (m == "avatar") return kBarAvatar;
    if (m == "systray") {
        std::vector<int> pin, oth;
        traySplit(app, pin, oth);
        return pin.size() * kTrayItem + (oth.empty() ? 0 : kQuickBox + oth.size() * kTrayItem * (1 - std::pow(1 - app.trayT, 3)));
    }
    if (m == "resources") {
        const size_t n = resItems(app).size();
        const double one = resOneW(app), val = resValW(app);
        return n ? n * (one + val) + (n - 1) * 6.0 : 0;
    }
    if (m == "weather") return app.barWeather.ok ? app.weatherBarW : 0;
    if (m == "aiusage") return app.aiBarW;
    if (m == "updates") return updatesShown(app) ? updatesBarW(app) : 0;
    if (m == "activewindow") return awinBarW(app);
    if (m == "keyboard") return app.kbd.list.empty() ? 0 : kbdBarW(app);
    if (isDivider(m)) return bardivider::extent(dividerOptions(app.cfg, m).style);
    if (m == "visualizer") return kVizBars * (kVizDot + kVizGap) + (!sideBar(app) && widgetStyle(app, "visualizer") == "m3" ? 2 * kVizM3Pad : 0);
    if (m == "battery") return app.batPresent || lowPeripheral(app) ? batteryW(app) : 0;
    return 0;
}

// Air between a widget's content and the sides of its cell, for the widgets whose bare look draws to the edge of its box. The slot of the
// widget is moved in by it, and the cell reaches it on both sides, so painting and hit tests keep using the content only.
double cellInsetR(const App& app, const std::string& m) {
    if (!hasGroupCell(app.cfg, m)) return 0;
    if (m == "activewindow") return kCellTitleInset;
    if (m == "media") return musicProgress(app) ? kCellTextInset : 0;  // (the cover look has its own pad: musicPad)
    return m == "visualizer" ? kVizCellPad : m == "aiusage" || m == "resources" || m == "weather" || m == "updates" || m == "network" ? kCellTextInset : 0;
}
// (the icon of the network widget already has air on its left)
double cellInsetL(const App& app, const std::string& m) { return m == "network" ? 0 : cellInsetR(app, m); }


// The gap between two widgets keeps the visible space between their shapes: a cell reaches kGroupPad beyond the widget on each side.
double moduleGap(const App& app, const std::string& a, const std::string& b) {
    const double base = dividerGap(app.cfg, a, b, app.cfg.barSpacing);
    if (!groupHasCells(app.cfg)) return base;
    const bool ca = hasGroupCell(app.cfg, a), cb = hasGroupCell(app.cfg, b);
    const double pads = (ca ? kGroupPad : 0) + (cb ? kGroupPad : 0);
    return (ca && cb ? app.cfg.groupSpacing : base) + pads;
}

// From the side of an island, notch or split body to the first widget: with cells it is the gap above and below a cell plus the pad of the
// cell, so the cell is as far from the side of the body as from its top and bottom.
double bodyPad(const App& app) {
    return groupHasCells(app.cfg) ? (kBarHeight - (kQuickBox + 2 * kGroupPad)) / 2.0 + kGroupPad : kNotchPad;
}

// Cells: one per widget that has one. The run of neighbouring cells (cut by a divider, a free widget or the end of a zone) decides the corners.
void groupCells(const App& app, Bar& bar, const std::vector<std::string>& mods, double x) {
    if (!groupHasCells(app.cfg) || sideBar(app)) return;
    const double full = (kQuickBox + 2 * kGroupPad) / 2.0, mid = kGroupMidRadius;
    const bool joined = app.cfg.groupStyle == "pills";
    size_t runStart = bar.groups.size();
    bool open = false;
    auto close = [&] {
        const size_t n = bar.groups.size() - runStart;
        for (size_t k = 0; k < n; k++) {
            GroupSpan& g = bar.groups[runStart + k];
            g.rl = !joined || k == 0 ? full : mid;
            g.rr = !joined || k + 1 == n ? full : mid;
        }
        open = false;
    };
    for (size_t i = 0; i < mods.size(); i++) {
        const double w = moduleWidth(app, mods[i]) + cellInsetL(app, mods[i]) + cellInsetR(app, mods[i]);
        if (hasGroupCell(app.cfg, mods[i])) {
            if (!open) { runStart = bar.groups.size(); open = true; }
            bar.groups.push_back({std::max(0.0, x - kGroupPad), std::min<double>(bar.width, x + w + kGroupPad), full, full});
        } else if (open) close();
        x += w + (i + 1 < mods.size() ? moduleGap(app, mods[i], mods[i + 1]) : 0);
    }
    if (open) close();
}

double edgePad(const App& app, const std::string& m) {
    if (app.cfg.barStyle != "float") return kEdgePad;
    double h = kModuleBox;
    if (m == "avatar") h = kBarAvatar;
    else if (m == "settings" || m == "power" || m == "tools" || m == "notifications") h = roundBtnW(app, m.c_str());
    else if (m == "launcher") h = kQuickBox;
    return std::max(0.0, (kBarHeight - h) / 2.0);
}

void layoutBar(Bar& bar) {
    const App& app = *bar.app;
    bar.slot.clear();
    bar.dividers.clear();
    bar.groups.clear();
    const double W = bar.width;
    auto live = [&](const std::vector<std::string>& v) {
        std::vector<std::string> out;
        for (const auto& m : v)
            if (moduleWidth(app, m) > 0) out.push_back(m);
        return out;
    };
    std::map<std::string, int> copies;
    bar.vizMirror.clear();
    auto flow = [&](const std::vector<std::string>& mods, double x) {
        const double x0 = x;
        int vizHere = 0;
        for (size_t i = 0; i < mods.size(); i++) {
            if (isDivider(mods[i])) {
                const auto style = dividerOptions(app.cfg, mods[i]).style;
                bar.dividers.emplace_back(x + bardivider::extent(style) / 2.0, style);
            }
            if (mods[i] == "visualizer") bar.vizMirror.push_back(vizHere++ % 2 == 1);
            bar.slot[repeatable(mods[i]) ? mods[i] + "#" + std::to_string(copies[mods[i]]++) : mods[i]] = x + cellInsetL(app, mods[i]);
            x += moduleWidth(app, mods[i]) + cellInsetL(app, mods[i]) + cellInsetR(app, mods[i]) + (i + 1 < mods.size() ? moduleGap(app, mods[i], mods[i + 1]) : 0);
        }
        groupCells(app, bar, mods, x0);
    };
    auto span = [&](const std::vector<std::string>& mods) {
        double w = 0;
        for (size_t i = 0; i < mods.size(); i++)
            w += moduleWidth(app, mods[i]) + cellInsetL(app, mods[i]) + cellInsetR(app, mods[i]) + (i + 1 < mods.size() ? moduleGap(app, mods[i], mods[i + 1]) : 0);
        return w;
    };

    if (app.cfg.barStyle == "split" && !sideBar(app)) {
        const auto l = live(app.cfg.layoutLeft), c = live(centerModules(app)), r = live(app.cfg.layoutRight);
        const double sl = span(l), sc = span(c), sr = span(r);
        bar.segs.clear();
        if (sl > 0) { bar.segs.push_back({0, kEdgePad + sl + bodyPad(app)}); flow(l, kEdgePad); }
        if (sc > 0) { const double x0 = (W - sc) / 2.0; bar.segs.push_back({x0 - bodyPad(app), x0 + sc + bodyPad(app)}); flow(c, x0); }
        if (sr > 0) { bar.segs.push_back({W - sr - bodyPad(app) - kEdgePad, W}); flow(r, W - kEdgePad - sr); }
        return;
    }
    if (app.cfg.barStyle == "notch" && !sideBar(app)) {
        const auto l = live(app.cfg.layoutLeft), c = live(app.cfg.layoutCenter), r = live(app.cfg.layoutRight);
        const double sl = span(l), sc = span(c), sr = span(r);
        int groups = 0;
        for (double v : {sl, sc, sr}) groups += v > 0;
        const double natural = sl + sc + sr + std::max(0, groups - 1) * kModuleGap + 2 * bodyPad(app);
        const double wide = W * app.cfg.notchWidth / 100.0;
        const double nw = std::clamp(std::max(natural, wide), 0.0, W - 2 * kNotchEar);
        const double x0 = (W - nw) / 2.0;
        bar.segs = {{x0, x0 + nw}};
        flow(l, x0 + bodyPad(app));
        flow(r, x0 + nw - bodyPad(app) - sr);
        const double lo = x0 + bodyPad(app) + sl + (sl > 0 ? kModuleGap : 0), hi = x0 + nw - bodyPad(app) - sr - (sr > 0 ? kModuleGap : 0) - sc;
        flow(c, wide > natural ? std::clamp((W - sc) / 2.0, lo, std::max(lo, hi)) : lo);
        return;
    }
    const auto left = live(app.cfg.layoutLeft);
    flow(left, left.empty() ? kEdgePad : edgePad(app, left.front()));
    const auto right = live(app.cfg.layoutRight);
    flow(right, W - (right.empty() ? kEdgePad : edgePad(app, right.back())) - span(right));

    const auto center = live(centerModules(app));
    flow(center, (W - span(center)) / 2.0);
}

bool hasSlot(const Bar& bar, const char* m) { return bar.slot.count(m) > 0; }
double slotX(const Bar& bar, const char* m) {
    auto it = bar.slot.find(m);
    return it == bar.slot.end() ? -10000 : it->second;
}
double notifX(const Bar& bar) { return slotX(bar, "notifications"); }
double settingsX(const Bar& bar) { return slotX(bar, "settings"); }
double launcherX(const Bar& bar) { return slotX(bar, "launcher"); }
double powerX(const Bar& bar) { return slotX(bar, "power"); }
double toolsX(const Bar& bar) { return slotX(bar, "tools"); }
double avatarX(const Bar& bar) { return slotX(bar, "avatar"); }
double clockX(const Bar& bar) { return slotX(bar, "clock"); }
double batteryX(const Bar& bar) { return slotX(bar, "battery"); }
double volumeX(const Bar& bar) { return slotX(bar, "volume"); }
double networkX(const Bar& bar) { return slotX(bar, "network"); }
double musicX(const Bar& bar) { return slotX(bar, "media"); }
double quickX(const Bar& bar) { return slotX(bar, "quickactions"); }

Hit hitTestSide(const Bar& bar, double x, double y);
Hit hitTest(const Bar& bar, double x, double y) {
    if (sideBar(*bar.app)) return hitTestSide(bar, x, y);
    if (y < kButtonMargin || y >= bar.height - kButtonMargin) return HitNone;
    const App& app = *bar.app;
    if (hasSlot(bar, "island")) {
        const double ix = slotX(bar, "island"), iw = islandW(app);
        if (x >= ix && x < ix + iw) {
            return HitIsland;
        }
    }
    if (const Hit dock = dtpHit(app, bar, x); dock != HitNone) return dock;
    if (hasSlot(bar, "avatar") && x >= avatarX(bar) && x < avatarX(bar) + kBarAvatar) return HitAvatar;
    if (hasSlot(bar, "systray")) {
        std::vector<int> pin, oth;
        traySplit(app, pin, oth);
        const double sx = slotX(bar, "systray"), p = 1 - std::pow(1 - app.trayT, 3), pw = pin.size() * kTrayItem, iw = oth.size() * kTrayItem * p;
        if (x >= sx && x < sx + pw) return kHitTray + pin[static_cast<int>((x - sx) / kTrayItem)];
        if (trayForward(app, false)) {
            if (!oth.empty() && x >= sx + pw && x < sx + pw + kQuickBox) return HitTray;
            const double ix0 = sx + pw + kQuickBox;
            if (p > 0.99 && x >= ix0 && x < ix0 + iw) return kHitTray + oth[static_cast<int>((x - ix0) / kTrayItem)];
        } else {
            if (!oth.empty() && x >= sx + pw + iw && x < sx + pw + iw + kQuickBox) return HitTray;
            if (p > 0.99 && x >= sx + pw && x < sx + pw + iw) return kHitTray + oth[static_cast<int>((x - sx - pw) / kTrayItem)];
        }
    }
    if (hasSlot(bar, "clock") && x >= clockX(bar) && x < clockX(bar) + app.clockW) return HitClock;
    if (hasSlot(bar, "battery") && (app.batPresent || lowPeripheral(app)) && x >= batteryX(bar) && x < batteryX(bar) + batteryW(app)) return kHitStatus + 3;
    if (hasSlot(bar, "resources")) {
        const auto items = resItems(app);
        const double one = resOneW(app), val = resValW(app);
        double rx = slotX(bar, "resources");
        for (const ResItem& ri : items) {
            if (x >= rx - 4 && x < rx + one + val + 4) return kHitRes + ri.kind;
            rx += one + val + 6;
        }
    }
    if (hasSlot(bar, "aiusage") && !aiShown(app).empty() && x >= slotX(bar, "aiusage") && x < slotX(bar, "aiusage") + app.aiBarW) {
        const auto shown = aiShown(app);
        const int k = shown.size() > 1 && x - slotX(bar, "aiusage") > app.aiPillSplit ? 1 : 0;
        return kHitAi + shown[k];
    }
    if (hasSlot(bar, "keyboard") && !app.kbd.list.empty() && x >= slotX(bar, "keyboard") && x < slotX(bar, "keyboard") + kbdBarW(app)) return HitKeyboard;
    if (hasSlot(bar, "weather") && app.barWeather.ok && x >= slotX(bar, "weather") && x < slotX(bar, "weather") + app.weatherBarW) return HitWeather;
    if (hasSlot(bar, "updates") && x >= slotX(bar, "updates") && x < slotX(bar, "updates") + updatesBarW(app)) return HitUpdates;
    if (hasSlot(bar, "volume") && x >= volumeX(bar) && x < volumeX(bar) + volumeW(app)) return HitVolume;
    if (hasSlot(bar, "drives") && drivesVisible(app) && x >= slotX(bar, "drives") && x < slotX(bar, "drives") + kQuickBox + softExtra(app, "drives")) return HitDrives;
    if (hasSlot(bar, "bluetooth") && x >= slotX(bar, "bluetooth") && x < slotX(bar, "bluetooth") + btBarW(app)) return HitBluetooth;
    if (hasSlot(bar, "network") && x >= networkX(bar) && x < networkX(bar) + netExtent(app, false)) return HitNetwork;
    if (hasSlot(bar, "settings") && x >= settingsX(bar) && x < settingsX(bar) + roundBtnWide(app, "settings")) return HitSettings;
    if (hasSlot(bar, "launcher") && x >= launcherX(bar) && x < launcherX(bar) + launcherWidth(app)) return HitLauncher;
    if (hasSlot(bar, "power") && x >= powerX(bar) && x < powerX(bar) + roundBtnWide(app, "power")) return HitPower;
    if (hasSlot(bar, "tools") && x >= toolsX(bar) && x < toolsX(bar) + roundBtnWide(app, "tools")) return HitTools;
    if (hasSlot(bar, "notifications") && x >= notifX(bar) && x < notifX(bar) + notifW(app)) return HitNotif;
    if (hasSlot(bar, "quickactions"))
        for (size_t i = 0, n = quickShown(app).size(); i < n; i++) {
            const double pitch = quickPitch(app), slack = (pitch - quickBase(app)) / 2;
            const double ix = quickX(bar) + quickPadX(app) + quickItemX(app, i);
            if (x >= ix - slack && x < ix + quickItemW(app, quickShown(app)[i]) + slack) return kHitQuick + quickShown(app)[i];
        }
    if (hasSlot(bar, "media") && musicProgress(app)) {
        if (x >= musicX(bar) && x < musicX(bar) + musicW(app)) return HitMedia;
    } else if (hasSlot(bar, "media") && musicM3(app)) {
        const double px = musicX(bar) + musicCtrlX(app);
        if (x >= px - 2 && x < px + kMediaPillW + 1) return HitPlay;
        if (x >= px + kMediaNextX - 1 && x < px + kMediaNextX + kM3Disc + 2) return HitNext;
        if (x >= musicX(bar) && x < px - kMusicGap / 2) return HitMedia;
    }
    if (hasSlot(bar, "media") && !musicM3(app)) {
        const double cx = musicX(bar) + musicPad(app) + musicCtrlX(app);
        const Hit ids[3] = {HitPrev, HitPlay, HitNext};
        for (int i = 0; i < 3; i++)
            if (x >= cx + i * kMusicBtn - 2 && x < cx + i * kMusicBtn + kQuickBox + 2) return ids[i];
        if (x >= musicX(bar) && x < cx - kMusicGap / 2) return HitMedia;
    }
    for (int k = 0;; k++) {
        const std::string key = "visualizer#" + std::to_string(k);
        if (!hasSlot(bar, key.c_str())) break;
        if (x >= slotX(bar, key.c_str()) && x < slotX(bar, key.c_str()) + moduleWidth(app, "visualizer")) return kHitViz + (k % 2);
    }
    if (hasSlot(bar, "workspaces"))
        for (int id = 1; id <= wsShown(app); id++) {
            const double wx = wsXOf(bar, id);
            if (x >= wx - wsGap(app) / 2 && x < wx + wsWidth(app, id) + wsGap(app) / 2) return kHitWs + id;
        }
    return HitNone;
}

bool readNetwork(App& app) {
    std::string kind = "none";
    std::ifstream f("/proc/net/route");
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        char iface[64];
        unsigned dest = 1, flags = 0;
        if (std::sscanf(line.c_str(), "%63s %x %*x %x", iface, &dest, &flags) != 3) continue;
        if (dest != 0 || (flags & 3) != 3) continue;
        const std::string base = std::string("/sys/class/net/") + iface;
        const std::string name = iface;
        if (access((base + "/wireless").c_str(), F_OK) == 0) kind = "wifi";
        else if (access((base + "/tun_flags").c_str(), F_OK) == 0 || name.rfind("wg", 0) == 0 ||
                 name.rfind("tailscale", 0) == 0 || name.rfind("ppp", 0) == 0) kind = "vpn";
        else kind = "ethernet";
        break;
    }
    const bool changed = kind != app.netKind;
    app.netKind = kind;
    return changed;
}

int startNetlink() {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) return -1;
    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;
    if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { close(fd); return -1; }
    return fd;
}

int startPowerWatcher() {
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) return -1;
    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = 1;
    if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) { close(fd); return -1; }
    return fd;
}
bool powerEventsPending(int fd) {
    bool power = false;
    char buf[8192];
    for (;;) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        for (const char* p = buf; p < buf + n; p += std::strlen(p) + 1)
            if (std::strcmp(p, "SUBSYSTEM=power_supply") == 0) { power = true; break; }
    }
    return power;
}

bool readVolume(App& app) {
    FILE* f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!f) return false;
    char line[128] = {};
    const char* got = std::fgets(line, sizeof line, f);
    pclose(f);
    if (!got) return false;
    double v = 0;
    if (std::sscanf(line, "Volume: %lf", &v) != 1) return false;
    const int pct = static_cast<int>(v * 100 + 0.5);
    const bool muted = std::strstr(line, "MUTED") != nullptr;
    const bool changed = pct != app.volume || muted != app.muted;
    app.volume = pct;
    app.muted = muted;
    return changed;
}

// Children must not inherit our signal setup: SIGINT/SIGTERM are blocked here (for the signalfd) and SIGCHLD is
// ignored, and both survive exec. Without this, "stop recording" (SIGINT) could never reach wf-recorder.
void childSignalReset() {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    signal(SIGCHLD, SIG_DFL);
}

// Fire and forget; SIGCHLD is ignored so nothing is left as a zombie.
void runDetachedArgs(std::vector<const char*> argv) {
    argv.push_back(nullptr);
    if (fork() == 0) {
        childSignalReset();
        setsid();
        if (const char* home = std::getenv("HOME"); home && *home && chdir(home) != 0) {}  // programs the user starts open in the home folder, not in the shell's own (~/.config/end4pC)
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
}
void runDetached(std::initializer_list<const char*> args) { runDetachedArgs(std::vector<const char*>(args)); }

int startAudioWatcher() {
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return -1;
    if (fork() == 0) {
        childSignalReset();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_WRONLY);
        dup2(nul, STDERR_FILENO);
        execlp("pactl", "pactl", "subscribe", nullptr);
        _exit(127);
    }
    close(p[1]);
    return p[0];
}

// ------------------------------------------------------------- compositor: which one runs, and Hyprland's command socket

enum class Wm { None, Hyprland, Niri, Sway };

Wm wmKind() {
    static const Wm kind = [] {
        const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (sig && *sig) return Wm::Hyprland;
        const char* sock = std::getenv("NIRI_SOCKET");
        if (sock && *sock) return Wm::Niri;
        const char* swaySock = std::getenv("SWAYSOCK");  // sway / SwayFX (only when neither of the others runs)
        if (swaySock && *swaySock) return Wm::Sway;
        return Wm::None;
    }();
    return kind;
}

std::string hyprSocketPath(const char* name) {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    return sig ? runtimeDir() + "/hypr/" + sig + "/" + name : "";
}

std::string hyprRequest(const std::string& cmd) {
    const std::string path = hyprSocketPath(".socket.sock");
    sockaddr_un a;
    if (path.empty() || unixAddr(a, path) < 0) return "";
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return "";
    timeval tv{0, 300000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    std::string out;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 &&
        write(fd, cmd.data(), cmd.size()) == static_cast<ssize_t>(cmd.size())) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) out.append(buf, n);
    }
    close(fd);
    return out;
}

std::string lowerCopy(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v;
}

std::string playerSelector(const std::string& preferred) { return preferred.empty() ? "%any" : preferred + ",%any"; }

int startMusicWatcher(const std::string& preferred, pid_t* pidOut) {
    int p[2];
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) return -1;
    const std::string sel = playerSelector(preferred);
    const pid_t pid = fork();
    if (pid == 0) {
        childSignalReset();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(p[1], STDOUT_FILENO);
        int nul = open("/dev/null", O_WRONLY);
        dup2(nul, STDERR_FILENO);
        execlp("playerctl", "playerctl", "-p", sel.c_str(), "-F", "metadata", "--format", "{{playerInstance}}\t{{status}}\t{{mpris:artUrl}}\t{{artist}}\t{{title}}", nullptr);
        _exit(127);
    }
    close(p[1]);
    if (pidOut) *pidOut = pid;
    return p[0];
}

std::string homeDir();
void loadCover(App& app, const std::string& path);

std::string percentDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit((unsigned char)in[i + 1]) && std::isxdigit((unsigned char)in[i + 2])) {
            out += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string artCachePath(const std::string& url) {
    const char* c = std::getenv("XDG_CACHE_HOME");
    const std::string dir = (c ? std::string(c) : homeDir() + "/.cache") + "/end4pC/art";
    char name[32];
    std::snprintf(name, sizeof name, "%016zx", std::hash<std::string>{}(url));
    return dir + "/" + name;
}

int g_artFetchFd = -1;
int g_artFetchWrite = -1;

void fetchCover(const std::string& url, const std::string& path) {
    if (fork() == 0) {
        childSignalReset();
        dup2(g_artFetchWrite, STDOUT_FILENO);
        int nul = open("/dev/null", O_WRONLY);
        dup2(nul, STDERR_FILENO);
        execlp("sh", "sh", "-c",
               "mkdir -p \"$(dirname \"$1\")\" && curl -sfL -m 15 -o \"$1.tmp\" \"$2\" && mv \"$1.tmp\" \"$1\"; echo done",
               "sh", path.c_str(), url.c_str(), nullptr);
        _exit(127);
    }
}

void updateCover(App& app, const std::string& url) {
    const bool same = url == app.musicArtUrl;
    if (same && (app.musicArt || url.empty())) return;
    if (!same) {
        app.musicArtAttempts = 0;
        app.musicArtRetryAt = {};
    }
    app.musicArtUrl = url;
    if (url.rfind("file://", 0) == 0) {
        loadCover(app, percentDecode(url.substr(7)));
    } else if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        const std::string path = artCachePath(url);
        loadCover(app, access(path.c_str(), R_OK) == 0 ? path : "");
        const auto now = std::chrono::steady_clock::now();
        // A completion event can also mean curl/decoding failed. Do not turn it
        // into an immediate retry loop. Polls may retry, at most three attempts
        // per selected URL, and never while the 15-second curl is still running.
        if (!app.musicArt && app.musicArtAttempts < 3 && now >= app.musicArtRetryAt) {
            ++app.musicArtAttempts;
            app.musicArtRetryAt = now + std::chrono::seconds(30);
            fetchCover(url, path);
        }
    } else {
        loadCover(app, "");
    }
}

constexpr int kClockPadL = 7, kClockGap = 6, kClockPadR = 9, kClockIcon = 18;
constexpr const char* kClockTimeFont = "Sans Bold 11";
constexpr const char* kClockDateFont = "Sans 9";

