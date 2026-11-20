

constexpr int kBarHeight = 40;
constexpr int kWsAppIconPx = 40;
constexpr int kLaunShadow = 18;
constexpr int kBarShadowBlur = 12, kBarShadowOffset = 2;
constexpr int kBarShadowRoom = kBarShadowBlur + kBarShadowOffset;
constexpr double kBarShadowAlpha = 0.55;
constexpr int kBarGap = 5;
constexpr double kFloatRadius = 18;
constexpr double kHugRadius = 20;
constexpr double kNotchEar = 16;
constexpr double kNotchPad = 5;
constexpr double kGroupPad = 2, kGroupMidRadius = 6;  // (a cell is the box of the widget plus kGroupPad on each side)
constexpr double kIslandPad = 5;
constexpr int kButtonMargin = 8;
constexpr const char* kCollageDefaultTree = R"({"t":"leaf","id":1,"img":""})";

struct App;
struct Bar;

struct CwpParams {
    bool on = false;
    std::string shape;
    int size = 400;
    double r = 0, g = 0, b = 0;
    double p = 0;
    cairo_surface_t* full = nullptr;
};

struct CollageInsets {
    double left = 0, right = 0, top = 0, bottom = 0;
    bool operator==(const CollageInsets&) const = default;
};
struct CollageSpec {
    bool on = false;
    int gap = 30, margin = 30, radius = 30, primary = 1, blur = 50;
    std::string tree;
    CollageInsets insets;
    bool operator==(const CollageSpec&) const = default;
};

struct WpJob {
    CwpParams cwp;
    CollageSpec fromCollage, toCollage;
    std::string fromPath, toPath;
    int w = 0, h = 0;
    double bgR = 0, bgG = 0, bgB = 0;
    cairo_surface_t* from = nullptr;
    cairo_surface_t* to = nullptr;
    std::atomic<int> pending{2};
    uint64_t serial = 0;
    std::string kind;
    int ms = 700;
    ~WpJob() {
        if (from) cairo_surface_destroy(from);
        if (to) cairo_surface_destroy(to);
    }
};

struct Strip {
    Bar* bar = nullptr;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    uint32_t w = 0, h = 0;
    int corner = -1;
};

// The compositor blur behind one shell surface (ext-background-effect-v1). `object` belongs to the surface: it is destroyed before
// the wl_surface (releaseBlurSurface). `sent` is the region last sent, so an unchanged one is not sent again (02b_blur_region.inc).
struct BlurRect { int x = 0, y = 0, w = 0, h = 0; bool operator==(const BlurRect&) const = default; };
struct BlurSurface {
    ext_background_effect_surface_v1* object = nullptr;
    std::vector<BlurRect> sent;
};

struct GroupSpan { double x0, x1, rl, rr; };

struct Bar {
    App* app = nullptr;
    wl_output* output = nullptr;
    uint32_t outputName = 0;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layerSurface = nullptr;
    bool configured = false;
    uint32_t width = 0;
    uint32_t height = kBarHeight;
    wl_buffer* buffer = nullptr;
    wl_surface* wpSurface = nullptr;
    zwlr_layer_surface_v1* wpLayer = nullptr;
    Bar* wpRef = nullptr;
    uint32_t wpW = 0, wpH = 0;
    wl_surface* bdSurface = nullptr;
    zwlr_layer_surface_v1* bdLayer = nullptr;
    uint32_t bdW = 0, bdH = 0;
    cairo_surface_t* bdPic = nullptr;
    // The bar's shadow lives on a layer surface of its own, under the bar's: the compositor's blur rule for the bar (SwayFX blurs everything of
    // the surface that is not fully transparent) would otherwise blur the shadow too, and a dark band would show.
    wl_surface* shSurface = nullptr;
    zwlr_layer_surface_v1* shLayer = nullptr;
    uint32_t shW = 0, shH = 0;
    std::string shSig;
    Strip strips[4];
    Strip rounds[4];
    std::string roundApplied;
    Strip corners[4];
    std::map<std::string, double> slot;
    std::vector<std::pair<double, bardivider::Style>> dividers;
    std::vector<GroupSpan> groups;  // the cells behind the widgets (cfg.groupStyle)
    std::vector<bool> vizMirror;
    std::shared_ptr<WpJob> wpJob;
    cairo_surface_t* animFrom = nullptr;
    cairo_surface_t* animTo = nullptr;
    std::chrono::steady_clock::time_point animStart;
    std::string animKind;
    int animMs = 700;
    int frameApplied = -1;
    std::string positionApplied;
    std::string styleApplied;
    uint32_t extra = 0;
    std::vector<std::pair<double, double>> regionSegs;
    cairo_surface_t* cwpFull = nullptr;
    mutable std::vector<std::pair<double, double>> segs;
    int contentY = 0;
    std::string connector;
    std::string screensApplied;
    BlurSurface blur;
};

// One screen as the compositor announces it: its global name, and the connector name once wl_output (v4) has said it.
struct OutInfo {
    App* app = nullptr;
    wl_output* output = nullptr;
    uint32_t name = 0;
    std::string connector;
};

struct Color {
    double r = 0.1, g = 0.1, b = 0.1;
};

char kPfxAction = '/', kPfxApp = '>', kPfxClipboard = ';', kPfxEmoji = ':', kPfxMath = '=',
     kPfxShell = '$', kPfxWeb = '?', kPfxKeybind = '<', kPfxTranslate = '.';
constexpr int kPrefixCount = 9;
constexpr const char* kPrefixKeys[kPrefixCount] = {"prefixAction", "prefixApp", "prefixClipboard", "prefixEmoji", "prefixMath", "prefixShell", "prefixWeb", "prefixKeybind", "prefixTranslate"};
constexpr const char* kPrefixDefaults = "/>;:=$?<.";
char* const kPrefixSlots[kPrefixCount] = {&kPfxAction, &kPfxApp, &kPfxClipboard, &kPfxEmoji, &kPfxMath, &kPfxShell, &kPfxWeb, &kPfxKeybind, &kPfxTranslate};
int g_animEnabled = 1, g_animSpeed = 100;
std::string g_animStyle = "boing";
std::string g_iconStyle = "broken";
int g_iconScale = 100;  // % of the size every icon is asked for (Settings > Appearance > Font & Icons)

const std::vector<std::string> kSchemes = {"scheme-tonal-spot", "scheme-neutral", "scheme-content", "scheme-expressive", "scheme-fidelity",
                                           "scheme-fruit-salad", "scheme-monochrome", "scheme-rainbow", "scheme-vibrant", "scheme-smart"};
const std::vector<std::string> kCornerActions = {"none", "controlcenter", "media", "wallpapers", "launcher", "overview", "notifications", "power", "lock", "settings"};
const std::vector<std::string> kModules = {"workspaces", "media", "quickactions", "network", "volume", "clock", "notifications", "settings", "avatar", "battery", "systray", "resources", "launcher", "power", "tools", "weather", "aiusage", "keyboard", "activewindow", "divider", "visualizer", "updates", "bluetooth", "docktopanel", "drives", "privacy"};

inline bool validNotifPosition(const std::string& p) {
    for (const char* n : {"top_left", "top_center", "top_right", "bottom_left", "bottom_center", "bottom_right"})
        if (p == n) return true;
    return false;
}
inline bool validLanguage(const std::string& s) {
    if (s.size() > 12) return false;
    for (const char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    return true;
}
inline bool validBarClock(const std::string& s) {
    for (const char* n : {"pill", "minimal", "outline", "tiles", "badge", "stacked", "analog", "split", "compact", "dot", "bold", "m3"})
        if (s == n) return true;
    return false;
}
struct WidgetStyleOption { const char* id; const char* label; bool framed = false; };  // framed: the look draws a body of its own
struct WidgetStyleSet { const char* mod; const char* title; const char* legacy; std::vector<WidgetStyleOption> options; };
inline const std::vector<WidgetStyleSet>& widgetStyleSets() {
    static const std::vector<WidgetStyleOption> frames = {{"plain", "Plain"}, {"pill", "Pill", true}, {"outline", "Outline", true}, {"badge", "Badge", true}, {"tonal", "Tonal", true}, {"square", "Square", true}};
    auto withFrames = [](std::vector<WidgetStyleOption> own) { std::vector<WidgetStyleOption> v = frames; v.insert(v.end(), own.begin(), own.end()); return v; };
    static const std::vector<WidgetStyleOption> roundFrames = {{"plain", "Plain"}, {"pill", "Pill", true}, {"outline", "Outline", true}, {"badge", "Badge", true}, {"square", "Square", true}};
    static const std::vector<WidgetStyleOption> btnLooks = {{"m3soft", "Filled tonal", true}, {"m3filled", "Filled", true}};
    auto withBtn = [](std::vector<WidgetStyleOption> v) { v.insert(v.end(), btnLooks.begin(), btnLooks.end()); return v; };
    static const std::vector<WidgetStyleSet> sets = [&] { std::vector<WidgetStyleSet> v = {
        {"clock", "Clock", "barClock", {{"pill", "Pill", true}, {"minimal", "Minimal"}, {"outline", "Outline", true}, {"tiles", "Tiles", true}, {"badge", "Badge", true}, {"stacked", "Stacked"}, {"analog", "Analog"}, {"split", "Split"}, {"compact", "Compact"}, {"dot", "Dot"}, {"bold", "Pixel"}}},
        {"workspaces", "Workspaces", "workspaceStyle", {{"icons", "Icons"}, {"default", "Squares"}, {"gnome", "Dots"}, {"pill", "Pill", true}, {"bar", "Bars"}, {"ring", "Rings"}}},
        {"resources", "Resources", "resStyle", {{"filled", "Filled", true}, {"outline", "Outline", true}, {"text", "Text"}, {"bars", "Bars"}, {"pill", "Pill", true}}},
        {"weather", "Weather", "", {{"plain", "Plain"}, {"pill", "Pill", true}, {"outline", "Outline", true}, {"badge", "Badge", true}, {"tonal", "Tonal", true}, {"square", "Square", true}, {"icon", "Icon"}, {"temp", "Temp"}, {"stacked", "Stacked"}}},
        {"volume", "Volume", "", withBtn(withFrames({{"level", "Level"}, {"percent", "Percent"}}))},
        {"network", "Network", "", withBtn(withFrames({{"label", "Label"}}))},
        {"battery", "Battery", "", withFrames({{"ring", "Ring"}, {"bar", "Bar"}, {"text", "Text"}})},
        {"keyboard", "Keyboard layout", "", withFrames({{"code", "Code"}, {"icon", "Icon"}})},
        {"aiusage", "AI usage", "", {{"plain", "Plain"}, {"pill", "Pill", true}, {"outline", "Outline", true}, {"badge", "Badge", true}, {"tonal", "Tonal", true}, {"square", "Square", true}, {"ring", "Ring"}, {"bar", "Bar"}, {"text", "Text"}}},
        {"activewindow", "Active window", "", withFrames({{"title", "Title"}, {"chip", "Chip"}})},
        {"notifications", "Notifications", "", withBtn({{"plain", "Plain"}, {"pill", "Pill", true}, {"outline", "Outline", true}, {"badge", "Badge", true}, {"square", "Square", true}, {"dot", "Dot"}, {"count", "Count"}})},
        {"settings", "Settings button", "", withBtn(roundFrames)},
        {"launcher", "Launcher button", "", withBtn(roundFrames)},
        {"power", "Power button", "", withBtn(roundFrames)},
        {"tools", "Tools button", "", withBtn(roundFrames)},
        {"updates", "Updates", "", withBtn(withFrames({}))},
        {"bluetooth", "Bluetooth", "", withBtn(withFrames({}))},
        {"drives", "Removable drives", "", withBtn(withFrames({}))},
        {"privacy", "Privacy indicators", "", withBtn(withFrames({}))},
        {"avatar", "Avatar", "", {{"plain", "Plain"}, {"ring", "Ring"}, {"squircle", "Squircle"}}},
        {"quickactions", "Quick actions", "", withBtn(roundFrames)},
        {"media", "Media", "", withFrames({{"compact", "Compact"}, {"cover", "Cover"}, {"progress", "Progress"}})},
        {"visualizer", "Visualizer", "", {{"dots", "Dots"}, {"winamp", "Winamp"}, {"reflect", "Reflection"}, {"m3", "M3"}}},
    };
    for (WidgetStyleSet& set : v) {
        if (std::string(set.mod) == "quickactions") set.options.push_back({"split", "Split"});
        const bool hasM3 = std::any_of(set.options.begin(), set.options.end(), [](const WidgetStyleOption& o) { return std::string(o.id) == "m3"; });
        if (!hasM3) set.options.push_back({"m3", "M3", true});
    }
    return v; }();
    return sets;
}
inline const WidgetStyleSet* widgetStyleSet(const std::string& mod) {
    for (const WidgetStyleSet& s : widgetStyleSets())
        if (mod == s.mod) return &s;
    return nullptr;
}
inline const std::vector<std::pair<std::string, std::vector<std::string>>>& sideStyleIds() {
    static const std::vector<std::string> frames = {"plain", "pill", "outline", "badge", "tonal", "square", "m3"};
    static const std::vector<std::string> btnFrames = {"plain", "pill", "outline", "badge", "tonal", "square", "m3soft", "m3filled", "m3"};
    static const std::vector<std::pair<std::string, std::vector<std::string>>> ids = {
        {"clock", {"pill", "outline", "m3"}},
        {"workspaces", {"icons", "default", "gnome", "pill", "bar", "ring", "m3"}},
        {"resources", {"filled", "outline", "text", "m3"}},
        {"avatar", {"plain", "ring", "squircle", "m3"}},
        {"visualizer", {"dots", "winamp", "reflect", "m3"}}, {"weather", frames}, {"volume", btnFrames}, {"network", btnFrames}, {"battery", frames}, {"keyboard", frames}, {"aiusage", frames},
        {"activewindow", frames}, {"media", frames},
        {"notifications", {"plain", "pill", "outline", "badge", "square", "dot", "m3soft", "m3filled", "m3"}},
        {"settings", {"plain", "pill", "outline", "badge", "square", "m3soft", "m3filled", "m3"}}, {"launcher", {"plain", "pill", "outline", "badge", "square", "m3soft", "m3filled", "m3"}},
        {"power", {"plain", "pill", "outline", "badge", "square", "m3soft", "m3filled", "m3"}}, {"tools", {"plain", "pill", "outline", "badge", "square", "m3soft", "m3filled", "m3"}},
        {"updates", btnFrames},
        {"bluetooth", btnFrames},
        {"drives", btnFrames}, {"privacy", btnFrames},
        {"quickactions", {"plain", "pill", "outline", "badge", "square", "m3soft", "m3filled", "m3"}},
    };
    return ids;
}
inline bool sideStyleAllowed(const std::string& mod, const std::string& id) {
    for (const auto& e : sideStyleIds())
        if (e.first == mod) return std::find(e.second.begin(), e.second.end(), id) != e.second.end();
    return false;
}
inline std::vector<WidgetStyleOption> widgetStyleChoices(const std::string& mod, bool side) {
    std::vector<WidgetStyleOption> out;
    if (const WidgetStyleSet* set = widgetStyleSet(mod))
        for (const WidgetStyleOption& o : set->options)
            if (!side || sideStyleAllowed(mod, o.id)) out.push_back(o);
    return out;
}
inline bool validWidgetStyle(const std::string& mod, const std::string& id) {
    const WidgetStyleSet* s = widgetStyleSet(mod);
    if (!s) return false;
    for (const WidgetStyleOption& o : s->options)
        if (id == o.id) return true;
    return false;
}
inline bool notchLike(const std::string& style) { return style == "notch" || style == "split"; }
inline bool validFrameColor(const std::string& r) {
    for (const char* n : {"black", "surface", "surface_container", "primary_container", "secondary_container", "tertiary_container", "primary", "secondary", "tertiary", "outline_variant"})
        if (r == n) return true;
    return false;
}
inline bool validBorderRole(const std::string& r) {
    for (const char* n : {"primary", "secondary", "tertiary", "primary_container", "secondary_container", "tertiary_container", "outline", "outline_variant"})
        if (r == n) return true;
    return false;
}
inline bool validGroupStyle(const std::string& s) {
    for (const char* n : {"free", "transparent", "pills", "separated"})
        if (s == n) return true;
    return false;
}
inline bool hexColorText(const std::string& s) { return s.size() == 7 && s[0] == '#' && s.find_first_not_of("0123456789abcdef", 1) == std::string::npos; }
inline bool validGroupColor(const std::string& r) {
    for (const char* n : {"surface", "surface_container_low", "surface_container", "surface_container_high", "surface_container_highest", "primary_container", "secondary_container", "tertiary_container"})
        if (r == n) return true;
    return hexColorText(r);
}
inline bool validModuleList(const std::string& s) { return s.find_first_not_of("abcdefghijklmnopqrstuvwxyz,") == std::string::npos; }
inline const char* const kWallpaperTransitions[] = {"none", "fade", "wipe", "circle", "slide", "blinds", "diamond", "split"};
inline const char* const kLyricsProviders[] = {"auto", "lrclib", "betterlyrics", "netease", "kugou"};
inline bool validLyricsProvider(const std::string& v) {
    for (const char* k : kLyricsProviders) if (v == k) return true;
    return false;
}
inline bool validWallpaperTransition(const std::string& v) {
    if (v == "random") return true;
    for (const char* k : kWallpaperTransitions) if (v == k) return true;
    return false;
}

struct Config {
    int workspaces = 8;
    int frame = 4;
    int networkSpeed = 0;
    int networkAlways = 0;
    int centeredWallpaper = 0;
    std::string centeredWallpaperShape = "Cookie7Sided";
    int centeredWallpaperSize = 400;
    std::string centeredWallpaperColor = "primary_container";
    int collage = 0;
    int collageGap = 30;
    int collageMargin = 30;
    int collageRadius = 30;
    int collageBlur = 50;
    int collagePrimary = 1;
    std::string collageTree = kCollageDefaultTree;
    int lockOnStart = 0;
    int unlockKeyring = 1;
    std::string frameColor = "black";
    int barFollowsFrame = 0;
    int fakeRounding = 0;
    int fakeRoundingSize = 0;
    int islandRounding = 34;
    int notchWidth = 0;
    int barOpacity = 80;
    int updatesEnable = 1;
    int updatesAur = 0;
    int updatesFlatpak = 0;
    int updatesInterval = 60;
    int updatesMinShow = 0;  // the bar shows the updates widget from this many pending updates (0: always)
    std::string cornerTL = "none", cornerTR = "controlcenter", cornerBL = "media", cornerBR = "wallpapers";
    int dividerSpacing = 0;
    int barShadow = 0;
    std::string islandView = "media";
    int dynamicIsland = 0;
    std::string lockAnimation = "none";  // none | split_h | split_v | shape: how the lock screen comes in and goes out (src/lock.cpp)
    std::string groupStyle = "free";  // free | transparent | pills | separated (see hasGroupCell)
    int groupSpacing = 2;  // px between the cells of the group styles (2 to 6)
    std::string groupColor = "surface_container_high";
    std::string groupFree = "island";  // comma separated modules that keep their own look and get no group cell
    int islandClick = 0;  // 1: the island opens on a click instead of on hover
    int barSpacing = 4;
    int sideBarSpacing = 4;
    int animEnabled = 1;
    int animSpeed = 100;
    std::string animStyle = "boing";
    int hotCorners = 1;
    int hotCornerWidth = 6, hotCornerHeight = 6, hotCornerVisible = 0, hotCornerHover = 0;  // the size of the corner regions, drawing them, and acting when the pointer enters (no click)
    std::string notificationPosition = "top_right";
    int doNotDisturb = 0;
    int notificationTimeout = 4000;
    int privacyAlways = 0, privacyMic = 1, privacyCam = 1, privacyScreen = 1;  // privacy indicators: show them even when idle; which of the three to watch
    std::string overviewPosition = "top";  // the overview strip: top, bottom, left or right
    std::string switcherStyle = "grid";   // Alt + Tab: "grid" of cards, or a "row" across the middle
    int overviewLauncher = 0;            // the overview also opens the launcher (and closes with it)
    int overviewLive = 1;                // the overview shows live windows (0: a box with the app icon)
    std::string overviewIcon = "right";  // where the app icon sits on a window of the overview: left, center or right
    int conflictCheck = 1;  // 1: at the first start of a session, look for programs that conflict with the shell
    int notificationAppIcons = 0;  // 1: a notification without a picture shows its app's icon instead of a symbol
    std::string osdPosition = "right";
    int osdTimeout = 2000;
    std::string barScreens;
    std::string widgetScreen;
    std::string barPosition = "top";
    std::string barStyle = "panel";
    std::vector<std::string> layoutLeft = {"media"};
    std::vector<std::string> layoutCenter = {"workspaces"};
    std::vector<std::string> layoutRight = {"quickactions", "network", "volume", "battery", "clock", "notifications", "settings"};
    std::vector<std::string> sideLeft = {"avatar", "workspaces"};
    std::vector<std::string> sideCenter = {};
    std::vector<std::string> sideRight = {"systray", "battery", "notifications", "settings", "clock"};
    std::string workspaceNumbers = "normal";
    std::string workspaceStyle = "icons";
    std::string colorScheme = "scheme-neutral";
    std::string colorMode = "dark";
    std::string wallpaperDir = "~/Pictures/Wallpapers";
    std::string wallpaperTransition = "fade";
    int wallpaperTransitionMs = 700;
    int wallpaperAuto = 0;
    int wallpaperAutoFavs = 0;
    int wallpaperAutoMin = 30;
    std::string worldClocks = "America/New_York,Europe/Madrid,Asia/Tokyo";
    std::string terminal = "kitty";
    std::string weatherCity;
    int clock24 = 1;
    std::string language;
    std::string sideWidgetStyles;
    std::string widgetStyles;
    std::string barClock = "pill";
    int clockDate = 1;
    int templateGtk = 0, templateKde = 0;
    int templateKitty = 0;
    int batteryPercent = 1;
    int workspaceApps = 0;
    int volumeMax = 100;
    int volumeStep = 2;
    int batteryLow = 20;
    int batteryCritical = 5;
    int batteryNotify = 1;
    int resCpu = 1;
    int resRam = 1;
    int resTemp = 0;
    int resDisk = 0;
    int resSwap = 0;
    int resValue = 0;
    int resInterval = 3000;
    int resWarnMem = 95;
    int resWarnSwap = 85;
    int resWarnCpu = 90;
    std::string dividerStyle = "line";
    int aiClaude = 1, aiCodex = 1;
    int aiTokenLimit = 1000;
    std::string resStyle = "filled";
    int weatherTheme = 0;
    int weatherInterval = 10;
    std::string weatherUnit = "celsius";
    std::string prefixes[9] = {"/", ">", ";", ":", "=", "$", "?", "<", "."};
    std::string lyricsProvider = "auto";
    std::string searchEngine = "https://www.google.com/search?q=";
    // The MPRIS player the bar and the media panel follow first; any other player is the fallback when it is not running.
    std::string preferredPlayer;
    std::string notificationShape = "Cookie9Sided";
    std::string selectorShape = "Cookie9Sided";
    std::string fontFamily;
    std::string iconStyle = "broken";
    int materialIcons = 0;
    int iconScale = 100;
    int fontWeightText = 450, fontWeightTitle = 550;  // wght of a variable font: normal text and bold/titles (Settings > Appearance > Font & Icons)
    int launcherFiles = 0;  // the launcher also searches files (off by default)
    std::string avatarPath;
    std::string themePrimary, themeSecondary, themeTertiary;
    std::string quickActions = "picker,record,screenshot,wallpaper";
    std::string theme;
    std::string accent;
    std::string avatarPicture;
    int peripheralNotify = 1;
    int peripheralLow = 20;
    int peripheralCritical = 5;
    int borderColor = 0;                           // 1: the window borders use palette colours (saved to Hyprland's overrides file)
    std::string borderActive = "primary", borderInactive = "outline_variant";
    int borderActiveOpacity = 47, borderInactiveOpacity = 20;
    int trayPinDefault = 0;
    int trayTint = 0;
    std::vector<std::string> trayExceptions;
    int barBackground = 1;
    int systray = 0;
    std::string customImagePath;
    std::string screenshotDir, recordDir;
    int screenshotCopy = 1;
    int recordFps = 0;
    int recordDesktop = 0;
    int recordAudio = 0;
    int cookieSides = 14, cookieTime = 1;
    std::string cookieDial = "full", cookieHour = "fill", cookieMinute = "medium", cookieSecond = "hide", cookieDate = "bubble";
    std::string clockDaily = "badge";
    std::string clockPixel = "horizontal";
    std::string clockStyle = "cookie";
    int calendarMonday = 1;
    int widgetGap = 12;
    // the desktop visualizer (14z_desk_viz.inc)
    std::string deskVizStyle = "bars", deskVizPos = "bottom", deskVizColor = "gradient";
    int deskVizSize = 220, deskVizBars = 48;
    // the text widget of the desktop (14zb_text_widget.inc)
    std::string customTextFontName, customTextFont = "Display", customTextColor = "on_surface", customTextAlign = "center";
    int customTextFit = 1, customTextSize = 56, customTextBold = 0, customTextItalic = 0, customTextShadow = 1;
    int widgetShadow = 1;
    int widgetsLocked = 0;
    int keepAwake = 0;
    std::string uiBackground = "themed";
    int dock = 0;
    std::string dockPinned = "kitty,firefox,org.kde.dolphin";
    int dockIconSize = 44;
    int dockMagnify = 1;
    int dockMagnification = 140;
    int dockHug = 0;
    int dockSpacing = 6;
    int dockRounding = 40;
    int dockBorder = 0;
    std::string dockPosition = "bottom";
    std::string dockScreen;
    std::string dockBorderColor = "primary";
    int dockLauncher = 0;
    int dockMedia = 0;
    int dockPin = 0;
    int dockAutoHide = 0;
};

struct AppAction { std::string name, icon, exec; };
struct AppEntry {
    std::string id, name, generic, comment, keywords, icon, exec;
    bool terminal = false;
    std::vector<AppAction> actions;
};
enum ResultKind { ResApp, ResMath, ResCommand, ResWeb, ResAction, ResClip, ResEmoji, ResKeybind, ResFile, ResQr, ResConv };
enum PopPhase { kPopOpen = 0, kPopIn = 1, kPopOut = 2, kPopGone = 3 };

struct PopAnim {
    int phase = kPopOpen;
    double reveal = 0, from = 0;
    double overshoot = 1.3;
    double inSeconds = 0;
    double outSeconds = 0;
    std::chrono::steady_clock::time_point animStart, settleAt;
};

struct LauncherResult {
    int kind = ResApp;
    std::string name, type, verb, comment;
    std::string icon;
    std::string payload;
    bool terminal = false;
    bool footer = false;
    bool pinned = false;
    bool image = false;
};

struct Launcher {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    std::string text;           // what the user typed (UTF-8)
    size_t cursor = 0;
    size_t anchor = 0;
    textsel::Clicks clicks;
    bool selDrag = false;
    textsel::Drag dragInfo;
    double fieldX = 0, fieldW = 0, scrollX = 0;
    std::chrono::steady_clock::time_point convPoll{};
    std::vector<LauncherResult> results;
    int selected = 0, scroll = 0, hover = -1;
    bool sliding = false;
    PopAnim anim;
    bool animInit = false;
    double hFrom = 0, hTo = 0, wFrom = 0, wTo = 0;
    std::chrono::steady_clock::time_point hStart, wStart;
    int btnHover = -1;
    std::string mathResult;
    pid_t mathPid = 0;
    int mathFd = -1;
    pid_t filePid = 0;  // `fd` looking for files by name (the results of a search come when it ends)
    int fileFd = -1;
    std::string fileBuf, fileFor;
    std::vector<std::string> fileHits;
    bool recentLoaded = false;
    std::vector<std::string> recent;  // files the desktop's apps opened lately (recently-used.xbel), newest first
    std::string mathBuf;
    bool mathIsTr = false;
    std::string trFor, trOut, trSrc, trTgt;
    bool trFailed = false, trPending = false;
    std::chrono::steady_clock::time_point trDue;
    std::vector<std::string> clipLines;
    bool clipLoaded = false;
    cairo_surface_t* shadow = nullptr;
    int shadowW = 0, shadowH = 0;
    std::set<std::string> clipThumbAsked;
    std::map<std::string, cairo_surface_t*> icons;
    bool open() const { return surface != nullptr; }
};

struct PowerMenu {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int w = 0, h = 0;
    int hover = -1;
    int selected = -1;
    int armed = -1;
    PopAnim anim;
    bool open() const { return surface != nullptr; }
};

struct PolkitReq {
    std::string action, message, icon, cookie;
    std::vector<uint32_t> uids;
    void* handle = nullptr;
};
struct PolkitDlg {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int w = 0, h = 0;
    bool active = false;
    PolkitReq req;
    std::vector<PolkitReq> queue;
    uint32_t uid = 0;
    std::string user;
    int fd = -1;
    std::string rbuf;
    bool gotAnything = false;
    bool prompting = false, busy = false, echo = false;
    std::string prompt = "Password", text, note;
    bool noteErr = false;
    int hover = -1;
    PopAnim anim;
    double cancelR[4] = {0, 0, 0, 0}, okR[4] = {0, 0, 0, 0};
    bool open() const { return surface != nullptr; }
};

struct SwitcherWin {
    std::string address, stable, cls, title;
    int workspace = 0;
    cairo_surface_t* thumb = nullptr;
};
struct Switcher {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int w = 0, h = 0;
    std::vector<SwitcherWin> wins;
    int sel = 0, hover = -1;
    bool rowMode = false;  // the "row" style: one row of cards across the middle
    bool holdFlavour = false;
    bool sawHeld = false;
    std::string cacheDir;
    std::chrono::steady_clock::time_point openedAt;
    PopAnim anim;
    bool open() const { return surface != nullptr; }
};

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct CcAp { std::string ssid; int strength = 0; bool active = false, secure = false, saved = false; };
struct CcDev { std::string mac, name, icon, state; int battery = -1; };
enum CcPage { CcPageNone = 0, CcPageWifi, CcPageBt };
enum CcDialog { CcDlgNone = 0, CcDlgWifiPassword, CcDlgWifiDetails, CcDlgBtPair, CcDlgBtDetails };
enum CcHitKind { CcHkNone = 0, CcHkBack, CcHkScan, CcHkSwitch, CcHkRow, CcHkInfo, CcHkButton };
enum CcDlgButton { CcDbCancel = 1, CcDbDone, CcDbSettings, CcDbSubmit, CcDbDisconnect, CcDbForget };
struct CcHit {
    Rect r; int kind = CcHkNone; int id = 0; std::string target, tip;
    std::string key() const { return std::to_string(kind) + "|" + std::to_string(id) + "|" + target; }
};
struct CcRadio {
    Elastic band;  // the rubber band at the ends of the list
    std::string wifi = "absent", bt = "absent";
    std::string wifiIp;
    bool wifiScan = false, btScan = false;
    std::vector<CcAp> aps;
    std::vector<CcDev> devs;
    long long listStamp = 0, actStamp = 0;
    std::string actId, actAction, actTarget, actStatus;
    std::string note;
    bool noteErr = false;
    int dialog = CcDlgNone;
    std::string dlgTarget, dlgError, text;
    bool dlgBusy = false;
    Rect dlgBox;
    std::vector<CcHit> hits;
    std::string hover;
    Bubble bubble;
    bool bubbleMoving = false;
    double scroll = 0;
    std::chrono::steady_clock::time_point nextPoll{};
};

struct ControlCenter {
    int tickId = 0;
    std::chrono::steady_clock::time_point tickSince{};
    bool ticking = false;
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int hover = 0;
    int drag = 0;
    std::string wifi = "absent", wifiName, bt = "absent", btName, night = "absent";
    time_t stamp = 0;
    std::chrono::steady_clock::time_point nextPoll, lastSend;
    std::string signature;
    cairo_surface_t* cover = nullptr;
    std::string coverKey;
    int page = CcPageNone;
    bool pageOpen = false;
    double slide = 0;
    std::chrono::steady_clock::time_point slideAt{};
    CcRadio radio;
    PopAnim anim;
    double along = -1;
    bool open() const { return surface != nullptr; }
};

struct Notif {
    uint32_t id = 0;
    std::string app, icon, summary, body, imagePath, desktopEntry;
    std::vector<std::pair<std::string, std::string>> actions;
    int urgency = 1;
    cairo_surface_t* image = nullptr;
    std::chrono::steady_clock::time_point hideAt;
    bool popup = true;
    bool leaving = false;
    std::chrono::steady_clock::time_point shownAt{}, leaveAt{};
    bool sticky = false;
    bool closedSent = false;
    time_t time = 0;
};

struct NotifPopup {
    struct Card {
        uint32_t id = 0;
        Rect rect, close;
        std::vector<std::pair<Rect, std::string>> buttons;
        double reveal = 1;
        bool leaving = false;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    std::vector<Card> cards;
    uint32_t hoverId = 0;
    int hoverButton = -1;
    std::map<std::string, cairo_surface_t*> icons;
    bool open() const { return surface != nullptr; }
};

struct NotifPanel {
    struct Row {
        uint32_t id = 0;
        Rect rect, close;
        std::vector<std::pair<Rect, std::string>> buttons;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    double scroll = 0, contentH = 0, maxScroll = 0;
    Elastic band;  // the rubber band at the ends of the list, and the fading scroll mark
    uint32_t hoverId = 0;
    bool overList = false;
    int hoverButton = -1;
    std::vector<Row> rows;
    Rect clearAll, dnd;
    cairo_surface_t* shadow = nullptr;
    int shadowH = 0;
    int pillX = 0;
    double originX = 0, originY = 0, pillHeight = 0;
    PopAnim anim;
    double along = -1;
    bool open() const { return surface != nullptr; }
};

enum OsdKind { OsdVolume, OsdBrightness };
struct Osd {
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int kind = OsdVolume;
    double shown = 0, target = 0;
    std::chrono::steady_clock::time_point hideAt;
    cairo_surface_t* shadow = nullptr;
    PopAnim anim;
    bool open() const { return surface != nullptr; }
};

struct OnlineItem { std::string id, thumb, full, dl, author, title; int w = 0, h = 0; };

struct Selector {
    wl_surface* surface = nullptr;
    PopAnim anim;
    cairo_surface_t* animPicture = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    std::vector<std::string> allFiles;
    std::vector<std::string> files;
    int scrollRow = 0;
    int hoverTile = -1, hoverChip = -1, hoverMode = -1;
    int hoverNav = -1, hoverBtn = -1;
    bool overGrid = false;
    int focusIdx = -1;
    bool kbFocus = false;
    std::vector<Rect> chipRects;
    std::map<std::string, cairo_surface_t*> thumbs;
    std::set<std::string> requested;
    std::vector<std::pair<std::string, std::string>> jobs;
    int inflight = 0;
    double modeT = 1, modeTarget = 1;
    TabAnim navAnim;
    int panelH = 0;
    int gridY = 0;
    std::vector<std::pair<std::string, std::string>> cats;
    int cat = 0;
    std::string query;
    size_t qCur = 0, qAnc = 0;
    textsel::Clicks qClicks;
    bool qDrag = false;
    textsel::Drag qDragInfo;
    std::vector<std::string> favs, recents;
    cairo_surface_t* hero = nullptr;
    std::string heroPath;
    int heroW = 0, heroH = 0;
    std::string onlineProvider;
    std::vector<OnlineItem> onlineItems;
    int onlinePage = 0, onlinePages = 0;
    int onlineState = 0;
    std::string onlineError, onlineQuery;
    int onlineGen = 0;
    std::string onlineSort = "toplist";
    int onlineRes = 0;
    std::string onlineSeed, onlineNote;
    bool keyPanel = false, keyReveal = false, keySaving = false;
    std::string keyInput, keyMsg;
    std::set<std::string> downloading;
    std::string msgShown;
    std::chrono::steady_clock::time_point msgAt{};
    std::string confirmPath;
    Bubble bubble;
    bool bubbleMoving = false;
    bool open() const { return surface != nullptr; }
};

struct CapBuf { wl_buffer* buf = nullptr; void* data = nullptr; size_t size = 0; bool busy = false; struct App* app = nullptr; };

struct CapAnnot {
    int type = 0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::vector<std::pair<double, double>> pts;
    Color col;
    double w = 5;
    std::string text;
    int n = 0;
    std::shared_ptr<cairo_surface_t> mosaic;
    double iw = 1, ih = 1;
    int block = 10;
};

struct Capture {
    int autoAct = 0;  // 1: text recognition, 2: Google Lens, 3: QR code, as soon as an area is chosen
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int w = 0, h = 0;
    cairo_surface_t* shot = nullptr;
    std::string shotFile;
    bool has = false;
    double sx = 0, sy = 0, sw = 0, sh = 0;
    int mode = 0;
    int handle = -1, hoverBtn = -1;
    double ax = 0, ay = 0;
    Rect start{};
    Bubble bubble;
    bool bubbleMoving = false;
    bool record = false;
    int recAudio = 0, recDesktop = 0, recFps = 0;
    std::string monName;
    int monX = 0, monY = 0;
    int tool = 0;
    int colorIdx = 0, sizeIdx = 1;
    std::vector<CapAnnot> items, redo;
    int editing = -1;
    int hoverStyle = -1;
    CapBuf bufs[2];
    cairo_surface_t* base = nullptr;
    bool dirty = false, framePending = false;
    bool open() const { return surface != nullptr; }
};

struct LyricLine {
    double time = 0;
    std::string text;
};
struct MediaColors {
    Color layer0, layer1, onLayer0, subtext, primary, primaryHover, primaryActive;
    Color secondaryContainer, secondaryContainerHover, secondaryContainerActive, onPrimary, onSecondaryContainer;
};

struct EqState {  // the equalizer of the media panel (see 13d_eq_backend.inc); it outlives the panel
    double gain[10] = {};
    double preamp = 0;
    bool enabled = false, loaded = false;
    int preset = 0;  // the index of the preset the curve matches, -1: a curve of its own
};
struct MediaPanel {
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int cardX = 0;
    double originX = 0, originY = 0;
    cairo_surface_t* cover = nullptr;
    cairo_surface_t* blur = nullptr;
    cairo_surface_t* shadow = nullptr;
    std::string coverKey;
    MediaColors col;
    Rect rPrev, rPlay, rNext, rArt, rSlider, rPin, rLyrics, rLyricsRetry;
    int hover = 0;
    int drag = 0;
    double position = 0, length = 0, volume = 1;
    std::string status;
    std::chrono::steady_clock::time_point polledAt, nextPoll, nextFrame, lastSend;
    pid_t cavaPid = 0;
    int cavaFd = -1;
    std::string cavaBuf;
    std::vector<double> points;
    bool lyricsMode = false;
    double lyricT = 0, lyricV = 0;
    bool eqMode = false, eqMoving = false;  // the equalizer page grows the card; eqT animates like lyricT
    double eqT = 0, eqV = 0;
    std::chrono::steady_clock::time_point eqLast{}, eqApplied{};
    Rect rEq, rEqToggle, eqBand[11], eqChip[8];
    int eqDrag = -1, eqHover = -1;  // a band (0..9, 10: the preamp) being dragged; hover: 100 + band, 200 + chip, 300 the switch
    bool lyricMoving = false;
    std::chrono::steady_clock::time_point lyricLast{}, lyricChange{};
    std::vector<LyricLine> lyrics;
    std::string lyricStatus = "idle";
    uint64_t lyricSeen = 0;
    int lyricActive = -2;
    double cardH = 140;
    bool anchoredBottom = false;
    int shadowH = 0;
    int inputPinned = -1, inputH = 0;
    PopAnim anim;
    double along = -1;
    bool open() const { return surface != nullptr; }
};

// One item of the system tray (StatusNotifierItem): what the app told us about itself, and its icon ready to paint.
struct TrayIcon {
    std::string bus, path, id, title, status, iconName, themePath;
    std::string menu;
    bool isMenu = false;
    int pw = 0, ph = 0;
    std::vector<unsigned char> px;
    cairo_surface_t* surf = nullptr;  // kTrayIcon x kTrayIcon, premultiplied
};

struct TrayMenu {
    struct Node {
        int id = 0, parent = -1, depth = 0;
        std::string label, type, shortcut;
        bool enabled = true, visible = true, expanded = false, hasChildren = false, checked = false;
        int toggle = 0;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    std::string bus, path;
    std::string trayId;
    std::vector<Node> nodes;
    std::vector<int> rows;
    std::vector<Rect> rects;
    int hover = -1;
    int cardW = 0, cardH = 0;
    bool open() const { return surface != nullptr; }
};

struct BtBar {
    std::string bt = "absent", name;
    time_t stamp = 0;
    std::chrono::steady_clock::time_point nextPoll{}, nextRead{};
};
struct BtMenu {
    struct Item {
        enum Kind { Power, Note, Row, Disconnect, Footer } kind = Power;
        int dev = -1;
        Rect r;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    Bar* bar = nullptr;
    double ax = 0, ay = 0, ix = 0, iy = 0, ox = 0, oy = 0;
    PopAnim anim;
    CcRadio radio;
    long long listStamp = 0, actStamp = 0;
    std::string actId, pending;
    std::string note;
    std::vector<Item> items;
    int cardW = 0, cardH = 0;
    int hover = -1, pressed = -1;
    Bubble bubble;
    bool bubbleMoving = false;
    std::chrono::steady_clock::time_point nextList{};
    bool open() const { return surface != nullptr; }
};

struct DriveInfo {
    std::string dev, parent, name, fstype, mount, icon;  // one entry per physical disk: dev is the disk, mount the first mount point of its partitions
    std::vector<std::string> parts, unmounted;     // the partitions with a file system, and the ones not mounted
    unsigned long long size = 0, biggest = 0;
};
struct PrivacyBar {
    std::string mic, cam, screen;  // the apps using each (empty: none)
    bool test = false;             // a preview made by hand (end4pC privacy test)
    std::chrono::steady_clock::time_point nextPoll{}, nextRead{};
};
struct DrivesBar {
    std::vector<DriveInfo> list;
    std::set<std::string> ejecting;
    bool primed = false;
    long long stamp = 0;
    std::chrono::steady_clock::time_point nextPoll{}, nextRead{};
};
struct DrivesMenu {
    struct Item {
        enum Kind { Row, Eject, Copy, Note } kind = Row;
        int dev = -1;
        Rect r;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    Bar* bar = nullptr;
    double ax = 0, ay = 0, ix = 0, iy = 0, ox = 0, oy = 0;
    PopAnim anim;
    std::string pending, note;
    std::chrono::steady_clock::time_point pendingAt{}, hoverAt{};
    Bubble bubble;
    bool bubbleMoving = false, marquee = false;
    int copied = -1;  // the drive whose mount point was just copied (its button shows a tick until the pointer moves on)
    std::vector<Item> items;
    int cardW = 0, cardH = 0, hover = -1, pressed = -1;
    bool open() const { return surface != nullptr; }
};

struct IslandCard {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    Bar* bar = nullptr;
    Rect col, exp;
    double barOx = 0, barOy = 0;
    bool notch = false, flip = false;
    PopAnim anim;
    int surfW = 0, surfH = 0;
    cairo_surface_t* art = nullptr;
    int hover = -1, pressed = -1;
    std::chrono::steady_clock::time_point leaveAt{}, nextFrame{};
    bool open() const { return surface != nullptr; }
    bool hidesBar() const { return surface != nullptr && configured; }
};

struct AudioDevice {
    bool sink = true;
    std::string action, target, extra, title, sub, icon, tags;
    bool isDefault = false;
    int vol = -1;
    bool muted = false;
    bool active = true;
};
struct AudioApp {
    int index = 0;
    std::string name, icon, media;
    int vol = 100;
    bool muted = false, corked = false;
};

struct AudioMenu {
    enum Tab { TabOutput = 0, TabInput = 1, TabApps = 2 };
    struct Item {
        enum Kind { Head, Tab, Label, Row, AppRow, Slider, Mute, Note } kind = Head;
        int section = 0;
        int dev = -1;
        bool fixed = false;
        Rect r;
    };
    struct Job {
        int fd = -1;
        pid_t pid = 0;
        std::string buf;
        bool dirty = false;
    };
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    Bar* bar = nullptr;
    double ax = 0, ay = 0;
    std::vector<AudioDevice> devs;
    std::vector<AudioApp> apps;
    std::map<std::string, std::shared_ptr<cairo_surface_t>> icons;
    bool loaded = false;
    int inVol = 0;
    bool inMuted = false;
    int tab = TabOutput;
    std::vector<Item> items;
    int cardW = 0, cardH = 0;
    int regionH = 0;
    int headH = 0;
    int hover = -1;
    int pressed = -1;
    int scroll = 0, contentH = 0;
    Elastic band;  // the rubber band at the ends of the list
    double viewH = 0, viewV = 0;
    double cardTop = 0;
    TabAnim ta;
    std::vector<Item> prevItems;
    int prevScroll = 0;
    bool layoutMoving = false;
    std::chrono::steady_clock::time_point stepLast{};
    cairo_surface_t* shadow = nullptr;
    int shadowH = -1;
    int inTop = -1, inH = -1;
    PopAnim anim;
    double ox = 0, oy = 0;
    double ix = 0, iy = 0;
    int drag = -1;
    int dragPct = 0;
    Job job;
    bool open() const { return surface != nullptr; }
};

struct DesktopMenu {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    std::vector<std::string> files;
    std::map<std::string, cairo_surface_t*> thumbs;
    std::vector<double> widths;
    double curAnim = 0;
    bool carMoving = false;
    int current = 0, hovered = -1;
    int hoverRow = -1;
    bool subOpen = false, subLeft = false;
    int subKind = 0;
    int hoverSub = -1;
    Rect card, sub;
    std::vector<std::pair<int, Rect>> subHits;
    PopAnim anim;
    double ox = 0, oy = 0;
    int surfW = 0, surfH = 0;
    bool animating = false;
    Bubble bubble;
    std::chrono::steady_clock::time_point last{};
    bool open() const { return surface != nullptr; }
};

struct App;
struct DeskAlt {
    std::string key;
    int l = 0, t = 0, w = 0, h = 0;
    bool pinned = false;
};

struct DeskWidget {
    const char* name = "";
    App* app = nullptr;
    int minW = 280, maxW = 640, minH = 300, maxH = 900;
    bool resizable = true, wantsKeys = false, lightHandle = false;
    bool noShadow = false;
    bool blurCard = false;               // the card is translucent: the compositor blurs what is behind it (02b_blur_region.inc)
    BlurSurface bgBlur;
    int radius = 28;
    cairo_surface_t* shadow = nullptr;
    int shadowW = 0, shadowH = 0;
    int moveZone = -1;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false, enabled = false;
    int w = 360, h = 480;
    int ml = -1, mt = 24;
    bool pinned = false;
    std::string scrKey;
    std::vector<DeskAlt> alts;
    int hover = -1;
    bool drag = false, pending = false, moveMode = false, busy = false, wantFrame = false, dirty = false;
    bool full = false, fullReady = false, settling = false;
    // Where the pointer positions of a drag are counted from. Hyprland sends them in the full surface as soon as it is configured; sway (a button
    // is held) goes on sending them from the origin the small surface had at the click. 0: not known yet (the first movement tells), 1: the
    // full surface, 2: the small one, whose origin is (frameOx, frameOy) in the full one.
    int frameMode = 0;
    double frameOx = 0, frameOy = 0;
    double lastGx = 0, lastGy = 0;  // where the pointer was, on the screen, at the last drag step: each step takes the reading that continues from it
    bool armed = false;
    int fullW = 0, fullH = 0, reqW = 0, reqH = 0;
    double gx = 0, gy = 0;
    std::chrono::steady_clock::time_point settlingSince{};
    std::chrono::steady_clock::time_point busySince{};
    double grabRight = 0, grabBottom = 0, grabX = 0, grabY = 0;
    bool editing = false;
    int edge = 0;
    double rsl = 0, rst = 0, rsw = 0, rsh = 0, rpx = 0, rpy = 0;
    std::vector<std::pair<int, Rect>> hits;
    void (*paint)(App&, DeskWidget&, cairo_t*, int, int) = nullptr;
    void (*click)(App&, DeskWidget&, int) = nullptr;
    void (*scroll)(App&, DeskWidget&, int) = nullptr;
    int (*cursor)(const DeskWidget&, int) = nullptr;
    void (*saveExtra)(const DeskWidget&, std::ostream&) = nullptr;
    void (*loadLine)(DeskWidget&, const std::string&) = nullptr;
    void (*onToggle)(App&, bool) = nullptr;
    bool kbFocus = false, noMove = false;
    double keepAspect = 0;
    bool (*typing)(const DeskWidget&) = nullptr;
    bool (*key)(App&, DeskWidget&, uint32_t) = nullptr;
    void (*blur)(App&, DeskWidget&) = nullptr;
    void (*onResize)(App&, DeskWidget&, int, int) = nullptr;
    void (*tap)(App&, DeskWidget&) = nullptr;
    bool (*motion)(App&, DeskWidget&) = nullptr;
    void (*release)(App&, DeskWidget&) = nullptr;
    bool pressed = false;
    double pressX = 0, pressY = 0;
    bool moved = false;
    bool open() const { return surface != nullptr; }
};

struct TodoItem {
    bool done = false;
    std::string text;
};
struct Todo : DeskWidget {
    std::vector<TodoItem> items;
    std::string text;
    size_t cursor = 0;
    size_t anchor = 0;
    textsel::Clicks clicks;
    textsel::Drag dragInfo;
    bool selDrag = false;
    double fieldShift = 0;
    bool fieldActive = false;
    double scrollY = 0;
    Rect list;
};

struct HourW {
    int hour = -1;
    double temp = 0;
    int code = 0;
    bool day = true;
};
struct DayW {
    int year = 0, mon = 0, mday = 0;
    double hi = 0, lo = 0;
    int code = 0;
};
struct WeatherData {
    bool ok = false;
    std::string city, unit;
    double temp = 0, feels = 0, hi = 0, lo = 0;
    int code = 0;
    bool day = true;
    HourW hours[3];
    HourW hourly[5];
    int humidity = -1;
    double wind = 0;
    int sunrise = -1, sunset = -1;
    DayW days[7];
    int nDays = 0;
    time_t fetched = 0;
};
bool weatherSvcGet(WeatherData& out, uint64_t* version);
void weatherSvcWant(App& app, bool want);
void weatherDescribe(int c, bool day, std::string& label, std::string& ic);
void barModulesSync(App& app);
struct WeatherW : DeskWidget {
    WeatherData data;
    uint64_t seen = 0;
};

struct MediaW : DeskWidget {
    cairo_surface_t* blur = nullptr;
    std::string blurKey, trackKey;
    std::vector<LyricLine> lines;
    std::vector<double> lineH, lineTop;
    int lineHW = 0;
    std::string lyricsStatus = "idle";
    uint64_t lyricsSeen = 0;
    int activeIdx = -1;
    double anim = 0;
    bool animating = false;
    std::chrono::steady_clock::time_point last{}, nextPoll{}, nextDraw{};
};

struct ResW : DeskWidget {
    double target[4] = {0, 0, 0, 0}, shown[4] = {0, 0, 0, 0};
    double ramUsedGb = 0, ramTotalGb = 0, diskUsedGb = 0, diskTotalGb = 0;
    double cpuTempC = -1;
    std::string tempPath;
    bool tempLooked = false;
    unsigned long long cpuIdle = 0, cpuTotal = 0;
    bool primed = false;
    bool dirty = false;
    std::string key;
    bool animating = false;
    std::chrono::steady_clock::time_point last{};
    std::vector<std::string> turnNames;
    int turn = 0;
    std::chrono::steady_clock::time_point turnAt{};
    TabAnim turnAnim;
    std::string prevName, prevIcon, prevSub;
    double prevVal = 0; bool prevOk = false;
    unsigned long long netRx = 0, netTx = 0;
    std::chrono::steady_clock::time_point netAt{};
    double netDown = 0, netUp = 0, netPeak = 0;
};

struct ClockTimer {
    int focus = 1500, shortBreak = 300, longBreak = 900, cycles = 4;
    int cycle = 0;
    bool running = false, isBreak = false;
    double left = 1500;
    std::chrono::steady_clock::time_point end{};
};

struct ClockPanel {
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int cardX = 0, cardY = 0;
    double originX = 0, originY = 0;
    double anchor = 0;
    std::string styleMod;
    int kind = 0;
    int tab = 0, hover = 0, lastMin = -1, lastSec = -1;
    TabAnim ta;
    double cardH = 0;
    int shadowForH = 0;
    bool adding = false;
    std::string query;
    int pick = 0;
    int year = 0, month = 0;
    int selY = -1, selM = 0, selD = 0;
    double reveal = 0;
    int phase = 0;
    double from = 0;
    double growX = 0, growY = 0;
    std::vector<double> cores;
    std::vector<unsigned long long> coreIdle, coreTotal;
    double uptimeS = 0, swapUsedGb = 0, swapTotalGb = 0, ramFreeGb = 0, netPeak = 1e6;
    int procs = 0;
    double batWatts = -1, batHours = -1, batHealth = -1;
    int batCycles = -1, lastBatPct = -1;
    bool lastBatChg = false;
    std::string profile;
    unsigned long long netRx = 0, netTx = 0;
    std::chrono::steady_clock::time_point netAt{};
    double netDown = 0, netUp = 0;
    WeatherData wx;
    uint64_t wxSeen = 0;
    bool wxRefreshing = false;
    uint64_t wxReqVersion = 0;
    std::chrono::steady_clock::time_point wxReqAt{};
    cairo_surface_t* shadow = nullptr;
    std::chrono::steady_clock::time_point animStart{}, settleAt{};
    std::vector<std::pair<int, Rect>> hits;
    bool open() const { return surface != nullptr; }
};

struct CalW : DeskWidget {
    int year = 0, month = 0;
    int selY = -1, selM = 0, selD = 0;
    int slide = 0;
    std::chrono::steady_clock::time_point slideStart{}, nextCheck{};
    int dayKey = 0;
    int viewDelta = 0;
};

struct ClockW : DeskWidget {
    std::string styleShown;
    WeatherData wx;
    uint64_t wxSeen = 0;
    int lastMinute = -1, lastSecond = -1;
    double hourAngle = 0, minuteAngle = 0;
    double hourTarget = 0, minuteTarget = 0;
    std::chrono::steady_clock::time_point last{}, nextCheck{};
};

struct UserW : DeskWidget {
    cairo_surface_t* hero = nullptr;
    cairo_surface_t* face = nullptr;
    std::string heroPath, facePath, name, host;
    std::string customImage;
    long upSeconds = 0;
    std::chrono::steady_clock::time_point nextRead{};
};

struct ImageW : DeskWidget {
    cairo_surface_t* img = nullptr;
    std::string imgPath;
    std::chrono::steady_clock::time_point lastClick{};
};

struct DockItem {
    std::string key;
    std::string name;
    std::string iconName;
    std::string entryId, exec;
    int special = 0;
    bool terminal = false, pinned = false, separator = false, running = false, focused = false;
    std::vector<std::string> addresses;
    std::shared_ptr<cairo_surface_t> icon;
    double scale = 1, vel = 0, target = 1;
    double extra = 0;
    double baseCx = 0;
    Rect slot;
    double shift = 0, shiftVel = 0;
};
struct OskState {  // the on-screen keyboard
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false, placed = false, caps = false, shiftLock = false, dragging = false, movePending = false, relayout = false;
    int mods = 0, hover = -1, pressed = -1;
    double marginLeft = 0, marginBottom = 40, grabX = 0, grabY = 0;
    Bar* bar = nullptr;                              // the bar of the monitor it is on
    bool onOther = false;                            // while dragging, the pointer is over another surface of the shell (it moved faster than the keyboard)
    double otherX = 0, otherY = 0, accDx = 0, accDy = 0;
};
struct CfItem { std::string comm, name, kind; };
struct CfState {  // the card that lists the programs that conflict with the shell
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int hover = -1;  // 100 not now / 101 never ask / 102 close them
    std::vector<CfItem> items;
};
struct FqWin { std::string address, cls, title; int pid = 0, ws = 0; };
struct FqState {  // the force quit card: the open windows to pick from
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int hover = -1, sel = -1, scroll = 0;  // hover: a row, or 100 cancel / 101 close / 102 force quit
    std::vector<FqWin> wins;
};
struct OverviewState;
struct OvWin {  // a window of the overview and the live capture of it (ext-image-copy-capture)
    std::string address, cls, title, handleId;
    int ws = 0;
    double x = 0, y = 0, w = 1, h = 1;  // where the window really is on its monitor
    ext_image_copy_capture_session_v1* ses = nullptr;
    ext_image_copy_capture_frame_v1* frame = nullptr;
    wl_shm_pool* pool = nullptr;
    wl_buffer* buf = nullptr;
    void* map = nullptr;
    size_t mapSize = 0;
    int bw = 0, bh = 0, fails = 0, lastReason = -1;
    long frames = 0;  // frames received
    uint32_t fmt = 0;
    cairo_surface_t* surf = nullptr;
    bool sized = false, inFlight = false, hasFrame = false, dead = false, realloc = false;
    bool fixed = false;  // floating or fullscreen: the gaps do not move it
    cairo_surface_t* last = nullptr;  // the last frame, small: it shows while the next one is not there (a resized window, or just opened)
    std::chrono::steady_clock::time_point next{};
    OverviewState* ov = nullptr;
    struct App* app = nullptr;
    OvWin() = default;
    OvWin(const OvWin&) = delete;
    ~OvWin() { if (last) cairo_surface_destroy(last); }
};
inline cairo_surface_t* ovImage(const OvWin& w) { return w.hasFrame && w.surf ? w.surf : w.last; }
struct OverviewState {  // the overview (SUPER + O): a strip with every workspace, live, on one edge of the screen; the real windows are pushed away from it
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false, dirty = false, gapsSet = false, hoverPlus = false;
    int width = 0, height = 0, monId = 0;      // (the strip's surface)
    std::string monName;                       // the monitor it is on: the one in focus, where the pointer is
    int edge = 0;                              // 0 top, 1 bottom, 2 left, 3 right
    double monW = 1, monH = 1, thick = 0, edgeReserved = 0;  // the monitor, how thick the strip is, and what the bar already takes on its edge
    std::vector<std::unique_ptr<OvWin>> wins;
    int sel = 1, extra = 0, hoverWs = 0;       // the workspace in use; workspaces added with +; the one under the pointer
    double stripScroll = 0, monX = 0, monY = 0, dragX = 0, dragY = 0;
    int pressWin = -1;                         // a window of a thumbnail under the pressed button; dragging: it is being carried
    bool carrying = false;
    double pressX = 0, pressY = 0;
    int dragSeen = 0;                          // polls in a row with the cursor over the strip and no events: a window is being dragged over it
    bool dragArmed = false;
    std::string dragWin;                       // the window being dragged (the one in focus when it began)
    double resv[4] = {0, 0, 0, 0};             // what the bar and the like reserve: left, top, right, bottom
    int gapNow[4] = {5, 5, 5, 5};              // the gaps while the overview is open
    int gapOrig[4] = {5, 5, 5, 5};             // gaps_out before the overview: top, right, bottom, left
    std::chrono::steady_clock::time_point nextDraw{}, switchedAt{}, nextPoll{}, reloadAt{}, nextRefresh{};
    wl_surface* resSurface = nullptr;          // invisible 1 px surface that reserves the room for the strip on this screen only
    zwlr_layer_surface_v1* resLayer = nullptr;
    wl_buffer* resBuf = nullptr;
    int resZone = 0;
    bool gapsPending = false;                  // the gaps wait for the first frames (they resize the windows, and a capture of a window that is resizing fails)
    std::chrono::steady_clock::time_point gapsAt{};
    bool closing = false;                      // sliding out; it goes when it is out
    std::chrono::steady_clock::time_point openT0{}, closeT0{};
    bool withLauncher = false;                 // opened together with the launcher: each closes the other
    bool reloadAgain = false;                  // a second look a moment later: the windows resize after they move
    struct Slot { wl_buffer* buf = nullptr; void* map = nullptr; bool busy = false; } slot[2];  // two buffers that take turns
    size_t slotSize = 0;
    int slotW = 0, slotH = 0;
};
struct FtHandle { ext_foreign_toplevel_handle_v1* h = nullptr; std::string id; };  // a toplevel the compositor lists (its id is hyprctl's stableId)
struct ConfirmState {  // the Android-style card that asks before clearing the cache
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int hover = -1;
    uintmax_t bytes = 0;
};
struct QrState {  // the card that shows a QR code made from a link or a text
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false;
    int hover = -1;
    std::string text, png;  // png: the module-per-pixel image qrencode wrote
    cairo_surface_t* image = nullptr;
    int modules = 0;
};
enum GameWidget { GwResources, GwVolume, GwRecorder, GwNotes, GwCrosshair, kGwCount };
struct GameHit;
// The visualizer of the desktop: one background-layer surface under the widgets, with no input, fed by the same cava stream as the bar's.
struct DeskViz {
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    wl_output* output = nullptr;
    bool enabled = false, configured = false;
    bool covered = false;                // a window is fullscreen on its screen: nothing is drawn
    int W = 0, H = 0;
    cairo_surface_t* cs[2] = {nullptr, nullptr};
    wl_buffer* buf[2] = {nullptr, nullptr};
    unsigned char* mem = nullptr;
    size_t memSize = 0;
    bool busy[2] = {false, false};
    int bufW = 0, bufH = 0, lastBuf = 1;
    bool framePending = false, dirty = false, idleDrawn = false;
    std::vector<double> shown, peaks;
    cairo_surface_t* art = nullptr;      // the cover of the ring, loaded at its own size (the bar's copy is tiny)
    std::string artKey;
    int ringL = -1, ringT = -1;          // where the ring sits (its own margins from the screen corner; -1: centred)
    bool dragging = false, movePending = false;
    double grabX = 0, grabY = 0;
    std::string sig, layoutSig;                     // style, position, size, colors and bars last applied
    std::chrono::steady_clock::time_point last{};
};

struct GameOverlay {  // the game overlay (SUPER + G)
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    bool configured = false, closing = false, animating = false, placed = false, volDrag = false;
    int W = 0, H = 0, hover = 0, dragW = -1;
    std::chrono::steady_clock::time_point t0{}, nextRes{}, lastVolSet{};
    bool open[kGwCount] = {true, false, false, false, false};
    double wx[kGwCount] = {}, wy[kGwCount] = {}, grabX = 0, grabY = 0;
    // the notes card: its size, the edge being dragged to resize it (1 right, 2 bottom, 3 both), and the translucent copy that stays when it is pinned
    double nw = 460, nh = 330, rsX = 0, rsY = 0, rsW = 0, rsH = 0;
    int rsMode = 0;
    bool notesPinned = false, ghostConfigured = false;
    wl_surface* ghostSurface = nullptr;
    zwlr_layer_surface_v1* ghostLayer = nullptr;
    std::vector<struct GameHit> hits;
    wl_surface* crossSurface = nullptr;
    zwlr_layer_surface_v1* crossLayer = nullptr;
    bool crossConfigured = false, crossOn = false;
    // two buffers kept and reused (a new 8 MB one on every move made dragging crawl), one redraw per frame, and only the moved part damaged
    wl_buffer* buf[2] = {nullptr, nullptr};
    void* mem[2] = {nullptr, nullptr};
    cairo_surface_t* cs[2] = {nullptr, nullptr};
    bool busy[2] = {false, false};
    size_t memSize = 0;
    int bufW = 0, bufH = 0, lastBuf = 1;
    bool framePending = false, dirty = false, needFull = false, partialRequest = false, pendValid = false;
    double pendX0 = 0, pendY0 = 0, pendX1 = 0, pendY1 = 0;
};
struct DockPanelState {  // the "docktopanel" bar module: the dock's apps in the bar
    std::vector<DockItem> items;
    std::map<std::string, std::shared_ptr<cairo_surface_t>> icons;
    bool dirty = true;
    int pressIdx = -1;
    bool dragging = false;
    int dragFrom = -1, dragTo = -1;
    double pressX = 0, dragX = 0;
};
enum DockMenuKind { DockMenuPin, DockMenuUnpin, DockMenuNew, DockMenuClose, DockMenuPrev, DockMenuNext, DockMenuPlayer };
struct DockState {
    uint64_t layerGen = 0;
    int gapOut = 5;  // Hyprland's gaps_out: the space the dock keeps from the screen edge, the same one windows keep above it
    wl_surface* surface = nullptr;
    BlurSurface blur;
    zwlr_layer_surface_v1* layer = nullptr;
    wl_output* output = nullptr;
    bool configured = false;
    bool barHere = false;
    int pos = 0;
    int w = 0, h = 0;
    int reqH = 0, reqW = 0;
    double squeeze = 1;
    double slide = 0, slideVel = 0, slideTarget = 0;
    bool moving = false;
    bool hideArmed = false;
    std::chrono::steady_clock::time_point lastStep{}, hideAt{}, nextRefresh{};
    std::vector<DockItem> items;
    Rect pill;
    int hover = -1;
    int pressIdx = -1;
    bool dragging = false;
    std::string dragKey;
    double pressX = 0, dragGrab = 0, dragCx = 0;
    std::map<std::string, std::shared_ptr<cairo_surface_t>> icons;
    bool menuOpen = false;
    int menuItem = -1, menuHover = -1;
    double menuX = 0;
    std::vector<std::pair<DockMenuKind, std::string>> menuRows;
    std::vector<Rect> menuRects;
    bool gridOpen = false, gridClosing = false;
    std::chrono::steady_clock::time_point gridStart{};
    double gridT = 0;
    int gridScroll = 0, gridHover = -1;
    std::vector<int> gridApps;
    std::vector<int> gridShown;
    std::string gridText;
    size_t gridAppsFor = 0;
    Rect gridCard;
    Rect mediaPlay, mediaNext;
    std::vector<std::pair<Rect, int>> gridCells;
    bool open() const { return surface != nullptr; }
};

struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    wp_cursor_shape_manager_v1* cursorMgr = nullptr;
    wp_cursor_shape_device_v1* cursorDev = nullptr;
    zwp_idle_inhibit_manager_v1* idleMgr = nullptr;
    zwp_idle_inhibitor_v1* idler = nullptr;
    uint32_t enterSerial = 0;
    int cursorShape = -1;
    zwlr_layer_shell_v1* layerShell = nullptr;
    ext_background_effect_manager_v1* bgEffectManager = nullptr;
    ext_foreign_toplevel_list_v1* ftList = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1* ftSrcMgr = nullptr;
    ext_image_copy_capture_manager_v1* copyCapMgr = nullptr;
    std::vector<FtHandle> ftHandles;
    OverviewState ov;
    std::map<std::string, cairo_surface_t*> ovCache;  // a small last picture of each window, from the last time the overview was open
    bool popupKeysBound = false;  // Ctrl + Tab is bound while a popup with tabs is open
    OverviewState swLive;  // the live captures of the window switcher (the same machinery as the overview)
    bool blurCapable = false;
    std::vector<std::unique_ptr<Bar>> bars;
    std::vector<std::pair<wl_output*, uint32_t>> pendingOutputs;
    std::vector<std::unique_ptr<OutInfo>> outputs;

    bool running = true;
    std::chrono::steady_clock::time_point updatesLast{};
    int updatesTotal = -1;
    time_t updatesStamp = -1;
    int updatesSources = -1;
    bool updatesChecking = false;
    std::chrono::steady_clock::time_point updatesCheckEnd{};
    std::chrono::steady_clock::time_point updatesRecheckAt{};
    bool ready = false;  // initial roundtrip finished: new outputs get a bar right away

    Bar* hoverBar = nullptr;
    double pointerX = 0;
    double pointerY = 0;
    // Vertical scroll: the source of the current pointer frame (wl_pointer.axis_source) and, for a touchpad,
    // the amount still to turn into steps (see onPointerAxis).
    uint32_t axisSource = WL_POINTER_AXIS_SOURCE_WHEEL;
    double axisPending = 0, axisPendingH = 0, axisCarry = 0;
    std::string clockTime, clockDate, clockAmPm;
    int clockW = 120;
    int islandTimeW = 40;
    double islandMusicT = 0;
    std::chrono::steady_clock::time_point islandLast{};
    struct BarWeather { bool ok = false; int temp = 0, code = 0; bool day = true; } barWeather;
    struct BarAi { bool seen = false; double pct = 0; int sessions = 0; long long resetsAt = 0; } barAi[2];
    int weatherBarW = 0, aiBarW = 0;
    double aiPillSplit = 0;
    // The bar's "keyboard" module and its popup: the layouts of the main keyboard (Hyprland's input:kb_layout) and the one in use.
    struct KbdLayout { std::string code, name; };
    struct { std::string cls, title; cairo_surface_t* icon = nullptr; std::string iconFor; int barW = 0, titleW = 0, clsW = 0, m3W = 0; } awin;
    struct { std::vector<KbdLayout> list; std::vector<std::string> variants; int active = 0; std::string device; int barW = 0, codeW = 0, m3W = 0; } kbd;
    double wsPill = 1;
    bool wsPillSeen = false;
    std::chrono::steady_clock::time_point wsPillLast{};
    std::vector<double> vizShown;
    std::chrono::steady_clock::time_point vizNext{};
    int workspace = 1;  // fed by Hyprland socket2
    std::set<int> occupied;
    int wsCount = 0;
    std::map<int, std::string> wsMainClass;
    std::map<std::string, cairo_surface_t*> wsIcons;
    std::string musicPlayer;  // exact MPRIS instance reported by the metadata watcher
    std::string musicPlaybackPlayer;  // native browser timeline paired with richer bridge metadata
    std::string musicStatus;
    std::string musicArtist, musicTitle;
    int musicTextW = 100;
    int musicTitleW = 100;
    int musicLineW = 100;  // "Title • Artist" of the Progress look of the media widget
    int musicM3TextW = 0;
    std::string musicArtUrl;
    std::string musicArtFallback;  // keep a fallback across metadata signals with no cover URL
    int musicArtAttempts = 0;
    std::chrono::steady_clock::time_point musicArtRetryAt{};
    cairo_surface_t* musicArt = nullptr;
    int volume = 0;
    bool micMuted = false;
    std::string perfProfile;
    std::string netKind = "none";
    double netDown = 0, netUp = 0;
    unsigned long long netPrevRx = 0, netPrevTx = 0;
    std::chrono::steady_clock::time_point netPrevAt{};
    std::string netText;
    bool recording = false;
    pid_t recPid = 0;
    int recFd = -1;
    int notifCount = 0;
    bool muted = false;
    std::vector<TrayIcon> tray;
    bool trayOpen = false;
    double trayT = 0;
    double quickGrow[8] = {};
    std::chrono::steady_clock::time_point quickGrowLast{};
    bool trayDirty = false;
    std::chrono::steady_clock::time_point trayLast{}, trayRead{};
    double statusT[4] = {0, 0, 0, 0};
    struct Periph { std::string name, state, kind; int pct = 0; };
    std::vector<Periph> periph;
    std::chrono::steady_clock::time_point periphLast{}, periphRead{}, periphSignalDue{};
    bool periphEverRead = false;
    struct Resources {
        double cpu = 0, ram = 0, swap = 0, disk = 0, temp = 0; unsigned long long idle = 0, total = 0; std::string tempPath;
        double ramUsedGb = 0, ramTotalGb = 0, diskUsedGb = 0, diskTotalGb = 0, tempC = -1;
    } res;
    std::chrono::steady_clock::time_point resLast{};
    bool batPresent = false;
    int batPct = 0;
    bool batCharging = false;
    std::string batFake;
    std::chrono::steady_clock::time_point statusLast{};
    Config cfg;
    Selector sel;
    Capture cap;
    Switcher sw;
    PolkitDlg pk;
    sd_bus* pkBus = nullptr;
    Launcher launcher;
    PowerMenu power;
    bool onPower = false;
    ControlCenter cc;
    bool onCc = false;
    wl_surface* catSurface = nullptr;
    zwlr_layer_surface_v1* catLayer = nullptr;
    bool onCatcher = false;
    Osd osd;
    int musicFd = -1;
    pid_t musicPid = 0;
    std::string watchedPlayer;
    int cwpDrawn = -1;
    struct {
        bool active = false;
        double from = 0, to = 0, p = 0;
        int ms = 650;
        std::chrono::steady_clock::time_point start;
    } cwpA;
    uint64_t wpSerial = 0;
    CollageSpec collageShown;
    sd_bus* bus = nullptr;
    std::vector<Notif> notifs;
    uint32_t nextNotifId = 1;
    NotifPopup np;
    bool onNotif = false;
    NotifPanel panel;
    bool onPanel = false;
    MediaPanel media;
    EqState eq;
    cairo_surface_t* avatar = nullptr;
    bool avatarTried = false;
    bool onMedia = false;
    TrayMenu tmenu;
    bool onTmenu = false;
    AudioMenu amenu;
    bool onAmenu = false;
    BtBar btBar;
    BtMenu btMenu;
    DrivesBar drives;
    PrivacyBar privacy;
    DrivesMenu drivesMenu;
    bool onBtMenu = false;
    bool onOsk = false, onQr = false, onConfirm = false, onFq = false, onCf = false, onOv = false, onDrives = false, onGame = false, onViz = false;
    IslandCard ic;
    bool onIsland = false;
    int screenH = 0;
    DesktopMenu dmenu;
    bool onDmenu = false;
    Todo todo;
    WeatherW weather;
    MediaW mediaW;
    ResW resW;
    CalW calW;
    ClockPanel clockP;
    ClockTimer timer;
    bool onClockP = false;
    DockState dock;
    DockPanelState dtp;
    OskState osk;
    QrState qr;
    ConfirmState confirm;
    FqState fq;
    CfState cf;
    std::chrono::steady_clock::time_point conflictCheckAt{};
    GameOverlay game;
    DeskViz dv;
    bool onDock = false;
    ClockW deskClock;
    UserW userW;
    ImageW imageW;
    DeskWidget* onDesk = nullptr;
    Bar* wallBar = nullptr;
    std::chrono::steady_clock::time_point wallpaperAutoNext{};
    int wallpaperAutoFor = 0;
    bool mediaPinned = false;
    uint64_t layerGen = 0;
    int hugRadius = 22, hugGap = 5;
    int brightness = -1;
    int hotCorner = -1;
    int cornerLatch = -1;  // the corner that opened something on hover: it does not act again until the pointer has left it with nothing open
    // brightness, one value per monitor (by output name): what the monitors are at, what is waiting for the first reading, which ones must be
    // written, and which one the OSD and the control center show (`brightness` is that one's value)
    std::map<std::string, int> brightVal;
    std::map<std::string, std::pair<int, int>> brightPend;
    std::set<std::string> brightDirtyOuts;
    std::string brightOut, brightJobOut;
    std::string hotCornerOut;  // the output of the hot corner the pointer is in
    bool brightDirty = false;
    std::chrono::steady_clock::time_point brightSetAt;
    int brightFd = -1;
    std::string brightBuf;
    bool brightJobIsGet = false;
    std::vector<AppEntry> apps;
    time_t appsStamp = 0;
    bool onLauncher = false;
    xkb_context* xkbCtx = nullptr;
    xkb_keymap* xkbMap = nullptr;
    xkb_state* xkbState = nullptr;
    // Key repeat: the compositor only tells us rate and delay (wl_keyboard.repeat_info); repeating is our job.
    int kbRate = 25, kbDelay = 600;
    uint32_t repeatKey = 0;
    std::chrono::steady_clock::time_point repeatAt;
    bool onPk = false;
    bool onSw = false;
    bool onCap = false;
    bool onSelector = false;
    std::map<std::string, Color> colors;
    std::map<std::string, Color> rawColors;
    std::string wallpaper;

    Color color(const char* name) const {
        auto it = colors.find(name);
        return it == colors.end() ? Color{} : it->second;
    }
};

void draw(Bar& bar);

constexpr int kSideBarW = 56;
bool sideBar(const App& a) { return a.cfg.barPosition == "left" || a.cfg.barPosition == "right"; }
bool barAtEnd(const App& a) { return a.cfg.barPosition == "bottom" || a.cfg.barPosition == "right"; }
int barThickness(const App& a) { return sideBar(a) ? kSideBarW : kBarHeight; }
int barSpan(const App& a) {
    return barThickness(a) + ((a.cfg.barStyle == "hug" || a.cfg.barStyle == "split") ? a.hugRadius + a.hugGap : (a.cfg.barStyle == "float" || a.cfg.barStyle == "island") ? kBarGap : 0);
}
int sideCardEdge(const App& a) {
    return a.cfg.frame + kSideBarW + ((a.cfg.barStyle == "float" || a.cfg.barStyle == "island") ? kBarGap : 0) + 10;
}
void toBarCoords(const Bar& bar, double& x, double& y) {
    if (sideBar(*bar.app)) x -= bar.contentY;
    else y -= bar.contentY;
}

constexpr int kVolIconW = 26;
constexpr int kQuickBox = 26;
constexpr int kQuickItem = 30;
constexpr double kModuleBox = kQuickItem, kModulePad = 10;
static_assert(kNotchPad == (kBarHeight - kModuleBox) / 2.0 && kIslandPad == kNotchPad, "the side padding of the bar bodies must equal the top padding");
constexpr double kM3Box = kQuickBox, kM3Inset = 2, kM3Disc = kM3Box - 2 * kM3Inset;  // (the M3 pills are as tall as the other designs; the disc inside keeps a 2 px margin, as the workspaces do)
enum QuickAct { kQaPicker = 0, kQaRecord, kQaScreenshot, kQaWallpaper, kQaMic, kQaDark, kQaPerf, kQaOsk, kQaCount };
const char* const kQuickNames[kQaCount] = {"picker", "record", "screenshot", "wallpaper", "mic", "darkmode", "perfprofile", "osk"};
constexpr int kNetW = 26;
constexpr int kNetLabelW = 84;
constexpr int kVolLevelW = 64, kVolPctW = 60;
constexpr int kNetSpeedW = 64;
constexpr int kNetSpeedH = 32;
constexpr int kNotifW = 26;
constexpr int kNotifCountW = 20;

void focusWorkspace(int id);
void updateCursor(App& app);
void toggleSelector(App& app);
void openCapture(App& app, bool record = false);
void closeAllPopups(App& app);
bool anyPopupOpen(const App& app);
void syncCatcher(App& app);
void closeOsd(App& app);
void dockApply(App& app);
void closeLauncher(App& app);
void closeNotifPanel(App& app);
void closeMediaPanel(App& app);
void closeClockPanel(App& app);
void btBarPaint(cairo_t* cr, App& app, const Bar& bar);
void btSidePaint(cairo_t* cr, App& app, Rect r, bool hot);
void drivesBarPaint(cairo_t* cr, App& app, const Bar& bar);
void drivesSidePaint(cairo_t* cr, App& app, Rect r, bool hot);
bool drivesVisible(const App& app);
void privacyBarPaint(cairo_t* cr, App& app, const Bar& bar);
void privacySidePaint(cairo_t* cr, App& app, Rect r);
bool privacyVisible(const App& app);
double privacyBarW(const App& app);
double privacySideH(const App& app);
void privacyTick(App& app, std::chrono::steady_clock::time_point now);
std::chrono::steady_clock::time_point privacyWake(const App& app);
void drivesTick(App& app, std::chrono::steady_clock::time_point now);
std::chrono::steady_clock::time_point drivesWake(const App& app);
void toggleDrivesMenu(App& app, Bar* bar);
void retractDrivesMenu(App& app);
void closeDrivesMenu(App& app);
void drivesMenuMotion(App& app);
void drivesMenuButton(App& app, uint32_t button, bool pressed);
bool drivesIconPressed(const App& app);
void oskDragOther(App& app, double x, double y);
void toggleGame(App& app);
void openGame(App& app);
void retractGame(App& app);
void closeGameNow(App& app);
void gameMotion(App& app);
void gameButton(App& app, uint32_t button, bool pressed);
void drawGame(App& app);
void tickGame(App& app, std::chrono::steady_clock::time_point now);
std::chrono::steady_clock::time_point gameWake(const App& app);
void oskButton(App& app, uint32_t button, bool pressed);
void tickDrivesMenu(App& app, std::chrono::steady_clock::time_point now);
void drawDrivesMenu(App& app);
bool btShown(const App& app);
void btTick(App& app, std::chrono::steady_clock::time_point now);
std::chrono::steady_clock::time_point btWake(const App& app);
void toggleBtMenu(App& app, Bar* bar);
void retractBtMenu(App& app);
void closeBtMenu(App& app);
void btTogglePower(App& app);
void btMenuMotion(App& app);
void islandCardMotion(App& app);
void islandCardButton(App& app, uint32_t button, bool pressed);
void islandHoverCheck(App& app);
void btMenuButton(App& app, uint32_t button, bool pressed);
bool btIconPressed(const App& app);
void tickBtMenu(App& app, std::chrono::steady_clock::time_point now);
void popIconCentre(const App& app, double ax, double ay, double& ix, double& iy);

void openTrayMenu(App& app, int idx);
void closeTrayMenu(App& app);
void closeAudioMenu(App& app);
void toggleAudioMenu(App& app, Bar* bar);
void audioMenuMotion(App& app);
void audioMenuButton(App& app, uint32_t button, bool pressed);
void audioMenuScroll(App& app, int dir);
void audioJobReadable(App& app);
void startAudioDevicesJob(App& app);
void closeDesktopMenu(App& app);
int deskCursorShape(const App& app);
void weatherSvcConfigure(const App& app);
void deskSync(App& app);
void deskVizSync(App& app);
void textWInit();
DeskWidget* deskTextWidget();
int gameCursorShape(const App& app);
void gameNotesGhostSync(App& app);
bool gameNotesFocused(const App& app);
bool gameNotesKey(App& app, uint32_t key);
void gameNotesScroll(App& app, int dir);
void deskVizMotion(App& app);
void deskVizButton(App& app, uint32_t button, bool pressed);
void deskVizToggle(App& app, const std::string& action, std::string& reply);
void deskVizFrame(App& app, std::chrono::steady_clock::time_point now);
void deskVizIdle(App& app);
bool deskVizWanted(const App& app);
void deskVizReopen(App& app);
void deskVizCheckCovered(App& app);
void deskRedrawAll(App& app);
void mediaWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs);
void mediaWApply(App& app);
void mediaWInit(MediaW& X);
void resWInit(ResW& X);
void calWInit(CalW& X);
void clockWInit(ClockW& X);
void userWInit(UserW& X);
void imageWInit(ImageW& X);
void userWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs);
void clockWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs);
void calWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs);
void resWTick(App& app, std::chrono::steady_clock::time_point now, int& timeoutMs);
void deskOpen(App& app, DeskWidget& d);
void deskClose(App& app, DeskWidget& d);
void openDesktopMenu(App& app, Bar* bar);
void drawSelector(App& app);
std::string runtimeDir();
int unixAddr(sockaddr_un& a, const std::string& path);

constexpr int kMusicArt = 26;
constexpr int kMusicTextMax = 200;
constexpr int kMusicTextMin = 40;
constexpr int kMusicBtn = kQuickItem;
constexpr int kMusicGap = 8;
constexpr int kM3TextGap = 7;
constexpr double kM3SidePad = kM3Inset;  // (the media pill: the same air at the ends as above and below the discs)
constexpr int kMusicLineSpacing = -4;
constexpr const char* kArtistFont = "Sans 8";
constexpr const char* kTitleFont = "Sans Bold 10";

using Hit = int;
constexpr Hit HitNone = 0, HitClock = 1, HitVolume = 2, HitPrev = 3, HitPlay = 4, HitNext = 5, HitNetwork = 6, HitNotif = 7, HitMedia = 8, HitSettings = 9, HitAvatar = 10, HitTray = 11, HitLauncher = 12, HitPower = 13, HitWeather = 14, HitDivider = 16, HitKeyboard = 17, HitActiveWindow = 18, HitTools = 15, HitUpdates = 19, HitBluetooth = 24, HitDrives = 26, HitPrivacy = 27, HitIsland = 25, kHitAi = 50, kHitViz = 60, kHitStatus = 20, kHitRes = 40, kHitWs = 100, kHitQuick = 300, kHitTray = 600, kHitDtp = 800;

std::string widgetStyle(const App& app, const std::string& mod);
double roundBtnW(const App& app, const char* mod) { return kQuickBox + (!sideBar(app) && widgetStyle(app, mod) == "m3" ? kM3Box - kQuickBox : 0); }
double roundBtnWide(const App& app, const char* mod) {
    const double h = roundBtnW(app, mod);
    if (sideBar(app)) return h;
    const std::string s = widgetStyle(app, mod);
    return s == "m3soft" ? std::round(h * 1.3) : h;
}
constexpr double kSoftExtra = 8;
double softExtra(const App& app, const char* mod) { return !sideBar(app) && widgetStyle(app, mod) == "m3soft" ? kSoftExtra : 0; }
double notifW(const App& app) {
    const std::string style = widgetStyle(app, "notifications");
    if (style == "dot" || style == "count") return kNotifW;
    if (style == "m3" || style == "m3filled") return roundBtnW(app, "notifications");
    return kNotifW + (app.notifCount <= 0 ? 0 : app.notifCount < 10 ? 10 : 18) + softExtra(app, "notifications");
}

constexpr double kWsGap = 8, kWsDot = 10, kWsActiveW = 28;
constexpr double kNumGap = 6, kNumW = 28, kNumActiveW = 42, kNumH = 26;
static_assert(kMusicArt == kNumH, "cover and workspace boxes share one height");

std::string rawWidgetStyle(const Config& cfg, const std::string& mod) {
    const WidgetStyleSet* set = widgetStyleSet(mod);
    if (!set) return "";
    const bool side = cfg.barPosition == "left" || cfg.barPosition == "right";
    if (!side) {
        if (mod == "clock") return cfg.barClock;
        if (mod == "workspaces") return cfg.workspaceStyle;
        if (mod == "resources") return cfg.resStyle;
    }
    size_t at = 0;
    const std::string& list = side ? cfg.sideWidgetStyles : cfg.widgetStyles;
    while (at < list.size()) {
        size_t end = list.find(',', at);
        if (end == std::string::npos) end = list.size();
        const size_t eq = list.find('=', at);
        if (eq != std::string::npos && eq < end && list.compare(at, eq - at, mod) == 0) {
            const std::string id = list.substr(eq + 1, end - eq - 1);
            if (side ? sideStyleAllowed(mod, id) : validWidgetStyle(mod, id)) return id;
        }
        at = end + 1;
    }
    if (side) {
        const std::string legacy = mod == "workspaces" ? cfg.workspaceStyle : mod == "resources" ? cfg.resStyle : mod == "clock" ? cfg.barClock : "";
        if (!legacy.empty() && sideStyleAllowed(mod, legacy)) return legacy;
        for (const WidgetStyleOption& o : set->options)
            if (sideStyleAllowed(mod, o.id)) return o.id;
    }
    return set->options.front().id;
}

// The group styles (Pills, Separated, Segmented...) put the widgets of a horizontal bar on cells painted by the bar: a widget that is not free
// is drawn in a look without a body of its own, and gets a cell. Free: the island, the modules of cfg.groupFree and any widget whose look is M3.
inline bool groupActive(const Config& c) { return c.groupStyle != "free" && c.barPosition != "left" && c.barPosition != "right"; }
inline bool groupHasCells(const Config& c) { return groupActive(c) && c.groupStyle != "transparent"; }
inline bool listHas(const std::string& list, const std::string& item) {
    size_t at = 0;
    while (at <= list.size()) {
        size_t end = list.find(',', at);
        if (end == std::string::npos) end = list.size();
        if (list.compare(at, end - at, item) == 0 && end - at == item.size()) return true;
        at = end + 1;
    }
    return false;
}
// The looks the group styles give to some widgets (nullptr: the widget keeps a frameless look of its own choice).
inline const char* groupForcedLook(const std::string& mod) {
    return mod == "media" ? "progress" : mod == "workspaces" ? "m3" : mod == "clock" ? "dot" : mod == "notifications" ? "count" : nullptr;
}
inline bool groupFreeWidget(const Config& c, const std::string& mod) {
    return mod == "island" || listHas(c.groupFree, mod) || (!groupForcedLook(mod) && mod != "resources" && mod != "visualizer" && rawWidgetStyle(c, mod) == "m3");
}
inline bool hasGroupCell(const Config& c, const std::string& mod) {
    return groupHasCells(c) && !bardivider::isDivider(mod) && !groupFreeWidget(c, mod);
}
std::string widgetStyle(const Config& cfg, const std::string& mod) {
    const std::string s = rawWidgetStyle(cfg, mod);
    if (!groupActive(cfg) || groupFreeWidget(cfg, mod)) return s;
    if (mod == "visualizer") return s == "reflect" ? "reflect" : "dots";  // (Reflection or Dots)
    if (mod == "notifications") return s == "dot" ? "dot" : "count";  // (a dot, or the count on a small ball)
    if (mod == "media") return s == "plain" ? "plain" : "progress";  // (the ring, or the cover with artist, title and controls)
    if (mod == "resources") return s == "bars" ? "filled" : s;  // (every look but Bars fits on a cell, smaller: see resBox)
    if (mod == "workspaces") return s == "gnome" ? "gnome" : "m3";  // (dots or M3)
    if (const char* forced = groupForcedLook(mod)) return forced;  // (media: a ring that fills with the song; workspaces: M3; clock: dot)
    const WidgetStyleSet* set = widgetStyleSet(mod);
    if (!set) return s;
    bool framed = false;
    for (const WidgetStyleOption& o : set->options)
        if (s == o.id) framed = o.framed;
    if (!framed) return s;
    for (const WidgetStyleOption& o : set->options)
        if (!o.framed) return o.id;
    return s;
}
std::string widgetStyle(const App& app, const std::string& mod) { return widgetStyle(app.cfg, mod); }

constexpr double kFrameGutter = 6;
constexpr double kMediaFrameH = kQuickBox;  // (as tall as the other widgets)
inline bool m3ButtonLook(const std::string& s) { return s == "m3soft" || s == "m3filled"; }
inline bool genericFrame(const std::string& style) { return m3ButtonLook(style) || style == "pill" || style == "outline" || style == "badge" || style == "tonal" || style == "square"; }
constexpr double kAwinMaxW = 280;
constexpr double kMediaTextMax = 120;
double kbdBarW(const App& app) {
    const std::string style = widgetStyle(app, "keyboard");
    if (style == "m3") return kM3Inset + kM3Disc + 6 + app.kbd.m3W + kModulePad;
    if (style == "icon") return kQuickBox;
    if (style == "code") return std::max<double>(kQuickBox, app.kbd.codeW + 16);
    return app.kbd.barW + (genericFrame(style) ? 2 * kFrameGutter : 0);
}
double awinBarW(const App& app) {
    const std::string style = widgetStyle(app, "activewindow");
    if (style == "m3") return app.awin.m3W;
    if (style == "title") return std::min(app.awin.titleW + 12.0, kAwinMaxW);
    if (style == "chip") return std::min(app.awin.clsW + 4 + 18 + 4 + 8.0, kAwinMaxW);
    return app.awin.barW + (genericFrame(style) ? 2 * kFrameGutter : 0);
}
// On a group cell the cover is the size of the M3 disc and sits as far from the left edge of the cell as from its top and bottom.
bool mediaOnCell(const App& app) { return hasGroupCell(app.cfg, "media"); }
double musicCoverPx(const App& app) { const std::string s = widgetStyle(app, "media"); return genericFrame(s) || mediaOnCell(app) ? kM3Disc : s == "compact" ? 23 : kMusicArt; }
std::string musicArtistShown(const App& app) { return app.musicStatus.empty() ? std::string(i18n::tr("Play")) : app.musicArtist; }
std::string musicTitleShown(const App& app) {
    if (!app.musicStatus.empty()) return app.musicTitle;
    return widgetStyle(app, "media") == "compact" ? std::string(i18n::tr("Play something")) : std::string(i18n::tr("Something"));
}
bool musicProgress(const App& app) { return widgetStyle(app, "media") == "progress"; }
// The Progress look: a ring that fills with the song, then "Title • Artist". No controls (middle click plays/pauses, right click skips).
constexpr const char* kProgFont = "Sans Medium 10";
constexpr double kProgRing = 20, kProgGap = 6;
constexpr int kProgLineMax = 260;
std::string musicLineShown(const App& app) {
    const std::string title = musicTitleShown(app), artist = app.musicStatus.empty() ? std::string() : app.musicArtist;
    return artist.empty() ? title : title + " \u2022 " + artist;
}
bool musicHasText(const App& app) { return widgetStyle(app, "media") != "cover"; }
// On a cell the cover sits as far from the left side as the last button's icon does from the right one (the cell pad plus the air of its box).
constexpr double kMediaCellPad = 4;
constexpr double kMusicCellTextGap = 2;  // (on a cell the title was about 16 px from the first button: this brings it to about 10)
double musicPad(const App& app) { return musicProgress(app) ? 0 : genericFrame(widgetStyle(app, "media")) ? kM3Inset : mediaOnCell(app) ? kMediaCellPad : 0; }
double musicPadRight(const App& app) { return genericFrame(widgetStyle(app, "media")) ? kM3Inset : mediaOnCell(app) ? -2 : 0; }  // (on a cell: the icon of the last button already has 2 px more air than the cover)  // (the last button has room of its own on a cell)  // (the cover sits as far from the left edge as from the top and bottom)
double musicTextColW(const App& app) { return widgetStyle(app, "media") == "compact" ? app.musicTitleW : app.musicTextW; }
constexpr double kMediaPillW = 40, kMediaNextX = kMediaPillW + 2;
constexpr int kMediaIcon = 18;
constexpr int kMediaM3Icon = 16;
bool musicM3(const App& app) { return widgetStyle(app, "media") == "m3"; }
double musicCtrlX(const App& app) {
    if (musicM3(app)) return kM3SidePad + kM3Disc + kM3TextGap + app.musicM3TextW + kMusicGap;
    return musicCoverPx(app) + kMusicGap + (musicHasText(app) ? musicTextColW(app) + (mediaOnCell(app) ? kMusicCellTextGap : kMusicGap) : 0);
}
double musicW(const App& app) {
    if (musicProgress(app)) return kProgRing + kProgGap + app.musicLineW;
    if (musicM3(app)) return musicCtrlX(app) + kMediaNextX + kM3Disc + kM3SidePad;
    return musicPad(app) + musicPadRight(app) + musicCtrlX(app) + kMusicBtn * 3 - (kQuickItem - kQuickBox);
}
double volumeW(const App& app) {
    const std::string s = widgetStyle(app, "volume");
    return (s == "level" ? kVolLevelW : s == "percent" ? kVolPctW : kVolIconW) + softExtra(app, "volume");
}
constexpr double kQuickGapSplit = 2, kQuickSplitExtra = 6;
bool quickM3(const App& app) { return !sideBar(app) && widgetStyle(app, "quickactions") == "m3"; }
double quickBase(const App& app) { return quickM3(app) ? kM3Disc : kQuickBox; }  // (the discs of the M3 group sit inside its pill)
double quickPitch(const App& app) {
    return widgetStyle(app, "quickactions") == "split" ? kQuickBox + kQuickGapSplit : quickM3(app) ? kM3Disc + 4 : kQuickItem;
}
double quickPadX(const App& app) { return !sideBar(app) && widgetStyle(app, "quickactions") == "m3" ? kM3Inset : 0; }
constexpr double kM3Cell = kNumH, kM3WsPill = kNumH - 4;
bool m3Ws(const App& app) { return widgetStyle(app, "workspaces") == "m3"; }
bool numberedStyle(const App& app) { return widgetStyle(app, "workspaces") == "default" || m3Ws(app); }
constexpr double kIcoGap = 4, kIcoW = 34, kIcoH = 30, kIcoSize = 20;
bool iconStyleWs(const App& app) { return widgetStyle(app, "workspaces") == "icons"; }
constexpr double kPillCell = 30, kPillH = 30, kPillPad = 3, kPillInner = 24;
bool pillStyle(const App& app) { return widgetStyle(app, "workspaces") == "pill"; }
constexpr double kWsTickW = 5, kWsTickGap = 7, kWsTickActive = 22, kWsTickBusy = 14, kWsTickEmpty = 8, kWsTickHover = 4;
bool barStyleWs(const App& app) { return widgetStyle(app, "workspaces") == "bar"; }
bool ringStyleWs(const App& app) { return widgetStyle(app, "workspaces") == "ring"; }
double wsGap(const App& app) {
    if (pillStyle(app) || m3Ws(app)) return 0;
    if (iconStyleWs(app)) return kIcoGap;
    if (numberedStyle(app) || ringStyleWs(app)) return kNumGap;
    return barStyleWs(app) ? kWsTickGap : kWsGap;
}
double wsWidth(const App& app, int id) {
    if (m3Ws(app)) return kM3Cell;
    if (pillStyle(app)) return kPillCell;
    if (iconStyleWs(app)) return kIcoW;
    if (ringStyleWs(app)) return kNumH;
    if (barStyleWs(app)) return kWsTickW;
    if (numberedStyle(app)) return id == app.workspace ? kNumActiveW : kNumW;
    return id == app.workspace ? kWsActiveW : kWsDot;
}
// Space between the sides of the workspaces' box and what they draw. The pill look has its own. The looks of small bare marks (dots, bars) get,
// the gap they have above and below a mark, so nothing touches the edge of the cell or the widget next to them.
double wsInset(const App& app) {
    if (pillStyle(app)) return kPillPad;
    const double pad = hasGroupCell(app.cfg, "workspaces") ? kGroupPad : 0;
    const double cell = kQuickBox + 2 * pad;
    if (barStyleWs(app)) return (cell - kWsTickBusy) / 2.0 - pad;
    if (!m3Ws(app) && !numberedStyle(app) && !iconStyleWs(app) && !ringStyleWs(app)) return (cell - kWsDot) / 2.0 - pad;
    return 0;
}
int wsShown(const App& app) { return app.wsCount > 0 ? std::clamp(app.wsCount, 1, 20) : app.cfg.workspaces; }
double wsTotalW(const App& app) {
    double w = 0;
    for (int i = 1; i <= wsShown(app); i++) w += wsWidth(app, i) + (i > 1 ? wsGap(app) : 0);
    return w + 2 * wsInset(app);
}
double wsXOf(const Bar& bar, int id) {
    const App& app = *bar.app;
    auto it = bar.slot.find("workspaces");
    double x = (it == bar.slot.end() ? 0 : it->second) + wsInset(app);
    for (int i = 1; i < id; i++) x += wsWidth(app, i) + wsGap(app);
    return x;
}
