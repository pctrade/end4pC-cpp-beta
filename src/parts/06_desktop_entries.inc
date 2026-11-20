

struct ThemeDir {
    std::string path;
    int size = 0, minSize = 0, maxSize = 0, threshold = 2;
    bool scalable = false;
};
struct IconTheme {
    std::vector<std::string> roots;
    std::vector<ThemeDir> dirs;
    std::vector<std::string> inherits;
};

std::vector<std::string> iconBaseDirs() {
    return {homeDir() + "/.local/share/icons", homeDir() + "/.icons", "/usr/share/icons", "/usr/local/share/icons",
            "/var/lib/flatpak/exports/share/icons", homeDir() + "/.local/share/flatpak/exports/share/icons"};
}

IconTheme loadTheme(const std::string& name) {
    IconTheme t;
    std::string indexPath;
    for (const auto& base : iconBaseDirs()) {
        if (access((base + "/" + name + "/index.theme").c_str(), R_OK) == 0) {
            t.roots.push_back(base + "/" + name);
            if (indexPath.empty()) indexPath = base + "/" + name + "/index.theme";
        }
    }
    if (indexPath.empty()) return t;
    std::ifstream f(indexPath);
    std::string line, section;
    std::vector<std::string> listed;
    std::map<std::string, ThemeDir> dirs;
    std::map<std::string, std::string> contexts;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (section == "Icon Theme") {
            if (k == "Directories") listed = splitList(v);
            else if (k == "Inherits") t.inherits = splitList(v);
        } else {
            ThemeDir& d = dirs[section];
            d.path = section;
            if (k == "Size") { d.size = std::atoi(v.c_str()); if (!d.minSize) d.minSize = d.size; if (!d.maxSize) d.maxSize = d.size; }
            else if (k == "MinSize") d.minSize = std::atoi(v.c_str());
            else if (k == "MaxSize") d.maxSize = std::atoi(v.c_str());
            else if (k == "Threshold") d.threshold = std::atoi(v.c_str());
            else if (k == "Type") d.scalable = v == "Scalable";
            else if (k == "Context") contexts[section] = v;
        }
    }
    for (const auto& name2 : listed) {
        auto it = dirs.find(name2);
        if (it == dirs.end()) continue;
        const std::string lower = lowerCopy(name2);
        if (lower.find("symbolic") != std::string::npos) continue;
        const bool apps = contexts[name2] == "Applications" || lower.find("apps") != std::string::npos;
        if (apps) t.dirs.push_back(it->second);
    }
    return t;
}

int dirDistance(const ThemeDir& d, int want) {
    if (d.scalable || d.minSize != d.maxSize) {
        if (want < d.minSize) return d.minSize - want;
        if (want > d.maxSize) return want - d.maxSize;
        return 0;
    }
    return std::abs(d.size - want);
}

std::string findInTheme(const IconTheme& t, const std::string& name, int want) {
    std::vector<const ThemeDir*> order;
    for (const auto& d : t.dirs) order.push_back(&d);
    std::stable_sort(order.begin(), order.end(),
                     [&](const ThemeDir* a, const ThemeDir* b) { return dirDistance(*a, want) < dirDistance(*b, want); });
    for (const ThemeDir* d : order)
        for (const auto& root : t.roots)
            for (const char* ext : {".svg", ".png"}) {
                const std::string path = root + "/" + d->path + "/" + name + ext;
                if (access(path.c_str(), R_OK) == 0) return path;
            }
    return "";
}

std::string currentIconTheme() {
    static std::string kept;
    static struct timespec keptStamp = {};
    static std::chrono::steady_clock::time_point keptAt{};
    static bool have = false;
    struct stat st = {};
    const bool statOk = stat((homeDir() + "/.config/dconf/user").c_str(), &st) == 0;
    const auto now = std::chrono::steady_clock::now();
    if (have && (statOk ? (st.st_mtim.tv_sec == keptStamp.tv_sec && st.st_mtim.tv_nsec == keptStamp.tv_nsec) : now - keptAt < std::chrono::seconds(5))) return kept;
    FILE* f = popen("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null", "r");
    char buf[128] = {};
    const char* got = f ? std::fgets(buf, sizeof buf, f) : nullptr;
    if (f) pclose(f);
    std::string v = got ? buf : "";
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\'' || c == '\n' || c == '"'; }), v.end());
    kept = v.empty() ? "hicolor" : v;
    keptStamp = st.st_mtim;
    keptAt = now;
    have = true;
    return kept;
}

std::string lookupIcon(const std::string& name, int size) {
    if (name.empty()) return "";
    if (name[0] == '/') return access(name.c_str(), R_OK) == 0 ? name : "";
    static std::map<std::string, IconTheme> cache;
    std::vector<std::string> todo = {currentIconTheme()}, seen;
    for (size_t i = 0; i < todo.size(); i++) {
        const std::string th = todo[i];
        if (std::find(seen.begin(), seen.end(), th) != seen.end()) continue;
        seen.push_back(th);
        auto it = cache.find(th);
        if (it == cache.end()) it = cache.emplace(th, loadTheme(th)).first;
        const std::string hit = findInTheme(it->second, name, size);
        if (!hit.empty()) return hit;
        for (const auto& parent : it->second.inherits) todo.push_back(parent);
        if (i + 1 == todo.size() && th != "hicolor") todo.push_back("hicolor");
    }
    for (const char* ext : {".svg", ".png"}) {
        const std::string path = std::string("/usr/share/pixmaps/") + name + ext;
        if (access(path.c_str(), R_OK) == 0) return path;
    }
    return "";
}

std::string desktopValue(const std::string& file, const std::string& key) {
    std::ifstream f(file);
    std::string line;
    bool inEntry = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '[') { inEntry = line == "[Desktop Entry]"; continue; }
        if (inEntry && line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    }
    return "";
}

std::string findDesktopFile(const std::string& id) {
    for (const auto& dir : {homeDir() + "/.local/share/applications", std::string("/usr/share/applications"),
                            std::string("/usr/local/share/applications"),
                            std::string("/var/lib/flatpak/exports/share/applications"),
                            homeDir() + "/.local/share/flatpak/exports/share/applications"}) {
        const std::string path = dir + "/" + id + ".desktop";
        if (access(path.c_str(), R_OK) == 0) return path;
    }
    return "";
}

cairo_surface_t* loadIconSurface(const std::string& path, int size) {
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".svg") == 0) {
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), nullptr);
        if (!h) return nullptr;
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(surf);
        RsvgRectangle vp = {0, 0, double(size), double(size)};
        rsvg_handle_render_document(h, cr, &vp, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
        return surf;
    }
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_size(path.c_str(), size, size, nullptr);
    if (!pb) return nullptr;
    cairo_surface_t* img = surfaceFromPixbuf(pb);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = mkcr(surf);
    cairo_set_source_surface(cr, img, (size - gdk_pixbuf_get_width(pb)) / 2.0, (size - gdk_pixbuf_get_height(pb)) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    g_object_unref(pb);
    return surf;
}

cairo_surface_t* numberMask(int n, int size) {
    static std::map<std::pair<int, int>, cairo_surface_t*> cache;
    const auto key = std::make_pair(n, size);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    cairo_surface_t* mask = nullptr;
    if (n >= 1 && n <= 10) mask = loadIconSurface(assetsDir() + "/icons/numbers/" + std::to_string(n) + ".svg", size);
    cache[key] = mask;
    return mask;
}

void roundedRectCorners(cairo_t* cr, double x, double y, double w, double h, double tl, double tr, double br, double bl) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - tr, y + tr, tr, -M_PI / 2, 0);
    cairo_arc(cr, x + w - br, y + h - br, br, 0, M_PI / 2);
    cairo_arc(cr, x + bl, y + h - bl, bl, M_PI / 2, M_PI);
    cairo_arc(cr, x + tl, y + tl, tl, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void drawNetSpeed(cairo_t* cr, const App& app, double x, double y, double w, double h, bool wide, const Color* ink = nullptr) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, (wide ? (ink ? 10 : 11) : 10) * PANGO_SCALE);
    pango_font_description_set_weight(fd, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    const double rate[2] = {app.netUp, app.netDown};
    const double rowH = wide ? (ink ? 11 : 13) : 14, top = y + (h - 2 * rowH) / 2;
    for (int k = 0; k < 2; k++) {
        const double ry = top + k * rowH;
        const int isz = wide ? (ink ? 10 : 11) : 9;
        const Color accent = ink ? *ink : app.color(k == 0 ? "tertiary" : "primary");
        const std::string text = netRateText(rate[k], !wide);
        pango_layout_set_text(layout, text.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        double ax, tx;
        if (wide) { ax = x + 2; tx = x + w - 2 - tw; }
        else { const double total = isz + 1 + tw; ax = x + (w - total) / 2; tx = ax + isz + 1; }
        cairo_save(cr);
        cairo_push_group(cr);
        drawIcon(cr, k == 0 ? "arrow-up" : "arrow-down", ax, ry + (rowH - isz) / 2.0, isz, accent);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, rate[k] > 0 ? 1.0 : 0.45);
        cairo_restore(cr);
        setSource(cr, ink ? *ink : app.color("on_surface"));
        cairo_move_to(cr, tx, ry + (rowH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
}

bool m3IconOnly(double w, double h) { return w < h * 1.5; }
void m3GroupPill(cairo_t* cr, const App& app, double x, double y, double w, double h) {
    if (m3IconOnly(w, h)) {
        constexpr double kShape = kM3Box;
        shapePath(cr, kM3IconShape, x + (w - kShape) / 2, y + (h - kShape) / 2, kShape);
        setSource(cr, app.color("secondary_container"));
    } else {
        roundedRect(cr, x, y + (h - kM3Box) / 2, w, kM3Box, kM3Box / 2);
        setSource(cr, app.color("surface_container_low"));
    }
    cairo_fill(cr);
}
void drawNetSpeedM3(cairo_t* cr, const App& app, double x, double y, double w, double h) {
    constexpr double kRowPitch = 10, kRowH = 13;
    constexpr int kArrow = 12;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, 10 * PANGO_SCALE);
    pango_font_description_set_weight(fd, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    const double rate[2] = {app.netUp, app.netDown};
    const double top = y + (h - (kRowH + kRowPitch)) / 2.0;
    for (int k = 0; k < 2; k++) {
        const double ry = top + k * kRowPitch;
        const std::string text = netRateText(rate[k], false);
        pango_layout_set_text(layout, text.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_save(cr);
        cairo_push_group(cr);
        drawIcon(cr, k == 0 ? "arrow-up" : "arrow-down", x + 2, ry + (kRowH - kArrow) / 2.0, kArrow, app.color(k == 0 ? "tertiary" : "primary"));
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, rate[k] > 0 ? 1.0 : 0.45);
        cairo_restore(cr);
        setSource(cr, app.color("on_surface_variant"));
        cairo_move_to(cr, x + w - 2 - tw, ry + (kRowH - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
}
void drawVolumeM3(cairo_t* cr, const App& app, double x, double barH) {
    const double y = (barH - kQuickBox) / 2.0;
    m3GroupPill(cr, app, x, y, kVolIconW, kQuickBox);
    drawIcon(cr, app.muted ? "volume_off" : "volume_up", x + (kVolIconW - 18) / 2.0, y + (kQuickBox - 18) / 2.0, 18, app.color(m3IconOnly(kVolIconW, kQuickBox) ? "on_secondary_container" : "on_surface_variant"));
}
void drawNetworkM3(cairo_t* cr, const App& app, double x, double barH) {
    const char* icon = app.netKind == "wifi" ? "wifi" : app.netKind == "ethernet" ? "settings_ethernet" : app.netKind == "vpn" ? "vpn_lock" : "link_broken";
    const double y = (barH - kQuickBox) / 2.0;
    m3GroupPill(cr, app, x, y, netExtent(app, false), kQuickBox);
    if (netIconShown(app)) drawIcon(cr, icon, x + (kNetW - 18) / 2.0, y + (kQuickBox - 18) / 2.0, 18, app.color(m3IconOnly(netExtent(app, false), kQuickBox) ? "on_secondary_container" : "on_surface_variant"));
    if (netSpeedShown(app)) drawNetSpeedM3(cr, app, x + netIconPart(app, false), y, kNetSpeedW, kQuickBox);
}
void drawBatteryM3(cairo_t* cr, const App& app, double x, double barH, int pct, bool charging, const Color& ink) {
    constexpr double kBarW = 30, kBarH = 18, kTipW = 2, kTipH = 10, kBoltW = 12, kBarRadius = 6;
    const double y = (barH - kQuickBox) / 2.0, cx = x + kBatW / 2.0, cy = barH / 2.0;
    roundedRect(cr, x, y + (kQuickBox - kM3Box) / 2, kBatW, kM3Box, kM3Box / 2);
    setSource(cr, app.color("primary_container"));
    cairo_fill(cr);
    const double bx = cx - (kBarW + 1 + kTipW) / 2.0, by = cy - kBarH / 2.0, frac = std::clamp(pct / 100.0, 0.0, 1.0);
    roundedRect(cr, bx, by, kBarW, kBarH, kBarRadius);
    setSource(cr, ink, 0.5);
    cairo_fill(cr);
    if (frac > 0) {
        cairo_save(cr);
        roundedRect(cr, bx, by, kBarW, kBarH, kBarRadius);
        cairo_clip(cr);
        roundedRect(cr, bx, by, std::max(4.0, kBarW * frac), kBarH, 2);
        setSource(cr, ink);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    roundedRect(cr, bx + kBarW + 1, cy - kTipH / 2.0, kTipW, kTipH, 1);
    setSource(cr, ink, frac > 0.95 ? 1.0 : 0.5);
    cairo_fill(cr);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    const std::string text = std::to_string(pct);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
    pango_font_description_set_weight(fd, text.size() > 2 ? PANGO_WEIGHT_MEDIUM : PANGO_WEIGHT_SEMIBOLD);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const bool bolt = charging && pct < 100;
    const double tx = bx + kBarW / 2.0 - ((bolt ? kBoltW : 0) + tw) / 2.0;
    const Color hole = app.color("primary_container");
    if (bolt) drawIcon(cr, "bolt", tx, cy - kBoltW / 2.0, 12, hole, true);
    setSource(cr, hole);
    cairo_move_to(cr, tx + (bolt ? kBoltW : 0), cy - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

struct SideItem { int id; Rect r; bardivider::Style dividerStyle = bardivider::Style::Line; };
const char* kSideWsIcons[] = {"code", "global", "music-note", "pen", "gallery", "chat-round-dots", "graph-up", "gamepad", "folder-with-files", "notebook"};
struct SideWsShape { double h, vw, gap; };

cairo_surface_t* wsAppIcon(const App& app, int id) {
    if (!app.cfg.workspaceApps) return nullptr;
    const auto cls = app.wsMainClass.find(id);
    if (cls == app.wsMainClass.end()) return nullptr;
    const auto icon = app.wsIcons.find(cls->second);
    return icon == app.wsIcons.end() ? nullptr : icon->second;
}

void drawWsAppIcon(cairo_t* cr, cairo_surface_t* icon, double cx, double cy, double box) {
    const double px = cairo_image_surface_get_width(icon);
    cairo_save(cr);
    cairo_translate(cr, cx - box / 2.0, cy - box / 2.0);
    cairo_scale(cr, box / px, box / px);
    cairo_set_source_surface(cr, icon, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

double wsTickLen(const App& app, int id) { return id == app.workspace ? kWsTickActive : app.occupied.count(id) ? kWsTickBusy : kWsTickEmpty; }
Color wsTickColor(const App& app, int id) {
    if (id == app.workspace) return app.color("primary");
    if (app.occupied.count(id)) return app.color("on_surface");
    const Color a = app.color("on_surface_variant"), b = app.color("surface_container_low");
    return Color{a.r * 0.45 + b.r * 0.55, a.g * 0.45 + b.g * 0.55, a.b * 0.45 + b.b * 0.55};
}
SideWsShape sideWsShape(const App& app, int id) {
    if (pillStyle(app)) return {kPillCell, kPillH + 4, 0};
    if (m3Ws(app)) return {kM3Cell, kM3Cell, 0};
    if (iconStyleWs(app)) return {40, 40, 4};
    if (ringStyleWs(app)) return {kNumH, kNumH, kNumGap};
    if (barStyleWs(app)) return {kWsTickW, wsTickLen(app, id), kWsTickGap};
    if (numberedStyle(app)) return {id == app.workspace ? kNumActiveW : kNumW, kNumH, kNumGap};
    return {id == app.workspace ? kWsActiveW : kWsDot, kWsDot, kWsGap};
}
double sideWsPad(const App& app) { return iconStyleWs(app) ? 0 : sideWsShape(app, 0).gap / 2; }
double sideWsTotal(const App& app) {
    double t = 0;
    for (int i = 1; i <= wsShown(app); i++) t += sideWsShape(app, i).h + (i > 1 ? sideWsShape(app, i).gap : 0);
    return t + (pillStyle(app) ? 2 * kPillPad : 0);
}
constexpr double kSideClockM3H = 78;
double sideBatteryH(const App& app) { return app.cfg.batteryPercent ? 58 : 40; }
double dtpSideHeight(const App& app);  // (14s_dock_panel.inc)
std::vector<std::pair<int, Rect>> dtpSideRects(const App& app, double x, double y);
void dtpDrawItem(cairo_t* cr, App& app, const DockItem& it, double bx, double by, bool hot, double alpha);
void dtpBarPaint(cairo_t* cr, App& app, const Bar& bar, Hit hot);
std::vector<DockItem>& dtpItems(const App& app);
double sideModuleH(const App& app, const std::string& m) {
    if (m == "workspaces") return sideWsTotal(app);
    if (m == "media") return app.musicStatus.empty() ? 0 : 40;
    if (m == "docktopanel") return dtpSideHeight(app);
    if (m == "quickactions") { const size_t n = quickShown(app).size(); return n ? n * 44 - 4.0 : 0; }
    if (m == "network") return netVisible(app) ? netExtent(app, true) : 0;
    if (m == "volume" || m == "notifications" || m == "settings" || m == "avatar" || m == "launcher" || m == "power" || m == "tools" || m == "bluetooth") return 40;
    if (m == "drives") return drivesVisible(app) ? 40 : 0;
    if (m == "privacy") return privacySideH(app);
    if (m == "clock") return widgetStyle(app, "clock") == "m3" ? kSideClockM3H : 54;
    if (m == "systray") {
        std::vector<int> pin, oth;
        traySplit(app, pin, oth);
        return pin.size() * 38 + (oth.empty() ? 0 : 40 + oth.size() * 38 * (1 - std::pow(1 - app.trayT, 3)));
    }
    if (m == "resources") {
        const size_t n = resItems(app).size();
        const double one = widgetStyle(app, "resources") == "text" ? 26 : 26 + (app.cfg.resValue ? 12 : 0);
        return n ? n * one + (n - 1) * 8.0 : 0;
    }
    if (m == "weather") return app.barWeather.ok ? 54 : 0;
    if (m == "updates") return updatesShown(app) ? 54 : 0;
    if (m == "aiusage") { const size_t n = aiShown(app).size(); return n ? n * 44 - 4.0 : 0; }
    if (m == "keyboard") return app.kbd.list.empty() ? 0 : 40;
    if (m == "activewindow") return 40;
    if (m == "visualizer") return kVizBars * (kVizDot + kVizGap) + (widgetStyle(app, "visualizer") == "m3" ? 2 * kVizM3Pad : 0);
    if (isDivider(m)) return bardivider::extent(dividerOptions(app.cfg, m).style);
    if (m == "battery") return app.statusT[3] > 0.001 ? sideBatteryH(app) * (1 - std::pow(1 - app.statusT[3], 3)) : 0;
    return 0;
}
double sideModuleGap(const App& app, const std::string& a, const std::string& b) {
    return dividerGap(app.cfg, a, b, app.cfg.sideBarSpacing);
}
std::vector<SideItem> sideLayout(const Bar& bar, std::vector<std::pair<double, double>>* spans = nullptr) {
    const App& app = *bar.app;
    std::vector<SideItem> v;
    const double x = (kSideBarW - 40) / 2.0, L = bar.width, edge = 8;
    const bool notch = app.cfg.barStyle == "notch", split = app.cfg.barStyle == "split";
    double nY0 = 0, nLen = 0, nSpan[3] = {0, 0, 0}, nNatural = 0, nWide = 0;
    if (notch || split) {
        const std::vector<std::string>* lists[3] = {&app.cfg.sideLeft, &app.cfg.sideCenter, &app.cfg.sideRight};
        int groups = 0;
        for (int k = 0; k < 3; k++) {
            std::vector<std::string> mods;
            for (const auto& m : *lists[k]) if (sideModuleH(app, m) > 0.001) mods.push_back(m);
            for (size_t i = 0; i < mods.size(); i++) nSpan[k] += sideModuleH(app, mods[i]) + (i + 1 < mods.size() ? sideModuleGap(app, mods[i], mods[i + 1]) : 0);
            groups += nSpan[k] > 0;
        }
        nNatural = nSpan[0] + nSpan[1] + nSpan[2] + std::max(0, groups - 1) * 14.0 + 2 * kNotchPad;
        nWide = L * app.cfg.notchWidth / 100.0;
        nLen = std::clamp(std::max(nNatural, nWide), 0.0, L - 2 * kNotchEar);
        nY0 = (L - nLen) / 2.0;
        bar.segs = {{nY0, nY0 + nLen}};
        if (split) {
            bar.segs.clear();
            const double mid = (L - nSpan[1]) / 2.0;
            if (nSpan[0] > 0) bar.segs.push_back({0, edge + nSpan[0] + kNotchPad});
            if (nSpan[1] > 0) bar.segs.push_back({mid - kNotchPad, mid + nSpan[1] + kNotchPad});
            if (nSpan[2] > 0) bar.segs.push_back({L - edge - nSpan[2] - kNotchPad, L});
        }
    }
    auto place = [&](const std::vector<std::string>& list, int where) {
        std::vector<std::string> mods;
        for (const auto& m : list)
            if (sideModuleH(app, m) > 0.001) mods.push_back(m);
        if (mods.empty()) return;
        int vizSeen = 0;
        double span = 0;
        for (size_t i = 0; i < mods.size(); i++) span += sideModuleH(app, mods[i]) + (i + 1 < mods.size() ? sideModuleGap(app, mods[i], mods[i + 1]) : 0);
        double y = where == 0 ? edge : where == 1 ? (L - span) / 2.0 : L - edge - span;
        if (split) y = where == 0 ? edge : where == 2 ? L - edge - span : (L - span) / 2.0;
        else if (notch) {
            const double top = nY0 + kNotchPad, lo = top + nSpan[0] + (nSpan[0] > 0 ? 14 : 0), hi = nY0 + nLen - kNotchPad - nSpan[2] - (nSpan[2] > 0 ? 14 : 0) - span;
            y = where == 0 ? top : where == 2 ? nY0 + nLen - kNotchPad - span : nWide > nNatural ? std::clamp((L - span) / 2.0, lo, std::max(lo, hi)) : lo;
        }
        if (spans) spans->push_back({y, y + span});
        for (size_t i = 0; i < mods.size(); i++) {
            const std::string& m = mods[i];
            if (m == "workspaces") {
                double wy = y + (pillStyle(app) ? kPillPad : 0);
                const double pad = sideWsPad(app);
                for (int id = 1; id <= wsShown(app); id++) {
                    const SideWsShape sh = sideWsShape(app, id);
                    v.push_back({kHitWs + id, {x, wy - pad, 40, sh.h + 2 * pad}});
                    wy += sh.h + sh.gap;
                }
            }
            else if (m == "media") v.push_back({HitMedia, {x, y, 40, 40}});
            else if (m == "docktopanel") for (const auto& p : dtpSideRects(app, x, y)) v.push_back({p.first, p.second});
            else if (m == "quickactions") { const auto q = quickShown(app); for (size_t k = 0; k < q.size(); k++) v.push_back({kHitQuick + q[k], {x, y + k * 44.0, 40, 40}}); }
            else if (m == "network") v.push_back({HitNetwork, {x, y, 40, netExtent(app, true)}});
            else if (m == "volume") v.push_back({HitVolume, {x, y, 40, 40}});
            else if (m == "bluetooth") v.push_back({HitBluetooth, {x, y, 40, 40}});
            else if (m == "drives") { if (drivesVisible(app)) v.push_back({HitDrives, {x, y, 40, 40}}); }
            else if (m == "privacy") { if (privacyVisible(app)) v.push_back({HitPrivacy, {x, y, 40, privacySideH(app)}}); }
            else if (m == "clock") v.push_back({HitClock, {x, y, 40, widgetStyle(app, "clock") == "m3" ? kSideClockM3H : 54}});
            else if (m == "notifications") v.push_back({HitNotif, {x, y, 40, 40}});
            else if (m == "settings") v.push_back({HitSettings, {x, y, 40, 40}});
            else if (m == "launcher") v.push_back({HitLauncher, {x, y, 40, 40}});
            else if (m == "power") v.push_back({HitPower, {x, y, 40, 40}});
            else if (m == "tools") v.push_back({HitTools, {x, y, 40, 40}});
            else if (m == "avatar") v.push_back({HitAvatar, {x, y, 40, 40}});
            else if (m == "resources") {
                const double one = widgetStyle(app, "resources") == "text" ? 26 : 26 + (app.cfg.resValue ? 12 : 0);
                const auto items = resItems(app);
                for (size_t k = 0; k < items.size(); k++) v.push_back({kHitRes + items[k].kind, {x, y + k * (one + 8), 40, one}});
            }
            else if (m == "weather") v.push_back({HitWeather, {x, y, 40, 54}});
            else if (m == "updates") v.push_back({HitUpdates, {x, y, 40, 54}});
            else if (m == "keyboard") v.push_back({HitKeyboard, {x, y, 40, 40}});
            else if (m == "activewindow") v.push_back({HitActiveWindow, {x, y, 40, 40}});
            else if (m == "aiusage") { const auto s = aiShown(app); for (size_t k = 0; k < s.size(); k++) v.push_back({kHitAi + s[k], {x, y + k * 44.0, 40, 40}}); }
            else if (m == "visualizer") v.push_back({kHitViz + (vizSeen++ % 2), {x, y, 40, sideModuleH(app, m)}});
            else if (isDivider(m)) v.push_back({HitDivider, {x, y, 40, sideModuleH(app, m)}, dividerOptions(app.cfg, m).style});
            else if (m == "battery") v.push_back({kHitStatus + 3, {x, y, 40, sideBatteryH(app)}});
            else if (m == "systray") {
                std::vector<int> pin, oth;
                traySplit(app, pin, oth);
                for (size_t k = 0; k < pin.size(); k++) v.push_back({kHitTray + pin[k], {x, y + k * 38.0, 40, 38}});
                const double top = y + pin.size() * 38.0;
                if (!oth.empty()) {
                    const double p = 1 - std::pow(1 - app.trayT, 3);
                    if (trayForward(app, true)) {
                        v.push_back({HitTray, {x, top, 40, 40}});
                        if (app.trayT > 0.001)
                            for (size_t k = 0; k < oth.size(); k++) v.push_back({kHitTray + oth[k], {x, top + 40 + k * 38 * p, 40, 38}});
                    } else {
                        v.push_back({HitTray, {x, top + oth.size() * 38 * p, 40, 40}});
                        if (app.trayT > 0.001)
                            for (size_t k = 0; k < oth.size(); k++) v.push_back({kHitTray + oth[k], {x, top + k * 38 * p, 40, 38}});
                    }
                }
            }
            y += sideModuleH(app, m) + (i + 1 < mods.size() ? sideModuleGap(app, m, mods[i + 1]) : 0);
        }
    };
    place(app.cfg.sideLeft, 0);
    place(app.cfg.sideCenter, 1);
    place(app.cfg.sideRight, 2);
    return v;
}
std::string resLookFor(const App& app, bool side) {
    const std::string& s = widgetStyle(app, "resources");
    return side && (s == "bars" || s == "pill") ? "filled" : s;
}
void drawResource(cairo_t* cr, const App& app, const ResItem& it, double cx, double cy, PangoLayout* layout, bool side = false) {
    const double u = std::clamp(it.pct, 0.0, 1.0);
    const bool warn = u * 100 >= it.warn;
    const Color tert = app.color("tertiary"), prim = app.color("primary"), err = app.color("error"), on = app.color("on_surface");
    const double blend = std::clamp((u - 0.5) / 0.3, 0.0, 1.0);
    const Color use = warn ? err : Color{tert.r * (1 - blend) + prim.r * blend, tert.g * (1 - blend) + prim.g * blend, tert.b * (1 - blend) + prim.b * blend};
    const std::string look = resLookFor(app, side);
    const double box = side ? kQuickBox : resBox(app), ic = 14 * box / kQuickBox;
    if (look == "pill") {
        const double h = box, w = app.cfg.resValue ? h + 18 : h, x = cx - h / 2, y = cy - h / 2;
        cairo_save(cr);
        roundedRect(cr, x, y, w, h, h / 2);
        cairo_clip(cr);
        setSource(cr, app.color("secondary_container"));
        cairo_paint(cr);
        if (u > 0.005) { cairo_rectangle(cr, x, y, w * u, h); setSource(cr, use, 0.45); cairo_fill(cr); }
        cairo_restore(cr);
        drawIcon(cr, it.icon, cx - ic / 2, cy - ic / 2, ic, app.color("on_secondary_container"));
        return;
    }
    if (look == "bars") {
        const double kTrackW = 6, kTrackH = box, kColGap = 5, kColW = 15;
        const double x0 = cx - 13, tx = x0, ty = cy - kTrackH / 2, colX = x0 + kTrackW + kColGap, colCx = colX + kColW / 2.0;
        roundedRect(cr, tx, ty, kTrackW, kTrackH, kTrackW / 2);
        setSource(cr, on, 0.16);
        cairo_fill(cr);
        if (u > 0.01) {
            const double fillH = std::max(kTrackW, kTrackH * u);
            roundedRect(cr, tx, ty + kTrackH - fillH, kTrackW, fillH, kTrackW / 2);
            setSource(cr, use);
            cairo_fill(cr);
        }
        drawIcon(cr, it.icon, colCx - 6, ty, 12, warn ? err : app.color("on_surface_variant"));
        PangoFontDescription* pf = pango_font_description_from_string("Sans Bold");
        pango_font_description_set_absolute_size(pf, 9 * PANGO_SCALE);
        pango_layout_set_font_description(layout, pf);
        const std::string pct = std::to_string(static_cast<int>(std::lround(u * 100)));
        pango_layout_set_text(layout, pct.c_str(), -1);
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(layout, &pw, &ph);
        setSource(cr, warn ? err : app.color("on_surface_variant"), 0.9);
        cairo_move_to(cr, colCx - pw / 2.0, ty + kTrackH - ph + 1);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(pf);
        return;
    }
    if (look == "text") {
        PangoFontDescription* f = pango_font_description_from_string("Sans Bold 8");
        pango_layout_set_font_description(layout, f);
        pango_layout_set_text(layout, it.label, -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        constexpr double kGap = -1;
        const double block = th + kGap + 4, top = cy - block / 2;
        setSource(cr, app.cfg.barFollowsFrame ? on : app.color("on_secondary_container"));
        cairo_move_to(cr, cx - tw / 2.0, top);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(f);
        roundedRect(cr, cx - 14, top + th + kGap, 28, 4, 2);
        setSource(cr, on, 0.2);
        cairo_fill(cr);
        if (u > 0.01) { roundedRect(cr, cx - 14, top + th + kGap, std::max(4.0, 28 * u), 4, 2); setSource(cr, use); cairo_fill(cr); }
        return;
    }
    const double r = box / 2.0;
    if (look == "outline") {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, r - 1.2, 0, 2 * M_PI);
        setSource(cr, on, 0.2);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        if (u > 0.005) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, cx, cy, r - 1.2, -M_PI / 2, -M_PI / 2 + 2 * M_PI * u);
            setSource(cr, use);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_stroke(cr);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        }
        drawIcon(cr, it.icon, cx - ic / 2, cy - ic / 2, ic, app.color("on_secondary_container"));
        return;
    }
    if (look == "m3") {
        drawProgressDisc(cr, app, cx, cy, box / 2.0, u, it.icon, 16 * box / kQuickBox, warn ? err : app.color("on_secondary_container"));
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    setSource(cr, on, 0.16);
    cairo_fill(cr);
    if (u > 0.005) {
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, r, -M_PI / 2, -M_PI / 2 + 2 * M_PI * std::min(u, 0.9999));
        cairo_close_path(cr);
        setSource(cr, use);
        cairo_fill(cr);
    }
    drawIcon(cr, it.icon, cx - ic / 2, cy - ic / 2, ic, app.color("on_secondary_container"));
    if (u > 0.005) {
        cairo_save(cr);
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, r, -M_PI / 2, -M_PI / 2 + 2 * M_PI * std::min(u, 0.9999));
        cairo_close_path(cr);
        cairo_clip(cr);
        drawIcon(cr, it.icon, cx - ic / 2, cy - ic / 2, ic, warn ? app.color("on_error") : app.color("on_primary"));
        cairo_restore(cr);
    }
}

std::vector<SideItem> sideItems(const Bar& bar) { return sideLayout(bar); }
bool sideModuleAtBottom(const App& app, const char* m, bool fallback) {
    const auto in = [&](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), m) != v.end(); };
    if (in(app.cfg.sideRight)) return true;
    if (in(app.cfg.sideLeft) || in(app.cfg.sideCenter)) return false;
    return fallback;
}
std::vector<Rect> islandRects(const Bar& bar) {
    const App& app = *bar.app;
    std::vector<Rect> out;
    if (sideBar(app)) {
        std::vector<std::pair<double, double>> spans;
        sideLayout(bar, &spans);
        for (const auto& [a, b] : spans) out.push_back({a - 8, 0, b - a + 16, double(bar.height)});
        return out;
    }
    std::map<std::string, int> copies;
    std::vector<std::string> keys[3];
    const std::vector<std::string> centerEff = centerModules(app);
    const std::vector<std::string>* regions[3] = {&app.cfg.layoutLeft, &centerEff, &app.cfg.layoutRight};
    for (int r : {0, 2, 1})
        for (const auto& m : *regions[r]) {
            if (moduleWidth(app, m) <= 0) continue;
            keys[r].push_back(repeatable(m) ? m + "#" + std::to_string(copies[m]++) : m);
        }
    for (int r = 0; r < 3; r++) {
        double x0 = 1e9, x1 = -1e9;
        size_t k = 0;
        for (const auto& m : *regions[r]) {
            if (moduleWidth(app, m) <= 0) continue;
            const std::string& key = keys[r][k++];
            if (!hasSlot(bar, key.c_str())) continue;
            x0 = std::min(x0, slotX(bar, key.c_str()) - cellInsetL(app, m));
            x1 = std::max(x1, slotX(bar, key.c_str()) + moduleWidth(app, m) + cellInsetR(app, m));
        }
        if (x0 < x1) { const double ip = groupHasCells(app.cfg) ? bodyPad(app) : kIslandPad; out.push_back({x0 - ip, 0, x1 - x0 + 2 * ip, double(bar.height)}); }
    }
    return out;
}

cairo_surface_t* makeShadowMaskOf(double w, double h, const std::function<void(cairo_t*, double)>& outline) {
    const int m = kLaunShadow, sw = static_cast<int>(w) + 2 * m, sh = static_cast<int>(h) + 2 * m;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, sw, sh);
    cairo_t* cr = mkcr(surf);
    outline(cr, m);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    unsigned char* px = cairo_image_surface_get_data(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    constexpr int r = 5;
    std::vector<unsigned char> line(std::max(sw, sh));
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < sh; y++) {
            unsigned char* row = px + y * stride;
            int sum = 0;
            for (int x = -r; x <= r; x++) sum += row[std::clamp(x, 0, sw - 1)] * (x >= 0 && x < sw);
            for (int x = 0; x < sw; x++) {
                line[x] = static_cast<unsigned char>(sum / (2 * r + 1));
                const int add = x + r + 1, sub = x - r;
                sum += (add < sw ? row[add] : 0) - (sub >= 0 ? row[sub] : 0);
            }
            std::memcpy(row, line.data(), sw);
        }
        for (int x = 0; x < sw; x++) {
            int sum = 0;
            for (int y = -r; y <= r; y++) sum += (y >= 0 && y < sh) ? px[y * stride + x] : 0;
            for (int y = 0; y < sh; y++) {
                line[y] = static_cast<unsigned char>(sum / (2 * r + 1));
                const int add = y + r + 1, sub = y - r;
                sum += (add < sh ? px[add * stride + x] : 0) - (sub >= 0 ? px[sub * stride + x] : 0);
            }
            for (int y = 0; y < sh; y++) px[y * stride + x] = line[y];
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

// blurredLayer: the card is on a layer surface the compositor blurs. On sway (SwayFX) the blur follows every pixel that is not fully
// transparent, so a soft shadow around the card would be blurred as a halo: there the card gets no shadow of its own (an empty mask).
// The desktop widgets (bottom layer, never blurred there) keep theirs. Every other compositor is unaffected.
cairo_surface_t* makeShadowMask(double w, double h, double radius, bool blurredLayer = true) {
    if (blurredLayer && wmKind() == Wm::Sway) {
        const int m = kLaunShadow;
        return cairo_image_surface_create(CAIRO_FORMAT_A8, static_cast<int>(w) + 2 * m, static_cast<int>(h) + 2 * m);
    }
    return makeShadowMaskOf(w, h, [&](cairo_t* cr, double m) { roundedRect(cr, m, m, w, h, radius); });
}

Hit hitTestSide(const Bar& bar, double x, double y) {
    for (const auto& it : sideItems(bar))
        if (it.r.contains(x, y)) return it.id;
    return HitNone;
}

void setBarRegion(Bar& bar) {
    const App& app = *bar.app;
    if (bar.segs == bar.regionSegs) return;
    bar.regionSegs = bar.segs;
    const bool side = sideBar(app);
    wl_region* region = wl_compositor_create_region(app.compositor);
    for (const auto& [a, b] : bar.segs) {
        const int from = std::max(0, static_cast<int>(a - kNotchEar)), len = static_cast<int>(std::min(b + kNotchEar, bar.width + 0.0) - from);
        if (side) wl_region_add(region, bar.contentY, from, kSideBarW, len);
        else wl_region_add(region, from, bar.contentY, len, kBarHeight);
    }
    wl_surface_set_input_region(bar.surface, region);
    wl_region_destroy(region);
}

Color frameColorOf(const App& app) { return app.cfg.frameColor == "black" ? Color{0, 0, 0} : app.color(app.cfg.frameColor.c_str()); }

struct BarInkScope {
    App& app;
    bool on = false;
    Color savedOn{}, savedVar{}, savedDim{};
    explicit BarInkScope(App& a) : app(a) {
        if (!a.cfg.barFollowsFrame) return;
        on = true;
        const Color fc = frameColorOf(a), light = a.color("on_surface"), dark = a.color("surface");
        auto lum = [](const Color& c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; };
        const Color ink = std::abs(lum(light) - lum(fc)) >= std::abs(lum(dark) - lum(fc)) ? light : dark;
        savedOn = a.color("on_surface");
        savedVar = a.color("on_surface_variant");
        savedDim = a.color("outline_variant");
        a.colors["on_surface"] = ink;
        a.colors["on_surface_variant"] = Color{ink.r * 0.8 + fc.r * 0.2, ink.g * 0.8 + fc.g * 0.2, ink.b * 0.8 + fc.b * 0.2};
        a.colors["outline_variant"] = Color{ink.r * 0.5 + fc.r * 0.5, ink.g * 0.5 + fc.g * 0.5, ink.b * 0.5 + fc.b * 0.5};
    }
    ~BarInkScope() {
        if (!on) return;
        app.colors["on_surface"] = savedOn;
        app.colors["on_surface_variant"] = savedVar;
        app.colors["outline_variant"] = savedDim;
    }
};

bool gIslandBodyOnly = false;

void paintBarBody(cairo_t* cr, const Bar& bar) {
    const App& app = *bar.app;
    const double W = bar.width, H = bar.height;
    const std::string& style = app.cfg.barStyle;
    if (app.cfg.barFollowsFrame) setSource(cr, frameColorOf(app));
    else setSource(cr, app.color("surface"), app.cfg.barOpacity / 100.0);
    if (style == "float") {
        roundedRect(cr, 0, 0, W, H, kFloatRadius);
        cairo_fill(cr);
    } else if (style == "island") {
        const bool dyn = hasSlot(bar, "island");
        const double ix = dyn ? slotX(bar, "island") : 0, iw = dyn ? islandW(app) : 0;
        for (const Rect& r : islandRects(bar)) {
            const bool isIsland = dyn && r.x <= ix + 0.5 && ix + iw <= r.x + r.w + 0.5;
            if (gIslandBodyOnly && !isIsland) continue;
            if (isIsland && app.ic.hidesBar()) continue;
            if (isIsland) {
                roundedRect(cr, r.x, r.y, r.w, r.h, r.h / 2.0);
            } else
                roundedRect(cr, r.x, r.y, r.w, r.h, r.h * app.cfg.islandRounding / 100.0);
            cairo_fill(cr);
        }
    } else if (style == "hug") {
        const bool atBottom = barAtEnd(app);
        const double S = app.hugRadius + app.hugGap;
        cairo_translate(cr, 0, bar.contentY);
        cairo_rectangle(cr, 0, 0, W, H);
        cairo_fill(cr);
        for (const bool left : {true, false}) {
            const double sx = left ? 0 : W - S;
            const double sy = atBottom ? -S : H;
            const double cx = left ? S : W - S;
            const double cy = atBottom ? -S : H + S;
            cairo_save(cr);
            cairo_rectangle(cr, sx, sy, S, S);
            cairo_clip(cr);
            cairo_rectangle(cr, sx, sy, S, S);
            cairo_arc(cr, cx, cy, S, 0, 2 * M_PI);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        cairo_translate(cr, 0, -bar.contentY);
    } else if (notchLike(style)) {
        const double e = kNotchEar, r0 = std::min(kFloatRadius + 2, H / 2.0 - 1);
        cairo_save(cr);
        cairo_translate(cr, 0, bar.contentY);
        if (barAtEnd(app)) { cairo_translate(cr, 0, H); cairo_scale(cr, 1, -1); }
        for (const auto& [x0, x1] : bar.segs) {
            const bool isIslandSeg = hasSlot(bar, "island") && x0 <= slotX(bar, "island") + 0.5 && slotX(bar, "island") + islandW(app) <= x1 + 0.5;
            if (gIslandBodyOnly && !isIslandSeg) continue;
            if (isIslandSeg && app.ic.hidesBar()) continue;
            const bool flushL = x0 <= 0.5, flushR = x1 >= W - 0.5;
            const double r = r0;
            if (flushL) { cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, H); }
            else {
                cairo_move_to(cr, x0 - e, 0);
                cairo_arc(cr, x0 - e, e, e, -M_PI / 2, 0);
                cairo_line_to(cr, x0, H - r);
                cairo_arc_negative(cr, x0 + r, H - r, r, M_PI, M_PI / 2);
            }
            if (flushR) { cairo_line_to(cr, W, H); cairo_line_to(cr, W, 0); }
            else {
                cairo_line_to(cr, x1 - r, H);
                cairo_arc_negative(cr, x1 - r, H - r, r, M_PI / 2, 0);
                cairo_line_to(cr, x1, e);
                cairo_arc(cr, x1 + e, e, e, M_PI, 1.5 * M_PI);
            }
            cairo_close_path(cr);
            cairo_fill(cr);
            const double S = app.hugRadius + app.hugGap;
            for (const bool left : {true, false}) {
                if (style != "split" || (left ? !flushL : !flushR)) continue;
                const double sx = left ? 0 : W - S, cx = left ? S : W - S;
                cairo_save(cr);
                cairo_rectangle(cr, sx, H, S, S);
                cairo_clip(cr);
                cairo_rectangle(cr, sx, H, S, S);
                cairo_arc(cr, cx, H + S, S, 0, 2 * M_PI);
                cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
                cairo_fill(cr);
                cairo_restore(cr);
            }
        }
        cairo_restore(cr);
    } else {
        cairo_rectangle(cr, 0, 0, W, H);
        cairo_fill(cr);
    }
}

void blurMask(std::vector<unsigned char>& a, int w, int h, int radius, int passes) {
    std::vector<unsigned char> tmp(a.size());
    const int win = 2 * radius + 1;
    for (int pass = 0; pass < passes; pass++) {
        for (int y = 0; y < h; y++) {
            const unsigned char* in = &a[static_cast<size_t>(y) * w];
            unsigned char* out = &tmp[static_cast<size_t>(y) * w];
            int sum = 0;
            for (int x = 0; x < std::min(radius, w); x++) sum += in[x];
            for (int x = 0; x < w; x++) {
                if (x + radius < w) sum += in[x + radius];
                if (x - radius - 1 >= 0) sum -= in[x - radius - 1];
                out[x] = static_cast<unsigned char>(sum / win);
            }
        }
        for (int x = 0; x < w; x++) {
            int sum = 0;
            for (int y = 0; y < std::min(radius, h); y++) sum += tmp[static_cast<size_t>(y) * w + x];
            for (int y = 0; y < h; y++) {
                if (y + radius < h) sum += tmp[static_cast<size_t>(y + radius) * w + x];
                if (y - radius - 1 >= 0) sum -= tmp[static_cast<size_t>(y - radius - 1) * w + x];
                a[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(sum / win);
            }
        }
    }
}

void paintBarShadow(cairo_t* cr, const Bar& bar) {
    const App& app = *bar.app;
    cairo_surface_t* target = cairo_get_target(cr);
    if (cairo_surface_get_type(target) != CAIRO_SURFACE_TYPE_IMAGE) return;
    const int w = cairo_image_surface_get_width(target), h = cairo_image_surface_get_height(target);
    const double opacity = app.cfg.barFollowsFrame ? 1.0 : app.cfg.barOpacity / 100.0;
    if (w <= 0 || h <= 0 || opacity < 0.02) return;
    cairo_surface_t* silhouette = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    {
        cairo_t* m = cairo_create(silhouette);
        cairo_matrix_t mat;
        cairo_get_matrix(cr, &mat);
        cairo_set_matrix(m, &mat);
        if (app.cfg.barStyle != "hug" && !notchLike(app.cfg.barStyle)) cairo_translate(m, 0, bar.contentY);
        paintBarBody(m, bar);
        cairo_destroy(m);
    }
    cairo_surface_flush(silhouette);
    const int stride = cairo_image_surface_get_stride(silhouette);
    const unsigned char* px = cairo_image_surface_get_data(silhouette);
    std::vector<unsigned char> body(static_cast<size_t>(w) * h), soft(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const double a = std::min(1.0, px[static_cast<size_t>(y) * stride + x] / 255.0 / opacity);
            body[static_cast<size_t>(y) * w + x] = soft[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(a * 255 + 0.5);
        }
    }
    cairo_surface_destroy(silhouette);
    blurMask(soft, w, h, kBarShadowBlur / 3, 3);
    const int dir = barAtEnd(app) ? -1 : 1;
    const int ox = sideBar(app) ? dir * kBarShadowOffset : 0, oy = sideBar(app) ? 0 : dir * kBarShadowOffset;
    cairo_surface_t* shadow = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    const int sstride = cairo_image_surface_get_stride(shadow);
    unsigned char* out = cairo_image_surface_get_data(shadow);
    const double strength = kBarShadowAlpha * opacity;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int sx = x - ox, sy = y - oy;
            const double s = sx >= 0 && sx < w && sy >= 0 && sy < h ? soft[static_cast<size_t>(sy) * w + sx] / 255.0 : 0.0;
            const double cut = 1.0 - body[static_cast<size_t>(y) * w + x] / 255.0;
            out[static_cast<size_t>(y) * sstride + x] = static_cast<unsigned char>(s * strength * cut * 255 + 0.5);
        }
    }
    cairo_surface_mark_dirty(shadow);
    cairo_save(cr);
    cairo_identity_matrix(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_mask_surface(cr, shadow, 0, 0);
    cairo_restore(cr);
    cairo_surface_destroy(shadow);
}

void drawGroupCells(cairo_t* cr, const Bar& bar) {
    const App& app = *bar.app;
    if (!groupHasCells(app.cfg)) return;
    const double h = kQuickBox + 2 * kGroupPad, y = (bar.height - h) / 2.0;
    for (const GroupSpan& g : bar.groups) {
        roundedRectCorners(cr, g.x0, y, g.x1 - g.x0, h, g.rl, g.rr, g.rr, g.rl);
        if (hexColorText(app.cfg.groupColor)) {
            unsigned rgb = 0;
            std::sscanf(app.cfg.groupColor.c_str() + 1, "%x", &rgb);
            setSource(cr, Color{((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0});
        } else setSource(cr, app.color(app.cfg.groupColor.c_str()));
        cairo_fill(cr);
    }
}

void drawBarBackground(cairo_t* cr, const Bar& bar) {
    const App& app = *bar.app;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    const bool islandOnly = !app.cfg.barBackground && islandActive(app) && hasSlot(bar, "island");
    if (!app.cfg.barBackground && !islandOnly) return;
    cairo_save(cr);
    if (app.cfg.barStyle != "hug" && !notchLike(app.cfg.barStyle)) cairo_translate(cr, 0, bar.contentY);
    gIslandBodyOnly = islandOnly;
    paintBarBody(cr, bar);
    gIslandBodyOnly = false;
    cairo_restore(cr);
}

void refreshBarShadow(Bar& bar) {
    if (!bar.shLayer || bar.shW == 0 || bar.shH == 0 || !bar.configured) return;
    const App& app = *bar.app;
    std::string sig = std::to_string(bar.shW) + "x" + std::to_string(bar.shH) + ":" + std::to_string(bar.extra) + ":" + std::to_string(bar.contentY) + ":" +
                      app.cfg.barStyle + ":" + app.cfg.barPosition + ":" + std::to_string(app.cfg.barOpacity) + ":" + std::to_string(app.cfg.barFollowsFrame) + ":" +
                      std::to_string(app.cfg.barBackground) + ":" + std::to_string(bar.width) + ":" + std::to_string(bar.height);
    for (const auto& [a, b] : bar.segs) sig += ":" + std::to_string(static_cast<int>(a)) + "-" + std::to_string(static_cast<int>(b));
    if (app.cfg.barStyle == "island")
        for (const Rect& r : islandRects(bar)) sig += ":" + std::to_string(static_cast<int>(r.x)) + "," + std::to_string(static_cast<int>(r.w));
    if (sig == bar.shSig) return;
    bar.shSig = sig;
    const int w = static_cast<int>(bar.shW), h = static_cast<int>(bar.shH), stride = w * 4, size = stride * h;
    const int fd = memfd_create("end4pC-bar-shadow", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { if (fd >= 0) close(fd); return; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return; }
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, w, h, stride);
    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (sideBar(app)) {
        cairo_matrix_t m;
        cairo_matrix_init(&m, 0, 1, 1, 0, 0, 0);
        cairo_set_matrix(cr, &m);
    }
    if (app.cfg.barBackground) paintBarShadow(cr, bar);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, nullptr);
    wl_surface_attach(bar.shSurface, buffer, 0, 0);
    wl_surface_damage_buffer(bar.shSurface, 0, 0, w, h);
    wl_surface_commit(bar.shSurface);
}

void loadAvatar(App& app) {
    app.avatarTried = true;
    std::string pic;
    {
        std::ifstream f(homeDir() + "/.config/illogical-impulse/config.json");
        std::stringstream ss;
        ss << f.rdbuf();
        readStringKey(ss.str(), "avatarPicture", pic);
    }
    for (const std::string& c : {app.cfg.avatarPicture, pic, homeDir() + "/.face", homeDir() + "/.face.icon"}) {
        if (c.empty() || access(c.c_str(), R_OK) != 0) continue;
        constexpr int size = 40;
        int iw = 0, ih = 0;
        if (!gdk_pixbuf_get_file_info(c.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) continue;
        const double sc = std::max(double(size) / iw, double(size) / ih);
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(c.c_str(), std::max(size, int(iw * sc + 0.5)), std::max(size, int(ih * sc + 0.5)), FALSE, nullptr);
        if (!pb) continue;
        cairo_surface_t* img = surfaceFromPixbuf(pb);
        app.avatar = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = mkcr(app.avatar);
        cairo_set_source_surface(cr, img, (size - gdk_pixbuf_get_width(pb)) / 2.0, (size - gdk_pixbuf_get_height(pb)) / 2.0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(img);
        g_object_unref(pb);
        return;
    }
}

void popWithShadow(cairo_t* cr, const App& app) {
    cairo_identity_matrix(cr);
    cairo_pattern_t* content = cairo_pop_group(cr);
    if (!app.cfg.barBackground) {
        static const double off[][3] = {{0, 1.5, 0.30}, {-1.5, 1.5, 0.18}, {1.5, 1.5, 0.18}, {0, 3, 0.16}, {-1, 0, 0.12}, {1, 0, 0.12}, {0, -1, 0.10}};
        for (const auto& o : off) {
            cairo_matrix_t m;
            cairo_matrix_init_translate(&m, -o[0], -o[1]);
            cairo_pattern_set_matrix(content, &m);
            cairo_set_source_rgba(cr, 0, 0, 0, o[2]);
            cairo_mask(cr, content);
        }
        cairo_matrix_t id;
        cairo_matrix_init_identity(&id);
        cairo_pattern_set_matrix(content, &id);
    }
    cairo_set_source(cr, content);
    cairo_paint(cr);
    cairo_pattern_destroy(content);
}

std::string weatherBarIcon(const App& app) { std::string label, ic; weatherDescribe(app.barWeather.code, app.barWeather.day, label, ic); return ic; }
std::string weatherBarText(const App& app) { return std::to_string(app.barWeather.temp) + "\xC2\xB0"; }
std::string aiBarText(const App& app, int kind) { return std::to_string(static_cast<int>(std::lround(std::clamp(app.barAi[kind].pct, 0.0, 1.0) * 100))) + "%"; }
Color aiBarColor(const App& app, int kind) {
    const App::BarAi& a = app.barAi[kind];
    if (a.pct >= 0.9) return app.color("error");
    return app.color((kind == 0 ? a.sessions > 0 : a.seen) ? "on_surface" : "on_surface_variant");
}
const char* aiBarIcon(int kind) { return kind == 0 ? "claude" : "openai"; }
Color aiM3Color(const App& app, int kind) {
    const App::BarAi& a = app.barAi[kind];
    return app.color((kind == 0 ? a.sessions > 0 : a.seen) ? "on_surface_variant" : "outline");
}

bool framedStyle(const std::string& style) { return m3ButtonLook(style) || style == "pill" || style == "outline" || style == "badge" || style == "tonal" || style == "square"; }
std::string weatherBarLabel(const App& app) { std::string label, ic; weatherDescribe(app.barWeather.code, app.barWeather.day, label, ic); return label; }

double weatherM3Paint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy) {
    constexpr double textPad = kModulePad / 2, gap = 6;
    constexpr int iconSize = 16;
    PangoFontDescription* f = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, weatherBarText(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double inset = (kM3Box - kM3Disc) / 2;
    const double w = kModulePad + textPad + tw + gap + kM3Disc + inset;
    if (cr) {
        roundedRect(cr, x, cy - kM3Box / 2, w, kM3Box, kM3Box / 2);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        const double textX = x + kModulePad + textPad, discX = textX + tw + gap;
        setSource(cr, app.color("on_background"));
        cairo_move_to(cr, textX, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        cairo_new_sub_path(cr);
        cairo_arc(cr, discX + kM3Disc / 2.0, cy, kM3Disc / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        drawIcon(cr, weatherBarIcon(app), discX + (kM3Disc - iconSize) / 2.0, cy - iconSize / 2.0, iconSize, app.color("on_primary"));
    }
    pango_font_description_free(f);
    return w;
}

double launcherM3Paint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, bool hot) {
    constexpr double gap = 6, disc = kM3Disc;
    constexpr int iconSize = 16;
    const std::string label = i18n::tr("Apps");
    PangoFontDescription* f = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, label.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double w = kM3Inset + disc + gap + tw + kModulePad;
    if (cr) {
        roundedRect(cr, x, cy - kM3Box / 2, w, kM3Box, kM3Box / 2);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        if (hot) {
            roundedRect(cr, x, cy - kM3Box / 2, w, kM3Box, kM3Box / 2);
            setSource(cr, app.color("on_primary_container"), 0.08);
            cairo_fill(cr);
        }
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + kM3Inset + disc / 2.0, cy, disc / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        drawIcon(cr, "magnifer", x + kM3Inset + (disc - iconSize) / 2.0, cy - iconSize / 2.0, iconSize, app.color("on_primary"));
        setSource(cr, app.color("on_primary_container"));
        cairo_move_to(cr, x + kM3Inset + disc + gap, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(f);
    return w;
}
double launcherM3W(const App& app) {
    static std::string cachedLabel;
    static double cachedW = 0;
    const std::string label = i18n::tr("Apps");
    if (label != cachedLabel || cachedW <= 0) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cairo_t* cr = mkcr(surf);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        cachedW = launcherM3Paint(nullptr, app, layout, 0, 0, false);
        cachedLabel = label;
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
    return cachedW;
}

double weatherBarPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, bool hot = false) {
    constexpr double ic = 20, baseGap = 5;
    const std::string style = widgetStyle(app, "weather");
    if (style == "m3") return weatherM3Paint(cr, app, layout, x, cy);
    const bool framed = framedStyle(style), stacked = style == "stacked";
    const double gap = stacked ? 9 : baseGap;
    const bool showIcon = style != "temp", showText = style != "icon";
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
    PangoFontDescription* small = pango_font_description_from_string("Sans 8");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, weatherBarText(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    int lw = 0, lh = 0;
    if (stacked) {
        pango_layout_set_font_description(layout, small);
        pango_layout_set_text(layout, weatherBarLabel(app).c_str(), -1);
        pango_layout_get_pixel_size(layout, &lw, &lh);
    }
    const double textW = stacked ? std::max(tw, lw) : tw;
    const double contentW = (showIcon ? ic : 0) + (showIcon && showText ? gap : 0) + (showText ? textW : 0);
    const double w = contentW + (framed ? 2 * kModulePad : 0);
    if (cr) {
        Color ink = app.color("on_surface");
        double left = x;
        if (framed) {
            ink = styleFrame(cr, app, style, x, cy - kQuickBox / 2, w, kQuickBox, hot);
            left += kModulePad;
        }
        if (showIcon) drawIcon(cr, weatherBarIcon(app), left, cy - ic / 2, 20, ink);
        const double tx = left + (showIcon ? ic + gap : 0);
        pango_layout_set_font_description(layout, f);
        pango_layout_set_text(layout, weatherBarText(app).c_str(), -1);
        setSource(cr, ink);
        if (stacked) {
            constexpr double kStackOverlap = 3;
            const double block = th + lh - kStackOverlap;
            cairo_move_to(cr, tx, cy - block / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_font_description(layout, small);
            pango_layout_set_text(layout, weatherBarLabel(app).c_str(), -1);
            setSource(cr, ink, 0.8);
            cairo_move_to(cr, tx, cy - block / 2.0 + th - kStackOverlap);
            pango_cairo_show_layout(cr, layout);
        } else if (showText) {
            cairo_move_to(cr, tx, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
    }
    pango_font_description_free(f);
    pango_font_description_free(small);
    return w;
}

double aiM3Content(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, int kind, const Color& ink) {
    constexpr double ic = 16, gap = 5;
    PangoFontDescription* f = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    const bool noData = kind == 1 && !app.barAi[kind].seen;
    const std::string text = noData ? "\xE2\x80\x94" : aiBarText(app, kind);
    pango_layout_set_text(layout, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    if (cr) {
        drawIcon(cr, aiBarIcon(kind), x, cy - ic / 2, 16, ink);
        setSource(cr, ink);
        cairo_move_to(cr, x + ic + gap, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    pango_font_description_free(f);
    return ic + gap + tw;
}
double aiPillContent(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, int kind, const std::string& style, const Color& ink) {
    if (style == "m3") return aiM3Content(cr, app, layout, x, cy, kind, ink);
    constexpr double ic = 16, gap = 4, ringD = 24, capW = 44, capH = 16;
    const double pct = std::clamp(app.barAi[kind].pct, 0.0, 1.0);
    const std::string num = aiBarText(app, kind);
    pango_layout_set_text(layout, num.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    if (style == "bar") {
        const double bx = x + ic + gap, by = cy - capH / 2.0;
        if (cr) {
            drawIcon(cr, aiBarIcon(kind), x, cy - ic / 2, 16, ink);
            roundedRect(cr, bx, by, capW, capH, capH / 2);
            setSource(cr, app.color("surface_container_highest"));
            cairo_fill(cr);
            roundedRect(cr, bx, by, std::max(capH, capW * pct), capH, capH / 2);
            setSource(cr, ink, 0.35);
            cairo_fill(cr);
            setSource(cr, ink);
            cairo_move_to(cr, bx + (capW - tw) / 2.0, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
        return ic + gap + capW;
    }
    if (style == "ring") {
        if (cr) {
            const double rcx = x + ringD / 2.0, r = ringD / 2.0 - 1.5;
            cairo_set_line_width(cr, 2.5);
            cairo_new_sub_path(cr);
            cairo_arc(cr, rcx, cy, r, 0, 2 * M_PI);
            setSource(cr, app.color("outline_variant"));
            cairo_stroke(cr);
            if (pct > 0) {
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_new_sub_path(cr);
                cairo_arc(cr, rcx, cy, r, -M_PI / 2, -M_PI / 2 + 2 * M_PI * pct);
                setSource(cr, ink);
                cairo_stroke(cr);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
            }
            drawIcon(cr, aiBarIcon(kind), rcx - 7, cy - 7, 14, ink);
            setSource(cr, ink);
            cairo_move_to(cr, x + ringD + gap, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
        return ringD + gap + tw;
    }
    if (style == "text") {
        const std::string name = kind == 0 ? "Claude" : "Codex";
        pango_layout_set_text(layout, name.c_str(), -1);
        int nw = 0, nh = 0;
        pango_layout_get_pixel_size(layout, &nw, &nh);
        if (cr) {
            setSource(cr, ink, 0.75);
            cairo_move_to(cr, x, cy - nh / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_text(layout, num.c_str(), -1);
            setSource(cr, ink);
            cairo_move_to(cr, x + nw + gap, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
        return nw + gap + tw;
    }
    if (cr) {
        drawIcon(cr, aiBarIcon(kind), x, cy - ic / 2, 16, ink);
        setSource(cr, ink);
        cairo_move_to(cr, x + ic + gap, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    return ic + gap + tw;
}
double aiBarPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, int hotKind = -1, double* firstW = nullptr) {
    const std::string style = widgetStyle(app, "aiusage");
    const bool framed = framedStyle(style), m3 = style == "m3";
    const double pad = framed ? kModulePad : 0, sep = framed ? 6 : 10;
    const double outer = m3 ? kModulePad : 0;
    if (m3 && cr) {
        const double total = aiBarPaint(nullptr, app, layout, 0, cy);
        roundedRect(cr, x, cy - kM3Box / 2, total, kM3Box, kM3Box / 2);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
    }
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
    pango_layout_set_font_description(layout, f);
    const std::vector<int> shown = aiShown(app);
    double w = outer;
    for (size_t i = 0; i < shown.size(); i++) {
        const int kind = shown[i];
        if (i > 0) w += sep;
        const double pillW = aiPillContent(nullptr, app, layout, 0, cy, kind, style, Color{}) + 2 * pad;
        if (cr) {
            Color ink = m3 ? aiM3Color(app, kind) : aiBarColor(app, kind);
            if (framed) {
                ink = styleFrame(cr, app, style, x + w, cy - kQuickBox / 2, pillW, kQuickBox, hotKind == kind);
                if (app.barAi[kind].pct >= 0.9 && style != "badge" && style != "tonal") ink = app.color("error");
            }
            aiPillContent(cr, app, layout, x + w + pad, cy, kind, style, ink);
        }
        w += pillW;
        if (i == 0 && firstW) *firstW = w + sep / 2.0;
    }
    pango_font_description_free(f);
    return w + outer;
}
void dividerPaint(cairo_t* cr, const App& app, double cx, double cy, bool tall, bardivider::Style style) {
    if (style == bardivider::Style::Space) return;
    const Color c = app.color("on_surface_variant");
    if (style == bardivider::Style::Dot) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, 2.5, 0, 2 * M_PI);
        setSource(cr, c, 0.8);
        cairo_fill(cr);
        return;
    }
    constexpr double len = 22, th = 2;
    cairo_pattern_t* grad = tall ? cairo_pattern_create_linear(0, cy - len / 2, 0, cy + len / 2) : cairo_pattern_create_linear(cx - len / 2, 0, cx + len / 2, 0);
    cairo_pattern_add_color_stop_rgba(grad, 0, c.r, c.g, c.b, 0);
    cairo_pattern_add_color_stop_rgba(grad, 0.5, c.r, c.g, c.b, 0.75);
    cairo_pattern_add_color_stop_rgba(grad, 1, c.r, c.g, c.b, 0);
    roundedRect(cr, cx - (tall ? th : len) / 2, cy - (tall ? len : th) / 2, tall ? th : len, tall ? len : th, 1);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
}
void vizPaint(cairo_t* cr, const App& app, double cx, double cy, bool vertical, bool mirrored) {
    const std::string look = widgetStyle(app, "visualizer");
    if (vertical && look != "dots") {
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        cairo_rotate(cr, M_PI / 2);
        vizPaint(cr, app, 0, 0, false, mirrored);
        cairo_restore(cr);
        return;
    }
    const bool m3 = look == "m3";
    const double maxLen = m3 ? kM3Box - 2 * kVizM3Pad : 40 * 0.7;
    const bool playing = app.musicStatus == "Playing" && !app.vizShown.empty();
    const double step = kVizDot + kVizGap, width = kVizBars * step - kVizGap, start = -width / 2;
    auto level = [&](int i, int n) {
        if (!playing) return 0.0;
        const size_t src = static_cast<size_t>(mirrored ? n - 1 - i : i);
        return src < app.vizShown.size() ? std::clamp(app.vizShown[src], 0.0, 1.0) : 0.0;
    };
    if (look == "winamp") {
        constexpr int segs = 5;
        constexpr double segH = 2, segGap = 1, pitch = segH + segGap, up = segs * pitch - segGap;
        static std::array<std::array<double, kVizBars>, 2> peaks{};
        static std::array<std::chrono::steady_clock::time_point, 2> last{};
        const size_t side = mirrored ? 1 : 0;
        const auto now = std::chrono::steady_clock::now();
        const double dt = last[side].time_since_epoch().count() == 0 ? 0.0 : std::min(0.1, std::chrono::duration<double>(now - last[side]).count());
        last[side] = now;
        const Color low = app.color("primary"), mid = app.color("secondary"), high = app.color("tertiary");
        auto led = [&](int s) {
            const double f = static_cast<double>(s) / (segs - 1), k = f < 0.5 ? f / 0.5 : (f - 0.5) / 0.5;
            const Color& a = f < 0.5 ? low : mid;
            const Color& b = f < 0.5 ? mid : high;
            return Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k};
        };
        const double base = cy, x0 = cx + start;
        for (int i = 0; i < kVizBars; i++) {
            const double v = level(i, kVizBars);
            peaks[side][i] = std::max(peaks[side][i] - dt * 0.45, v);
            const int lit = static_cast<int>(std::lround(v * segs)), cap = std::min(segs - 1, static_cast<int>(peaks[side][i] * segs));
            const double x = x0 + i * step;
            for (int sg = 0; sg < segs; sg++) {
                const bool on = playing && (sg < lit || (sg == cap && cap >= lit));
                const Color c = led(sg);
                const double alpha = on ? 1.0 : 0.1;
                cairo_rectangle(cr, x, base - 0.5 - sg * pitch - segH, kVizDot, segH);
                setSource(cr, c, alpha);
                cairo_fill(cr);
                cairo_rectangle(cr, x, base + 0.5 + sg * pitch, kVizDot, segH);
                setSource(cr, c, (on ? 0.4 : 0.05) * (1.0 - 0.7 * sg / (segs - 1)));
                cairo_fill(cr);
            }
        }
        (void)up;
        return;
    }
    if (look == "reflect") {
        constexpr double up = 15, refl = 0.5;
        const double base = cy + (up - up * refl) / 2.0;
        cairo_pattern_t* fade = cairo_pattern_create_linear(0, base, 0, base + up * refl);
        const Color pc = app.color("primary");
        cairo_pattern_add_color_stop_rgba(fade, 0, pc.r, pc.g, pc.b, 0.45);
        cairo_pattern_add_color_stop_rgba(fade, 1, pc.r, pc.g, pc.b, 0);
        for (int i = 0; i < kVizBars; i++) {
            const double v = level(i, kVizBars), h = std::max(1.0, v * up), x = cx + start + i * step;
            cairo_rectangle(cr, x, base - h, kVizDot, h);
            setSource(cr, pc, playing ? 0.55 + 0.45 * v : 0.3);
            cairo_fill(cr);
            cairo_rectangle(cr, x, base, kVizDot, h * refl);
            cairo_set_source(cr, fade);
            cairo_fill(cr);
        }
        cairo_pattern_destroy(fade);
        cairo_rectangle(cr, cx + start, base, width, 0.5);
        setSource(cr, app.color("on_surface"), 0.35);
        cairo_fill(cr);
        return;
    }
    if (m3) {
        const double boxW = width + 2 * kVizM3Pad;
        roundedRect(cr, cx - boxW / 2, cy - kM3Box / 2, boxW, kM3Box, kM3Box / 2);
        setSource(cr, app.color("secondary_container"));
        cairo_fill(cr);
    }
    setSource(cr, vertical ? app.color("primary") : app.color(m3 ? "on_secondary_container" : "on_surface"), playing ? 0.85 : 0.3);
    for (int i = 0; i < kVizBars; i++) {
        const double len = playing ? std::max(kVizDot, level(i, kVizBars) * maxLen) : kVizDot;
        const double o = start + i * step;
        if (vertical) roundedRect(cr, cx - len / 2, cy + o, len, kVizDot, kVizDot / 2);
        else roundedRect(cr, cx + o, cy - len / 2, kVizDot, len, kVizDot / 2);
        cairo_fill(cr);
    }
}
constexpr int kUpdatesWarn = 25, kUpdatesAlert = 100;
std::string updatesText(const App& app) {
    if (app.updatesChecking) return "…";
    return app.updatesTotal < 0 ? "-" : std::to_string(app.updatesTotal);
}
void updatesTone(const App& app, const char*& role, const char*& on) {
    if (app.updatesTotal >= kUpdatesAlert) { role = "error"; on = "on_error"; }
    else if (app.updatesTotal >= kUpdatesWarn) { role = "tertiary"; on = "on_tertiary"; }
    else { role = nullptr; on = "on_primary"; }
}
double updatesBarPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double cy, bool hot) {
    constexpr double ic = 20, gap = 5, disc = kM3Disc;
    constexpr int discIconSize = 16;
    const std::string style = widgetStyle(app, "updates");
    const bool m3 = style == "m3", framed = framedStyle(style);
    const char *role = nullptr, *on = nullptr;
    updatesTone(app, role, on);
    PangoFontDescription* f = pango_font_description_from_string(m3 ? "Sans" : "Sans Bold 10");
    if (m3) pango_font_description_set_absolute_size(f, 15 * PANGO_SCALE);
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, updatesText(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double w = m3 ? kM3Inset + disc + gap + tw + kModulePad : ic + gap + tw + (framed ? 2 * kModulePad : 0);
    if (cr) {
        if (m3) {
            roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
            setSource(cr, app.color("primary_container"));
            cairo_fill(cr);
            if (hot) {
                roundedRect(cr, x, cy - kM3Box / 2.0, w, kM3Box, kM3Box / 2.0);
                setSource(cr, app.color("on_primary_container"), 0.08);
                cairo_fill(cr);
            }
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + kM3Inset + disc / 2.0, cy, disc / 2.0, 0, 2 * M_PI);
            setSource(cr, app.color(role ? role : "primary"));
            cairo_fill(cr);
            drawIcon(cr, "download-square", x + kM3Inset + (disc - discIconSize) / 2.0, cy - discIconSize / 2.0, discIconSize, app.color(on));
            setSource(cr, app.color("on_primary_container"));
            cairo_move_to(cr, x + kM3Inset + disc + gap, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        } else {
            Color ink = app.color("on_surface");
            double left = x;
            if (framed) {
                ink = styleFrame(cr, app, style, x, cy - kQuickBox / 2.0, w, kQuickBox, hot);
                left += kModulePad;
            }
            drawIcon(cr, "download-square", left, cy - ic / 2.0, ic, role ? app.color(role) : ink);
            setSource(cr, ink);
            cairo_move_to(cr, left + ic + gap, cy - th / 2.0);
            pango_cairo_show_layout(cr, layout);
        }
    }
    pango_font_description_free(f);
    return w;
}
double updatesBarW(const App& app) {
    static std::string cachedKey;
    static double cachedW = 0;
    const std::string key = updatesText(app) + "|" + widgetStyle(app, "updates");
    if (key != cachedKey || cachedW <= 0) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
        cairo_t* cr = mkcr(surf);
        PangoLayout* layout = pango_cairo_create_layout(cr);
        cachedW = updatesBarPaint(nullptr, app, layout, 0, 0, false);
        cachedKey = key;
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
    return cachedW;
}
void updatesSidePaint(cairo_t* cr, const App& app, PangoLayout* layout, const Rect& r, bool hot) {
    const std::string style = widgetStyle(app, "updates");
    const bool m3 = style == "m3";
    constexpr double kIconBox = 24, kGap = 4, kDiscIcon = 16;
    const double box = m3 ? kQuickBox : kIconBox;
    const char *role = nullptr, *on = nullptr;
    updatesTone(app, role, on);
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 9");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, updatesText(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double top = r.y + (r.h - (box + kGap + th)) / 2.0;
    Color ink = app.color("on_surface");
    if (m3) {
        roundedRect(cr, r.x, r.y, r.w, r.h, 16);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, r.x + r.w / 2.0, top + box / 2.0, box / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color(role ? role : "primary"));
        cairo_fill(cr);
        drawIcon(cr, "download-square", r.x + (r.w - kDiscIcon) / 2.0, top + (box - kDiscIcon) / 2.0, kDiscIcon, app.color(on));
        ink = app.color("on_primary_container");
    } else {
        ink = styleFrame(cr, app, style, r.x, r.y, r.w, r.h, hot, true);
        drawIcon(cr, "download-square", r.x + (r.w - kIconBox) / 2, top, kIconBox, role ? app.color(role) : ink);
    }
    setSource(cr, ink);
    cairo_move_to(cr, r.x + (r.w - tw) / 2.0, top + box + kGap);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(f);
}
void weatherSidePaint(cairo_t* cr, const App& app, PangoLayout* layout, const Rect& r, bool hot) {
    const std::string style = widgetStyle(app, "weather");
    const bool m3 = style == "m3";
    constexpr double kIconBox = 24, kGap = 4;
    const double box = m3 ? kQuickBox : kIconBox;
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 9");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, weatherBarText(app).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const double top = r.y + (r.h - (box + kGap + th)) / 2.0;
    Color ink = app.color("on_surface");
    if (m3) {
        roundedRect(cr, r.x, r.y, r.w, r.h, 16);
        setSource(cr, app.color("primary_container"));
        cairo_fill(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, r.x + r.w / 2.0, top + box / 2.0, box / 2.0, 0, 2 * M_PI);
        setSource(cr, app.color("primary"));
        cairo_fill(cr);
        drawIcon(cr, weatherBarIcon(app), r.x + (r.w - 16) / 2.0, top + (box - 16) / 2.0, 16, app.color("on_primary"));
        ink = app.color("on_background");
    } else {
        ink = styleFrame(cr, app, style, r.x, r.y, r.w, r.h, hot, true);
        drawIcon(cr, weatherBarIcon(app), r.x + (r.w - kIconBox) / 2, top, kIconBox, ink);
    }
    setSource(cr, ink);
    cairo_move_to(cr, r.x + (r.w - tw) / 2.0, top + box + kGap);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(f);
}
void aiSidePaint(cairo_t* cr, const App& app, PangoLayout* layout, const Rect& r, int kind, bool hot) {
    const std::string style = widgetStyle(app, "aiusage");
    Color ink = style == "m3" ? aiM3Color(app, kind) : aiBarColor(app, kind);
    if (style != "m3") {
        const Color framed = styleFrame(cr, app, style, r.x, r.y, r.w, r.h, hot, true);
        if (genericFrame(style)) ink = app.barAi[kind].pct >= 0.9 && style != "badge" && style != "tonal" ? app.color("error") : framed;
    }
    drawIcon(cr, aiBarIcon(kind), r.x + (r.w - 18) / 2, r.y + 2, 18, ink);
    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 8");
    pango_layout_set_font_description(layout, f);
    pango_layout_set_text(layout, aiBarText(app, kind).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, ink);
    cairo_move_to(cr, r.x + (r.w - tw) / 2.0, r.y + 24);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(f);
}
void drawSideM3(cairo_t* cr, const App& app, Hit id, const Rect& r) {
    constexpr double kSideBox = 40;
    const bool network = id == HitNetwork;
    const bool speed = network && netSpeedShown(app);
    const bool iconOnly = !speed;
    const char* icon = network ? (app.netKind == "wifi" ? "wifi" : app.netKind == "ethernet" ? "settings_ethernet" : app.netKind == "vpn" ? "vpn_lock" : "link_broken")
                               : (app.muted ? "volume_off" : "volume_up");
    if (iconOnly) {
        const double box = std::min(r.w, r.h);
        shapePath(cr, kM3IconShape, r.x + (r.w - box) / 2, r.y + (r.h - box) / 2, box);
        setSource(cr, app.color("secondary_container"));
    } else {
        roundedRect(cr, r.x, r.y, r.w, r.h, r.w / 2);
        setSource(cr, app.color("surface_container_low"));
    }
    cairo_fill(cr);
    if (!network || netIconShown(app)) drawIcon(cr, icon, r.x + (r.w - 18) / 2.0, r.y + (kSideBox - 18) / 2.0, 18, app.color(iconOnly ? "on_secondary_container" : "on_surface_variant"));
    if (speed) drawNetSpeed(cr, app, r.x, r.y + (netIconShown(app) ? kSideBox : 0), r.w, kNetSpeedH, false);
}
void measureBarModules(App& app) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = mkcr(surf);
    PangoLayout* layout = pango_cairo_create_layout(cr);
    app.weatherBarW = static_cast<int>(std::ceil(weatherBarPaint(nullptr, app, layout, 0, 0)));
    app.aiBarW = static_cast<int>(std::ceil(aiBarPaint(nullptr, app, layout, 0, 0, -1, &app.aiPillSplit)));
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

void sideM3WsPaint(cairo_t* cr, const App& app, PangoLayout* layout, double x, double top, double w, Hit hit) {
    const int n = wsShown(app);
    const double W = kM3Cell, cx = x + w / 2.0;
    auto cellY = [&](int id) { return top + (id - 1) * W; };
    auto occupied = [&](int id) { return id >= 1 && id <= n && app.occupied.count(id) > 0; };
    const Color container = app.color("secondary_container"), focus = app.color("primary"), onFocus = app.color("on_primary");
    const Color onContainer = app.color("on_secondary_container"), onSurfVar = app.color("on_surface_variant");
    const Color layer1 = app.color("surface_container_low");
    const Color inactive{onSurfVar.r * 0.45 + layer1.r * 0.55, onSurfVar.g * 0.45 + layer1.g * 0.55, onSurfVar.b * 0.45 + layer1.b * 0.55};
    const double pillX = cx - kM3WsPill / 2, pillY0 = top + (W - kM3WsPill) / 2;
    cairo_push_group(cr);
    for (int id = 1; id <= n; id++) {
        if (!occupied(id)) continue;
        const double upper = cellY(id) - (occupied(id - 1) ? W / 2 : 0), lower = cellY(id) + W + (occupied(id + 1) ? W / 2 : 0);
        roundedRect(cr, cx - W / 2, upper, W, lower - upper, W / 2);
        setSource(cr, container);
        cairo_fill(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.6);
    for (int id = 1; id <= n; id++) {
        if (hit != kHitWs + id) continue;
        roundedRect(cr, cx - kM3WsPill / 2, cellY(id) + (W - kM3WsPill) / 2, kM3WsPill, kM3WsPill, kM3WsPill / 2);
        setSource(cr, focus, 0.227);
        cairo_fill(cr);
    }
    const double focusY = pillY0 + (app.wsPill - 1) * W;
    if (app.workspace >= 1 && app.workspace <= n) {
        roundedRect(cr, pillX, focusY, kM3WsPill, kM3WsPill, kM3WsPill / 2);
        setSource(cr, focus);
        cairo_fill(cr);
    }
    PangoFontDescription* nf = pango_font_description_from_string("Sans Bold 11");
    for (int id = 1; id <= n; id++) {
        if (cairo_surface_t* icon = wsAppIcon(app, id)) {
            drawWsAppIcon(cr, icon, cx, cellY(id) + W / 2.0, 18);
            continue;
        }
        const std::string num = wsNumberText(app, id);
        setWsNumber(layout, nf, num);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        const double nx = cx - tw / 2.0, ny = cellY(id) + (W - th) / 2.0;
        setSource(cr, occupied(id) ? onContainer : inactive);
        cairo_move_to(cr, nx, ny);
        pango_cairo_show_layout(cr, layout);
        if (id == app.workspace) {
            cairo_save(cr);
            roundedRect(cr, pillX, focusY, kM3WsPill, kM3WsPill, kM3WsPill / 2);
            cairo_clip(cr);
            setSource(cr, onFocus);
            cairo_move_to(cr, nx, ny);
            pango_cairo_show_layout(cr, layout);
            cairo_restore(cr);
        }
    }
    pango_font_description_free(nf);
}

constexpr double kSideMusicDisc = 34;
void sideMusicM3(cairo_t* cr, const App& app, double x, double y, double w, double h, bool hot) {
    const bool playing = app.musicStatus == "Playing";
    const double cx = x + w / 2.0, cy = y + h / 2.0, r = kSideMusicDisc / 2.0;
    auto disc = [&] { cairo_new_sub_path(cr); cairo_arc(cr, cx, cy, r, 0, 2 * M_PI); };
    disc();
    setSource(cr, app.color(playing ? "primary" : "secondary_container"));
    cairo_fill(cr);
    if (app.musicArt) {
        cairo_save(cr);
        disc();
        cairo_clip(cr);
        cairo_translate(cr, cx - r, cy - r);
        const double sc = kSideMusicDisc / cairo_image_surface_get_width(app.musicArt);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.musicArt, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        drawIcon(cr, "music_note", cx - kMediaIcon / 2.0, cy - kMediaIcon / 2.0, kMediaIcon, app.color(playing ? "on_primary" : "on_secondary_container"));
    }
    if (hot) {
        disc();
        setSource(cr, app.color(playing ? "on_primary" : "on_secondary_container"), 0.08);
        cairo_fill(cr);
    }
}

void drawSide(Bar& bar) {
    App& app = *bar.app;
    BarInkScope inkScope(app);
    if (!app.avatarTried) loadAvatar(app);
    const int W = kSideBarW + static_cast<int>(bar.extra), H = static_cast<int>(bar.width);
    const int stride = W * 4, size = stride * H;
    int fd = memfd_create("end4pC-buffer", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) { std::perror("shm"); std::exit(1); }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, W, H, stride);
    cairo_t* cr = mkcr(surface);

    if (notchLike(app.cfg.barStyle)) { sideLayout(bar); setBarRegion(bar); }
    {
        cairo_matrix_t m;
        cairo_matrix_init(&m, 0, 1, 1, 0, 0, 0);
        cairo_set_matrix(cr, &m);
        drawBarBackground(cr, bar);
        cairo_identity_matrix(cr);
    }
    cairo_push_group(cr);
    cairo_translate(cr, bar.contentY, 0);

    const Hit hit = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    int tw = 0, th = 0;
    const std::vector<SideItem> items = sideItems(bar);
    Rect aiBox;
    int aiFirst = -1;
    for (const SideItem& s : items)
        if (s.id >= kHitAi && s.id < kHitAi + 2) {
            if (aiFirst < 0) { aiFirst = s.id; aiBox = s.r; }
            else aiBox.h = s.r.y + s.r.h - aiBox.y;
        }
    for (const auto& it : items) {
        const Rect& r = it.r;
        const bool hot = hit == it.id;
        if (it.id == HitWeather) { weatherSidePaint(cr, app, layout, r, hot); continue; }
        if (it.id == HitUpdates) { updatesSidePaint(cr, app, layout, r, hot); continue; }
        if (it.id == HitBluetooth) { btSidePaint(cr, app, r, hot); continue; }
        if (it.id == HitDrives) { drivesSidePaint(cr, app, r, hot); continue; }
        if (it.id == HitPrivacy) { privacySidePaint(cr, app, r); continue; }
        if (it.id == HitActiveWindow) { awinPaint(cr, app, layout, r.x, r.y, r.w, r.h, true, hot); continue; }
        if (it.id == HitMedia && musicM3(app)) { sideMusicM3(cr, app, r.x, r.y, r.w, r.h, hot); continue; }
        if (it.id == HitKeyboard) { kbdPaint(cr, app, layout, r.x, r.y, r.w, r.h, true, hot); continue; }
        if (it.id >= kHitAi && it.id < kHitAi + 2) {
            if (it.id == aiFirst && paintStyle(app, "aiusage") == "m3") {
                roundedRect(cr, aiBox.x, aiBox.y, aiBox.w, aiBox.h, aiBox.w / 2);
                setSource(cr, app.color("primary_container"));
                cairo_fill(cr);
            }
            aiSidePaint(cr, app, layout, r, it.id - kHitAi, hot);
            continue;
        }
        if (it.id == HitDivider) { dividerPaint(cr, app, r.x + r.w / 2, r.y + r.h / 2, false, it.dividerStyle); continue; }
        if (it.id >= kHitViz && it.id < kHitViz + 2) { vizPaint(cr, app, r.x + r.w / 2, r.y + r.h / 2, true, it.id == kHitViz + 1); continue; }
        if (it.id == HitClock && widgetStyle(app, "clock") == "m3") {
            const size_t colon = app.clockTime.find(':');
            const std::string hh = colon == std::string::npos ? app.clockTime : app.clockTime.substr(0, colon);
            const std::string mm = colon == std::string::npos ? "" : app.clockTime.substr(colon + 1);
            PangoFontDescription* f = pango_font_description_from_string("Sans Bold 12");
            pango_layout_set_font_description(layout, f);
            setSource(cr, app.color("primary"));
            pango_layout_set_text(layout, hh.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            const double disc = kQuickBox, cxm = r.x + r.w / 2.0;
            roundedRect(cr, r.x, r.y, r.w, r.h, r.w / 2);
            setSource(cr, app.color("primary_container"));
            cairo_fill(cr);
            const double pad = (r.w - disc) / 2, topPad = pad + 3, lineGap = 4;
            PangoRectangle ink{}, logical{};
            pango_layout_set_text(layout, hh.c_str(), -1);
            pango_layout_get_pixel_extents(layout, &ink, &logical);
            setSource(cr, app.color("on_primary_container"));
            cairo_move_to(cr, cxm - ink.width / 2.0 - ink.x, r.y + topPad - ink.y);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_text(layout, mm.c_str(), -1);
            PangoRectangle ink2{}, logical2{};
            pango_layout_get_pixel_extents(layout, &ink2, &logical2);
            cairo_move_to(cr, cxm - ink2.width / 2.0 - ink2.x, r.y + topPad + ink.height + lineGap - ink2.y);
            pango_cairo_show_layout(cr, layout);
            const double dy = r.y + r.h - pad - disc;
            cairo_new_sub_path(cr);
            cairo_arc(cr, cxm, dy + disc / 2, disc / 2, 0, 2 * M_PI);
            setSource(cr, app.color("primary"));
            cairo_fill(cr);
            drawIcon(cr, "schedule", cxm - 7, dy + disc / 2 - 7, 14, app.color("on_primary"));
            pango_font_description_free(f);
        } else if (it.id == HitClock) {
            Color ink = app.color("on_primary_container");
            if (widgetStyle(app, "clock") == "outline") ink = styleFrame(cr, app, "outline", r.x, r.y, r.w, r.h, false, true);
            else {
                roundedRect(cr, r.x, r.y, r.w, r.h, 16);
                setSource(cr, app.color("primary_container"));
                cairo_fill(cr);
            }
            PangoFontDescription* f = pango_font_description_from_string("Sans Bold 12");
            pango_layout_set_font_description(layout, f);
            const size_t colon = app.clockTime.find(':');
            const std::string hh = colon == std::string::npos ? app.clockTime : app.clockTime.substr(0, colon);
            const std::string mm = colon == std::string::npos ? "" : app.clockTime.substr(colon + 1);
            setSource(cr, ink);
            pango_layout_set_text(layout, hh.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            const double top = r.y + (r.h - (2 * th - 4)) / 2.0;
            cairo_move_to(cr, r.x + (r.w - tw) / 2.0, top);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_text(layout, mm.c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr, r.x + (r.w - tw) / 2.0, top + th - 4);
            pango_cairo_show_layout(cr, layout);
            pango_font_description_free(f);
        } else if (it.id >= kHitWs && it.id < kHitQuick) {
            const int id = it.id - kHitWs;
            const bool active = id == app.workspace;
            if (m3Ws(app)) {
                if (id == 1) sideM3WsPaint(cr, app, layout, r.x, r.y, r.w, hit);
                continue;
            }
            if (pillStyle(app)) {
                const bool occ = app.occupied.count(id) > 0;
                const int n = wsShown(app);
                if (id == 1) {
                    const double cx = r.x + r.w / 2, top = r.y - kPillPad, len = n * kPillCell + 2 * kPillPad;
                    roundedRect(cr, cx - (kPillH + 4) / 2, top, kPillH + 4, len, (kPillH + 4) / 2);
                    setSource(cr, app.color("surface_container_high"));
                    cairo_fill(cr);
                    if (app.workspace >= 1 && app.workspace <= n) {
                        roundedRect(cr, cx - kPillInner / 2 - 2, top + kPillPad + (app.wsPill - 1) * kPillCell + (kPillCell - kPillInner) / 2, kPillInner + 4, kPillInner, kPillInner / 2);
                        setSource(cr, app.color("primary"));
                        cairo_fill(cr);
                    }
                }
                if (hot && !active) { roundedRect(cr, r.x + (r.w - kPillInner - 4) / 2, r.y + (kPillCell - kPillInner) / 2, kPillInner + 4, kPillInner, kPillInner / 2); setSource(cr, app.color("on_surface"), 0.1); cairo_fill(cr); }
                const double near = std::max(0.0, 1.0 - std::abs(app.wsPill - id));
                if (cairo_surface_t* icon = wsAppIcon(app, id)) {
                    drawWsAppIcon(cr, icon, r.x + r.w / 2.0, r.y + kPillCell / 2.0, 18);
                    continue;
                }
                const Color a = app.color(hot ? "on_surface" : occ ? "on_surface_variant" : "outline"), b = app.color("on_primary");
                setSource(cr, Color{a.r + (b.r - a.r) * near, a.g + (b.g - a.g) * near, a.b + (b.b - a.b) * near});
                const std::string num = wsNumberText(app, id);
                PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
                setWsNumber(layout, f, num);
                int tw = 0, th = 0;
                pango_layout_get_pixel_size(layout, &tw, &th);
                cairo_move_to(cr, r.x + (r.w - tw) / 2.0, r.y + (kPillCell - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_font_description_free(f);
                continue;
            }
            if (!iconStyleWs(app)) {
                const SideWsShape sh = sideWsShape(app, id);
                const double vx = r.x + (r.w - sh.vw) / 2.0, vy = r.y + sideWsPad(app);
                const bool occ = app.occupied.count(id) > 0;
                if (numberedStyle(app) || ringStyleWs(app)) {
                    roundedRect(cr, vx, vy, sh.vw, sh.h, ringStyleWs(app) ? sh.vw / 2 : active ? 10 : 8);
                    if (active) { setSource(cr, app.color("primary")); cairo_fill(cr); }
                    else if (!ringStyleWs(app)) {
                        if (hot || occ) { setSource(cr, app.color(hot ? "secondary_container" : "surface_container_high")); cairo_fill(cr); }
                        cairo_new_path(cr);
                    } else {
                        if (hot) { setSource(cr, app.color("surface_container_high")); cairo_fill_preserve(cr); }
                        setSource(cr, app.color(hot ? "on_surface" : occ ? "on_surface_variant" : "outline_variant"));
                        cairo_set_line_width(cr, 1.6);
                        cairo_stroke(cr);
                    }
                    if (cairo_surface_t* icon = wsAppIcon(app, id)) {
                        drawWsAppIcon(cr, icon, vx + sh.vw / 2.0, vy + sh.h / 2.0, ringStyleWs(app) ? 17 : 18);
                        continue;
                    }
                    setSource(cr, app.color(active ? "on_primary" : hot ? (ringStyleWs(app) ? "on_surface" : "on_secondary_container") : occ ? "on_surface_variant" : "outline_variant"));
                    const std::string num = wsNumberText(app, id);
                    PangoFontDescription* f = pango_font_description_from_string("Sans Bold 11");
                    setWsNumber(layout, f, num);
                    int tw = 0, th = 0;
                    pango_layout_get_pixel_size(layout, &tw, &th);
                    cairo_move_to(cr, vx + (sh.vw - tw) / 2.0, vy + (sh.h - th) / 2.0);
                    pango_cairo_show_layout(cr, layout);
                    pango_font_description_free(f);
                } else if (barStyleWs(app)) {
                    const double len = sh.vw + (hot ? kWsTickHover : 0);
                    roundedRect(cr, r.x + (r.w - len) / 2.0, vy, len, sh.h, sh.h / 2);
                    setSource(cr, wsTickColor(app, id));
                    cairo_fill(cr);
                } else {
                    roundedRect(cr, vx, vy, sh.vw, sh.h, sh.vw / 2);
                    setSource(cr, app.color(active ? "primary" : hot ? "on_surface" : occ ? "on_surface_variant" : "outline_variant"));
                    cairo_fill(cr);
                }
                continue;
            }
            if (active || hot) {
                roundedRect(cr, r.x, r.y, r.w, r.h, 14);
                setSource(cr, app.color(active ? "surface_container_highest" : "surface_container_high"));
                cairo_fill(cr);
            }
            const Color c = app.color(active ? "on_surface" : hot ? "on_surface" : app.occupied.count(id) ? "on_surface_variant" : "outline");
            if (cairo_surface_t* icon = wsAppIcon(app, id)) drawWsAppIcon(cr, icon, r.x + r.w / 2.0, r.y + r.h / 2.0, 20);
            else if (id >= 1 && id <= 10) drawIcon(cr, kSideWsIcons[id - 1], r.x + (r.w - 22) / 2.0, r.y + (r.h - 22) / 2.0, 22, c, active);
            else {
                const std::string num = wsNumberText(app, id);
                PangoFontDescription* f = pango_font_description_from_string("Sans Bold 11");
                setWsNumber(layout, f, num);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, c);
                cairo_move_to(cr, r.x + (r.w - tw) / 2.0, r.y + (r.h - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_font_description_free(f);
            }
        } else if (it.id == HitMedia || it.id == HitNetwork || it.id == HitVolume || (it.id >= kHitQuick && it.id < kHitQuick + kQaCount)) {
            bool qLive = false, qAlert = false;
            const char* qIc = it.id >= kHitQuick ? quickIcon(app, it.id - kHitQuick, qLive, qAlert) : nullptr;
            const bool rec = qLive;
            const std::string frameStyle = it.id == HitVolume ? paintStyle(app, "volume") : it.id == HitNetwork ? paintStyle(app, "network")
                : it.id == HitMedia ? paintStyle(app, "media") : it.id >= kHitQuick ? paintStyle(app, "quickactions") : "plain";
            if (frameStyle == "m3" && (it.id == HitVolume || it.id == HitNetwork)) { drawSideM3(cr, app, it.id, r); continue; }
            const bool m3Frame = frameStyle == "m3" && it.id >= kHitQuick;
            Color framed = app.color("on_surface");
            if (genericFrame(frameStyle)) framed = styleFrame(cr, app, frameStyle, r.x, r.y, r.w, r.h, hot, true);
            else if (m3Frame) {
                roundedRect(cr, r.x, r.y, r.w, r.h, r.w / 2);
                setSource(cr, app.color(hot || rec ? "primary" : "surface"), hot || rec ? 1 : 0.2);
                cairo_fill(cr);
                framed = app.color(hot || rec ? "on_primary" : "primary");
            }
            else if (hot || rec) {
                roundedRect(cr, r.x, r.y, r.w, r.h, 14);
                if (rec) {
                    const Color e = app.color("error"), base = app.color("surface_container_high");
                    setSource(cr, Color{e.r * 0.3 + base.r * 0.7, e.g * 0.3 + base.g * 0.7, e.b * 0.3 + base.b * 0.7});
                } else setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            const char* ic = "music-note";
            Color ink = app.color(app.musicStatus == "Playing" ? "primary" : "on_surface");
            if (it.id == HitNetwork) {
                ic = app.netKind == "wifi" ? "wifi" : app.netKind == "ethernet" ? "settings_ethernet" : app.netKind == "vpn" ? "vpn_lock" : "link_broken";
                ink = app.color(app.netKind == "none" ? "outline" : "on_surface");
            } else if (it.id == HitVolume) {
                ic = app.muted || app.volume == 0 ? "volume_off" : app.volume < 50 ? "volume_down" : "volume_up";
                ink = app.color(app.muted ? "outline" : "on_surface");
            } else if (it.id >= kHitQuick) {
                ic = qIc;
                ink = app.color(rec || qAlert ? "error" : "on_surface");
            }
            if ((genericFrame(frameStyle) || m3Frame) && !(it.id == HitVolume && app.muted) && !(it.id == HitNetwork && app.netKind == "none")) ink = framed;
            if (qAlert) ink = app.color("error");
            if (it.id == HitNetwork) {
                if (netIconShown(app)) drawIcon(cr, ic, r.x + (r.w - 22) / 2.0, r.y + (40 - 22) / 2.0, 22, ink, hot);
                if (netSpeedShown(app)) drawNetSpeed(cr, app, r.x, r.y + (netIconShown(app) ? 40 : 0), r.w, kNetSpeedH, false, genericFrame(frameStyle) || m3Frame ? &framed : nullptr);
            } else drawIcon(cr, ic, r.x + (r.w - 22) / 2.0, r.y + (r.h - 22) / 2.0, 22, ink, hot || rec);
        } else if (it.id == HitLauncher || it.id == HitPower || it.id == HitTools) {
            const std::string style = paintStyle(app, it.id == HitLauncher ? "launcher" : it.id == HitTools ? "tools" : "power");
            Color ink = app.color("on_surface");
            if (style != "m3") ink = styleFrame(cr, app, style, r.x, r.y, r.w, r.h, hot, true);
            else if (it.id == HitLauncher) ink = m3ShapedCircle(cr, app, r.x, r.y, r.w, hot, "primary_container", "on_primary_container", "on_primary_container", "Cookie9Sided");
            else if (it.id == HitPower) ink = m3ShapedCircle(cr, app, r.x, r.y, r.w, hot, "primary", "on_primary", "on_primary", "Cookie12Sided");
            else ink = m3Circle(cr, app, r.x, r.y, r.w, hot, "primary_container", "on_primary_container", "on_surface");
            drawIcon(cr, it.id == HitLauncher ? "magnifer" : it.id == HitTools ? "ruler-pen" : "power", r.x + (r.w - 22) / 2.0, r.y + (r.h - 22) / 2.0, 22, ink, hot);
        } else if (it.id == HitNotif || it.id == HitSettings) {
            const bool notif = it.id == HitNotif, any = notif && app.notifCount > 0, dnd = notif && app.cfg.doNotDisturb != 0;
            const std::string style = paintStyle(app, notif ? "notifications" : "settings");
            const bool inverse = style == "m3" || style == "badge" || style == "m3filled";
            Color ink = app.color(dnd ? "on_surface_variant" : any ? "primary" : "on_surface");
            if (style == "m3") ink = notif ? m3Circle(cr, app, r.x, r.y, r.w, hot, "primary", "on_primary", "on_primary")
                                           : m3Circle(cr, app, r.x, r.y, r.w, hot, "primary_container", "on_primary_container", "on_surface");
            else if (const Color framed = styleFrame(cr, app, style, r.x, r.y, r.w, r.h, hot, true); genericFrame(style)) ink = framed;
            drawIcon(cr, !notif ? "settings" : dnd ? "notifications_off" : any ? "notifications_active" : "notifications", r.x + (r.w - 22) / 2.0,
                     r.y + (r.h - 22) / 2.0, 22, ink, hot);
            if (any && style == "dot") {
                cairo_arc(cr, r.x + r.w - 6, r.y + 6, 3.5, 0, 2 * M_PI);
                setSource(cr, app.color(inverse ? "on_primary" : "primary"));
                cairo_fill(cr);
            } else if (any) {
                char cnt[8];
                std::snprintf(cnt, sizeof cnt, "%d", app.notifCount > 99 ? 99 : app.notifCount);
                PangoFontDescription* f = pango_font_description_from_string("Sans Bold 8");
                pango_layout_set_font_description(layout, f);
                pango_layout_set_text(layout, cnt, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                const double bw = std::max(14.0, tw + 8.0), bx = r.x + r.w - bw + 1, by = r.y - 1;
                roundedRect(cr, bx, by, bw, 14, 7);
                setSource(cr, app.color(inverse ? "on_primary" : "primary"));
                cairo_fill(cr);
                setSource(cr, app.color(inverse ? "primary" : "on_primary"));
                cairo_move_to(cr, bx + (bw - tw) / 2.0, by + (14 - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_font_description_free(f);
            }
        } else if (it.id == HitTray) {
            if (hot) {
                roundedRect(cr, r.x, r.y, r.w, r.h, 14);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, (app.trayOpen == trayForward(app, true)) ? "alt-arrow-up" : "alt-arrow-down", r.x + (r.w - 22) / 2.0, r.y + (r.h - 22) / 2.0, 22, app.color("on_surface"), hot);
        } else if (it.id >= kHitDtp && it.id < kHitDtp + 100) {
            const auto& dockItems = dtpItems(app);
            const size_t k = it.id - kHitDtp;
            if (k < dockItems.size()) dtpDrawItem(cr, app, dockItems[k], r.x + (r.w - 28) / 2.0, r.y, hot, 1);
        } else if (it.id >= kHitTray && it.id < kHitTray + 100) {
            const size_t k = it.id - kHitTray;
            const double p = 1 - std::pow(1 - app.trayT, 3);
            if (k < app.tray.size()) {
                if (hot) {
                    roundedRect(cr, r.x, r.y, r.w, r.h, 14);
                    setSource(cr, app.color("surface_container_high"));
                    cairo_fill(cr);
                }
                const bool pinnedIcon = trayPinned(app, app.tray[k]);
                drawTrayGlyph(cr, app, app.tray[k].surf, r.x + (r.w - kTrayIcon) / 2.0, r.y + (r.h - kTrayIcon) / 2.0, pinnedIcon ? 1.0 : p);
            }
        } else if (it.id >= kHitRes && it.id < kHitRes + 5) {
            for (const ResItem& ri : resItems(app))
                if (ri.kind == it.id - kHitRes) {
                    const bool text = widgetStyle(app, "resources") == "text";
                    drawResource(cr, app, ri, r.x + r.w / 2, text ? r.y + r.h / 2 : r.y + 12, layout, true);
                    if (app.cfg.resValue && !text) {
                        char vb[8];
                        std::snprintf(vb, sizeof vb, "%d", static_cast<int>(std::lround(std::clamp(ri.pct, 0.0, 1.0) * 100)));
                        PangoFontDescription* f = pango_font_description_from_string("Sans Bold 8");
                        pango_layout_set_font_description(layout, f);
                        pango_layout_set_text(layout, vb, -1);
                        pango_layout_get_pixel_size(layout, &tw, &th);
                        setSource(cr, app.color("on_surface_variant"));
                        cairo_move_to(cr, r.x + (r.w - tw) / 2.0, r.y + 25);
                        pango_cairo_show_layout(cr, layout);
                        pango_font_description_free(f);
                    }
                }
        } else if (it.id >= kHitStatus && it.id < kHitStatus + 4) {
            const int k = it.id - kHitStatus;
            const double t = app.statusT[k], p = 1 - std::pow(1 - t, 3);
            const char* icons[4] = {"stop_circle", "volume_off", "link_broken", "battery-low"};
            std::string lowIcon = icons[k];
            if (k == 3 && !(app.batPresent && !app.batCharging && app.batPct <= app.cfg.batteryLow))
                if (const App::Periph* d = lowPeripheral(app)) lowIcon = d->kind == "keyboard" ? "keyboard" : d->kind == "headset" || d->kind == "headphones" ? "headphones-round" : d->kind == "mouse" ? "mouse" : "battery-low";
            Color ink = app.color(k == 0 || k == 3 ? "error" : k == 1 ? "outline" : "on_surface_variant");
            cairo_push_group(cr);
            const double dy = (1 - p) * 14;
            const std::string batStyle = k == 3 ? paintStyle(app, "battery") : "plain";
            if (k == 3 && batStyle == "m3") {
                roundedRect(cr, r.x, r.y + dy, r.w, r.h, r.h / 2);
                setSource(cr, app.color("secondary_container"));
                cairo_fill(cr);
            } else if (k == 3 && batStyle == "badge") {
                roundedRect(cr, r.x, r.y + dy, r.w, r.h, 16);
                setSource(cr, app.color(hot ? "error_container" : "error"));
                cairo_fill(cr);
                ink = app.color(hot ? "on_error_container" : "on_error");
            } else if (k == 3 && genericFrame(batStyle)) {
                const Color framed = styleFrame(cr, app, batStyle, r.x, r.y + dy, r.w, r.h, hot, true);
                if (batStyle == "badge" || batStyle == "tonal") ink = framed;
            } else if (hot) {
                roundedRect(cr, r.x, r.y + dy, r.w, r.h, 14);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            if (k == 3 && app.cfg.batteryPercent) {
                const App::Periph* dev = app.batPresent ? nullptr : lowPeripheral(app);
                const int level = std::clamp(dev ? dev->pct : app.batPct, 0, 100);
                const double iconY = r.y + dy + 8;
                drawIcon(cr, lowIcon, r.x + (r.w - 22) / 2.0, iconY, 22, ink, false);
                PangoFontDescription* bf = pango_font_description_from_string("Sans Bold 9");
                pango_layout_set_font_description(layout, bf);
                pango_layout_set_text(layout, (std::to_string(level) + "%").c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, ink);
                cairo_move_to(cr, r.x + (r.w - tw) / 2.0, iconY + 22 + 2);
                pango_cairo_show_layout(cr, layout);
                pango_font_description_free(bf);
            } else drawIcon(cr, lowIcon, r.x + (r.w - 22) / 2.0, r.y + dy + (r.h - 22) / 2.0, 22, ink, k == 0);
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, p);
        } else if (it.id == HitAvatar) {
            const std::string aStyle = widgetStyle(app, "avatar");
            constexpr double kSidePad = 3;
            const bool m3a = aStyle == "m3";
            const double side = r.w - 2 * (m3a ? 1 : kSidePad), px = r.x + (m3a ? 1 : kSidePad), py = r.y + (r.h - side) / 2.0 + (m3a ? 0 : 0) * kSidePad;
            cairo_save(cr);
            cairo_new_sub_path(cr);
            if (m3a) shapePath(cr, kM3IconShape, px, py, side);
            else if (aStyle == "squircle") roundedRect(cr, px, py, side, side, side * 0.36);
            else cairo_arc(cr, px + side / 2, py + side / 2, side / 2, 0, 2 * M_PI);
            if (app.avatar) {
                cairo_clip(cr);
                cairo_translate(cr, px, py);
                const double sc = side / cairo_image_surface_get_width(app.avatar);
                cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, app.avatar, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
                cairo_paint(cr);
            } else {
                setSource(cr, app.color("primary_container"));
                cairo_fill(cr);
            }
            cairo_restore(cr);
            if (aStyle == "ring") {
                cairo_new_sub_path(cr);
                cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2 - 1, 0, 2 * M_PI);
                setSource(cr, app.color("primary"));
                cairo_set_line_width(cr, 2);
                cairo_stroke(cr);
            }
            if (!app.avatar) drawIcon(cr, "user-circle", px + (side - 26) / 2.0, py + (side - 26) / 2.0, 26, app.color("on_primary_container"));
        }
    }
    g_object_unref(layout);
    popWithShadow(cr, app);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, &bar);
    updateBlurRegion(app, bar.surface, bar.blur, W, H, [&](cairo_t* m) {
        cairo_matrix_t swap;
        cairo_matrix_init(&swap, 0, 1, 1, 0, 0, 0);
        cairo_set_matrix(m, &swap);
        drawBarBackground(m, bar);
    });
    wl_surface_attach(bar.surface, buffer, 0, 0);
    wl_surface_damage_buffer(bar.surface, 0, 0, W, H);
    wl_surface_commit(bar.surface);
    refreshBarShadow(bar);
}

void islandPaintBase(cairo_t* cr, const App& app, PangoLayout* layout, const Bar& bar) {
    const double x0 = slotX(bar, "island"), w = islandW(app), cy = bar.height / 2.0, m = std::clamp(app.islandMusicT, 0.0, 1.0);
    const double idleA = 1 - islandSmooth(0.0, 0.55, m), musicA = islandSmooth(0.45, 1.0, m);
    const double idleDx = -10 * (1 - idleA), musicDx = 10 * (1 - musicA);
    const double lx = x0 + kIslandPadIn, rx = x0 + w - kIslandPadIn, iy = cy - kIslandItem / 2.0;
    const bool playing = app.musicStatus == "Playing" && !app.vizShown.empty();
    auto round = [&](double a) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, lx + kIslandItem / 2.0, cy, kIslandItem / 2.0, 0, 2 * M_PI);
        (void)a;
    };
    if (idleA > 0.003) {
        cairo_save(cr);
        cairo_translate(cr, idleDx, 0);
        round(0);
        if (app.avatar) {
            cairo_clip(cr);
            cairo_translate(cr, lx, iy);
            const double sc = kIslandItem / cairo_image_surface_get_width(app.avatar);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, app.avatar, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint_with_alpha(cr, idleA);
        } else {
            setSource(cr, app.color("primary_container"), idleA);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    if (musicA > 0.003) {
        cairo_save(cr);
        cairo_translate(cr, musicDx, 0);
        round(0);
        if (app.musicArt) {
            cairo_clip(cr);
            cairo_translate(cr, lx, iy);
            const double sc = kIslandItem / cairo_image_surface_get_width(app.musicArt);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, app.musicArt, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint_with_alpha(cr, musicA);
        } else {
            setSource(cr, app.color("surface_container_high"), musicA);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        if (!app.musicArt) {
            const double noteS = 16;
            drawIcon(cr, "music_note", lx + musicDx + (kIslandItem - noteS) / 2.0, cy - noteS / 2.0, noteS, app.color("on_surface_variant"));
        }
    }
    if (idleA > 0.003) {
        PangoFontDescription* tf = pango_font_description_from_string(kClockTimeFont);
        pango_layout_set_font_description(layout, tf);
        pango_layout_set_text(layout, app.clockTime.c_str(), -1);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        setSource(cr, app.color("on_surface"), idleA);
        cairo_move_to(cr, idleDx + rx - kIslandEndInset - tw, cy - th / 2.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(tf);
    }
    if (musicA > 0.003) {
        constexpr double step = kVizDot + kVizGap, maxLen = kIslandItem * 0.7;
        const double vx = musicDx + rx - kIslandEndInset - kIslandVizW;
        setSource(cr, app.color("primary"), (playing ? 0.9 : 0.35) * musicA);
        for (int i = 0; i < kIslandViz; i++) {
            double v = 0;
            if (playing) {
                const size_t src = std::min(app.vizShown.size() - 1, static_cast<size_t>(i) * app.vizShown.size() / kIslandViz);
                v = std::clamp(app.vizShown[src], 0.0, 1.0);
            }
            const double len = std::max<double>(kVizDot, v * maxLen);
            roundedRect(cr, vx + i * step, cy - len / 2.0, kVizDot, len, kVizDot / 2.0);
            cairo_fill(cr);
        }
    }
}

void islandPaintRec(cairo_t* cr, const App& app, PangoLayout* layout, const Bar& bar) {
    const double x0 = slotX(bar, "island"), w = islandW(app), cy = bar.height / 2.0;
    const double lx = x0 + kIslandPadIn, rx = x0 + w - kIslandPadIn - kIslandEndInset;
    const long sec = static_cast<long>(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_recStartAt).count());
    cairo_new_sub_path(cr);
    cairo_arc(cr, lx + kIslandItem / 2.0, cy, kIslandRecDot / 2.0, 0, 2 * M_PI);
    setSource(cr, app.color("error"), sec % 2 == 0 ? 1.0 : 0.55);
    cairo_fill(cr);
    PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, 13 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, "REC", -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_surface"));
    cairo_move_to(cr, lx + kIslandItem, cy - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_set_absolute_size(fd, 14 * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(layout, recElapsed().c_str(), -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    setSource(cr, app.color("on_surface_variant"));
    cairo_move_to(cr, rx - tw, cy - th / 2.0);
    pango_cairo_show_layout(cr, layout);
}

void islandPaint(cairo_t* cr, const App& app, PangoLayout* layout, const Bar& bar) {
    const double r = std::clamp(g_islandRecT, 0.0, 1.0);
    const double baseA = 1 - islandSmooth(0.0, 0.55, r), recA = islandSmooth(0.45, 1.0, r);
    if (baseA > 0.003) {
        cairo_save(cr);
        cairo_translate(cr, -10 * (1 - baseA), 0);
        if (baseA < 0.997) {
            cairo_push_group(cr);
            islandPaintBase(cr, app, layout, bar);
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, baseA);
        } else islandPaintBase(cr, app, layout, bar);
        cairo_restore(cr);
    }
    if (recA > 0.003) {
        cairo_save(cr);
        cairo_translate(cr, 10 * (1 - recA), 0);
        cairo_push_group(cr);
        islandPaintRec(cr, app, layout, bar);
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, recA);
        cairo_restore(cr);
    }
}

double clockBarPaint(cairo_t* cr, const App& app, PangoLayout* layout, double bx, double barH, bool hover) {
    const std::string style = widgetStyle(app, "clock");
    const std::string time = app.clockAmPm.empty() ? app.clockTime : app.clockTime + " " + app.clockAmPm;
    const bool hasDate = !app.clockDate.empty();
    PangoFontDescription* tf = pango_font_description_from_string(kClockTimeFont);
    PangoFontDescription* df = pango_font_description_from_string(kClockDateFont);
    auto size = [&](const std::string& s, PangoFontDescription* f, double& w, double& h) {
        int iw = 0, ih = 0;
        pango_layout_set_font_description(layout, f);
        pango_layout_set_text(layout, s.c_str(), -1);
        pango_layout_get_pixel_size(layout, &iw, &ih);
        w = iw; h = ih;
    };
    auto text = [&](const std::string& s, PangoFontDescription* f, double x, double cy, const Color& c, double a = 1) {
        if (!cr) return;
        double w = 0, h = 0;
        size(s, f, w, h);
        setSource(cr, c, a);
        cairo_move_to(cr, x, cy - h / 2);
        pango_cairo_show_layout(cr, layout);
    };
    auto fillRR = [&](double x, double y, double w, double h, double tl, double tr, double br, double bl, const Color& c, double a = 1) {
        if (!cr) return;
        roundedRectCorners(cr, x, y, w, h, tl, tr, br, bl);
        setSource(cr, c, a);
        cairo_fill(cr);
    };
    double tw = 0, th = 0, dw = 0, dh = 0;
    size(time, tf, tw, th);
    if (hasDate) size(app.clockDate, df, dw, dh);
    const double cy = barH / 2, bh = kQuickBox, by = cy - bh / 2;  // (every look of the clock is as tall as the other widgets)
    const Color primary = app.color("primary"), onPrimary = app.color("on_primary"), cont = app.color("primary_container"), onCont = app.color("on_primary_container");
    const Color onSurf = app.color("on_surface");
    double w = 0;

    if (style == "m3") {
        constexpr double kM3Overlap = kTightGap;
        PangoFontDescription* mf = pango_font_description_from_string(kClockTimeFont);
        PangoFontDescription* mdf = pango_font_description_from_string(kClockDateFont);
        double mtw = 0, mth = 0, mdw = 0, mdh = 0, maw = 0, mah = 0;
        size(app.clockTime, mf, mtw, mth);
        if (hasDate) size(app.clockDate, mdf, mdw, mdh);
        if (!app.clockAmPm.empty()) size(app.clockAmPm, mdf, maw, mah);
        const double dateW = hasDate ? mdw + kClockGap : 0, timeW = kClockPadL + mtw + kClockPadL, ampmW = maw > 0 ? kClockPadL + maw + kClockPadL : 0;
        const double bh = kM3Disc, inset = (kM3Box - bh) / 2;
        const double lead = hasDate ? kClockGap : inset;
        const double px = bx + lead + dateW, py = cy - bh / 2, ampmX = px + timeW - kM3Overlap;
        w = lead + dateW + timeW + (ampmW > 0 ? ampmW - kM3Overlap : 0) + inset;
        fillRR(bx, cy - kM3Box / 2, w, kM3Box, kM3Box / 2, kM3Box / 2, kM3Box / 2, kM3Box / 2, cont);
        if (hasDate) text(app.clockDate, mdf, bx + lead, cy, onCont);
        fillRR(px, py, timeW, bh, bh / 2, bh / 2, bh / 2, bh / 2, primary);
        text(app.clockTime, mf, px + kClockPadL, cy, onPrimary);
        if (ampmW > 0) {
            fillRR(ampmX, py, ampmW, bh, bh / 2, bh / 2, bh / 2, bh / 2, app.color("tertiary_container"));
            text(app.clockAmPm, mdf, ampmX + kClockPadL, cy, primary);
        }
        pango_font_description_free(mf);
        pango_font_description_free(mdf);
    } else if (style == "minimal") {
        w = 16 + tw + (hasDate ? 8 + dw : 0);
        if (hover) fillRR(bx, by, w, bh, bh / 2, bh / 2, bh / 2, bh / 2, cont, 0.55);
        text(time, tf, bx + 8, cy, onSurf);
        if (hasDate) text(app.clockDate, df, bx + 8 + tw + 8, cy, onSurf, 0.65);
    } else if (style == "outline") {
        w = 28 + tw + (hasDate ? 20 + dw : 0);
        if (cr) {
            roundedRect(cr, bx + 0.75, by + 0.75, w - 1.5, bh - 1.5, (bh - 1.5) / 2);
            if (hover) { setSource(cr, cont, 0.6); cairo_fill_preserve(cr); }
            setSource(cr, primary, 0.9);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
        }
        text(time, tf, bx + 14, cy, onSurf);
        if (hasDate) {
            if (cr) { cairo_new_sub_path(cr); cairo_arc(cr, bx + 14 + tw + 10, cy, 2, 0, 2 * M_PI); setSource(cr, primary); cairo_fill(cr); }
            text(app.clockDate, df, bx + 14 + tw + 20, cy, onSurf, 0.75);
        }
    } else if (style == "tiles") {
        const size_t colon = app.clockTime.find(':');
        const std::string hh = colon == std::string::npos ? app.clockTime : app.clockTime.substr(0, colon), mm = colon == std::string::npos ? "" : app.clockTime.substr(colon + 1);
        double zw = 0, zh = 0, aw = 0, ah = 0;
        size("00", tf, zw, zh);
        if (!app.clockAmPm.empty()) size(app.clockAmPm, df, aw, ah);
        const double tile = zw + 14, cg = 9;
        w = 2 * tile + cg + (aw > 0 ? 6 + aw : 0) + (hasDate ? 10 + dw : 0);
        fillRR(bx, by, tile, bh, 9, 9, 9, 9, hover ? primary : cont);
        fillRR(bx + tile + cg, by, tile, bh, 9, 9, 9, 9, hover ? primary : cont);
        double hw = 0, hhgt = 0, mw = 0;
        size(hh, tf, hw, hhgt);
        size(mm, tf, mw, hhgt);
        const Color ink = hover ? onPrimary : onCont;
        text(hh, tf, bx + (tile - hw) / 2, cy, ink);
        text(mm, tf, bx + tile + cg + (tile - mw) / 2, cy, ink);
        if (cr) {
            setSource(cr, onSurf, 0.8);
            for (const double dy : {-3.5, 3.5}) { cairo_new_sub_path(cr); cairo_arc(cr, bx + tile + cg / 2, cy + dy, 1.7, 0, 2 * M_PI); cairo_fill(cr); }
        }
        double x = bx + 2 * tile + cg;
        if (aw > 0) { text(app.clockAmPm, df, x + 6, cy, onSurf, 0.7); x += 6 + aw; }
        if (hasDate) text(app.clockDate, df, x + 10, cy, onSurf, 0.75);
    } else if (style == "badge") {
        const double pw = 24 + tw;
        w = pw + (hasDate ? 8 + dw : 0);
        fillRR(bx, by, pw, bh, bh / 2, bh / 2, bh / 2, bh / 2, hover ? cont : primary);
        text(time, tf, bx + 12, cy, hover ? onCont : onPrimary);
        if (hasDate) text(app.clockDate, df, bx + pw + 8, cy, onSurf, 0.8);
    } else if (style == "stacked") {
        const double inner = std::max(tw, dw), ph = bh;
        w = 28 + inner;
        fillRR(bx, cy - ph / 2, w, ph, ph / 2.4, ph / 2.4, ph / 2.4, ph / 2.4, hover ? primary : cont);
        const Color ink = hover ? onPrimary : onCont;
        if (hasDate) {
            const double block = th + dh - 4, top = cy - block / 2;
            text(time, tf, bx + 14 + (inner - tw) / 2, top + th / 2, ink);
            text(app.clockDate, df, bx + 14 + (inner - dw) / 2, top + th - 4 + dh / 2, ink, 0.8);
        } else {
            text(time, tf, bx + 14, cy, ink);
        }
    } else if (style == "analog") {
        const double face = 18, left = 7;
        w = left + face + 8 + tw + (hasDate ? 8 + dw : 0) + 12;
        fillRR(bx, by, w, bh, bh / 2, bh / 2, bh / 2, bh / 2, hover ? primary : cont);
        if (cr) {
            std::time_t now = std::time(nullptr);
            std::tm tm{};
            localtime_r(&now, &tm);
            const double fx = bx + left + face / 2, fy = cy, hrA = ((tm.tm_hour % 12) + tm.tm_min / 60.0) / 12 * 2 * M_PI - M_PI / 2, mnA = tm.tm_min / 60.0 * 2 * M_PI - M_PI / 2;
            cairo_new_sub_path(cr);
            cairo_arc(cr, fx, fy, face / 2, 0, 2 * M_PI);
            setSource(cr, hover ? onPrimary : primary);
            cairo_fill(cr);
            const Color hand = hover ? primary : onPrimary;
            setSource(cr, hand);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_width(cr, 2);
            cairo_move_to(cr, fx, fy); cairo_line_to(cr, fx + std::cos(hrA) * 4, fy + std::sin(hrA) * 4); cairo_stroke(cr);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, fx, fy); cairo_line_to(cr, fx + std::cos(mnA) * 6.3, fy + std::sin(mnA) * 6.3); cairo_stroke(cr);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        }
        const Color ink = hover ? onPrimary : onCont;
        text(time, tf, bx + left + face + 8, cy, ink);
        if (hasDate) text(app.clockDate, df, bx + left + face + 8 + tw + 8, cy, ink, 0.8);
    } else if (style == "split") {
        const double tseg = 24 + tw, dseg = hasDate ? 20 + dw : 0, r = bh / 2, rs = 5, gap = 2;
        w = tseg + (hasDate ? gap + dseg : 0);
        fillRR(bx, by, tseg, bh, r, hasDate ? rs : r, hasDate ? rs : r, r, hover ? primary : cont);
        text(time, tf, bx + 12, cy, hover ? onPrimary : onCont);
        if (hasDate) {
            fillRR(bx + tseg + gap, by, dseg, bh, rs, r, r, rs, app.color(hover ? "secondary" : "secondary_container"));
            text(app.clockDate, df, bx + tseg + gap + 10, cy, app.color(hover ? "on_secondary" : "on_secondary_container"));
        }
    } else if (style == "compact") {
        w = 20 + tw;
        fillRR(bx, by, w, bh, bh / 2, bh / 2, bh / 2, bh / 2, hover ? primary : cont);
        text(time, tf, bx + 10, cy, hover ? onPrimary : onCont);
    } else if (style == "dot") {
        w = 16 + tw + (hasDate ? 21 + dw : 0);
        if (hover) fillRR(bx, by, w, bh, bh / 2, bh / 2, bh / 2, bh / 2, cont, 0.55);
        text(time, tf, bx + 8, cy, onSurf);
        if (hasDate) {
            if (cr) { cairo_new_sub_path(cr); cairo_arc(cr, bx + 8 + tw + 8 + 2.5, cy, 2.5, 0, 2 * M_PI); setSource(cr, app.color("tertiary")); cairo_fill(cr); }
            if (cr) {  // the date sits on the same baseline as the time (their boxes have different ascents, so centring both made the date ride high)
                size(time, tf, tw, th);
                const double base = cy - th / 2.0 + pango_layout_get_baseline(layout) / static_cast<double>(PANGO_SCALE);
                size(app.clockDate, df, dw, dh);
                const double dateTop = base - pango_layout_get_baseline(layout) / static_cast<double>(PANGO_SCALE);
                text(app.clockDate, df, bx + 8 + tw + 21, dateTop + dh / 2.0, onSurf, 0.6);
            }
        }
    } else if (style == "bold") {  // "Pixel": the chunky digits of the desktop clock, in a row, and the date on a chip beside them
        constexpr double nh = kQuickBox, nw = nh * 2.8;
        w = nw;
        std::string dateUp;
        double chipW = 0;
        constexpr double kChipH = 20, kChipGap = 2, kChipPad = 9;  // (the digits have some air of their own at the right)
        PangoFontDescription* cf = pango_font_description_new();
        pango_font_description_set_family(cf, kDisplayFamily);
        pango_font_description_set_weight(cf, PANGO_WEIGHT_BOLD);
        pango_font_description_set_absolute_size(cf, 11 * PANGO_SCALE);
        if (hasDate) {
            char* up = g_utf8_strup(app.clockDate.c_str(), -1);
            dateUp = up;
            g_free(up);
            double cw = 0, ch = 0;
            size(dateUp, cf, cw, ch);
            chipW = cw + 2 * kChipPad;
            w += kChipGap + chipW;
        }
        if (cr) {
            const size_t colon = app.clockTime.find(':');
            std::string hh = colon == std::string::npos ? app.clockTime : app.clockTime.substr(0, colon), mm = colon == std::string::npos ? "" : app.clockTime.substr(colon + 1);
            if (hh.size() < 2) hh = std::string(2 - hh.size(), '0') + hh;
            const std::string hm = hh.substr(0, 2) + mm.substr(0, 2);
            const double tileW = nw * 0.327, tileH = nh * 0.9, glyph = nh * 0.85, fringe = nh * 0.03;
            const double gx[4] = {0, nw * 0.163, nw * 0.50, nw * 0.653}, gy = nh * 0.05;
            cairo_save(cr);
            cairo_translate(cr, bx, cy - nh / 2);
            PangoFontDescription* fd = pango_font_description_new();
            pango_font_description_set_family(fd, kDisplayFamily);
            pango_font_description_set_weight(fd, static_cast<PangoWeight>(1000));
            pango_font_description_set_variations(fd, "wght=1000");
            pango_font_description_set_absolute_size(fd, glyph * PANGO_SCALE);
            pango_layout_set_font_description(layout, fd);
            pango_font_description_free(fd);
            auto digitPath = [&](int i) {
                char one[2] = {hm[static_cast<size_t>(i)], 0};
                pango_layout_set_text(layout, one, -1);
                int lw = 0, lh = 0;
                pango_layout_get_pixel_size(layout, &lw, &lh);
                cairo_move_to(cr, gx[i] + (tileW - lw) / 2.0, gy + (tileH - lh) / 2.0);
                pango_cairo_layout_path(cr, layout);
            };
            const Color soft = app.color("primary_container"), strong = app.color("primary");
            cairo_push_group(cr);
            for (int i = 0; i < 4; i++) {
                if (i % 2 == 1) {  // (the second digit of each pair cuts a thin gap out of the first)
                    digitPath(i);
                    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                    cairo_set_line_width(cr, fringe * 2);
                    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                    cairo_stroke_preserve(cr);
                    cairo_fill(cr);
                    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                }
                digitPath(i);
                setSource(cr, i == 0 || i == 3 ? soft : strong);
                cairo_fill(cr);
            }
            const double dot = nh * 0.14, gap = nh * 0.08, colonX = gx[1] + tileW + (gx[2] - (gx[1] + tileW)) / 2 - dot / 2, top = gy + tileH / 2 - (2 * dot + gap) / 2;
            setSource(cr, strong);
            for (int k = 0; k < 2; k++) {
                cairo_new_path(cr);
                cairo_arc(cr, colonX + dot / 2, top + dot / 2 + k * (dot + gap), dot / 2, 0, 2 * M_PI);
                cairo_fill(cr);
            }
            cairo_pop_group_to_source(cr);
            cairo_paint(cr);
            cairo_restore(cr);
            if (hasDate) {
                const double cx0 = bx + nw + kChipGap;
                fillRR(cx0, cy - kChipH / 2, chipW, kChipH, kChipH / 2, kChipH / 2, kChipH / 2, kChipH / 2, hover ? primary : app.color("secondary_container"));
                text(dateUp, cf, cx0 + kChipPad, cy, hover ? onPrimary : app.color("on_secondary_container"));
            }
        }
        pango_font_description_free(cf);
    } else {
        w = kClockPadL + kClockIcon + kClockGap + tw + (hasDate ? kClockGap + dw : 0) + kClockPadR;
        fillRR(bx, by, w, bh, bh / 2, bh / 2, bh / 2, bh / 2, hover ? primary : cont);
        const Color ink = hover ? onPrimary : onCont;
        if (cr) drawIcon(cr, "schedule", bx + kClockPadL, by + (bh - kClockIcon) / 2.0, kClockIcon, ink, hover);
        const double tx = bx + kClockPadL + kClockIcon + kClockGap;
        text(time, tf, tx, cy, ink);
        if (hasDate) text(app.clockDate, df, tx + tw + kClockGap, cy, ink, 0.8);
    }
    pango_font_description_free(tf);
    pango_font_description_free(df);
    return w;
}

Color quickM3Frame(cairo_t* cr, const App& app, const std::string& style, double x, double y, double box, size_t k, size_t n, bool lit, double w = -1) {
    if (style == "split") {
        constexpr double kInnerRadius = 5;
        const double outer = box / 2, left = k == 0 ? outer : kInnerRadius, right = k + 1 == n ? outer : kInnerRadius;
        roundedRectCorners(cr, x, y, w < 0 ? box : w, box, left, right, right, left);
        setSource(cr, app.color(lit ? "primary" : "secondary_container"));
        cairo_fill(cr);
        return app.color(lit ? "on_primary" : "on_secondary_container");
    }
    roundedRect(cr, x, y, w < 0 ? box : w, box, box / 2);
    setSource(cr, app.color(lit ? "primary" : "on_primary_container"), lit ? 1 : 0.12);
    cairo_fill(cr);
    return app.color(lit ? "on_primary" : "on_primary_container");
}

void m3WsPaint(cairo_t* cr, const App& app, const Bar& bar, PangoLayout* layout, Hit hit, const PangoFontDescription* font) {
    const int n = wsShown(app);
    const double x0 = slotX(bar, "workspaces"), cy = bar.height / 2.0, W = kM3Cell;
    auto cellX = [&](int id) { return x0 + (id - 1) * W; };
    auto occupied = [&](int id) { return id >= 1 && id <= n && app.occupied.count(id) > 0; };
    const Color container = app.color("secondary_container"), focus = app.color("primary"), onFocus = app.color("on_primary");
    const Color onContainer = app.color("on_secondary_container"), onSurfVar = app.color("on_surface_variant");
    const Color layer1 = app.color("surface_container_low");
    const Color inactive{onSurfVar.r * 0.45 + layer1.r * 0.55, onSurfVar.g * 0.45 + layer1.g * 0.55, onSurfVar.b * 0.45 + layer1.b * 0.55};
    const double pillTop = cy - W / 2;
    cairo_push_group(cr);
    for (int id = 1; id <= n; id++) {
        if (!occupied(id)) continue;
        const double left = cellX(id) - (occupied(id - 1) ? W / 2 : 0), right = cellX(id) + W + (occupied(id + 1) ? W / 2 : 0);
        roundedRect(cr, left, pillTop, right - left, W, W / 2);
        setSource(cr, container);
        cairo_fill(cr);
    }
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.6);
    const double pillX = cellX(1) + (app.wsPill - 1) * W + (W - kM3WsPill) / 2, pillY = cy - kM3WsPill / 2;
    for (int id = 1; id <= n; id++) {
        if (hit != kHitWs + id) continue;
        roundedRect(cr, cellX(id) + (W - kM3WsPill) / 2, pillY, kM3WsPill, kM3WsPill, kM3WsPill / 2);
        setSource(cr, focus, 0.227);
        cairo_fill(cr);
    }
    if (app.workspace >= 1 && app.workspace <= n) {
        roundedRect(cr, pillX, pillY, kM3WsPill, kM3WsPill, kM3WsPill / 2);
        setSource(cr, focus);
        cairo_fill(cr);
    }
    PangoFontDescription* nf = pango_font_description_from_string("Sans Bold 11");
    for (int id = 1; id <= n; id++) {
        if (cairo_surface_t* icon = wsAppIcon(app, id)) {
            drawWsAppIcon(cr, icon, cellX(id) + W / 2.0, cy, 18);
            continue;
        }
        const std::string num = wsNumberText(app, id);
        setWsNumber(layout, nf, num);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        const double nx = cellX(id) + (W - tw) / 2.0, ny = cy - th / 2.0;
        setSource(cr, occupied(id) ? onContainer : inactive);
        cairo_move_to(cr, nx, ny);
        pango_cairo_show_layout(cr, layout);
        if (id == app.workspace) {
            cairo_save(cr);
            roundedRect(cr, pillX, pillY, kM3WsPill, kM3WsPill, kM3WsPill / 2);
            cairo_clip(cr);
            setSource(cr, onFocus);
            cairo_move_to(cr, nx, ny);
            pango_cairo_show_layout(cr, layout);
            cairo_restore(cr);
        }
    }
    pango_font_description_free(nf);
    pango_layout_set_font_description(layout, font);
}

void draw(Bar& bar) {
    if (!bar.configured || bar.width == 0) return;
    if (sideBar(*bar.app)) { drawSide(bar); return; }
    App& app = *bar.app;
    BarInkScope inkScope(app);
    layoutBar(bar);
    if (notchLike(app.cfg.barStyle)) setBarRegion(bar);

    const int totalH = static_cast<int>(bar.height + bar.extra);
    const int stride = static_cast<int>(bar.width) * 4;
    const int size = stride * totalH;

    int fd = memfd_create("end4pC-buffer", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        std::perror("shm");
        std::exit(1);
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, bar.width, totalH, stride);
    cairo_t* cr = mkcr(surface);

    drawBarBackground(cr, bar);
    cairo_push_group(cr);
    cairo_translate(cr, 0, bar.contentY);
    drawGroupCells(cr, bar);

    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Sans Bold 13");
    pango_layout_set_font_description(layout, font);

    int tw = 0, th = 0;

    if (hasSlot(bar, "workspaces")) {
        const bool hov = app.hoverBar == &bar;
        const Hit hit = hov ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
        PangoFontDescription* numFont = pango_font_description_from_string("Sans Bold 11");
        const int wsCount = wsShown(app);
        if (pillStyle(app)) {
            const double bx = slotX(bar, "workspaces"), by = (bar.height - kPillH) / 2.0;
            roundedRect(cr, bx, by, wsTotalW(app), kPillH, kPillH / 2);
            setSource(cr, app.color("surface_container_high"));
            cairo_fill(cr);
            if (app.workspace >= 1 && app.workspace <= wsCount) {
                roundedRect(cr, bx + kPillPad + (app.wsPill - 1) * kPillCell, (bar.height - kPillInner) / 2.0, kPillCell, kPillInner, kPillInner / 2);
                setSource(cr, app.color("primary"));
                cairo_fill(cr);
            }
        }
        if (m3Ws(app)) m3WsPaint(cr, app, bar, layout, hit, font);
        else for (int id = 1; id <= wsShown(app); id++) {
            const double wx = wsXOf(bar, id), ww = wsWidth(app, id);
            const bool active = id == app.workspace;
            if (pillStyle(app)) {
                const bool hot = hit == kHitWs + id, occ = app.occupied.count(id) > 0;
                if (hot && !active) { roundedRect(cr, wx, (bar.height - kPillInner) / 2.0, ww, kPillInner, kPillInner / 2); setSource(cr, app.color("on_surface"), 0.1); cairo_fill(cr); }
                if (cairo_surface_t* icon = wsAppIcon(app, id)) {
                    drawWsAppIcon(cr, icon, wx + ww / 2.0, bar.height / 2.0, 18);
                    continue;
                }
                const double near = std::max(0.0, 1.0 - std::abs(app.wsPill - id));
                const Color a = app.color(hot ? "on_surface" : occ ? "on_surface_variant" : "outline"), b = app.color("on_primary");
                setSource(cr, Color{a.r + (b.r - a.r) * near, a.g + (b.g - a.g) * near, a.b + (b.b - a.b) * near});
                const std::string num = wsNumberText(app, id);
                setWsNumber(layout, numFont, num);
                pango_layout_get_pixel_size(layout, &tw, &th);
                cairo_move_to(cr, wx + (ww - tw) / 2.0, (bar.height - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_layout_set_font_description(layout, font);
            } else if (iconStyleWs(app)) {
                const double wy = (bar.height - kIcoH) / 2.0;
                const bool hot = hit == kHitWs + id;
                if (active || hot) {
                    roundedRect(cr, wx, wy, ww, kIcoH, 12);
                    setSource(cr, app.color(active ? "surface_container_highest" : "surface_container_high"));
                    cairo_fill(cr);
                }
                const Color c = app.color(active || hot ? "on_surface" : app.occupied.count(id) ? "on_surface_variant" : "outline");
                if (cairo_surface_t* icon = wsAppIcon(app, id)) {
                    drawWsAppIcon(cr, icon, wx + ww / 2.0, wy + kIcoH / 2.0, 20);
                } else if (id >= 1 && id <= 10) {
                    drawIcon(cr, kSideWsIcons[id - 1], wx + (ww - kIcoSize) / 2.0, wy + (kIcoH - kIcoSize) / 2.0, kIcoSize, c, active);
                } else {
                    const std::string num = wsNumberText(app, id);
                    setWsNumber(layout, numFont, num);
                    pango_layout_get_pixel_size(layout, &tw, &th);
                    setSource(cr, c);
                    cairo_move_to(cr, wx + (ww - tw) / 2.0, wy + (kIcoH - th) / 2.0);
                    pango_cairo_show_layout(cr, layout);
                    pango_layout_set_font_description(layout, font);
                }
            } else if (numberedStyle(app) || ringStyleWs(app)) {
                const double wh = kNumH;
                const double wy = (bar.height - wh) / 2.0;
                const bool hot = hit == kHitWs + id;
                roundedRect(cr, wx, wy, ww, wh, ringStyleWs(app) ? wh / 2 : active ? 10 : 8);
                if (active) {
                    setSource(cr, app.color("primary"));
                    cairo_fill(cr);
                } else if (!ringStyleWs(app)) {
                    if (hot || app.occupied.count(id)) { setSource(cr, app.color(hot ? "secondary_container" : "surface_container_high")); cairo_fill(cr); }
                    cairo_new_path(cr);
                } else {
                    if (hot) {
                        setSource(cr, app.color("surface_container_high"));
                        cairo_fill_preserve(cr);
                    }
                    setSource(cr, app.color(hot ? "on_surface" : app.occupied.count(id) ? "on_surface_variant" : "outline_variant"));
                    cairo_set_line_width(cr, 1.6);
                    cairo_stroke(cr);
                }
                if (cairo_surface_t* icon = wsAppIcon(app, id)) {
                    drawWsAppIcon(cr, icon, wx + ww / 2.0, wy + wh / 2.0, ringStyleWs(app) ? 17 : 18);
                    continue;
                }
                setSource(cr, app.color(active ? "on_primary" : hot ? (ringStyleWs(app) ? "on_surface" : "on_secondary_container") : app.occupied.count(id) ? "on_surface_variant" : "outline_variant"));
                {
                    const std::string num = wsNumberText(app, id);
                    setWsNumber(layout, numFont, num);
                    pango_layout_get_pixel_size(layout, &tw, &th);
                    cairo_move_to(cr, wx + (ww - tw) / 2.0, wy + (wh - th) / 2.0);
                    pango_cairo_show_layout(cr, layout);
                    pango_layout_set_font_description(layout, font);
                }
            } else if (barStyleWs(app)) {
                const double len = wsTickLen(app, id) + (hit == kHitWs + id ? kWsTickHover : 0), wy = (bar.height - len) / 2.0;
                roundedRect(cr, wx, wy, ww, len, ww / 2);
                setSource(cr, wsTickColor(app, id));
                cairo_fill(cr);
            } else {
                const double hh = kWsDot, wy = (bar.height - hh) / 2.0;
                roundedRect(cr, wx, wy, ww, hh, hh / 2);
                if (active) setSource(cr, app.color("primary"));
                else if (hit == kHitWs + id) setSource(cr, app.color("on_surface"));
                else if (app.occupied.count(id)) setSource(cr, app.color("on_surface_variant"));
                else setSource(cr, app.color("outline_variant"));
                cairo_fill(cr);
            }
        }
        pango_font_description_free(numFont);
    }

    if (hasSlot(bar, "media") && musicProgress(app)) musicPaintProgress(cr, app, bar, layout, font);
    if (hasSlot(bar, "media") && musicM3(app)) musicPaintM3(cr, app, bar, layout, font);
    if (hasSlot(bar, "media") && !musicM3(app) && !musicProgress(app)) {
        const std::string style = widgetStyle(app, "media");
        const Hit hit = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
        const bool lit = hit == HitMedia || hit == HitPrev || hit == HitPlay || hit == HitNext;
        const bool framed = genericFrame(style);
        Color ink = app.color("on_surface");
        if (framed) ink = styleFrame(cr, app, style, musicX(bar), (bar.height - kMediaFrameH) / 2.0, musicW(app), kMediaFrameH, lit);
        const bool roundCover = (framed && style != "square") || (mediaOnCell(app) && app.cfg.groupStyle == "separated");
        const double cov = musicCoverPx(app), scale = cov / kMusicArt;
        const double mx = musicX(bar) + musicPad(app), artY = (bar.height - cov) / 2.0;

        cairo_save(cr);
        cairo_translate(cr, mx, artY);
        cairo_scale(cr, scale, scale);
        const double cr_r = roundCover ? kMusicArt / 2.0 : 7;
        cairo_new_sub_path(cr);
        cairo_arc(cr, kMusicArt - cr_r, cr_r, cr_r, -M_PI / 2, 0);
        cairo_arc(cr, kMusicArt - cr_r, kMusicArt - cr_r, cr_r, 0, M_PI / 2);
        cairo_arc(cr, cr_r, kMusicArt - cr_r, cr_r, M_PI / 2, M_PI);
        cairo_arc(cr, cr_r, cr_r, cr_r, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
        if (app.musicArt) {
            cairo_clip(cr);
            cairo_set_source_surface(cr, app.musicArt, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            if (framed) setSource(cr, ink, 0.15); else setSource(cr, app.color("surface_container_high"));
            cairo_fill(cr);
            cairo_restore(cr);
            const double noteS = 18 * scale;
            drawIcon(cr, "music_note", mx + (cov - noteS) / 2.0, artY + (cov - noteS) / 2.0, noteS, framed ? ink : app.color("on_surface_variant"));
        }

        if (musicHasText(app)) {
            const std::string artistText = musicArtistShown(app), titleText = musicTitleShown(app);
            const double tx = mx + cov + kMusicGap;
            PangoFontDescription* small = pango_font_description_from_string(kArtistFont);
            PangoFontDescription* titleFont = pango_font_description_from_string(kTitleFont);
            pango_layout_set_width(layout, musicTextColW(app) * PANGO_SCALE);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            const Color titleInk = framed ? ink : app.color(app.musicStatus == "Playing" ? "on_surface" : "on_surface_variant");
            if (style == "compact") {
                pango_layout_set_font_description(layout, titleFont);
                pango_layout_set_text(layout, titleText.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, titleInk);
                cairo_move_to(cr, tx, (static_cast<double>(bar.height) - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
            } else {
                pango_layout_set_font_description(layout, small);
                pango_layout_set_text(layout, artistText.empty() ? " " : artistText.c_str(), -1);
                int ah = 0;
                pango_layout_get_pixel_size(layout, &tw, &ah);
                pango_layout_set_font_description(layout, titleFont);
                pango_layout_set_text(layout, titleText.c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                const double top = (static_cast<double>(bar.height) - (ah + th + kMusicLineSpacing)) / 2.0;

                pango_layout_set_font_description(layout, small);
                pango_layout_set_text(layout, artistText.c_str(), -1);
                if (framed) setSource(cr, ink, 0.75); else setSource(cr, app.color("on_surface_variant"));
                cairo_move_to(cr, tx, top);
                pango_cairo_show_layout(cr, layout);

                pango_layout_set_font_description(layout, titleFont);
                pango_layout_set_text(layout, titleText.c_str(), -1);
                setSource(cr, titleInk);
                cairo_move_to(cr, tx, top + ah + kMusicLineSpacing);
                pango_cairo_show_layout(cr, layout);
            }

            pango_layout_set_width(layout, -1);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
            pango_layout_set_font_description(layout, font);
            pango_font_description_free(small);
            pango_font_description_free(titleFont);
        }

        const double cx = mx + musicCtrlX(app);
        const double by = (bar.height - kQuickBox) / 2.0;
        const char* icons[3] = {"skip_previous", app.musicStatus == "Playing" ? "pause" : "play_arrow", "skip_next"};
        const Hit ids[3] = {HitPrev, HitPlay, HitNext};
        for (int i = 0; i < 3; i++) {
            const double ix = cx + i * kMusicBtn;
            const bool hot = hit == ids[i];
            if (hot) {
                cairo_arc(cr, ix + kQuickBox / 2.0, by + kQuickBox / 2.0, kQuickBox / 2.0, 0, 2 * M_PI);
                if (framed) setSource(cr, ink, 0.15); else setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, icons[i], ix + (kQuickBox - 18) / 2.0, by + (kQuickBox - 18) / 2.0, 18, framed ? ink : app.color("on_surface"), hot);
        }
    }

    if (hasSlot(bar, "quickactions")) {
        const Hit hit = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
        const double by = (bar.height - kQuickBox) / 2.0;
        const std::vector<int> shown = quickShown(app);
        const std::string qStyle = widgetStyle(app, "quickactions");
        const bool m3Group = qStyle == "m3" || qStyle == "split";
        if (qStyle == "m3" && !shown.empty()) {
            roundedRect(cr, quickX(bar), (bar.height - kM3Box) / 2.0, quickW(app), kM3Box, kM3Box / 2);
            setSource(cr, app.color("primary_container"));
            cairo_fill(cr);
        }
        for (size_t i = 0; i < shown.size(); i++) {
            const double ix = quickX(bar) + quickPadX(app) + quickItemX(app, i), iw = quickItemW(app, shown[i]);
            const bool hot = hit == kHitQuick + shown[i];
            bool live = false, alert = false;
            const char* qIcon = quickIcon(app, shown[i], live, alert);
            Color ink = app.color("on_surface");
            if (m3Group) ink = quickM3Frame(cr, app, qStyle, ix, qStyle == "m3" ? by + kM3Inset : by, quickBase(app), i, shown.size(), hot || live, iw);
            else if (qStyle != "plain") ink = roundButtonFrame(cr, app, qStyle, ix, by, iw, kQuickBox, hot || live);
            else if (hot || live) {
                cairo_arc(cr, ix + kQuickBox / 2.0, by + kQuickBox / 2.0, kQuickBox / 2.0, 0, 2 * M_PI);
                if (live) {
                    const Color e = app.color("error"), base = app.color("surface_container_high");
                    setSource(cr, Color{e.r * 0.3 + base.r * 0.7, e.g * 0.3 + base.g * 0.7, e.b * 0.3 + base.b * 0.7});
                } else setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, qIcon, ix + (iw - 18) / 2.0, by + (kQuickBox - 18) / 2.0, 18,
                     alert || (live && !m3Group) ? app.color("error") : ink, hot || live);
        }
    }

    if (hasSlot(bar, "network") && paintStyle(app, "network") == "m3") {
        drawNetworkM3(cr, app, networkX(bar), bar.height);
    } else if (hasSlot(bar, "network")) {
        const bool nh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitNetwork;
        const double nx = networkX(bar), ny = (bar.height - kQuickBox) / 2.0, nhgt = kQuickBox;
        const std::string ns = widgetStyle(app, "network");
        const double ne = netSpeedShown(app) ? 0 : softExtra(app, "network");
        const Color framed = styleFrame(cr, app, ns, nx, ny, netExtent(app, false), nhgt, nh);
        const Color ink = app.netKind == "none" ? app.color("outline") : framed;
        const char* icon = app.netKind == "wifi" ? "wifi" : app.netKind == "ethernet" ? "settings_ethernet"
                         : app.netKind == "vpn" ? "vpn_lock" : "link_broken";
        if (ns == "label") {
            const char* text = app.netKind == "wifi" ? "Wi-Fi" : app.netKind == "ethernet" ? "Ethernet" : app.netKind == "vpn" ? "VPN"
                             : app.netKind == "none" ? "Offline" : "Network";
            PangoFontDescription* f = pango_font_description_from_string("Sans 10");
            pango_font_description_set_weight(f, PANGO_WEIGHT_MEDIUM);
            int tw = 0, th = 0;
            pango_layout_set_font_description(layout, f);
            pango_layout_set_text(layout, text, -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            const double gx = nx + ne / 2 + (kNetLabelW - (18 + 6 + tw)) / 2.0;
            drawIcon(cr, icon, gx, ny + (nhgt - 18) / 2.0, 18, ink, nh);
            setSource(cr, ink);
            cairo_move_to(cr, gx + 24, ny + (nhgt - th) / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_font_description(layout, font);
            pango_font_description_free(f);
        } else if (netIconShown(app)) {
            drawIcon(cr, icon, nx + ne / 2 + (kNetW - 18) / 2.0, ny + (nhgt - 18) / 2.0, 18, ink, nh);
        }
        if (netSpeedShown(app)) drawNetSpeed(cr, app, nx + netIconPart(app, false), ny, kNetSpeedW, nhgt, true, framedStyle(ns) ? &framed : nullptr);
    }

    if (hasSlot(bar, "battery")) {
        const double bx = batteryX(bar), bhgt = kQuickBox, by = (bar.height - bhgt) / 2.0;
        const App::Periph* dev = app.batPresent ? nullptr : lowPeripheral(app);
        const int pct = dev ? dev->pct : app.batPct;
        const bool low = dev || (!app.batCharging && app.batPct <= app.cfg.batteryLow);
        const std::string icon = batteryIconName(app);
        const std::string bs = paintStyle(app, "battery");
        const double bw = batteryW(app), frac = std::clamp(pct / 100.0, 0.0, 1.0);
        const bool bh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == kHitStatus + 3;
        Color ink = app.color(low ? "error" : app.batCharging ? "primary" : "on_surface");
        if (bs == "badge" && low) {
            roundedRect(cr, bx, by, bw, bhgt, bhgt / 2);
            setSource(cr, app.color(bh ? "error_container" : "error"));
            cairo_fill(cr);
            ink = app.color(bh ? "on_error_container" : "on_error");
        } else if (bs != "plain" && bs != "m3") {
            const Color framed = styleFrame(cr, app, bs, bx, by, bw, bhgt, bh);
            if (genericFrame(bs) && !low) ink = framed;
        }
        if (bs == "m3") {
            drawBatteryM3(cr, app, bx, bar.height, pct, app.batCharging, app.color(low && !app.batCharging ? "error" : "on_primary_container"));
        } else if (bs == "ring") {
            const double cx = bx + bw / 2.0, cy = by + bhgt / 2.0, rad = 11.0;
            cairo_save(cr);
            cairo_set_line_width(cr, 2.5);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
            setSource(cr, app.color("secondary_container"));
            cairo_stroke(cr);
            if (frac > 0) {
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, rad, -M_PI / 2, -M_PI / 2 + 2 * M_PI * frac);
                setSource(cr, ink);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
            drawIcon(cr, icon, cx - 7, cy - 7, 14, ink);
        } else if (bs == "bar") {
            const double barL = iconInkInset(icon, 18, false), barInk = 18 - barL - iconInkInset(icon, 18, true);
            const double capX = bx + 4 + barInk + 6, capH = 12, capW = bx + bw - 4 - capX, capY = by + (bhgt - capH) / 2.0;
            drawIcon(cr, icon, bx + 4 - barL, by + (bhgt - 18) / 2.0, 18, ink);
            roundedRect(cr, capX, capY, capW, capH, capH / 2);
            setSource(cr, app.color("secondary_container"));
            cairo_fill(cr);
            roundedRect(cr, capX, capY, std::max(capH, capW * frac), capH, capH / 2);
            setSource(cr, ink);
            cairo_fill(cr);
        } else {
            const bool showPct = bs == "text" || app.cfg.batteryPercent;
            const double inkL = iconInkInset(icon, static_cast<int>(kBatIcon), false), inkW = kBatIcon - inkL - iconInkInset(icon, static_cast<int>(kBatIcon), true);
            if (bs != "text") drawIcon(cr, icon, bx + (showPct ? kBatInkPad : (bw - inkW) / 2.0) - inkL, by + (bhgt - kBatIcon) / 2.0, kBatIcon, ink);
            if (showPct) {
                PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
                int tw = 0, th = 0;
                pango_layout_set_font_description(layout, f);
                pango_layout_set_text(layout, (std::to_string(pct) + "%").c_str(), -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, ink);
                cairo_move_to(cr, bs == "text" ? bx + (bw - tw) / 2.0 : bx + kBatInkPad + inkW + kBatGap, by + (bhgt - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_layout_set_font_description(layout, font);
                pango_font_description_free(f);
            }
        }
    }

    if (hasSlot(bar, "bluetooth")) btBarPaint(cr, app, bar);
    if (hasSlot(bar, "drives")) drivesBarPaint(cr, app, bar);
    if (hasSlot(bar, "privacy")) privacyBarPaint(cr, app, bar);
    if (hasSlot(bar, "volume") && paintStyle(app, "volume") == "m3") {
        drawVolumeM3(cr, app, volumeX(bar), bar.height);
    } else if (hasSlot(bar, "volume")) {
        const bool vh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitVolume;
        const double vx = volumeX(bar), vy = (bar.height - kQuickBox) / 2.0, vw = volumeW(app), vhgt = kQuickBox, ve = softExtra(app, "volume");
        const std::string vs = widgetStyle(app, "volume");
        const Color framed = styleFrame(cr, app, vs, vx, vy, vw, vhgt, vh);
        const char* icon = app.muted || app.volume == 0 ? "volume_off" : app.volume < 50 ? "volume_down" : "volume_up";
        const Color ink = app.muted ? app.color("outline") : framed;
        drawIcon(cr, icon, vx + ve / 2 + (kVolIconW - 18) / 2.0, vy + (vhgt - 18) / 2.0, 18, ink, vh);
        if (vs == "level") {
            const double lx = vx + ve / 2 + 26, lw = vw - 30 - ve, ly = vy + (vhgt - 4) / 2.0;
            const double frac = app.muted ? 0.0 : std::clamp(app.volume / 100.0, 0.0, 1.0);
            roundedRect(cr, lx, ly, lw, 4, 2);
            setSource(cr, app.color("secondary_container"));
            cairo_fill(cr);
            if (frac > 0) {
                roundedRect(cr, lx, ly, std::max(4.0, lw * frac), 4, 2);
                setSource(cr, app.color("primary"));
                cairo_fill(cr);
            }
        } else if (vs == "percent") {
            PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
            int tw = 0, th = 0;
            pango_layout_set_font_description(layout, f);
            pango_layout_set_text(layout, (std::to_string(app.volume) + "%").c_str(), -1);
            pango_layout_get_pixel_size(layout, &tw, &th);
            setSource(cr, ink);
            cairo_move_to(cr, vx + ve / 2 + 26 + (vw - 30 - ve - tw) / 2.0, vy + (vhgt - th) / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_layout_set_font_description(layout, font);
            pango_font_description_free(f);
        }
    }

    if (hasSlot(bar, "resources")) {
        const auto items = resItems(app);
        const std::string resLook = widgetStyle(app, "resources");
        const double one = resOneW(app), val = resValW(app);
        double x = slotX(bar, "resources");
        for (const ResItem& ri : items) {
            drawResource(cr, app, ri, x + one / 2, bar.height / 2.0, layout);
            if (app.cfg.resValue && resLook != "bars") {
                char vb[8];
                std::snprintf(vb, sizeof vb, "%d", static_cast<int>(std::lround(std::clamp(ri.pct, 0.0, 1.0) * 100)));
                PangoFontDescription* f = pango_font_description_from_string("Sans Bold 10");
                pango_layout_set_font_description(layout, f);
                pango_layout_set_text(layout, vb, -1);
                int vw = 0, vh = 0;
                pango_layout_get_pixel_size(layout, &vw, &vh);
                setSource(cr, app.color("on_surface_variant"));
                const double vx = resLook == "pill" ? x + 7 + 14 + 3 + (one + val - 7 - 14 - 3 - 6 - vw) / 2.0 : x + one + 3;
                cairo_move_to(cr, vx, (bar.height - vh) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_layout_set_font_description(layout, font);
                pango_font_description_free(f);
            }
            x += one + val + 6;
        }
    }

    const Hit hotNow = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
    dtpBarPaint(cr, app, bar, hotNow);
    if (hasSlot(bar, "weather")) { weatherBarPaint(cr, app, layout, slotX(bar, "weather"), bar.height / 2.0, hotNow == HitWeather); pango_layout_set_font_description(layout, font); }
    if (hasSlot(bar, "updates")) { updatesBarPaint(cr, app, layout, slotX(bar, "updates"), bar.height / 2.0, hotNow == HitUpdates); pango_layout_set_font_description(layout, font); }
    if (hasSlot(bar, "activewindow")) { awinPaint(cr, app, layout, slotX(bar, "activewindow"), (bar.height - kQuickBox) / 2.0, awinBarW(app), kQuickBox, false); pango_layout_set_font_description(layout, font); }
    if (hasSlot(bar, "keyboard") && !app.kbd.list.empty()) { kbdPaint(cr, app, layout, slotX(bar, "keyboard"), (bar.height - kQuickBox) / 2.0, kbdBarW(app), kQuickBox, false, app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitKeyboard); pango_layout_set_font_description(layout, font); }
    if (hasSlot(bar, "aiusage")) { aiBarPaint(cr, app, layout, slotX(bar, "aiusage"), bar.height / 2.0, hotNow >= kHitAi && hotNow < kHitAi + 2 ? hotNow - kHitAi : -1); pango_layout_set_font_description(layout, font); }
    for (const auto& [x, style] : bar.dividers)
        dividerPaint(cr, app, x, bar.height / 2.0, true, style);
    for (int k = 0;; k++) {
        const std::string vz = "visualizer#" + std::to_string(k);
        if (!hasSlot(bar, vz.c_str())) break;
        vizPaint(cr, app, slotX(bar, vz.c_str()) + moduleWidth(app, "visualizer") / 2.0, bar.height / 2.0, false, k < static_cast<int>(bar.vizMirror.size()) && bar.vizMirror[k]);
    }

    if (hasSlot(bar, "island")) {
        if (!app.avatarTried) loadAvatar(const_cast<App&>(app));
        if (!app.ic.hidesBar()) islandPaint(cr, app, layout, bar);
        pango_layout_set_font_description(layout, font);
    }

    if (hasSlot(bar, "clock")) {
        clockBarPaint(cr, app, layout, clockX(bar), bar.height, false);
        pango_layout_set_font_description(layout, font);
    }

    if (hasSlot(bar, "avatar")) {
        if (!app.avatarTried) loadAvatar(const_cast<App&>(app));
        const bool ah = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitAvatar;
        const std::string aStyle = widgetStyle(app, "avatar");
        const double avSize = kBarAvatar, pop = aStyle == "m3" && ah ? kM3AvatarHover : 1;
        const double ax = avatarX(bar) - (avSize * pop - avSize) / 2, ay = (bar.height - avSize * pop) / 2.0;
        constexpr double kRingPad = 3;
        constexpr double kShapeGrow = 4;
        const bool m3a = aStyle == "m3";
        const double pad = aStyle == "ring" ? kRingPad : m3a ? -kShapeGrow / 2 : 0, side = avSize * pop - 2 * pad;
        const double px = ax + pad, py = ay + pad;
        cairo_save(cr);
        cairo_new_sub_path(cr);
        if (m3a) shapePath(cr, kM3IconShape, px, py, side);
        else if (aStyle == "squircle") roundedRect(cr, px, py, side, side, side * 0.36);
        else cairo_arc(cr, px + side / 2, py + side / 2, side / 2, 0, 2 * M_PI);
        if (app.avatar) {
            cairo_clip(cr);
            cairo_translate(cr, px, py);
            const double sc = side / cairo_image_surface_get_width(app.avatar);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, app.avatar, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint_with_alpha(cr, ah ? 1.0 : 0.92);
        } else {
            setSource(cr, app.color("primary_container"));
            cairo_fill(cr);
        }
        cairo_restore(cr);
        if (aStyle == "ring") {
            cairo_arc(cr, ax + avSize / 2, ay + avSize / 2, avSize / 2 - 1, 0, 2 * M_PI);
            setSource(cr, app.color("primary"));
            cairo_set_line_width(cr, 2);
            cairo_stroke(cr);
        }
        if (!app.avatar) {
            const int glyph = static_cast<int>(side) - 8;
            drawIcon(cr, "user-circle", px + (side - glyph) / 2.0, py + (side - glyph) / 2.0, glyph, app.color("on_primary_container"));
        }
    }

    if (hasSlot(bar, "systray")) {
        std::vector<int> pin, oth;
        traySplit(app, pin, oth);
        const double sx = slotX(bar, "systray"), p = 1 - std::pow(1 - app.trayT, 3), pw = pin.size() * kTrayItem, iw = oth.size() * kTrayItem * p;
        const double by = (bar.height - kQuickBox) / 2.0;
        const Hit hh = app.hoverBar == &bar ? hitTest(bar, app.pointerX, app.pointerY) : HitNone;
        auto icon = [&](int i, double ix, double alpha) {
            if (hh == kHitTray + i) {
                roundedRect(cr, ix, by, kTrayItem, kQuickBox, kQuickBox / 2.0);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawTrayGlyph(cr, app, app.tray[i].surf, ix + (kTrayItem - kTrayIcon) / 2.0, (bar.height - kTrayIcon) / 2.0, alpha);
        };
        for (size_t k = 0; k < pin.size(); k++) icon(pin[k], sx + k * kTrayItem, 1.0);
        if (!oth.empty()) {
            const bool fwd = trayForward(app, false);
            const double ix0 = sx + pw + (fwd ? kQuickBox : 0);
            if (iw > 0.5) {
                cairo_save(cr);
                cairo_rectangle(cr, ix0, 0, iw, bar.height);
                cairo_clip(cr);
                for (size_t k = 0; k < oth.size(); k++) icon(oth[k], ix0 + k * kTrayItem, p);
                cairo_restore(cr);
            }
            const double cx = fwd ? sx + pw : sx + pw + iw;
            if (hh == HitTray) {
                roundedRect(cr, cx, by, kQuickBox, kQuickBox, kQuickBox / 2.0);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, (app.trayOpen == fwd) ? "alt-arrow-left" : "alt-arrow-right", cx + (kQuickBox - 18) / 2.0, by + (kQuickBox - 18) / 2.0, 18, app.color("on_surface"), hh == HitTray);
        }
    }

    if (hasSlot(bar, "notifications")) {
        const bool nh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitNotif;
        const double nw = notifW(app), nhgt = std::max<double>(kQuickBox, roundBtnW(app, "notifications")), nx = notifX(bar), ny = (bar.height - nhgt) / 2.0;
        const bool any = app.notifCount > 0;
        const bool dnd = app.cfg.doNotDisturb != 0;
        const char* bell = dnd ? "notifications_off" : any ? "notifications_active" : "notifications";
        const std::string style = widgetStyle(app, "notifications");
        if (style == "m3" || style == "m3filled") {  // the count is a small pill on the top corner, not beside the bell
            const Color ink = style == "m3" ? m3Circle(cr, app, nx, ny, nhgt, nh, "primary", "on_primary", "on_primary") : roundButtonFrame(cr, app, style, nx, ny, nw, nhgt, nh);
            drawIcon(cr, bell, nx + (nw - 18) / 2.0, ny + (nhgt - 18) / 2.0, 18, ink, nh);
            if (any) {
                char cnt[8];
                std::snprintf(cnt, sizeof cnt, "%d", app.notifCount > 99 ? 99 : app.notifCount);
                PangoFontDescription* bf = pango_font_description_from_string("Sans Bold 7");
                pango_layout_set_font_description(layout, bf);
                pango_layout_set_text(layout, cnt, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                constexpr double badgeH = 12;
                const double badgeW = std::max<double>(tw + 6, badgeH);
                const double badgeX = nx + nw - badgeW + 3, badgeY = ny - 3;
                roundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2);
                setSource(cr, app.color("on_primary"));
                cairo_fill(cr);
                setSource(cr, app.color("primary"));
                cairo_move_to(cr, badgeX + (badgeW - tw) / 2.0, badgeY + (badgeH - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_layout_set_font_description(layout, font);
                pango_font_description_free(bf);
            }
        } else if (style == "count") {  // the bell, with the number on a small ball at the top corner
            if (nh) {
                roundedRect(cr, nx, ny, nw, nhgt, nhgt / 2);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, bell, nx + (nw - 18) / 2.0, ny + (nhgt - 18) / 2.0, 18, app.color(dnd ? "on_surface_variant" : "on_surface"), nh);
            if (any) {
                char cnt[8];
                std::snprintf(cnt, sizeof cnt, "%d", app.notifCount > 99 ? 99 : app.notifCount);
                PangoFontDescription* bf = pango_font_description_from_string("Sans Bold 7");
                pango_layout_set_font_description(layout, bf);
                pango_layout_set_text(layout, cnt, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                constexpr double badgeH = 12;
                const double badgeW = std::max<double>(tw + 6, badgeH);
                const double badgeX = nx + nw - badgeW + 1, badgeY = ny - 1;
                roundedRect(cr, badgeX, badgeY, badgeW, badgeH, badgeH / 2);
                setSource(cr, app.color("primary"));
                cairo_fill(cr);
                setSource(cr, app.color("on_primary"));
                cairo_move_to(cr, badgeX + (badgeW - tw) / 2.0, badgeY + (badgeH - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
                pango_layout_set_font_description(layout, font);
                pango_font_description_free(bf);
            }
        } else if (style == "plain" || style == "dot") {
            if (nh) {
                roundedRect(cr, nx, ny, nw, nhgt, nhgt / 2);
                setSource(cr, app.color("surface_container_high"));
                cairo_fill(cr);
            }
            drawIcon(cr, bell, nx + (kNotifW - 18) / 2.0, ny + (nhgt - 18) / 2.0, 18,
                     app.color(dnd ? "on_surface_variant" : any ? "primary" : "on_surface"), nh);
            if (any && style == "dot") {
                cairo_arc(cr, nx + kNotifW - 5, ny + 6, 3.5, 0, 2 * M_PI);
                setSource(cr, app.color("primary"));
                cairo_fill(cr);
            } else if (any) {
                char cnt[8];
                std::snprintf(cnt, sizeof cnt, "%d", app.notifCount > 99 ? 99 : app.notifCount);
                pango_layout_set_text(layout, cnt, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                setSource(cr, app.color("primary"));
                cairo_move_to(cr, nx + kNotifW - 3, ny + (nhgt - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
            }
        } else {
            const Color ink = roundButtonFrame(cr, app, style, nx, ny, nw, nhgt, nh);
            double bx = nx + (nw - 18) / 2.0;
            if (any) {
                char cnt[8];
                std::snprintf(cnt, sizeof cnt, "%d", app.notifCount > 99 ? 99 : app.notifCount);
                pango_layout_set_text(layout, cnt, -1);
                pango_layout_get_pixel_size(layout, &tw, &th);
                bx = nx + (nw - (19 + tw)) / 2.0;
                setSource(cr, ink);
                cairo_move_to(cr, bx + 19, ny + (nhgt - th) / 2.0);
                pango_cairo_show_layout(cr, layout);
            }
            drawIcon(cr, bell, bx, ny + (nhgt - 18) / 2.0, 18, ink, nh);
        }
    }

    if (hasSlot(bar, "settings")) {
        const bool sh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == HitSettings;
        const double box = roundBtnW(app, "settings"), bw = roundBtnWide(app, "settings"), sx = settingsX(bar), sy = (bar.height - box) / 2.0;
        const std::string ss = widgetStyle(app, "settings");
        const Color ink = ss == "m3" ? m3Circle(cr, app, sx, sy, box, sh, "primary_container", "on_primary_container", "on_surface")
                                     : roundButtonFrame(cr, app, ss, sx, sy, bw, box, sh);
        drawIcon(cr, "settings", sx + (bw - 18) / 2.0, sy + (box - 18) / 2.0, 18, ink, sh);
    }

    for (const char* mod : {"launcher", "power", "tools"}) {
        if (!hasSlot(bar, mod)) continue;
        const bool isL = std::string(mod) == "launcher", isT = std::string(mod) == "tools";
        const bool sh = app.hoverBar == &bar && hitTest(bar, app.pointerX, app.pointerY) == (isL ? HitLauncher : isT ? HitTools : HitPower);
        const double box = roundBtnW(app, mod), bw = roundBtnWide(app, mod), bx = slotX(bar, mod), by = (bar.height - box) / 2.0;
        const std::string ms = widgetStyle(app, mod);
        if (isL && ms == "m3") { launcherM3Paint(cr, app, layout, bx, bar.height / 2.0, sh); pango_layout_set_font_description(layout, font); continue; }
        const bool power = !isL && !isT;
        const Color ink = ms != "m3" ? roundButtonFrame(cr, app, ms, bx, by, bw, box, sh)
            : power ? m3ShapedCircle(cr, app, bx, by, box, sh, "primary", "on_primary", "on_primary", "Cookie12Sided")
            : isL ? m3ShapedCircle(cr, app, bx, by, box, sh, "primary_container", "on_primary_container", "on_primary_container", "Cookie9Sided")
                    : m3Circle(cr, app, bx, by, box, sh, "primary_container", "on_primary_container", "on_surface");
        drawIcon(cr, isL ? "magnifer" : isT ? "ruler-pen" : "power", bx + (bw - 18) / 2.0, by + (box - 18) / 2.0, 18, ink, sh);
    }

    pango_font_description_free(font);
    g_object_unref(layout);
    popWithShadow(cr, app);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, bar.width, totalH, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, &bar);
    bar.buffer = buffer;
    updateBlurRegion(app, bar.surface, bar.blur, bar.width, totalH, [&](cairo_t* m) { drawBarBackground(m, bar); });

    wl_surface_attach(bar.surface, buffer, 0, 0);
    wl_surface_damage_buffer(bar.surface, 0, 0, bar.width, totalH);
    wl_surface_commit(bar.surface);
    refreshBarShadow(bar);
}

void drawAll(App& app) {
    for (auto& bar : app.bars) draw(*bar);
    deskSync(app);
}

void redrawWallpapers(App& app) {
    for (auto& bar : app.bars) drawWallpaper(*bar, app);
    malloc_trim(0);
}
