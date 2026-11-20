

void onBufferRelease(void* userData, wl_buffer* buffer) {
    auto* bar = static_cast<Bar*>(userData);
    if (bar && bar->buffer == buffer) bar->buffer = nullptr;
    wl_buffer_destroy(buffer);
}
const wl_buffer_listener kBufferListener = {onBufferRelease};

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}
std::string stateDir() {
    const char* d = std::getenv("XDG_STATE_HOME");
    return (d ? std::string(d) : homeDir() + "/.local/state") + "/end4pC";
}
struct CookieChoice { const char* key; const char* def; std::vector<std::string> allowed; };
const std::vector<CookieChoice> kCookieChoices = {
    {"cookieDial", "full", {"dots", "numbers", "full", "none"}},
    {"cookieHour", "fill", {"classic", "fill", "hollow", "hide"}},
    {"cookieMinute", "medium", {"classic", "thin", "medium", "bold", "hide"}},
    {"cookieSecond", "hide", {"dot", "line", "classic", "hide"}},
    {"cookieDate", "bubble", {"bubble", "rect", "hide"}},
};
std::string* cookieField(Config& c, const std::string& key) {
    return key == "cookieDial" ? &c.cookieDial : key == "cookieHour" ? &c.cookieHour : key == "cookieMinute" ? &c.cookieMinute : key == "cookieSecond" ? &c.cookieSecond : key == "cookieDate" ? &c.cookieDate : nullptr;
}
bool cookieChanged(const Config& a, const Config& b) {
    return a.cookieSides != b.cookieSides || a.cookieTime != b.cookieTime || a.cookieDial != b.cookieDial || a.cookieHour != b.cookieHour || a.cookieMinute != b.cookieMinute ||
           a.cookieSecond != b.cookieSecond || a.cookieDate != b.cookieDate;
}
const std::vector<std::string> kDeskVizStyles = {"bars", "reflect", "wave", "dots", "winamp", "ring"};
const std::vector<std::string> kDeskVizPositions = {"bottom", "top", "center", "left", "right"};
const std::vector<std::string> kDeskVizColors = {"gradient", "primary", "secondary", "tertiary", "on_surface"};
bool inList(const std::vector<std::string>& l, const std::string& v) { return std::find(l.begin(), l.end(), v) != l.end(); }
const std::vector<std::string> kTextFonts = {"Display", "Sans", "Serif", "Mono", "Caveat", "Dancing Script", "Pacifico", "Permanent Marker", "Indie Flower", "Great Vibes", "League Gothic"};
const std::vector<std::string> kTextColors = {"on_surface", "primary", "secondary", "tertiary", "primary_container", "secondary_container", "tertiary_container", "outline", "white", "black"};
const std::vector<std::string> kTextAligns = {"left", "center", "right"};
bool deskTextChanged(const Config& a, const Config& b) {
    return a.customTextFont != b.customTextFont || a.customTextFontName != b.customTextFontName || a.customTextColor != b.customTextColor || a.customTextAlign != b.customTextAlign || a.customTextFit != b.customTextFit || a.customTextSize != b.customTextSize ||
           a.customTextBold != b.customTextBold || a.customTextItalic != b.customTextItalic || a.customTextShadow != b.customTextShadow;
}
bool hotCornerChanged(const Config& a, const Config& b) {
    return a.hotCornerWidth != b.hotCornerWidth || a.hotCornerHeight != b.hotCornerHeight || a.hotCornerVisible != b.hotCornerVisible || a.hotCornerHover != b.hotCornerHover;
}
bool deskVizChanged(const Config& a, const Config& b) {
    return a.deskVizStyle != b.deskVizStyle || a.deskVizPos != b.deskVizPos || a.deskVizColor != b.deskVizColor || a.deskVizSize != b.deskVizSize || a.deskVizBars != b.deskVizBars;
}
std::string generatedDir() { return stateDir() + "/generated"; }

void applyUiBackground(App& app) {
    app.colors = app.rawColors;
    const bool light = app.cfg.colorMode == "light";
    if (light && app.cfg.uiBackground == "white") {  // the light counterpart of OLED: the main surfaces pure white
        for (const char* n : {"background", "surface", "surface_container_lowest"}) app.colors[n] = Color{1, 1, 1};
        return;
    }
    if (app.cfg.uiBackground != "oled" || light) return;  // (black surfaces make no sense in a light theme)
    for (const char* n : {"background", "surface", "surface_dim", "surface_container_lowest"}) app.colors[n] = Color{0, 0, 0};
    for (const char* n : {"surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "surface_variant", "surface_bright"}) {
        auto it = app.rawColors.find(n);
        if (it == app.rawColors.end()) continue;
        app.colors[n] = Color{it->second.r * kOledSurfaceLift, it->second.g * kOledSurfaceLift, it->second.b * kOledSurfaceLift};
    }
}

void loadColors(App& app) {
    std::ifstream f(generatedDir() + "/colors.json");
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string t = ss.str();
    std::map<std::string, Color> out;
    size_t pos = 0;
    while ((pos = t.find('"', pos)) != std::string::npos) {
        size_t end = t.find('"', pos + 1);
        if (end == std::string::npos) break;
        std::string key = t.substr(pos + 1, end - pos - 1);
        size_t q = t.find('"', end + 1);
        size_t colon = t.find(':', end);
        if (colon == std::string::npos || q == std::string::npos) break;
        size_t qe = t.find('"', q + 1);
        std::string val = t.substr(q + 1, qe - q - 1);
        if (val.size() == 7 && val[0] == '#') {
            unsigned v = std::strtoul(val.c_str() + 1, nullptr, 16);
            out[key] = {((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0};
        }
        pos = qe + 1;
    }
    if (!out.empty()) { app.rawColors = std::move(out); applyUiBackground(app); }
}

std::string xdgConfigHome() {
    const char* d = std::getenv("XDG_CONFIG_HOME");
    return d && *d ? std::string(d) : homeDir() + "/.config";
}
std::string xdgDataHome() {
    const char* d = std::getenv("XDG_DATA_HOME");
    return d && *d ? std::string(d) : homeDir() + "/.local/share";
}
std::string legacyConfigDir() { return xdgConfigHome() + "/end4pC"; }
std::string legacyUserDir() { return xdgConfigHome() + "/end4pC-user"; }
std::string configDir() { return xdgDataHome() + "/end4pC"; }
std::string configPath() { return configDir() + "/config.json"; }

std::string assetsDir();
std::string expandHome(const std::string& path);
std::string currentWallpaper();

void migrateConfig() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(configDir(), ec);
    struct Seed {
        ~Seed() {
            namespace fs2 = std::filesystem;
            std::error_code e;
            if (!seedDefaults) return;
            const std::string pic = assetsDir() + "/wallpapers/default.jpg";
            if (!fs2::exists(pic, e) || !currentWallpaper().empty()) return;
            const std::string dir = expandHome("~/Pictures/Wallpapers"), dst = dir + "/end4pC-default.jpg";
            fs2::create_directories(dir, e);
            if (!fs2::exists(dst, e)) fs2::copy_file(pic, dst, e);
            const std::string apply = assetsDir() + "/../matugen/apply.sh";
            runDetached({"bash", apply.c_str(), dst.c_str(), "dark", "scheme-neutral"});
        }
        bool seedDefaults = false;
    } seed;
    if (!fs::exists(configPath(), ec) && !fs::exists(legacyUserDir() + "/config.json", ec) && !fs::exists(legacyConfigDir() + "/config.json", ec)) {
        const std::string defaults = assetsDir() + "/default-config.json";
        if (fs::exists(defaults, ec) && fs::copy_file(defaults, configPath(), ec)) {
            seed.seedDefaults = true;
            fs::create_directories(stateDir(), ec);
            for (const auto& e : fs::directory_iterator(assetsDir() + "/default-state", ec))
                if (e.path().extension() == ".txt" && !fs::exists(stateDir() + "/" + e.path().filename().string(), ec))
                    fs::copy_file(e.path(), stateDir() + "/" + e.path().filename().string(), ec);
        }
    }
    for (const std::string& from : {legacyUserDir(), legacyConfigDir()}) {
        if (!fs::exists(configPath(), ec) && fs::exists(from + "/config.json", ec)) fs::copy_file(from + "/config.json", configPath(), ec);
        for (const char* sub : {"themes", "actions", "i18n"})
            if (!fs::exists(configDir() + "/" + sub, ec) && fs::is_directory(from + "/" + sub, ec))
                fs::copy(from + "/" + sub, configDir() + "/" + sub, fs::copy_options::recursive, ec);
    }
}

bool readIntKey(const std::string& text, const char* key, int& out) {
    const size_t k = text.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k);
    if (colon == std::string::npos) return false;
    out = std::atoi(text.c_str() + colon + 1);
    return true;
}

bool readStringKey(const std::string& text, const char* key, std::string& out) {
    const size_t k = text.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return false;
    const size_t colon = text.find(':', k);
    const size_t q1 = colon == std::string::npos ? colon : text.find('"', colon);
    const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out = text.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

bool readStringArray(const std::string& text, const char* key, std::vector<std::string>& out, size_t from = 0) {
    const size_t k = text.find(std::string("\"") + key + "\"", from);
    if (k == std::string::npos) return false;
    const size_t open = text.find('[', k), close = text.find(']', k);
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    std::vector<std::string> items;
    size_t pos = open;
    while ((pos = text.find('"', pos)) != std::string::npos && pos < close) {
        const size_t end = text.find('"', pos + 1);
        if (end == std::string::npos || end > close) break;
        items.push_back(text.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }
    out = std::move(items);
    return true;
}

bool validIconStyle(const std::string& v) {
    return v == "outline" || v == "broken" || v == "line-duotone" || v == "bold-duotone";
}

std::vector<std::string> splitList(const std::string& v, char sep = ',');
Config sanitized(Config c) {
    c.workspaces = std::clamp(c.workspaces, 1, 20);
    c.animEnabled = c.animEnabled ? 1 : 0;
    c.barSpacing = std::clamp(c.barSpacing, 0, 24);
    c.barShadow = c.barShadow ? 1 : 0;
    c.dynamicIsland = c.dynamicIsland ? 1 : 0;
    c.islandClick = c.islandClick ? 1 : 0;
    if (c.lockAnimation != "none" && c.lockAnimation != "split_h" && c.lockAnimation != "split_v" && c.lockAnimation != "shape") c.lockAnimation = "none";
    if (!validGroupStyle(c.groupStyle)) c.groupStyle = "free";
    c.groupSpacing = std::clamp(c.groupSpacing, 2, 6);
    if (!validGroupColor(c.groupColor)) c.groupColor = "surface_container_high";
    if (!validModuleList(c.groupFree)) c.groupFree = "island";
    if (c.islandView != "media" && c.islandView != "clock") c.islandView = "media";
    c.dividerSpacing = std::clamp(c.dividerSpacing, 0, 24);
    if (c.workspaceNumbers != "normal" && c.workspaceNumbers != "roman" && c.workspaceNumbers != "han") c.workspaceNumbers = "normal";
    c.sideBarSpacing = std::clamp(c.sideBarSpacing, 0, 24);
    c.animSpeed = std::clamp(c.animSpeed, 50, 200);
    if (c.animStyle != "boing" && c.animStyle != "fade" && c.animStyle != "slide" && c.animStyle != "zoom") c.animStyle = "boing";
    c.frame = std::clamp(c.frame, 0, 16);
    c.notificationTimeout = std::clamp(c.notificationTimeout, 500, 60000);
    c.notificationAppIcons = std::clamp(c.notificationAppIcons, 0, 1);
    c.conflictCheck = std::clamp(c.conflictCheck, 0, 1);
    c.overviewLive = std::clamp(c.overviewLive, 0, 1);
    c.overviewLauncher = std::clamp(c.overviewLauncher, 0, 1);
    if (c.switcherStyle != "grid" && c.switcherStyle != "row") c.switcherStyle = "grid";
    if (c.overviewPosition != "top" && c.overviewPosition != "bottom" && c.overviewPosition != "left" && c.overviewPosition != "right") c.overviewPosition = "top";
    if (c.overviewIcon != "left" && c.overviewIcon != "center" && c.overviewIcon != "right") c.overviewIcon = "right";
    c.privacyAlways = std::clamp(c.privacyAlways, 0, 1);
    c.privacyMic = std::clamp(c.privacyMic, 0, 1);
    c.privacyCam = std::clamp(c.privacyCam, 0, 1);
    c.privacyScreen = std::clamp(c.privacyScreen, 0, 1);
    c.doNotDisturb = std::clamp(c.doNotDisturb, 0, 1);
    c.hotCorners = std::clamp(c.hotCorners, 0, 1);
    c.hotCornerWidth = std::clamp(c.hotCornerWidth, 4, 240);
    c.hotCornerHeight = std::clamp(c.hotCornerHeight, 4, 240);
    c.hotCornerVisible = c.hotCornerVisible ? 1 : 0;
    c.hotCornerHover = c.hotCornerHover ? 1 : 0;
    c.updatesEnable = std::clamp(c.updatesEnable, 0, 1);
    c.updatesAur = std::clamp(c.updatesAur, 0, 1);
    c.updatesFlatpak = std::clamp(c.updatesFlatpak, 0, 1);
    c.updatesInterval = std::clamp(c.updatesInterval, 5, 1440);
    c.updatesMinShow = std::clamp(c.updatesMinShow, 0, 500);
    c.barOpacity = std::clamp(c.barOpacity, 0, 100);
    c.notchWidth = std::clamp(c.notchWidth, 0, 100);
    c.islandRounding = std::clamp(c.islandRounding, 0, 50);
    c.fakeRounding = std::clamp(c.fakeRounding, 0, 2);
    c.unlockKeyring = c.unlockKeyring ? 1 : 0;
    c.barFollowsFrame = c.barFollowsFrame ? 1 : 0;
    if (!validFrameColor(c.frameColor)) c.frameColor = "black";
    c.lockOnStart = c.lockOnStart ? 1 : 0;
    c.fakeRoundingSize = std::clamp(c.fakeRoundingSize, 0, 60);
    c.centeredWallpaper = c.centeredWallpaper ? 1 : 0;
    c.centeredWallpaperSize = std::clamp(c.centeredWallpaperSize, 400, 800);
    if (!validBorderRole(c.centeredWallpaperColor)) c.centeredWallpaperColor = "primary_container";
    if (c.centeredWallpaperShape.empty()) c.centeredWallpaperShape = "Cookie7Sided";
    c.collage = c.collage ? 1 : 0;
    c.collageGap = std::clamp(c.collageGap, 0, 80);
    c.collageMargin = std::clamp(c.collageMargin, 0, 120);
    c.collageRadius = std::clamp(c.collageRadius, 0, 80);
    c.collageBlur = std::clamp(c.collageBlur, 0, 100);
    c.collagePrimary = std::max(1, c.collagePrimary);
    if (std::string treeError; !parseCollageTree(c.collageTree, treeError)) c.collageTree = kCollageDefaultTree;
    c.networkAlways = std::clamp(c.networkAlways, 0, 1);
    c.networkSpeed = std::clamp(c.networkSpeed, 0, 1);
    c.screenshotCopy = std::clamp(c.screenshotCopy, 0, 1);
    c.recordAudio = std::clamp(c.recordAudio, 0, 1);
    c.recordDesktop = std::clamp(c.recordDesktop, 0, 1);
    if (c.recordFps != 0 && c.recordFps != 24 && c.recordFps != 30 && c.recordFps != 60) c.recordFps = 0;
    std::set<std::string> seen;
    for (auto* region : {&c.layoutLeft, &c.layoutCenter, &c.layoutRight}) {
        std::vector<std::string> clean;
        for (const auto& m : *region)
            if ((isDivider(m) || std::find(kModules.begin(), kModules.end(), m) != kModules.end()) && (repeatable(m) || seen.insert(m).second)) clean.push_back(m);
        *region = std::move(clean);
    }
    std::set<std::string> seenSide;
    for (auto* region : {&c.sideLeft, &c.sideCenter, &c.sideRight}) {
        std::vector<std::string> clean;
        for (const auto& m : *region)
            if ((isDivider(m) || std::find(kModules.begin(), kModules.end(), m) != kModules.end()) && (repeatable(m) || seenSide.insert(m).second)) clean.push_back(m);
        *region = std::move(clean);
    }
    if (!validIconStyle(c.iconStyle)) c.iconStyle = "broken";
    c.keepAwake = c.keepAwake ? 1 : 0;
    c.widgetsLocked = c.widgetsLocked ? 1 : 0;
    c.widgetShadow = c.widgetShadow ? 1 : 0;
    c.widgetGap = std::clamp(c.widgetGap, 0, 48);
    if (!inList(kDeskVizStyles, c.deskVizStyle)) c.deskVizStyle = "bars";
    if (!inList(kDeskVizPositions, c.deskVizPos)) c.deskVizPos = "bottom";
    if (!inList(kDeskVizColors, c.deskVizColor)) c.deskVizColor = "gradient";
    c.deskVizSize = std::clamp(c.deskVizSize, 80, 600);
    c.deskVizBars = std::clamp(c.deskVizBars, 12, 96);
    if (!inList(kTextFonts, c.customTextFont)) c.customTextFont = "Display";
    if (!inList(kTextColors, c.customTextColor)) c.customTextColor = "on_surface";
    if (!inList(kTextAligns, c.customTextAlign)) c.customTextAlign = "center";
    c.customTextSize = std::clamp(c.customTextSize, 12, 400);
    c.customTextFontName.erase(std::remove_if(c.customTextFontName.begin(), c.customTextFontName.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20 || ch == '"' || ch == '\\'; }), c.customTextFontName.end());
    c.customTextFontName = c.customTextFontName.substr(0, 80);
    while (!c.customTextFontName.empty() && c.customTextFontName.back() == ' ') c.customTextFontName.pop_back();
    while (!c.customTextFontName.empty() && c.customTextFontName.front() == ' ') c.customTextFontName.erase(0, 1);
    for (int* f : {&c.customTextFit, &c.customTextBold, &c.customTextItalic, &c.customTextShadow}) *f = *f ? 1 : 0;
    c.calendarMonday = c.calendarMonday ? 1 : 0;
    c.systray = c.systray ? 1 : 0;
    c.barBackground = c.barBackground ? 1 : 0;
    c.trayPinDefault = c.trayPinDefault ? 1 : 0;
    c.trayTint = c.trayTint ? 1 : 0;
    c.weatherInterval = std::clamp(c.weatherInterval, 5, 120);
    c.weatherTheme = c.weatherTheme ? 1 : 0;
    c.materialIcons = c.materialIcons ? 1 : 0;
    c.iconScale = std::clamp((c.iconScale + 5) / 10 * 10, 60, 150);
    c.fontWeightText = std::clamp((c.fontWeightText + 5) / 10 * 10, 300, 700);
    c.fontWeightTitle = std::clamp((c.fontWeightTitle + 5) / 10 * 10, 400, 900);
    c.launcherFiles = c.launcherFiles ? 1 : 0;  // (steps of 10: an in-between size draws the glyphs on half pixels and they look dull)
    if (!validNotifPosition(c.notificationPosition)) c.notificationPosition = "top_right";
    c.clock24 = std::clamp(c.clock24, 0, 1);
    c.clockDate = std::clamp(c.clockDate, 0, 1);
    c.workspaceApps = std::clamp(c.workspaceApps, 0, 1);
    c.batteryPercent = std::clamp(c.batteryPercent, 0, 1);
    c.templateKitty = std::clamp(c.templateKitty, 0, 1);
    c.templateGtk = std::clamp(c.templateGtk, 0, 1);
    c.templateKde = std::clamp(c.templateKde, 0, 1);
    if (!validBarClock(c.barClock)) c.barClock = "pill";
    {
        std::string clean;
        for (const std::string& pair : splitList(c.widgetStyles)) {
            const size_t eq = pair.find('=');
            if (eq != std::string::npos && validWidgetStyle(pair.substr(0, eq), pair.substr(eq + 1))) clean += (clean.empty() ? "" : ",") + pair;
        }
        c.widgetStyles = clean;
        clean.clear();
        for (const std::string& pair : splitList(c.sideWidgetStyles)) {
            const size_t eq = pair.find('=');
            if (eq != std::string::npos && sideStyleAllowed(pair.substr(0, eq), pair.substr(eq + 1))) clean += (clean.empty() ? "" : ",") + pair;
        }
        c.sideWidgetStyles = clean;
    }
    if (!validLanguage(c.language)) c.language.clear();
    c.volumeMax = std::clamp(c.volumeMax, 30, 100);
    c.volumeStep = std::clamp(c.volumeStep, 1, 10);
    c.batteryLow = std::clamp(c.batteryLow, 5, 50);
    c.batteryCritical = std::clamp(c.batteryCritical, 1, 20);
    c.batteryNotify = std::clamp(c.batteryNotify, 0, 1);
    c.resCpu = std::clamp(c.resCpu, 0, 1);
    c.resRam = std::clamp(c.resRam, 0, 1);
    c.resTemp = std::clamp(c.resTemp, 0, 1);
    c.resDisk = std::clamp(c.resDisk, 0, 1);
    c.resSwap = std::clamp(c.resSwap, 0, 1);
    c.resValue = std::clamp(c.resValue, 0, 1);
    c.resInterval = std::clamp(c.resInterval, 1000, 10000);
    c.resWarnMem = std::clamp(c.resWarnMem, 50, 100);
    c.resWarnSwap = std::clamp(c.resWarnSwap, 50, 100);
    c.resWarnCpu = std::clamp(c.resWarnCpu, 50, 100);
    for (std::string* a : {&c.cornerTL, &c.cornerTR, &c.cornerBL, &c.cornerBR})
        if (std::find(kCornerActions.begin(), kCornerActions.end(), *a) == kCornerActions.end()) *a = "none";
    c.cookieSides = std::clamp(c.cookieSides, 6, 20);
    c.cookieTime = std::clamp(c.cookieTime, 0, 1);
    for (const auto& ch : kCookieChoices) {
        std::string* f = cookieField(c, ch.key);
        if (f && std::find(ch.allowed.begin(), ch.allowed.end(), *f) == ch.allowed.end()) *f = ch.def;
    }
    {
        std::string clean;
        std::set<std::string> seen;
        for (const std::string& n : splitList(c.quickActions))
            for (int i = 0; i < kQaCount; i++)
                if (n == kQuickNames[i] && seen.insert(n).second) clean += (clean.empty() ? "" : ",") + n;
        c.quickActions = clean;
    }
    for (std::string* a : {&c.themePrimary, &c.themeSecondary, &c.themeTertiary})
        a->erase(std::remove_if(a->begin(), a->end(), [](char ch) { return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'); }), a->end());
    c.theme.erase(std::remove_if(c.theme.begin(), c.theme.end(), [](char ch) { return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-'); }), c.theme.end());
    if (c.clockStyle != "cookie" && c.clockStyle != "digital" && c.clockStyle != "daily" && c.clockStyle != "pixel") c.clockStyle = "cookie";
    if (c.clockDaily != "badge" && c.clockDaily != "morning" && c.clockDaily != "script" && c.clockDaily != "bold") c.clockDaily = "badge";
    if (c.clockPixel != "horizontal" && c.clockPixel != "vertical") c.clockPixel = "horizontal";
    if (!validWidgetStyle("resources", c.resStyle)) c.resStyle = "filled";
    if (c.dividerStyle != "line" && c.dividerStyle != "dot" && c.dividerStyle != "space") c.dividerStyle = "line";
    c.aiClaude = c.aiClaude ? 1 : 0;
    c.aiCodex = c.aiCodex ? 1 : 0;
    c.aiTokenLimit = std::clamp(c.aiTokenLimit, 50, 20000);
    if (c.batteryCritical > c.batteryLow) c.batteryCritical = c.batteryLow;
    c.borderColor = c.borderColor ? 1 : 0;
    c.borderActiveOpacity = std::clamp(c.borderActiveOpacity, 0, 100);
    c.borderInactiveOpacity = std::clamp(c.borderInactiveOpacity, 0, 100);
    for (std::string* role : {&c.borderActive, &c.borderInactive})
        if (!validBorderRole(*role)) *role = role == &c.borderActive ? "primary" : "outline_variant";
    c.peripheralNotify = c.peripheralNotify ? 1 : 0;
    c.peripheralLow = std::clamp(c.peripheralLow, 5, 50);
    c.peripheralCritical = std::clamp(c.peripheralCritical, 1, std::min(20, c.peripheralLow));
    if (c.uiBackground != "themed" && c.uiBackground != "oled" && c.uiBackground != "white") c.uiBackground = "themed";
    if (c.barPosition != "top" && c.barPosition != "bottom" && c.barPosition != "left" && c.barPosition != "right") c.barPosition = "top";
    if (c.barStyle != "panel" && c.barStyle != "float" && c.barStyle != "island" && c.barStyle != "hug" && c.barStyle != "notch" && c.barStyle != "split") c.barStyle = "panel";
    if (c.dynamicIsland && (c.barStyle == "island" || c.barStyle == "split")) c.layoutCenter.clear();
    {
        std::string used;
        for (int i = 0; i < kPrefixCount; i++) {
            std::string& v = c.prefixes[i];
            const unsigned char ch = v.size() == 1 ? static_cast<unsigned char>(v[0]) : 0;
            if (!(ch > 32 && ch < 127 && !std::isalnum(ch) && ch != '"' && ch != '\\') || used.find(v[0]) != std::string::npos) v = std::string(1, kPrefixDefaults[i]);
            used += v;
        }
    }
    if (!validLyricsProvider(c.lyricsProvider)) c.lyricsProvider = "auto";
    if (c.weatherUnit != "celsius" && c.weatherUnit != "fahrenheit") c.weatherUnit = "celsius";
    if (!validWidgetStyle("workspaces", c.workspaceStyle)) c.workspaceStyle = "icons";
    if (c.colorMode != "dark" && c.colorMode != "light") c.colorMode = "dark";
    if (std::find(kSchemes.begin(), kSchemes.end(), c.colorScheme) == kSchemes.end()) c.colorScheme = "scheme-neutral";
    if (c.wallpaperDir.empty()) c.wallpaperDir = "~/Pictures/Wallpapers";
    if (!validWallpaperTransition(c.wallpaperTransition)) c.wallpaperTransition = "fade";
    c.wallpaperTransitionMs = std::clamp(c.wallpaperTransitionMs, 100, 3000);
    c.wallpaperAuto = c.wallpaperAuto ? 1 : 0;
    c.wallpaperAutoFavs = c.wallpaperAutoFavs ? 1 : 0;
    c.wallpaperAutoMin = std::clamp(c.wallpaperAutoMin, 1, 1440);
    c.dock = c.dock ? 1 : 0;
    c.dockIconSize = std::clamp(c.dockIconSize, 28, 72);
    c.dockMagnify = c.dockMagnify ? 1 : 0;
    c.dockMagnification = std::clamp(c.dockMagnification, 100, 220);
    c.dockAutoHide = c.dockAutoHide ? 1 : 0;
    c.dockPin = c.dockPin ? 1 : 0;
    c.dockMedia = c.dockMedia ? 1 : 0;
    c.dockLauncher = c.dockLauncher ? 1 : 0;
    c.dockHug = c.dockHug ? 1 : 0;
    c.dockRounding = std::clamp(c.dockRounding, 8, 40);
    c.dockSpacing = std::clamp(c.dockSpacing, 0, 24);
    c.dockBorder = std::clamp(c.dockBorder, 0, 6);
    if (!validBorderRole(c.dockBorderColor)) c.dockBorderColor = "primary";
    if (c.dockPosition != "bottom" && c.dockPosition != "left" && c.dockPosition != "right" && c.dockPosition != "top") c.dockPosition = "bottom";
    if (c.osdPosition != "right" && c.osdPosition != "left" && c.osdPosition != "top" && c.osdPosition != "bottom") c.osdPosition = "right";
    c.osdTimeout = std::clamp(c.osdTimeout, 500, 6000);
    return c;
}

bool loadConfig(App& app) {
    Config next;
    std::ifstream f(configPath());
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string t = ss.str();
        readIntKey(t, "workspaces", next.workspaces);
        readIntKey(t, "frame", next.frame);
        readIntKey(t, "notificationTimeout", next.notificationTimeout);
        readIntKey(t, "notificationAppIcons", next.notificationAppIcons);
        readIntKey(t, "conflictCheck", next.conflictCheck);
        readIntKey(t, "overviewLive", next.overviewLive);
        readIntKey(t, "overviewLauncher", next.overviewLauncher);
        readStringKey(t, "switcherStyle", next.switcherStyle);
        readStringKey(t, "overviewIcon", next.overviewIcon);
        readStringKey(t, "overviewPosition", next.overviewPosition);
        readIntKey(t, "privacyAlways", next.privacyAlways);
        readIntKey(t, "privacyMic", next.privacyMic);
        readIntKey(t, "privacyCam", next.privacyCam);
        readIntKey(t, "privacyScreen", next.privacyScreen);
        readIntKey(t, "doNotDisturb", next.doNotDisturb);
        readIntKey(t, "hotCorners", next.hotCorners);
        readIntKey(t, "hotCornerWidth", next.hotCornerWidth);
        readIntKey(t, "hotCornerHeight", next.hotCornerHeight);
        readIntKey(t, "hotCornerVisible", next.hotCornerVisible);
        readIntKey(t, "hotCornerHover", next.hotCornerHover);
        readIntKey(t, "updatesEnable", next.updatesEnable);
        readIntKey(t, "updatesAur", next.updatesAur);
        readIntKey(t, "updatesFlatpak", next.updatesFlatpak);
        readIntKey(t, "updatesInterval", next.updatesInterval);
        readIntKey(t, "updatesMinShow", next.updatesMinShow);
        readIntKey(t, "barOpacity", next.barOpacity);
        readIntKey(t, "notchWidth", next.notchWidth);
        readIntKey(t, "islandRounding", next.islandRounding);
        readIntKey(t, "fakeRounding", next.fakeRounding);
        readIntKey(t, "unlockKeyring", next.unlockKeyring);
        readIntKey(t, "barFollowsFrame", next.barFollowsFrame);
        readStringKey(t, "frameColor", next.frameColor);
        readIntKey(t, "lockOnStart", next.lockOnStart);
        readIntKey(t, "fakeRoundingSize", next.fakeRoundingSize);
        readIntKey(t, "centeredWallpaper", next.centeredWallpaper);
        readIntKey(t, "centeredWallpaperSize", next.centeredWallpaperSize);
        readStringKey(t, "centeredWallpaperShape", next.centeredWallpaperShape);
        readStringKey(t, "centeredWallpaperColor", next.centeredWallpaperColor);
        readIntKey(t, "collage", next.collage);
        readIntKey(t, "collageGap", next.collageGap);
        readIntKey(t, "collageMargin", next.collageMargin);
        readIntKey(t, "collageRadius", next.collageRadius);
        readIntKey(t, "collageBlur", next.collageBlur);
        readIntKey(t, "collagePrimary", next.collagePrimary);
        readJsonStringKey(t, "collageTree", next.collageTree);
        readIntKey(t, "networkAlways", next.networkAlways);
        readIntKey(t, "networkSpeed", next.networkSpeed);
        readIntKey(t, "screenshotCopy", next.screenshotCopy);
        readIntKey(t, "recordAudio", next.recordAudio);
        readIntKey(t, "recordDesktop", next.recordDesktop);
        readIntKey(t, "recordFps", next.recordFps);
        readStringKey(t, "screenshotDir", next.screenshotDir);
        readStringKey(t, "recordDir", next.recordDir);
        readStringKey(t, "barPosition", next.barPosition);
        readStringKey(t, "barScreens", next.barScreens);
        readStringKey(t, "widgetScreen", next.widgetScreen);
        readStringKey(t, "barStyle", next.barStyle);
        readStringKey(t, "workspaceStyle", next.workspaceStyle);
        readStringKey(t, "workspaceNumbers", next.workspaceNumbers);
        readIntKey(t, "dynamicIsland", next.dynamicIsland);
        readIntKey(t, "islandClick", next.islandClick);
        readStringKey(t, "groupStyle", next.groupStyle);
        readStringKey(t, "lockAnimation", next.lockAnimation);
        readIntKey(t, "groupSpacing", next.groupSpacing);
        readStringKey(t, "groupColor", next.groupColor);
        readStringKey(t, "groupFree", next.groupFree);
        readStringKey(t, "islandView", next.islandView);
        readStringKey(t, "colorScheme", next.colorScheme);
        readStringKey(t, "colorMode", next.colorMode);
        readStringKey(t, "wallpaperDir", next.wallpaperDir);
        readStringKey(t, "wallpaperTransition", next.wallpaperTransition);
        readIntKey(t, "wallpaperTransitionMs", next.wallpaperTransitionMs);
        readIntKey(t, "wallpaperAuto", next.wallpaperAuto);
        readIntKey(t, "wallpaperAutoFavs", next.wallpaperAutoFavs);
        readIntKey(t, "wallpaperAutoMin", next.wallpaperAutoMin);
        readStringKey(t, "selectorShape", next.selectorShape);
        readStringKey(t, "notificationShape", next.notificationShape);
        readStringKey(t, "preferredPlayer", next.preferredPlayer);
        readStringKey(t, "terminal", next.terminal);
        readStringKey(t, "worldClocks", next.worldClocks);
        readStringKey(t, "searchEngine", next.searchEngine);
        readStringKey(t, "weatherCity", next.weatherCity);
        readStringKey(t, "weatherUnit", next.weatherUnit);
        readStringKey(t, "lyricsProvider", next.lyricsProvider);
        readStringKey(t, "notificationPosition", next.notificationPosition);
        for (int i = 0; i < kPrefixCount; i++) readStringKey(t, kPrefixKeys[i], next.prefixes[i]);
        if (const size_t lay = t.find("\"layout\""); lay != std::string::npos) {
            readStringArray(t, "left", next.layoutLeft, lay);
            readStringArray(t, "center", next.layoutCenter, lay);
            readStringArray(t, "right", next.layoutRight, lay);
        }
        if (const size_t sl = t.find("\"sideLayout\""); sl != std::string::npos) {
            readStringArray(t, "left", next.sideLeft, sl);
            readStringArray(t, "center", next.sideCenter, sl);
            readStringArray(t, "right", next.sideRight, sl);
        }
        readStringKey(t, "iconStyle", next.iconStyle);
        readStringKey(t, "fontFamily", next.fontFamily);
        readStringKey(t, "uiBackground", next.uiBackground);
        readIntKey(t, "dock", next.dock);
        readStringKey(t, "dockPinned", next.dockPinned);
        readIntKey(t, "dockIconSize", next.dockIconSize);
        readIntKey(t, "dockMagnify", next.dockMagnify);
        readIntKey(t, "dockMagnification", next.dockMagnification);
        readIntKey(t, "dockAutoHide", next.dockAutoHide);
        readIntKey(t, "dockPin", next.dockPin);
        readIntKey(t, "dockMedia", next.dockMedia);
        readIntKey(t, "dockLauncher", next.dockLauncher);
        readIntKey(t, "dockHug", next.dockHug);
        readIntKey(t, "dockRounding", next.dockRounding);
        readIntKey(t, "dockSpacing", next.dockSpacing);
        readIntKey(t, "dockBorder", next.dockBorder);
        readStringKey(t, "dockBorderColor", next.dockBorderColor);
        readStringKey(t, "dockPosition", next.dockPosition);
        readStringKey(t, "dockScreen", next.dockScreen);
        readStringKey(t, "osdPosition", next.osdPosition);
        readIntKey(t, "osdTimeout", next.osdTimeout);
        readIntKey(t, "animEnabled", next.animEnabled);
        readIntKey(t, "barSpacing", next.barSpacing);
        readIntKey(t, "barShadow", next.barShadow);
        readIntKey(t, "dividerSpacing", next.dividerSpacing);
        readIntKey(t, "sideBarSpacing", next.sideBarSpacing);
        readIntKey(t, "animSpeed", next.animSpeed);
        readStringKey(t, "animStyle", next.animStyle);
        readIntKey(t, "keepAwake", next.keepAwake);
        readIntKey(t, "widgetsLocked", next.widgetsLocked);
        readIntKey(t, "widgetShadow", next.widgetShadow);
        readIntKey(t, "widgetGap", next.widgetGap);
        readStringKey(t, "deskVizStyle", next.deskVizStyle);
        readStringKey(t, "deskVizPos", next.deskVizPos);
        readStringKey(t, "deskVizColor", next.deskVizColor);
        readIntKey(t, "deskVizSize", next.deskVizSize);
        readIntKey(t, "deskVizBars", next.deskVizBars);
        readStringKey(t, "customTextFont", next.customTextFont);
        readStringKey(t, "customTextFontName", next.customTextFontName);
        readStringKey(t, "customTextColor", next.customTextColor);
        readStringKey(t, "customTextAlign", next.customTextAlign);
        readIntKey(t, "customTextFit", next.customTextFit);
        readIntKey(t, "customTextSize", next.customTextSize);
        readIntKey(t, "customTextBold", next.customTextBold);
        readIntKey(t, "customTextItalic", next.customTextItalic);
        readIntKey(t, "customTextShadow", next.customTextShadow);
        readIntKey(t, "calendarMonday", next.calendarMonday);
        readIntKey(t, "systray", next.systray);
        readIntKey(t, "barBackground", next.barBackground);
        readIntKey(t, "trayPinDefault", next.trayPinDefault);
        readIntKey(t, "trayTint", next.trayTint);
        readStringArray(t, "trayExceptions", next.trayExceptions);
        readIntKey(t, "weatherInterval", next.weatherInterval);
        readIntKey(t, "weatherTheme", next.weatherTheme);
        readIntKey(t, "materialIcons", next.materialIcons);
        readIntKey(t, "iconScale", next.iconScale);
        readIntKey(t, "fontWeightText", next.fontWeightText);
        readIntKey(t, "fontWeightTitle", next.fontWeightTitle);
        readIntKey(t, "launcherFiles", next.launcherFiles);
        readIntKey(t, "clock24", next.clock24);
        readIntKey(t, "clockDate", next.clockDate);
        readIntKey(t, "workspaceApps", next.workspaceApps);
        readIntKey(t, "batteryPercent", next.batteryPercent);
        readIntKey(t, "templateKitty", next.templateKitty);
        readIntKey(t, "templateGtk", next.templateGtk);
        readIntKey(t, "templateKde", next.templateKde);
        readStringKey(t, "barClock", next.barClock);
        readStringKey(t, "widgetStyles", next.widgetStyles);
        readStringKey(t, "sideWidgetStyles", next.sideWidgetStyles);
        readStringKey(t, "language", next.language);
        readIntKey(t, "volumeMax", next.volumeMax);
        readIntKey(t, "volumeStep", next.volumeStep);
        readIntKey(t, "batteryLow", next.batteryLow);
        readIntKey(t, "batteryCritical", next.batteryCritical);
        readIntKey(t, "batteryNotify", next.batteryNotify);
        readIntKey(t, "resCpu", next.resCpu);
        readIntKey(t, "resRam", next.resRam);
        readIntKey(t, "resTemp", next.resTemp);
        readIntKey(t, "resDisk", next.resDisk);
        readIntKey(t, "resSwap", next.resSwap);
        readIntKey(t, "resValue", next.resValue);
        readIntKey(t, "resInterval", next.resInterval);
        readIntKey(t, "resWarnMem", next.resWarnMem);
        readIntKey(t, "resWarnSwap", next.resWarnSwap);
        readIntKey(t, "resWarnCpu", next.resWarnCpu);
        readStringKey(t, "resStyle", next.resStyle);
        readStringKey(t, "dividerStyle", next.dividerStyle);
        readIntKey(t, "aiClaude", next.aiClaude);
        readIntKey(t, "aiCodex", next.aiCodex);
        readIntKey(t, "aiTokenLimit", next.aiTokenLimit);
        readStringKey(t, "clockStyle", next.clockStyle);
        readStringKey(t, "theme", next.theme);
        readStringKey(t, "quickActions", next.quickActions);
        readIntKey(t, "cookieSides", next.cookieSides);
        readIntKey(t, "cookieTime", next.cookieTime);
        for (const auto& ch : kCookieChoices) readStringKey(t, ch.key, *cookieField(next, ch.key));
        readStringKey(t, "themePrimary", next.themePrimary);
        readStringKey(t, "themeSecondary", next.themeSecondary);
        readStringKey(t, "themeTertiary", next.themeTertiary);
        readStringKey(t, "clockDaily", next.clockDaily);
        readStringKey(t, "clockPixel", next.clockPixel);
        readStringKey(t, "cornerTL", next.cornerTL);
        readStringKey(t, "cornerTR", next.cornerTR);
        readStringKey(t, "cornerBL", next.cornerBL);
        readStringKey(t, "cornerBR", next.cornerBR);
        readStringKey(t, "customImagePath", next.customImagePath);
        readIntKey(t, "borderColor", next.borderColor);
        readIntKey(t, "borderActiveOpacity", next.borderActiveOpacity);
        readIntKey(t, "borderInactiveOpacity", next.borderInactiveOpacity);
        readStringKey(t, "borderActive", next.borderActive);
        readStringKey(t, "borderInactive", next.borderInactive);
        readIntKey(t, "peripheralNotify", next.peripheralNotify);
        readIntKey(t, "peripheralLow", next.peripheralLow);
        readIntKey(t, "peripheralCritical", next.peripheralCritical);
        readStringKey(t, "avatarPath", next.avatarPath);
        readStringKey(t, "avatarPicture", next.avatarPicture);
        readStringKey(t, "accent", next.accent);
    }
    next = sanitized(next);
    const bool changed = deskVizChanged(next, app.cfg) || hotCornerChanged(next, app.cfg) || deskTextChanged(next, app.cfg) || next.iconScale != app.cfg.iconScale || next.overviewLive != app.cfg.overviewLive || next.overviewLauncher != app.cfg.overviewLauncher || next.switcherStyle != app.cfg.switcherStyle || next.overviewIcon != app.cfg.overviewIcon || next.overviewPosition != app.cfg.overviewPosition || next.privacyAlways != app.cfg.privacyAlways || next.privacyMic != app.cfg.privacyMic || next.privacyCam != app.cfg.privacyCam || next.privacyScreen != app.cfg.privacyScreen || next.fontWeightText != app.cfg.fontWeightText || next.fontWeightTitle != app.cfg.fontWeightTitle || next.launcherFiles != app.cfg.launcherFiles || next.islandView != app.cfg.islandView || next.dynamicIsland != app.cfg.dynamicIsland || next.islandClick != app.cfg.islandClick || next.groupStyle != app.cfg.groupStyle || next.lockAnimation != app.cfg.lockAnimation || next.groupSpacing != app.cfg.groupSpacing || next.groupColor != app.cfg.groupColor || next.groupFree != app.cfg.groupFree || next.barShadow != app.cfg.barShadow || next.dividerSpacing != app.cfg.dividerSpacing || next.workspaceNumbers != app.cfg.workspaceNumbers || next.barSpacing != app.cfg.barSpacing || next.sideBarSpacing != app.cfg.sideBarSpacing || next.animEnabled != app.cfg.animEnabled || next.animSpeed != app.cfg.animSpeed || next.animStyle != app.cfg.animStyle || next.collageBlur != app.cfg.collageBlur || next.osdPosition != app.cfg.osdPosition || next.osdTimeout != app.cfg.osdTimeout || next.barScreens != app.cfg.barScreens || next.widgetScreen != app.cfg.widgetScreen || next.customImagePath != app.cfg.customImagePath || next.clockStyle != app.cfg.clockStyle || next.theme != app.cfg.theme || next.quickActions != app.cfg.quickActions || cookieChanged(next, app.cfg) || next.themePrimary != app.cfg.themePrimary || next.themeSecondary != app.cfg.themeSecondary || next.themeTertiary != app.cfg.themeTertiary || next.clockDaily != app.cfg.clockDaily || next.clockPixel != app.cfg.clockPixel || next.cornerTL != app.cfg.cornerTL || next.cornerTR != app.cfg.cornerTR || next.cornerBL != app.cfg.cornerBL || next.cornerBR != app.cfg.cornerBR || next.widgetsLocked != app.cfg.widgetsLocked || next.widgetShadow != app.cfg.widgetShadow || next.widgetGap != app.cfg.widgetGap || next.calendarMonday != app.cfg.calendarMonday || next.resCpu != app.cfg.resCpu || next.resRam != app.cfg.resRam || next.resTemp != app.cfg.resTemp || next.resDisk != app.cfg.resDisk || next.resSwap != app.cfg.resSwap || next.resValue != app.cfg.resValue || next.resInterval != app.cfg.resInterval || next.resWarnMem != app.cfg.resWarnMem || next.resWarnSwap != app.cfg.resWarnSwap || next.resWarnCpu != app.cfg.resWarnCpu || next.resStyle != app.cfg.resStyle || next.dividerStyle != app.cfg.dividerStyle || next.aiClaude != app.cfg.aiClaude || next.aiCodex != app.cfg.aiCodex || next.aiTokenLimit != app.cfg.aiTokenLimit || next.trayPinDefault != app.cfg.trayPinDefault || next.trayTint != app.cfg.trayTint || next.trayExceptions != app.cfg.trayExceptions || next.notificationPosition != app.cfg.notificationPosition || next.clock24 != app.cfg.clock24 || next.clockDate != app.cfg.clockDate || next.workspaceApps != app.cfg.workspaceApps || next.barClock != app.cfg.barClock || next.widgetStyles != app.cfg.widgetStyles || next.batteryPercent != app.cfg.batteryPercent || next.templateKitty != app.cfg.templateKitty || next.templateGtk != app.cfg.templateGtk || next.templateKde != app.cfg.templateKde || next.fontFamily != app.cfg.fontFamily || next.islandRounding != app.cfg.islandRounding || next.sideWidgetStyles != app.cfg.sideWidgetStyles || next.language != app.cfg.language || next.volumeMax != app.cfg.volumeMax || next.volumeStep != app.cfg.volumeStep || next.batteryLow != app.cfg.batteryLow || next.batteryCritical != app.cfg.batteryCritical || next.batteryNotify != app.cfg.batteryNotify || next.weatherInterval != app.cfg.weatherInterval || next.borderColor != app.cfg.borderColor || next.borderActive != app.cfg.borderActive || next.borderInactive != app.cfg.borderInactive || next.borderActiveOpacity != app.cfg.borderActiveOpacity || next.borderInactiveOpacity != app.cfg.borderInactiveOpacity || next.barBackground != app.cfg.barBackground || next.systray != app.cfg.systray || next.peripheralNotify != app.cfg.peripheralNotify || next.peripheralLow != app.cfg.peripheralLow || next.peripheralCritical != app.cfg.peripheralCritical || next.workspaces != app.cfg.workspaces || next.weatherTheme != app.cfg.weatherTheme || next.iconStyle != app.cfg.iconStyle || next.materialIcons != app.cfg.materialIcons || next.uiBackground != app.cfg.uiBackground || next.dock != app.cfg.dock || next.dockPinned != app.cfg.dockPinned || next.dockIconSize != app.cfg.dockIconSize || next.dockMagnify != app.cfg.dockMagnify || next.dockMagnification != app.cfg.dockMagnification || next.dockAutoHide != app.cfg.dockAutoHide || next.dockPin != app.cfg.dockPin || next.dockMedia != app.cfg.dockMedia || next.dockLauncher != app.cfg.dockLauncher || next.dockHug != app.cfg.dockHug || next.dockRounding != app.cfg.dockRounding || next.dockSpacing != app.cfg.dockSpacing || next.dockBorder != app.cfg.dockBorder || next.dockBorderColor != app.cfg.dockBorderColor || next.dockPosition != app.cfg.dockPosition || next.dockScreen != app.cfg.dockScreen || next.keepAwake != app.cfg.keepAwake || next.avatarPicture != app.cfg.avatarPicture || next.accent != app.cfg.accent || next.avatarPath != app.cfg.avatarPath || next.frame != app.cfg.frame || next.notificationTimeout != app.cfg.notificationTimeout || next.notificationAppIcons != app.cfg.notificationAppIcons || next.conflictCheck != app.cfg.conflictCheck || next.doNotDisturb != app.cfg.doNotDisturb || next.hotCorners != app.cfg.hotCorners || next.updatesEnable != app.cfg.updatesEnable || next.updatesAur != app.cfg.updatesAur || next.updatesFlatpak != app.cfg.updatesFlatpak || next.updatesInterval != app.cfg.updatesInterval || next.updatesMinShow != app.cfg.updatesMinShow || next.barOpacity != app.cfg.barOpacity || next.notchWidth != app.cfg.notchWidth || next.fakeRounding != app.cfg.fakeRounding || next.unlockKeyring != app.cfg.unlockKeyring || next.barFollowsFrame != app.cfg.barFollowsFrame || next.frameColor != app.cfg.frameColor || next.lockOnStart != app.cfg.lockOnStart || next.fakeRoundingSize != app.cfg.fakeRoundingSize || next.centeredWallpaper != app.cfg.centeredWallpaper || next.centeredWallpaperSize != app.cfg.centeredWallpaperSize || next.centeredWallpaperShape != app.cfg.centeredWallpaperShape || next.centeredWallpaperColor != app.cfg.centeredWallpaperColor || next.collage != app.cfg.collage || next.collageGap != app.cfg.collageGap || next.collageMargin != app.cfg.collageMargin || next.collageRadius != app.cfg.collageRadius || next.collagePrimary != app.cfg.collagePrimary || next.collageTree != app.cfg.collageTree || next.networkAlways != app.cfg.networkAlways || next.networkSpeed != app.cfg.networkSpeed || next.screenshotCopy != app.cfg.screenshotCopy || next.recordAudio != app.cfg.recordAudio || next.recordDesktop != app.cfg.recordDesktop || next.recordFps != app.cfg.recordFps || next.screenshotDir != app.cfg.screenshotDir || next.recordDir != app.cfg.recordDir || next.barPosition != app.cfg.barPosition || next.barStyle != app.cfg.barStyle || next.workspaceStyle != app.cfg.workspaceStyle || next.colorScheme != app.cfg.colorScheme ||
                         next.colorMode != app.cfg.colorMode || next.wallpaperDir != app.cfg.wallpaperDir || next.wallpaperTransition != app.cfg.wallpaperTransition ||
                         next.wallpaperTransitionMs != app.cfg.wallpaperTransitionMs || next.wallpaperAuto != app.cfg.wallpaperAuto || next.wallpaperAutoFavs != app.cfg.wallpaperAutoFavs || next.wallpaperAutoMin != app.cfg.wallpaperAutoMin || next.selectorShape != app.cfg.selectorShape || next.notificationShape != app.cfg.notificationShape || next.preferredPlayer != app.cfg.preferredPlayer || next.terminal != app.cfg.terminal || next.worldClocks != app.cfg.worldClocks || next.searchEngine != app.cfg.searchEngine || next.weatherCity != app.cfg.weatherCity || next.weatherUnit != app.cfg.weatherUnit || next.lyricsProvider != app.cfg.lyricsProvider || !std::equal(next.prefixes, next.prefixes + kPrefixCount, app.cfg.prefixes) ||
                         next.sideLeft != app.cfg.sideLeft || next.sideCenter != app.cfg.sideCenter || next.sideRight != app.cfg.sideRight || next.layoutLeft != app.cfg.layoutLeft || next.layoutCenter != app.cfg.layoutCenter || next.layoutRight != app.cfg.layoutRight;
    app.cfg = next;
    for (int i = 0; i < kPrefixCount; i++) *kPrefixSlots[i] = next.prefixes[i][0];
    g_iconStyle = next.iconStyle;
    g_iconScale = next.iconScale;
    g_animEnabled = next.animEnabled;
    g_animSpeed = next.animSpeed;
    g_animStyle = next.animStyle;
    matsym::enabled() = next.materialIcons != 0;
    return changed;
}

std::vector<std::string> splitList(const std::string& v, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, sep))
        if (!item.empty()) out.push_back(item);
    return out;
}

std::string joinQuoted(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) out += (i ? ", \"" : "\"") + v[i] + "\"";
    return out;
}

void saveConfig(const App& app) {
    const std::string tmp = configPath() + ".tmp";
    {
        std::ofstream f(tmp);
        f << "{\n"
          << "  \"workspaces\": " << app.cfg.workspaces << ",\n"
          << "  \"frame\": " << app.cfg.frame << ",\n"
          << "  \"barOpacity\": " << app.cfg.barOpacity << ",\n"
          << "  \"notchWidth\": " << app.cfg.notchWidth << ",\n"
          << "  \"islandRounding\": " << app.cfg.islandRounding << ",\n"
          << "  \"fakeRounding\": " << app.cfg.fakeRounding << ",\n"
          << "  \"unlockKeyring\": " << app.cfg.unlockKeyring << ",\n"
          << "  \"barFollowsFrame\": " << app.cfg.barFollowsFrame << ",\n"
          << "  \"frameColor\": \"" << app.cfg.frameColor << "\",\n"
          << "  \"lockOnStart\": " << app.cfg.lockOnStart << ",\n"
          << "  \"fakeRoundingSize\": " << app.cfg.fakeRoundingSize << ",\n"
          << "  \"centeredWallpaper\": " << app.cfg.centeredWallpaper << ",\n"
          << "  \"centeredWallpaperSize\": " << app.cfg.centeredWallpaperSize << ",\n"
          << "  \"centeredWallpaperShape\": \"" << app.cfg.centeredWallpaperShape << "\",\n"
          << "  \"centeredWallpaperColor\": \"" << app.cfg.centeredWallpaperColor << "\",\n"
          << "  \"collage\": " << app.cfg.collage << ",\n"
          << "  \"collageGap\": " << app.cfg.collageGap << ",\n"
          << "  \"collageMargin\": " << app.cfg.collageMargin << ",\n"
          << "  \"collageRadius\": " << app.cfg.collageRadius << ",\n"
          << "  \"collageBlur\": " << app.cfg.collageBlur << ",\n"
          << "  \"collagePrimary\": " << app.cfg.collagePrimary << ",\n"
          << "  \"collageTree\": \"" << jsonEscape(app.cfg.collageTree) << "\",\n"
          << "  \"networkAlways\": " << app.cfg.networkAlways << ",\n"
          << "  \"networkSpeed\": " << app.cfg.networkSpeed << ",\n"
          << "  \"screenshotCopy\": " << app.cfg.screenshotCopy << ",\n"
          << "  \"recordAudio\": " << app.cfg.recordAudio << ",\n"
          << "  \"recordDesktop\": " << app.cfg.recordDesktop << ",\n"
          << "  \"recordFps\": " << app.cfg.recordFps << ",\n"
          << "  \"screenshotDir\": \"" << app.cfg.screenshotDir << "\",\n"
          << "  \"recordDir\": \"" << app.cfg.recordDir << "\",\n"
          << "  \"notificationTimeout\": " << app.cfg.notificationTimeout << ",\n"
          << "  \"notificationAppIcons\": " << app.cfg.notificationAppIcons << ",\n"
          << "  \"conflictCheck\": " << app.cfg.conflictCheck << ",\n"
          << "  \"overviewLive\": " << app.cfg.overviewLive << ",\n"
          << "  \"overviewLauncher\": " << app.cfg.overviewLauncher << ",\n"
          << "  \"switcherStyle\": \"" << app.cfg.switcherStyle << "\",\n"
          << "  \"overviewIcon\": \"" << app.cfg.overviewIcon << "\",\n"
          << "  \"overviewPosition\": \"" << app.cfg.overviewPosition << "\",\n"
          << "  \"privacyAlways\": " << app.cfg.privacyAlways << ",\n"
          << "  \"privacyMic\": " << app.cfg.privacyMic << ",\n"
          << "  \"privacyCam\": " << app.cfg.privacyCam << ",\n"
          << "  \"privacyScreen\": " << app.cfg.privacyScreen << ",\n"
          << "  \"doNotDisturb\": " << app.cfg.doNotDisturb << ",\n"
          << "  \"barSpacing\": " << app.cfg.barSpacing << ",\n"
          << "  \"barShadow\": " << app.cfg.barShadow << ",\n"
          << "  \"dividerSpacing\": " << app.cfg.dividerSpacing << ",\n"
          << "  \"sideBarSpacing\": " << app.cfg.sideBarSpacing << ",\n"
          << "  \"animEnabled\": " << app.cfg.animEnabled << ",\n"
          << "  \"animSpeed\": " << app.cfg.animSpeed << ",\n"
          << "  \"animStyle\": \"" << app.cfg.animStyle << "\",\n"
          << "  \"hotCorners\": " << app.cfg.hotCorners << ",\n"
          << "  \"hotCornerWidth\": " << app.cfg.hotCornerWidth << ",\n"
          << "  \"hotCornerHeight\": " << app.cfg.hotCornerHeight << ",\n"
          << "  \"hotCornerVisible\": " << app.cfg.hotCornerVisible << ",\n"
          << "  \"hotCornerHover\": " << app.cfg.hotCornerHover << ",\n"
          << "  \"updatesEnable\": " << app.cfg.updatesEnable << ",\n"
          << "  \"updatesAur\": " << app.cfg.updatesAur << ",\n"
          << "  \"updatesFlatpak\": " << app.cfg.updatesFlatpak << ",\n"
          << "  \"updatesInterval\": " << app.cfg.updatesInterval << ",\n"
          << "  \"updatesMinShow\": " << app.cfg.updatesMinShow << ",\n"
          << "  \"barPosition\": \"" << app.cfg.barPosition << "\",\n"
          << "  \"barScreens\": \"" << app.cfg.barScreens << "\",\n"
          << "  \"widgetScreen\": \"" << app.cfg.widgetScreen << "\",\n"
          << "  \"barStyle\": \"" << app.cfg.barStyle << "\",\n"
          << "  \"workspaceNumbers\": \"" << app.cfg.workspaceNumbers << "\",\n"
          << "  \"dynamicIsland\": " << app.cfg.dynamicIsland << ",\n"
          << "  \"islandClick\": " << app.cfg.islandClick << ",\n"
          << "  \"groupStyle\": \"" << app.cfg.groupStyle << "\",\n"
          << "  \"lockAnimation\": \"" << app.cfg.lockAnimation << "\",\n"
          << "  \"groupSpacing\": " << app.cfg.groupSpacing << ",\n"
          << "  \"groupColor\": \"" << app.cfg.groupColor << "\",\n"
          << "  \"groupFree\": \"" << app.cfg.groupFree << "\",\n"
          << "  \"islandView\": \"" << app.cfg.islandView << "\",\n"
          << "  \"workspaceStyle\": \"" << app.cfg.workspaceStyle << "\",\n"
          << "  \"colorScheme\": \"" << app.cfg.colorScheme << "\",\n"
          << "  \"colorMode\": \"" << app.cfg.colorMode << "\",\n"
          << "  \"wallpaperDir\": \"" << app.cfg.wallpaperDir << "\",\n"
          << "  \"wallpaperTransition\": \"" << app.cfg.wallpaperTransition << "\",\n"
          << "  \"wallpaperTransitionMs\": " << app.cfg.wallpaperTransitionMs << ",\n"
          << "  \"wallpaperAuto\": " << app.cfg.wallpaperAuto << ",\n"
          << "  \"wallpaperAutoFavs\": " << app.cfg.wallpaperAutoFavs << ",\n"
          << "  \"wallpaperAutoMin\": " << app.cfg.wallpaperAutoMin << ",\n"
          << "  \"selectorShape\": \"" << app.cfg.selectorShape << "\",\n"
          << "  \"notificationShape\": \"" << app.cfg.notificationShape << "\",\n"
          << "  \"preferredPlayer\": \"" << app.cfg.preferredPlayer << "\",\n"
          << "  \"terminal\": \"" << app.cfg.terminal << "\",\n"
          << "  \"worldClocks\": \"" << app.cfg.worldClocks << "\",\n"
          << "  \"searchEngine\": \"" << app.cfg.searchEngine << "\",\n"
          << "  \"weatherCity\": \"" << app.cfg.weatherCity << "\",\n"
          << "  \"weatherInterval\": " << app.cfg.weatherInterval << ",\n"
          << "  \"weatherTheme\": " << app.cfg.weatherTheme << ",\n"
          << "  \"materialIcons\": " << app.cfg.materialIcons << ",\n"
          << "  \"iconScale\": " << app.cfg.iconScale << ",\n"
          << "  \"fontWeightText\": " << app.cfg.fontWeightText << ",\n"
          << "  \"fontWeightTitle\": " << app.cfg.fontWeightTitle << ",\n"
          << "  \"launcherFiles\": " << app.cfg.launcherFiles << ",\n"
          << "  \"notificationPosition\": \"" << app.cfg.notificationPosition << "\",\n"
          << "  \"clock24\": " << app.cfg.clock24 << ",\n"
          << "  \"clockDate\": " << app.cfg.clockDate << ",\n"
          << "  \"workspaceApps\": " << app.cfg.workspaceApps << ",\n"
          << "  \"batteryPercent\": " << app.cfg.batteryPercent << ",\n"
          << "  \"templateKitty\": " << app.cfg.templateKitty << ",\n"
          << "  \"templateGtk\": " << app.cfg.templateGtk << ",\n"
          << "  \"templateKde\": " << app.cfg.templateKde << ",\n"
          << "  \"barClock\": \"" << app.cfg.barClock << "\",\n"
          << "  \"fontFamily\": \"" << app.cfg.fontFamily << "\",\n"
          << "  \"widgetStyles\": \"" << app.cfg.widgetStyles << "\",\n"
          << "  \"sideWidgetStyles\": \"" << app.cfg.sideWidgetStyles << "\",\n"
          << "  \"language\": \"" << app.cfg.language << "\",\n"
          << "  \"volumeMax\": " << app.cfg.volumeMax << ",\n"
          << "  \"volumeStep\": " << app.cfg.volumeStep << ",\n"
          << "  \"batteryLow\": " << app.cfg.batteryLow << ",\n"
          << "  \"batteryCritical\": " << app.cfg.batteryCritical << ",\n"
          << "  \"batteryNotify\": " << app.cfg.batteryNotify << ",\n"
          << "  \"resCpu\": " << app.cfg.resCpu << ",\n"
          << "  \"resRam\": " << app.cfg.resRam << ",\n"
          << "  \"resTemp\": " << app.cfg.resTemp << ",\n"
          << "  \"resDisk\": " << app.cfg.resDisk << ",\n"
          << "  \"resSwap\": " << app.cfg.resSwap << ",\n"
          << "  \"resValue\": " << app.cfg.resValue << ",\n"
          << "  \"resInterval\": " << app.cfg.resInterval << ",\n"
          << "  \"resWarnMem\": " << app.cfg.resWarnMem << ",\n"
          << "  \"resWarnSwap\": " << app.cfg.resWarnSwap << ",\n"
          << "  \"resWarnCpu\": " << app.cfg.resWarnCpu << ",\n"
          << "  \"resStyle\": \"" << app.cfg.resStyle << "\",\n"
          << "  \"dividerStyle\": \"" << app.cfg.dividerStyle << "\",\n"
          << "  \"aiClaude\": " << app.cfg.aiClaude << ",\n"
          << "  \"aiCodex\": " << app.cfg.aiCodex << ",\n"
          << "  \"aiTokenLimit\": " << app.cfg.aiTokenLimit << ",\n"
          << "  \"clockStyle\": \"" << app.cfg.clockStyle << "\",\n"
          << "  \"theme\": \"" << app.cfg.theme << "\",\n"
          << "  \"quickActions\": \"" << app.cfg.quickActions << "\",\n"
          << "  \"cookieSides\": " << app.cfg.cookieSides << ",\n"
          << "  \"cookieTime\": " << app.cfg.cookieTime << ",\n"
          << "  \"cookieDial\": \"" << app.cfg.cookieDial << "\",\n"
          << "  \"cookieHour\": \"" << app.cfg.cookieHour << "\",\n"
          << "  \"cookieMinute\": \"" << app.cfg.cookieMinute << "\",\n"
          << "  \"cookieSecond\": \"" << app.cfg.cookieSecond << "\",\n"
          << "  \"cookieDate\": \"" << app.cfg.cookieDate << "\",\n"
          << "  \"themePrimary\": \"" << app.cfg.themePrimary << "\",\n"
          << "  \"themeSecondary\": \"" << app.cfg.themeSecondary << "\",\n"
          << "  \"themeTertiary\": \"" << app.cfg.themeTertiary << "\",\n"
          << "  \"clockDaily\": \"" << app.cfg.clockDaily << "\",\n"
          << "  \"clockPixel\": \"" << app.cfg.clockPixel << "\",\n"
          << "  \"cornerTL\": \"" << app.cfg.cornerTL << "\",\n"
          << "  \"cornerTR\": \"" << app.cfg.cornerTR << "\",\n"
          << "  \"cornerBL\": \"" << app.cfg.cornerBL << "\",\n"
          << "  \"cornerBR\": \"" << app.cfg.cornerBR << "\",\n"
          << "  \"customImagePath\": \"" << app.cfg.customImagePath << "\",\n"
          << "  \"lyricsProvider\": \"" << app.cfg.lyricsProvider << "\",\n"
          << "  \"weatherUnit\": \"" << app.cfg.weatherUnit << "\",\n";
    for (int i = 0; i < kPrefixCount; i++) f << "  \"" << kPrefixKeys[i] << "\": \"" << app.cfg.prefixes[i] << "\",\n";
    f
          << "  \"layout\": {\n"
          << "    \"left\": [" << joinQuoted(app.cfg.layoutLeft) << "],\n"
          << "    \"center\": [" << joinQuoted(app.cfg.layoutCenter) << "],\n"
          << "    \"right\": [" << joinQuoted(app.cfg.layoutRight) << "]\n"
          << "  },\n"
          << "  \"sideLayout\": {\n"
          << "    \"left\": [" << joinQuoted(app.cfg.sideLeft) << "],\n"
          << "    \"center\": [" << joinQuoted(app.cfg.sideCenter) << "],\n"
          << "    \"right\": [" << joinQuoted(app.cfg.sideRight) << "]\n"
          << "  },\n"
          << "  \"avatarPath\": \"" << app.cfg.avatarPath << "\",\n"
          << "  \"avatarPicture\": \"" << app.cfg.avatarPicture << "\",\n"
          << "  \"accent\": \"" << app.cfg.accent << "\",\n"
          << "  \"keepAwake\": " << app.cfg.keepAwake << ",\n"
          << "  \"widgetsLocked\": " << app.cfg.widgetsLocked << ",\n"
          << "  \"widgetShadow\": " << app.cfg.widgetShadow << ",\n"
          << "  \"widgetGap\": " << app.cfg.widgetGap << ",\n"
          << "  \"deskVizStyle\": \"" << app.cfg.deskVizStyle << "\",\n"
          << "  \"deskVizPos\": \"" << app.cfg.deskVizPos << "\",\n"
          << "  \"deskVizColor\": \"" << app.cfg.deskVizColor << "\",\n"
          << "  \"deskVizSize\": " << app.cfg.deskVizSize << ",\n"
          << "  \"deskVizBars\": " << app.cfg.deskVizBars << ",\n"
          << "  \"customTextFont\": \"" << app.cfg.customTextFont << "\",\n"
          << "  \"customTextFontName\": \"" << app.cfg.customTextFontName << "\",\n"
          << "  \"customTextColor\": \"" << app.cfg.customTextColor << "\",\n"
          << "  \"customTextAlign\": \"" << app.cfg.customTextAlign << "\",\n"
          << "  \"customTextFit\": " << app.cfg.customTextFit << ",\n"
          << "  \"customTextSize\": " << app.cfg.customTextSize << ",\n"
          << "  \"customTextBold\": " << app.cfg.customTextBold << ",\n"
          << "  \"customTextItalic\": " << app.cfg.customTextItalic << ",\n"
          << "  \"customTextShadow\": " << app.cfg.customTextShadow << ",\n"
          << "  \"calendarMonday\": " << app.cfg.calendarMonday << ",\n"
          << "  \"barBackground\": " << app.cfg.barBackground << ",\n"
          << "  \"trayPinDefault\": " << app.cfg.trayPinDefault << ",\n"
          << "  \"trayTint\": " << app.cfg.trayTint << ",\n"
          << "  \"trayExceptions\": [" << joinQuoted(app.cfg.trayExceptions) << "],\n"
          << "  \"borderColor\": " << app.cfg.borderColor << ",\n"
          << "  \"borderActive\": \"" << app.cfg.borderActive << "\",\n"
          << "  \"borderActiveOpacity\": " << app.cfg.borderActiveOpacity << ",\n"
          << "  \"borderInactive\": \"" << app.cfg.borderInactive << "\",\n"
          << "  \"borderInactiveOpacity\": " << app.cfg.borderInactiveOpacity << ",\n"
          << "  \"systray\": " << app.cfg.systray << ",\n"
          << "  \"peripheralNotify\": " << app.cfg.peripheralNotify << ",\n"
          << "  \"peripheralLow\": " << app.cfg.peripheralLow << ",\n"
          << "  \"peripheralCritical\": " << app.cfg.peripheralCritical << ",\n"
          << "  \"uiBackground\": \"" << app.cfg.uiBackground << "\",\n"
          << "  \"dock\": " << app.cfg.dock << ",\n"
          << "  \"dockPinned\": \"" << app.cfg.dockPinned << "\",\n"
          << "  \"dockIconSize\": " << app.cfg.dockIconSize << ",\n"
          << "  \"dockMagnify\": " << app.cfg.dockMagnify << ",\n"
          << "  \"dockMagnification\": " << app.cfg.dockMagnification << ",\n"
          << "  \"dockAutoHide\": " << app.cfg.dockAutoHide << ",\n"
          << "  \"dockPin\": " << app.cfg.dockPin << ",\n"
          << "  \"dockMedia\": " << app.cfg.dockMedia << ",\n"
          << "  \"dockLauncher\": " << app.cfg.dockLauncher << ",\n"
          << "  \"dockHug\": " << app.cfg.dockHug << ",\n"
          << "  \"dockRounding\": " << app.cfg.dockRounding << ",\n"
          << "  \"dockSpacing\": " << app.cfg.dockSpacing << ",\n"
          << "  \"dockBorder\": " << app.cfg.dockBorder << ",\n"
          << "  \"dockBorderColor\": \"" << app.cfg.dockBorderColor << "\",\n"
          << "  \"dockPosition\": \"" << app.cfg.dockPosition << "\",\n"
          << "  \"dockScreen\": \"" << app.cfg.dockScreen << "\",\n"
          << "  \"osdPosition\": \"" << app.cfg.osdPosition << "\",\n"
          << "  \"osdTimeout\": " << app.cfg.osdTimeout << ",\n"
          << "  \"iconStyle\": \"" << app.cfg.iconStyle << "\"\n"
          << "}\n";
    }
    std::rename(tmp.c_str(), configPath().c_str());
}

bool isDockKey(const std::string& key) {
    return key == "dock" || key == "dockPinned" || key == "dockIconSize" || key == "dockMagnify" || key == "dockMagnification" || key == "dockAutoHide" || key == "dockPin" || key == "dockMedia" || key == "dockLauncher" || key == "dockHug" || key == "dockRounding" || key == "dockSpacing" || key == "dockBorder" || key == "dockBorderColor" || key == "dockPosition" || key == "dockScreen";
}
std::string setDockKey(Config& next, const std::string& key, const std::string& value) {
    if (key == "dockPinned") {
        const std::string v = value == "-" ? "" : value;
        if (v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-,") != std::string::npos) return "Pinned apps are desktop ids or window classes, separated by commas";
        next.dockPinned = v;
        return "";
    }
    if (key == "dockScreen") {
        const std::string v = value == "-" ? "" : value;
        if (v.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:+,") != std::string::npos || v.find(',') != std::string::npos) return "value must be a monitor name";
        next.dockScreen = v;
        return "";
    }
    if (key == "dockPosition") {
        if (value != "bottom" && value != "left" && value != "right" && value != "top") return "value must be bottom, left, right or top";
        next.dockPosition = value;
        return "";
    }
    if (key == "dockBorderColor") {
        if (!validBorderRole(value)) return "unknown palette role";
        next.dockBorderColor = value;
        return "";
    }
    struct DockIntKey { const char* name; int* field; int lo, hi; };
    const DockIntKey table[] = {{"dock", &next.dock, 0, 1}, {"dockIconSize", &next.dockIconSize, 28, 72}, {"dockMagnify", &next.dockMagnify, 0, 1},
                                {"dockMagnification", &next.dockMagnification, 100, 220}, {"dockAutoHide", &next.dockAutoHide, 0, 1}, {"dockPin", &next.dockPin, 0, 1}, {"dockMedia", &next.dockMedia, 0, 1}, {"dockLauncher", &next.dockLauncher, 0, 1}, {"dockHug", &next.dockHug, 0, 1}, {"dockRounding", &next.dockRounding, 8, 40}, {"dockSpacing", &next.dockSpacing, 0, 24}, {"dockBorder", &next.dockBorder, 0, 6}};
    for (const DockIntKey& k : table) {
        if (key != k.name) continue;
        if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos) return "Value must be an integer";
        const int v = std::atoi(value.c_str());
        if (v < k.lo || v > k.hi) return "Value must be from " + std::to_string(k.lo) + " to " + std::to_string(k.hi);
        *k.field = v;
        return "";
    }
    return "unknown key";
}

std::string setConfig(App& app, const std::string& key, const std::string& value);
std::string setConfig(App& app, const std::string& key, const std::string& value) {
    const bool sideNow = app.cfg.barPosition == "left" || app.cfg.barPosition == "right";
    if (sideNow && (key == "barClock" || key == "workspaceStyle" || key == "resStyle"))
        return setConfig(app, "widgetStyle", std::string(key == "barClock" ? "clock" : key == "workspaceStyle" ? "workspaces" : "resources") + "=" + value);
    Config next = app.cfg;
    if (key == "overviewLauncher" || key == "overviewLive" || key == "privacyAlways" || key == "privacyMic" || key == "privacyCam" || key == "privacyScreen" || key == "workspaces" || key == "frame" || key == "barOpacity" || key == "notchWidth" || key == "fakeRounding" || key == "fakeRoundingSize" || key == "unlockKeyring" || key == "barFollowsFrame" || key == "lockOnStart" || key == "centeredWallpaper" || key == "centeredWallpaperSize" || key == "networkAlways" || key == "networkSpeed" || key == "screenshotCopy" || key == "recordAudio" || key == "notificationTimeout" || key == "notificationAppIcons" || key == "conflictCheck" || key == "doNotDisturb" || key == "hotCorners" || key == "updatesEnable" || key == "updatesAur" || key == "updatesFlatpak" || key == "updatesInterval" || key == "updatesMinShow" || key == "keepAwake" || key == "widgetsLocked" || key == "widgetShadow" || key == "widgetGap" || key == "calendarMonday" || key == "systray" || key == "barBackground" || key == "trayPinDefault" || key == "trayTint" || key == "weatherInterval" || key == "weatherTheme" || key == "materialIcons" || key == "resCpu" || key == "resRam" || key == "resTemp" || key == "resDisk" || key == "resSwap" || key == "resValue" || key == "resInterval" || key == "resWarnMem" || key == "resWarnSwap" || key == "resWarnCpu" || key == "clock24" || key == "clockDate" || key == "workspaceApps" || key == "batteryPercent" || key == "templateKitty" || key == "templateGtk" || key == "templateKde" || key == "volumeMax" || key == "volumeStep" || key == "batteryLow" || key == "batteryCritical" || key == "batteryNotify" || key == "borderColor" || key == "borderActiveOpacity" || key == "borderInactiveOpacity" || key == "peripheralNotify" || key == "peripheralLow" || key == "peripheralCritical" || key == "wallpaperTransitionMs" || key == "aiClaude" || key == "aiCodex" || key == "aiTokenLimit") {
        if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos) return "value must be an integer";
        (key == "overviewLauncher" ? next.overviewLauncher : key == "overviewLive" ? next.overviewLive : key == "privacyAlways" ? next.privacyAlways : key == "privacyMic" ? next.privacyMic : key == "privacyCam" ? next.privacyCam : key == "privacyScreen" ? next.privacyScreen : key == "workspaces" ? next.workspaces : key == "frame" ? next.frame : key == "barOpacity" ? next.barOpacity : key == "notchWidth" ? next.notchWidth : key == "fakeRounding" ? next.fakeRounding : key == "unlockKeyring" ? next.unlockKeyring : key == "barFollowsFrame" ? next.barFollowsFrame : key == "lockOnStart" ? next.lockOnStart : key == "fakeRoundingSize" ? next.fakeRoundingSize : key == "centeredWallpaper" ? next.centeredWallpaper : key == "centeredWallpaperSize" ? next.centeredWallpaperSize : key == "networkAlways" ? next.networkAlways : key == "networkSpeed" ? next.networkSpeed : key == "screenshotCopy" ? next.screenshotCopy : key == "recordAudio" ? next.recordAudio : key == "notificationTimeout" ? next.notificationTimeout : key == "notificationAppIcons" ? next.notificationAppIcons : key == "conflictCheck" ? next.conflictCheck : key == "doNotDisturb" ? next.doNotDisturb : key == "hotCorners" ? next.hotCorners : key == "updatesEnable" ? next.updatesEnable : key == "updatesAur" ? next.updatesAur : key == "updatesFlatpak" ? next.updatesFlatpak : key == "updatesInterval" ? next.updatesInterval : key == "updatesMinShow" ? next.updatesMinShow : key == "keepAwake" ? next.keepAwake : key == "widgetsLocked" ? next.widgetsLocked : key == "widgetShadow" ? next.widgetShadow : key == "widgetGap" ? next.widgetGap : key == "calendarMonday" ? next.calendarMonday : key == "systray" ? next.systray : key == "barBackground" ? next.barBackground : key == "trayPinDefault" ? next.trayPinDefault : key == "trayTint" ? next.trayTint : key == "weatherTheme" ? next.weatherTheme : key == "materialIcons" ? next.materialIcons : key == "weatherInterval" ? next.weatherInterval : key == "resCpu" ? next.resCpu : key == "resRam" ? next.resRam : key == "resTemp" ? next.resTemp : key == "resDisk" ? next.resDisk : key == "resSwap" ? next.resSwap : key == "resValue" ? next.resValue : key == "resInterval" ? next.resInterval : key == "resWarnMem" ? next.resWarnMem : key == "resWarnSwap" ? next.resWarnSwap : key == "resWarnCpu" ? next.resWarnCpu : key == "clock24" ? next.clock24 : key == "clockDate" ? next.clockDate : key == "workspaceApps" ? next.workspaceApps : key == "batteryPercent" ? next.batteryPercent : key == "templateKitty" ? next.templateKitty : key == "templateGtk" ? next.templateGtk : key == "templateKde" ? next.templateKde : key == "volumeMax" ? next.volumeMax : key == "volumeStep" ? next.volumeStep : key == "batteryLow" ? next.batteryLow : key == "batteryCritical" ? next.batteryCritical : key == "batteryNotify" ? next.batteryNotify : key == "borderColor" ? next.borderColor : key == "borderActiveOpacity" ? next.borderActiveOpacity : key == "borderInactiveOpacity" ? next.borderInactiveOpacity : key == "peripheralNotify" ? next.peripheralNotify : key == "peripheralLow" ? next.peripheralLow : key == "peripheralCritical" ? next.peripheralCritical : key == "aiClaude" ? next.aiClaude : key == "aiCodex" ? next.aiCodex : key == "aiTokenLimit" ? next.aiTokenLimit : next.wallpaperTransitionMs) = std::atoi(value.c_str());
    } else if (key == "islandRounding") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be an integer from 0 to 50";
        next.islandRounding = std::clamp(std::stoi(value), 0, 50);
    } else if (key == "launcherFiles") {
        if (value != "0" && value != "1") return "value must be 0 or 1";
        next.launcherFiles = value == "1" ? 1 : 0;
    } else if (key == "fontWeightText" || key == "fontWeightTitle") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be an integer weight (text 300 to 700, titles 400 to 900)";
        if (key == "fontWeightText") next.fontWeightText = std::clamp(std::stoi(value), 300, 700);
        else next.fontWeightTitle = std::clamp(std::stoi(value), 400, 900);
    } else if (key == "iconScale") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be an integer from 60 to 150";
        next.iconScale = std::clamp(std::stoi(value), 60, 150);
    } else if (key == "islandView") {
        if (value != "media" && value != "clock") return "value must be media or clock";
        next.islandView = value;
    } else if (key == "lockAnimation") {
        if (value != "none" && value != "split_h" && value != "split_v" && value != "shape") return "value must be none, split_h, split_v or shape";
        next.lockAnimation = value;
    } else if (key == "groupSpacing") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be an integer from 2 to 6";
        next.groupSpacing = std::clamp(std::stoi(value), 2, 6);
    } else if (key == "groupStyle") {
        if (!validGroupStyle(value)) return "value must be free, transparent, pills or separated";
        next.groupStyle = value;
    } else if (key == "groupColor") {
        std::string v = value == "-" || value.empty() ? "surface_container_high" : value;
        if (!validGroupColor(v)) {  // a color of your own: #rrggbb or #rgb (with or without the #)
            if (v[0] == '#') v.erase(0, 1);
            if ((v.size() != 3 && v.size() != 6) || v.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                return "value must be a palette color (surface_container_low... tertiary_container) or a color like #1e1e2e (- for the default)";
            if (v.size() == 3) v = std::string{v[0], v[0], v[1], v[1], v[2], v[2]};
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
            v = "#" + v;
        }
        next.groupColor = v;
    } else if (key == "groupFree") {
        const std::string v = value == "-" ? "" : value;
        if (!validModuleList(v)) return "a list of modules separated by commas, like island,workspaces (- for none)";
        next.groupFree = v;
    } else if (key == "islandClick") {
        if (value != "0" && value != "1") return "value must be 0 or 1";
        next.islandClick = value == "1";
    } else if (key == "dynamicIsland") {
        if (value != "0" && value != "1") return "value must be 0 or 1";
        next.dynamicIsland = value == "1";
    } else if (key == "workspaceNumbers") {
        if (value != "normal" && value != "roman" && value != "han") return "value must be normal, roman or han";
        next.workspaceNumbers = value;
    } else if (key == "barSpacing" || key == "sideBarSpacing" || key == "dividerSpacing") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be a whole number";
        const int v = std::atoi(value.c_str());
        if (v > 24) return "value must be from 0 to 24";
        (key == "barSpacing" ? next.barSpacing : key == "sideBarSpacing" ? next.sideBarSpacing : next.dividerSpacing) = v;
    } else if (key == "animEnabled" || key == "animSpeed" || key == "barShadow") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be a whole number";
        const int v = std::atoi(value.c_str());
        if (key == "animEnabled" || key == "barShadow") { if (v > 1) return "value must be 0 or 1"; (key == "animEnabled" ? next.animEnabled : next.barShadow) = v; }
        else { if (v < 50 || v > 200) return "value must be from 50 to 200"; next.animSpeed = v; }
    } else if (key == "animStyle") {
        if (value != "boing" && value != "fade" && value != "slide" && value != "zoom") return "value must be boing, fade, slide or zoom";
        next.animStyle = value;
    } else if (key == "osdPosition") {
        if (value != "right" && value != "left" && value != "top" && value != "bottom") return "value must be right, left, top or bottom";
        next.osdPosition = value;
    } else if (key == "osdTimeout") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "value must be a number of milliseconds";
        const int ms = std::atoi(value.c_str());
        if (ms < 500 || ms > 6000) return "value must be from 500 to 6000";
        next.osdTimeout = ms;
    } else if (isCollageKey(key)) {
        if (const std::string err = setCollageKey(next, key, value); !err.empty()) return err;
    } else if (key == "wallpaperAuto" || key == "wallpaperAutoFavs" || key == "wallpaperAutoMin") {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return "Value must be a whole number";
        const int v = std::atoi(value.c_str());
        if (key == "wallpaperAuto" || key == "wallpaperAutoFavs") { if (v > 1) return "Value must be 0 or 1"; (key == "wallpaperAuto" ? next.wallpaperAuto : next.wallpaperAutoFavs) = v; }
        else { if (v < 1 || v > 1440) return "Value must be from 1 to 1440"; next.wallpaperAutoMin = v; }
    } else if (isDockKey(key)) {
        if (const std::string err = setDockKey(next, key, value); !err.empty()) return err;
    } else if (key == "barPosition") {
        if (value != "top" && value != "bottom" && value != "left" && value != "right") return "value must be top, bottom, left or right";
        next.barPosition = value;
    } else if (key == "barScreens" || key == "widgetScreen") {
        const std::string v = value == "-" ? "" : value;
        if (v.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:+,") != std::string::npos) return "value must be monitor names separated by commas";
        (key == "barScreens" ? next.barScreens : next.widgetScreen) = v;
    } else if (key == "screenshotDir" || key == "recordDir") {
        (key == "screenshotDir" ? next.screenshotDir : next.recordDir) = value == "-" ? "" : value;
    } else if (key == "customImagePath") {
        next.customImagePath = value == "-" ? "" : value;
    } else if (key == "cornerTL" || key == "cornerTR" || key == "cornerBL" || key == "cornerBR") {
        if (std::find(kCornerActions.begin(), kCornerActions.end(), value) == kCornerActions.end()) return "unknown action: " + value;
        (key == "cornerTL" ? next.cornerTL : key == "cornerTR" ? next.cornerTR : key == "cornerBL" ? next.cornerBL : next.cornerBR) = value;
    } else if (key == "quickActions") {
        std::string clean;
        if (value != "-") {
            for (const std::string& n : splitList(value)) {
                bool ok = false;
                for (int i = 0; i < kQaCount; i++) ok = ok || n == kQuickNames[i];
                if (!ok) return "unknown quick action: " + n;
            }
            clean = value;
        }
        next.quickActions = clean;
    } else if (key == "recordDesktop") {
        next.recordDesktop = std::atoi(value.c_str()) ? 1 : 0;
    } else if (key == "recordFps") {
        const int v = std::atoi(value.c_str());
        if (v != 0 && v != 24 && v != 30 && v != 60) return "fps must be 0 (automatic), 24, 30 or 60";
        next.recordFps = v;
    } else if (key == "cookieSides" || key == "cookieTime") {
        next.cookieSides = key == "cookieSides" ? std::clamp(std::atoi(value.c_str()), 6, 20) : next.cookieSides;
        next.cookieTime = key == "cookieTime" ? (std::atoi(value.c_str()) ? 1 : 0) : next.cookieTime;
    } else if (key.compare(0, 6, "cookie") == 0) {
        const CookieChoice* hit = nullptr;
        for (const auto& ch : kCookieChoices) if (key == ch.key) hit = &ch;
        if (!hit) return "unknown key";
        if (std::find(hit->allowed.begin(), hit->allowed.end(), value) == hit->allowed.end()) {
            std::string list;
            for (const auto& a : hit->allowed) list += (list.empty() ? "" : ", ") + a;
            return "value must be one of: " + list;
        }
        *cookieField(next, key) = value;
    } else if (key == "themePrimary" || key == "themeSecondary" || key == "themeTertiary") {
        const std::string v = value == "-" ? "" : value;
        if (v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) return "an accent name has letters, digits, - and _ only";
        (key == "themePrimary" ? next.themePrimary : key == "themeSecondary" ? next.themeSecondary : next.themeTertiary) = v;
    } else if (key == "theme") {
        const std::string v = value == "-" ? "" : value;
        if (v.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) return "a theme name has letters, digits, - and _ only";
        next.theme = v;
    } else if (key == "clockStyle") {
        if (value != "cookie" && value != "digital" && value != "daily" && value != "pixel") return "value must be cookie, digital, daily or pixel";
        next.clockStyle = value;
    } else if (key == "clockDaily") {
        if (value != "badge" && value != "morning" && value != "script" && value != "bold") return "value must be badge, morning, script or bold";
        next.clockDaily = value;
    } else if (key == "deskVizStyle" || key == "deskVizPos" || key == "deskVizColor") {
        const auto& list = key == "deskVizStyle" ? kDeskVizStyles : key == "deskVizPos" ? kDeskVizPositions : kDeskVizColors;
        if (!inList(list, value)) return "unknown value";
        (key == "deskVizStyle" ? next.deskVizStyle : key == "deskVizPos" ? next.deskVizPos : next.deskVizColor) = value;
    } else if (key == "hotCornerWidth" || key == "hotCornerHeight" || key == "hotCornerVisible" || key == "hotCornerHover") {
        (key == "hotCornerWidth" ? next.hotCornerWidth : key == "hotCornerHeight" ? next.hotCornerHeight : key == "hotCornerVisible" ? next.hotCornerVisible : next.hotCornerHover) = std::atoi(value.c_str());
    } else if (key == "customTextFontName") {
        next.customTextFontName = value == "-" ? "" : value;
    } else if (key == "customTextFont" || key == "customTextColor" || key == "customTextAlign") {
        const auto& list = key == "customTextFont" ? kTextFonts : key == "customTextColor" ? kTextColors : kTextAligns;
        if (!inList(list, value)) return "unknown value";
        (key == "customTextFont" ? next.customTextFont : key == "customTextColor" ? next.customTextColor : next.customTextAlign) = value;
    } else if (key == "customTextFit" || key == "customTextSize" || key == "customTextBold" || key == "customTextItalic" || key == "customTextShadow") {
        (key == "customTextFit" ? next.customTextFit : key == "customTextSize" ? next.customTextSize : key == "customTextBold" ? next.customTextBold : key == "customTextItalic" ? next.customTextItalic : next.customTextShadow) = std::atoi(value.c_str());
    } else if (key == "deskVizSize" || key == "deskVizBars") {
        (key == "deskVizSize" ? next.deskVizSize : next.deskVizBars) = std::atoi(value.c_str());
    } else if (key == "clockPixel") {
        if (value != "horizontal" && value != "vertical") return "value must be horizontal or vertical";
        next.clockPixel = value;
    } else if (key == "switcherStyle") {
        if (value != "grid" && value != "row") return "value must be grid or row";
        next.switcherStyle = value;
    } else if (key == "overviewPosition") {
        if (value != "top" && value != "bottom" && value != "left" && value != "right") return "value must be top, bottom, left or right";
        next.overviewPosition = value;
    } else if (key == "overviewIcon") {
        if (value != "left" && value != "center" && value != "right") return "value must be left, center or right";
        next.overviewIcon = value;
    } else if (key == "dividerStyle") {
        if (value != "line" && value != "dot" && value != "space") return "value must be line, dot or space";
        next.dividerStyle = value;
    } else if (key == "resStyle") {
        if (!validWidgetStyle("resources", value)) return "value must be filled, outline, text, bars, pill or m3";
        next.resStyle = value;
    } else if (key == "trayExceptions") {
        next.trayExceptions = value == "-" ? std::vector<std::string>{} : splitList(value);
    } else if (key == "sideLeft" || key == "sideCenter" || key == "sideRight") {
        std::vector<std::string> list = value == "-" ? std::vector<std::string>{} : splitList(value);
        for (const auto& m : list)
            if (!isDivider(m) && std::find(kModules.begin(), kModules.end(), m) == kModules.end()) return "unknown module: " + m;
        (key == "sideLeft" ? next.sideLeft : key == "sideCenter" ? next.sideCenter : next.sideRight) = list;
    } else if (key == "left" || key == "center" || key == "right") {
        std::vector<std::string> list = value == "-" ? std::vector<std::string>{} : splitList(value);
        for (const auto& m : list)
            if (!isDivider(m) && std::find(kModules.begin(), kModules.end(), m) == kModules.end()) return "unknown module: " + m;
        (key == "left" ? next.layoutLeft : key == "center" ? next.layoutCenter : next.layoutRight) = list;
    } else if (key == "colorScheme") {
        if (std::find(kSchemes.begin(), kSchemes.end(), value) == kSchemes.end()) return "unknown scheme";
        next.colorScheme = value;
    } else if (key == "colorMode") {
        if (value != "dark" && value != "light") return "value must be dark or light";
        next.colorMode = value;
    } else if (key == "preferredPlayer") {
        next.preferredPlayer = value == "-" ? "" : value;
    } else if (key == "terminal" || key == "searchEngine") {
        if (value.empty()) return "empty value";
        (key == "terminal" ? next.terminal : next.searchEngine) = value;
    } else if (key == "worldClocks") {
        next.worldClocks = value == "-" ? "" : value;
    } else if (key == "weatherCity") {
        next.weatherCity = value == "-" ? "" : value;
    } else if (std::find_if(kPrefixKeys, kPrefixKeys + kPrefixCount, [&](const char* k) { return key == k; }) != kPrefixKeys + kPrefixCount) {
        const int i = static_cast<int>(std::find_if(kPrefixKeys, kPrefixKeys + kPrefixCount, [&](const char* k) { return key == k; }) - kPrefixKeys);
        const unsigned char ch = value.size() == 1 ? static_cast<unsigned char>(value[0]) : 0;
        if (!(ch > 32 && ch < 127 && !std::isalnum(ch) && ch != '"' && ch != '\\')) return "prefix must be one symbol (no letters, digits or spaces)";
        for (int j = 0; j < kPrefixCount; j++) if (j != i && next.prefixes[j] == value) return "that symbol is already used by another prefix";
        next.prefixes[i] = value;
    } else if (key == "lyricsProvider") {
        if (!validLyricsProvider(value)) return "value must be auto, lrclib, betterlyrics, netease or kugou";
        next.lyricsProvider = value;
    } else if (key == "weatherUnit") {
        if (value != "celsius" && value != "fahrenheit") return "value must be celsius or fahrenheit";
        next.weatherUnit = value;
    } else if (key == "notificationShape") {
        if (value.empty()) return "empty name";
        next.notificationShape = value;
    } else if (key == "selectorShape") {
        if (value.empty()) return "empty name";
        next.selectorShape = value;
    } else if (key == "wallpaperTransition") {
        if (!validWallpaperTransition(value)) return "value must be none, fade, wipe, circle, slide, blinds, diamond, split or random";
        next.wallpaperTransition = value;
    } else if (key == "wallpaperDir") {
        if (value.empty()) return "empty path";
        next.wallpaperDir = value;
    } else if (key == "barStyle") {
        if (value != "panel" && value != "float" && value != "island" && value != "hug" && value != "notch" && value != "split") return "value must be panel, float, island, hug, notch or split";
        next.barStyle = value;
    } else if (key == "workspaceStyle") {
        if (!validWidgetStyle("workspaces", value)) return "value must be icons, default, gnome, pill, bar, ring or m3";
        next.workspaceStyle = value;
    } else if (key == "fontFamily") {
        const std::string v = value == "-" ? "" : value;
        if (!validFontFamily(v)) return "a font family name, like Google Sans Flex (or - for the default)";
        next.fontFamily = v;
    } else if (key == "iconStyle") {
        if (!validIconStyle(value)) return "value must be outline, broken, line-duotone or bold-duotone";
        next.iconStyle = value;
    } else if (key == "avatarPath" || key == "avatarPicture") {
        const std::string v = value == "-" ? "" : value;
        if (v.find('"') != std::string::npos || v.find('\\') != std::string::npos) return "bad character in the path";
        (key == "avatarPath" ? next.avatarPath : next.avatarPicture) = v;
        if (key == "avatarPicture") {
            if (app.avatar) { cairo_surface_destroy(app.avatar); app.avatar = nullptr; }
            app.avatarTried = false;
        }
    } else if (key == "accent") {
        const std::string v = value == "-" ? "" : value;
        if (!v.empty()) {
            if (v.size() != 7 || v[0] != '#' || v.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos) return "accent must be #rrggbb (or - for the wallpaper)";
        }
        next.accent = v;
    } else if (key == "notificationPosition") {
        if (!validNotifPosition(value)) return "value must be top_left, top_center, top_right, bottom_left, bottom_center or bottom_right";
        next.notificationPosition = value;
    } else if (key == "borderActive" || key == "borderInactive") {
        if (!validBorderRole(value)) return "unknown palette role";
        (key == "borderActive" ? next.borderActive : next.borderInactive) = value;
    } else if (key == "centeredWallpaperColor") {
        if (!validBorderRole(value)) return "unknown palette role";
        next.centeredWallpaperColor = value;
    } else if (key == "centeredWallpaperShape") {
        if (value.empty()) return "a shape name is needed";
        next.centeredWallpaperShape = value;
    } else if (key == "language") {
        const std::string v = value == "-" ? "" : value;
        if (!validLanguage(v)) return "a language like es or pt-BR (or - for the system language)";
        next.language = v;
    } else if (key == "widgetStyle") {
        const size_t eq = value.find('=');
        const std::string mod = eq == std::string::npos ? "" : value.substr(0, eq), id = eq == std::string::npos ? "" : value.substr(eq + 1);
        const bool side = next.barPosition == "left" || next.barPosition == "right";
        if (!(side ? sideStyleAllowed(mod, id) : validWidgetStyle(mod, id))) return "value must be module=style, like volume=pill";
        auto put = [&](std::string& list) {
            std::string out;
            for (const std::string& pair : splitList(list))
                if (pair.compare(0, mod.size() + 1, mod + "=") != 0) out += (out.empty() ? "" : ",") + pair;
            out += (out.empty() ? "" : ",") + mod + "=" + id;
            list = out;
        };
        if (side) put(next.sideWidgetStyles);
        else if (mod == "clock") next.barClock = id;
        else if (mod == "workspaces") next.workspaceStyle = id;
        else if (mod == "resources") next.resStyle = id;
        else put(next.widgetStyles);
    } else if (key == "barClock") {
        if (!validBarClock(value)) return "value must be pill, minimal, outline, tiles, badge, stacked, analog, split, compact, dot, bold or m3";
        next.barClock = value;
    } else if (key == "frameColor") {
        if (!validFrameColor(value)) return "unknown frame colour";
        next.frameColor = value;
    } else if (key == "uiBackground") {
        if (value != "themed" && value != "oled" && value != "white") return "value must be themed, oled (dark mode) or white (light mode)";
        next.uiBackground = value;
    } else {
        return "unknown key";
    }
    app.cfg = sanitized(next);
    g_iconStyle = app.cfg.iconStyle;
    g_iconScale = app.cfg.iconScale;
    g_animEnabled = app.cfg.animEnabled;
    g_animSpeed = app.cfg.animSpeed;
    g_animStyle = app.cfg.animStyle;
    matsym::enabled() = app.cfg.materialIcons != 0;
    applyUiBackground(app);
    saveConfig(app);
    return "";
}

std::string currentWallpaper() {
    std::ifstream f(stateDir() + "/wallpaper.txt");
    std::string path;
    std::getline(f, path);
    return path;
}

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// A clip to the current path (a rounded shape, say) for a stack of layers. Clipping each layer separately blends every one with the
// shape's anti-aliased edge on its own, and the layers underneath leak out there as a fringe of light dots (a bright picture under
// a dark shade, for instance). Here the layers are drawn into a group inside the shape's bounding box, and the shape cuts the finished
// group once. begin* takes the path from cairo (what roundedRect just built); end* must come before the matching cairo_restore.
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

void setSource(cairo_t* cr, const Color& c, double a = 1.0) { cairo_set_source_rgba(cr, c.r, c.g, c.b, gShapeOnlyPaint ? 1.0 : a); }

// gdk_cairo_set_source_pixbuf lives in GTK; convert RGB(A) -> premultiplied ARGB by hand.
cairo_surface_t* surfaceFromPixbuf(const GdkPixbuf* pb) {
    const int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb);
    const int nch = gdk_pixbuf_get_n_channels(pb), prs = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    unsigned char* dst = cairo_image_surface_get_data(img);
    const int drs = cairo_image_surface_get_stride(img);
    for (int y = 0; y < ph; y++) {
        const guchar* sp = px + y * prs;
        uint32_t* dp = reinterpret_cast<uint32_t*>(dst + y * drs);
        for (int x = 0; x < pw; x++, sp += nch) {
            const uint32_t a = nch == 4 ? sp[3] : 255;
            dp[x] = (a << 24) | ((sp[0] * a / 255) << 16) | ((sp[1] * a / 255) << 8) | (sp[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(img);
    return img;
}

void loadCover(App& app, const std::string& path) {
    if (app.musicArt) { cairo_surface_destroy(app.musicArt); app.musicArt = nullptr; }
    int iw = 0, ih = 0;
    if (path.empty() || !gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) || iw <= 0 || ih <= 0) return;
    const double scale = std::max(double(kMusicArt) / iw, double(kMusicArt) / ih);
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(
        path.c_str(), std::max(kMusicArt, int(iw * scale + 0.5)), std::max(kMusicArt, int(ih * scale + 0.5)), FALSE, nullptr);
    if (!pb) return;
    cairo_surface_t* img = surfaceFromPixbuf(pb);
    app.musicArt = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kMusicArt, kMusicArt);
    cairo_t* cr = mkcr(app.musicArt);
    cairo_set_source_surface(cr, img, (kMusicArt - gdk_pixbuf_get_width(pb)) / 2.0, (kMusicArt - gdk_pixbuf_get_height(pb)) / 2.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    g_object_unref(pb);
}

void shapePath(cairo_t* cr, const std::string& name, double x, double y, double size);

CwpParams cwpParams(const App& app) {
    CwpParams p;
    p.on = app.cfg.centeredWallpaper != 0 || app.cwpA.active;
    p.p = app.cwpA.active ? app.cwpA.p : 0;
    p.shape = app.cfg.centeredWallpaperShape;
    p.size = app.cfg.centeredWallpaperSize;
    const Color c = app.color(app.cfg.centeredWallpaperColor.c_str());
    p.r = c.r; p.g = c.g; p.b = c.b;
    return p;
}

const std::map<std::string, std::vector<double>>& materialShapes();
double shapeMinRadius(const std::string& name) {
    static std::map<std::string, double> cache;
    auto c = cache.find(name);
    if (c != cache.end()) return c->second;
    const auto& all = materialShapes();
    auto it = all.find(name);
    if (it == all.end()) it = all.find("Cookie9Sided");
    double best = 0.5;
    if (it != all.end()) {
        const auto& n = it->second;
        double px = n[0], py = n[1];
        for (size_t i = 2; i + 5 < n.size(); i += 6) {
            for (int s = 1; s <= 24; s++) {
                const double t = s / 24.0, u = 1 - t;
                const double x = u * u * u * px + 3 * u * u * t * n[i] + 3 * u * t * t * n[i + 2] + t * t * t * n[i + 4];
                const double y = u * u * u * py + 3 * u * u * t * n[i + 1] + 3 * u * t * t * n[i + 3] + t * t * t * n[i + 5];
                best = std::min(best, std::hypot(x - 0.5, y - 0.5));
            }
            px = n[i + 4]; py = n[i + 5];
        }
    }
    return cache[name] = std::max(0.2, best);
}

void cwpPaint(cairo_t* cr, int w, int h, const std::string& path, const CwpParams& p) {
    cairo_set_source_rgb(cr, p.r, p.g, p.b);
    cairo_paint(cr);
    const double minDim = std::min(w, h), s0 = std::min<double>(p.size, minDim * 0.96);
    const double maxBox = std::ceil(std::hypot(w / 2.0, h / 2.0) / shapeMinRadius(p.shape) * 1.02);
    const double box = s0 + p.p * (maxBox - s0), bx = (w - box) / 2, by = (h - box) / 2;
    const double overscan = box >= minDim ? 1.0 : 1.08 - 0.08 * (box - s0) / std::max(1.0, minDim - s0);
    const double k = overscan * box / std::max(box, minDim);
    cairo_save(cr);
    shapePath(cr, p.shape, bx, by, box);
    cairo_clip(cr);
    if (p.full) {
        cairo_translate(cr, w / 2.0, h / 2.0);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, p.full, -w / 2.0, -h / 2.0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);
        return;
    }
    int iw = 0, ih = 0;
    if (!path.empty() && gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih) && iw > 0 && ih > 0) {
        const double tw = std::max(box, w * k), th = std::max(box, h * k), scale = std::max(tw / iw, th / ih);
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), std::max(1, int(iw * scale + 0.5)), std::max(1, int(ih * scale + 0.5)), FALSE, nullptr);
        if (pb) {
            cairo_surface_t* img = surfaceFromPixbuf(pb);
            cairo_set_source_surface(cr, img, (w - gdk_pixbuf_get_width(pb)) / 2.0, (h - gdk_pixbuf_get_height(pb)) / 2.0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            cairo_surface_destroy(img);
            g_object_unref(pb);
        }
    }
    cairo_restore(cr);
}

void collagePaintNow(cairo_t* cr, Bar& bar, App& app);
bool collageReady(const Bar& bar, const App& app);

void backdropSetPicture(Bar& bar, cairo_surface_t* full);

void drawWallpaper(Bar& bar, App& app) {
    if (!bar.wpSurface || bar.wpW == 0 || bar.wpH == 0) return;
    if (app.cfg.collage && !collageReady(bar, app)) return;
    const int w = bar.wpW, h = bar.wpH;
    const int stride = w * 4;
    const int size = stride * h;

    int fd = memfd_create("end4pC-wallpaper", 0);
    if (fd < 0 || ftruncate(fd, size) < 0) return;
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32, w, h, stride);
    cairo_t* cr = mkcr(surface);
    setSource(cr, app.color("surface"));
    cairo_paint(cr);

    int iw = 0, ih = 0;
    CwpParams cwp = cwpParams(app);
    cwp.full = bar.cwpFull;
    if (!app.cwpA.active) app.cwpDrawn = app.cfg.centeredWallpaper;
    if (app.cfg.collage) collagePaintNow(cr, bar, app);
    else if (cwp.on) cwpPaint(cr, w, h, app.wallpaper, cwp);
    else if (!app.wallpaper.empty() && gdk_pixbuf_get_file_info(app.wallpaper.c_str(), &iw, &ih) && iw > 0 && ih > 0) {
        const double scale = std::max(double(w) / iw, double(h) / ih);
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(
            app.wallpaper.c_str(), std::max(w, int(iw * scale + 0.5)), std::max(h, int(ih * scale + 0.5)), FALSE, nullptr);
        if (pb) {
            const double dx = (w - gdk_pixbuf_get_width(pb)) / 2.0;
            const double dy = (h - gdk_pixbuf_get_height(pb)) / 2.0;
            cairo_surface_t* img = surfaceFromPixbuf(pb);
            cairo_set_source_surface(cr, img, dx, dy);
            cairo_paint(cr);
            cairo_surface_destroy(img);
            g_object_unref(pb);
        }
    }
    cairo_destroy(cr);
    if (bar.bdSurface) backdropSetPicture(bar, surface);
    cairo_surface_destroy(surface);

    wl_shm_pool* pool = wl_shm_create_pool(app.shm, fd, size);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    wl_buffer_add_listener(buffer, &kBufferListener, &bar);
    wl_surface_attach(bar.wpSurface, buffer, 0, 0);
    wl_surface_damage_buffer(bar.wpSurface, 0, 0, w, h);
    wl_surface_commit(bar.wpSurface);
}
