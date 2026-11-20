// Tools > Keybinds, the data: the table of binds (hyprland/binds.default.tsv, or the user's own copy ~/.config/hypr/keybinds.tsv) that
// hyprland/binds.lua registers, how its keys are named and shown, and the submap that lets a new shortcut be captured.
// Included by src/tools.cpp (inside its anonymous namespace), before the page.

struct Kb {
    std::string cat, keys, kind, arg, desc, flags, rng;
    bool has(char c) const { return flags.find(c) != std::string::npos; }
    void set(char c, bool on) {
        const size_t p = flags.find(c);
        if (on && p == std::string::npos) flags += c;
        else if (!on && p != std::string::npos) flags.erase(p, 1);
    }
};

struct KbKind {
    const char* id;
    const char* label;
    const char* icon;
    const char* argLabel;                 // null: the action takes no argument
    bool textArg;                         // a text field for the argument (else only the picks)
    bool listed;                          // offered in the editor (the rest are only read from the table)
    std::vector<std::pair<const char*, const char*>> picks;  // (argument, label)
};

const std::vector<KbKind>& kbKinds() {
    static const std::vector<KbKind> k = {
        {"exec", "Run a command", "code", "Command", true, true, {}},
        {"shell", "Shell action", "settings", "Action", true, true,
         {{"settings", "Settings"}, {"tools", "Tools"}, {"launcher toggle", "Launcher"}, {"overview", "Overview"},
          {"selector toggle", "Wallpaper"}, {"media toggle", "Media"},
          {"power toggle", "Power menu"}, {"game toggle", "Game overlay"}, {"osk toggle", "Keyboard"}, {"screenshot region", "Screenshot"},
          {"screenshot lens", "Lens"}, {"quick 1", "Record"}, {"brightness up", "Brightness +"}, {"brightness down", "Brightness -"}}},
        {"close", "Close window", "close-circle", nullptr, false, true, {}},
        {"float", "Float / tile", "layers", nullptr, false, true, {}},
        {"fullscreen", "Fullscreen", "full-screen", "Mode", false, true, {{"fullscreen", "Fullscreen"}, {"maximized", "Maximize"}}},
        {"fullscreen_state", "Fullscreen spoof", "full-screen", "Internal and client state", true, false, {}},
        {"focus_dir", "Focus a window", "cursor", "Direction", false, true, {{"l", "Left"}, {"r", "Right"}, {"u", "Up"}, {"d", "Down"}}},
        {"move_dir", "Move the window", "cursor", "Direction", false, true, {{"l", "Left"}, {"r", "Right"}, {"u", "Up"}, {"d", "Down"}}},
        {"workspace", "Go to workspace", "home", "Workspace (3, r+1, r-1, m+1...)", true, true, {}},
        {"movews", "Send window to workspace", "arrow-right-up", "Workspace", true, true, {}},
        {"movews_silent", "Send window, stay here", "arrow-right-up", "Workspace", true, true, {}},
        {"movews_plain", "Send window to workspace", "arrow-right-up", "Workspace", true, false, {}},
        {"special", "Toggle scratchpad", "layers-minimalistic", "Name", true, true, {}},
        {"movespecial", "Send to scratchpad", "layers-minimalistic", "Name", true, true, {}},
        {"drag", "Drag the window", "mouse", nullptr, false, true, {}},
        {"resize", "Resize the window", "maximize-square-2", nullptr, false, true, {}},
        {"layout", "Layout command", "widget-2", "Command (splitratio +0.1)", true, true, {}},
        {"lua", "Lua", "code", "Lua expression", true, false, {}},
    };
    return k;
}
const KbKind* kbKind(const std::string& id) {
    for (const KbKind& k : kbKinds()) if (id == k.id) return &k;
    return nullptr;
}

// ---- the table on disk ----------------------------------------------------------------------------------------------------------------
std::string kbUserPath() { return envDir("XDG_CONFIG_HOME", homeDir() + "/.config") + "/hypr/keybinds.tsv"; }
std::string kbDefaultPath() { return projectDir() + "/hyprland/binds.default.tsv"; }

std::string kbEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}
std::string kbUnescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; o += s[i] == 't' ? '\t' : s[i] == 'n' ? '\n' : s[i]; }
        else o += s[i];
    }
    return o;
}

std::vector<Kb> kbParse(const std::string& text) {
    std::vector<Kb> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t i = 0;
        while (true) {
            const size_t t = line.find('\t', i);
            f.push_back(kbUnescape(line.substr(i, t == std::string::npos ? std::string::npos : t - i)));
            if (t == std::string::npos) break;
            i = t + 1;
        }
        if (f.size() < 3 || f[1].empty()) continue;
        f.resize(7);
        out.push_back({f[0], f[1], f[2], f[3], f[4], f[5], f[6]});
    }
    return out;
}
bool kbHasUserFile() { return access(kbUserPath().c_str(), R_OK) == 0; }
std::vector<Kb> kbLoad() {
    std::string t = kbHasUserFile() ? readFile(kbUserPath()) : std::string();
    if (t.empty()) t = readFile(kbDefaultPath());
    return kbParse(t);
}

bool kbSave(const std::vector<Kb>& rows) {
    std::string s =
        "# end4pC keybinds (edited in Tools > Keybinds). One bind per line, fields separated by a tab:\n"
        "#   category  keys  action  argument  name  flags  repeat\n"
        "# flags: l works when locked, r repeats when held, e on key release, m mouse bind, n keeps the key for the app, h alternate, d disabled\n"
        "# Delete this file to go back to the binds that ship with end4pC.\n";
    for (const Kb& b : rows)
        s += kbEscape(b.cat) + "\t" + kbEscape(b.keys) + "\t" + kbEscape(b.kind) + "\t" + kbEscape(b.arg) + "\t" + kbEscape(b.desc) + "\t" + b.flags + "\t" + b.rng + "\n";
    const std::string path = kbUserPath(), tmp = path + ".tmp";
    mkdir((path.substr(0, path.rfind('/'))).c_str(), 0755);
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << s;
        if (!f.good()) return false;
    }
    return rename(tmp.c_str(), path.c_str()) == 0;
}
void kbApply() { (void)!std::system("hyprctl reload >/dev/null 2>&1 &"); }

// ---- the names of the keys ------------------------------------------------------------------------------------------------------------
std::string kbUpper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string kbLower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

std::vector<std::string> kbSplitKeys(const std::string& keys) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= keys.size()) {
        size_t e = keys.find(" + ", i);
        if (e == std::string::npos) e = keys.size();
        std::string t = keys.substr(i, e - i);
        if (!t.empty()) out.push_back(t);
        i = e + 3;
    }
    return out;
}
std::string kbModName(const std::string& tok) {
    const std::string u = kbUpper(tok);
    if (u == "SUPER" || u == "WIN" || u == "LOGO" || u == "MOD4") return "SUPER";
    if (u == "CTRL" || u == "CONTROL") return "CTRL";
    if (u == "SHIFT") return "SHIFT";
    if (u == "ALT" || u == "MOD1") return "ALT";
    return "";
}
// The same keys in a fixed order and case, to find two binds on one shortcut.
std::string kbNorm(const std::string& keys) {
    std::string mods, key;
    for (const std::string& t : kbSplitKeys(keys)) {
        const std::string m = kbModName(t);
        if (m.empty()) key = kbLower(t);
        else mods += m + "+";
    }
    std::string o;
    for (const char* m : {"SUPER", "CTRL", "SHIFT", "ALT"}) if (mods.find(std::string(m) + "+") != std::string::npos) o += std::string(m) + "+";
    return o + key;
}

// What a key says on its cap.
std::string kbCap(const std::string& tok, const std::string& rng) {
    static const std::map<std::string, std::string> names = {
        {"super", "Super"}, {"ctrl", "Ctrl"}, {"control", "Ctrl"}, {"shift", "Shift"}, {"alt", "Alt"}, {"super_l", "Super"}, {"super_r", "Super"},
        {"slash", "/"}, {"return", "Enter"}, {"escape", "Esc"}, {"left", "\xE2\x86\x90"}, {"right", "\xE2\x86\x92"}, {"up", "\xE2\x86\x91"},
        {"down", "\xE2\x86\x93"}, {"equal", "="}, {"minus", "-"}, {"plus", "+"}, {"semicolon", ";"}, {"apostrophe", "'"}, {"comma", ","},
        {"period", "."}, {"bracketleft", "["}, {"bracketright", "]"}, {"backslash", "\\"}, {"grave", "`"}, {"space", "Space"}, {"tab", "Tab"},
        {"page_down", "PgDn"}, {"page_up", "PgUp"}, {"next", "PgDn"}, {"prior", "PgUp"}, {"print", "PrtSc"}, {"delete", "Del"}, {"insert", "Ins"},
        {"backspace", "Back"}, {"home", "Home"}, {"end", "End"}, {"mouse:272", "Left click"}, {"mouse:273", "Right click"}, {"mouse:274", "Middle click"},
        {"mouse:275", "Mouse back"}, {"mouse:276", "Mouse forward"}, {"mouse_up", "Scroll \xE2\x86\x91"}, {"mouse_down", "Scroll \xE2\x86\x93"},
        {"xf86audioraisevolume", "Vol +"}, {"xf86audiolowervolume", "Vol -"}, {"xf86audiomute", "Mute"}, {"xf86audiomicmute", "Mic mute"},
        {"xf86audionext", "Next"}, {"xf86audioprev", "Prev"}, {"xf86audioplay", "Play"}, {"xf86audiopause", "Pause"},
        {"xf86monbrightnessup", "Bright +"}, {"xf86monbrightnessdown", "Bright -"}, {"xf86audiostop", "Stop"},
    };
    if (tok == "#") return rng == "pad" ? "Keypad 1-0" : "1-0";
    const std::string low = kbLower(tok);
    if (auto it = names.find(low); it != names.end()) return it->second;
    if (low.rfind("code:", 0) == 0) {
        const int c = std::atoi(low.c_str() + 5);
        if (c >= 10 && c <= 19) return std::string(1, "1234567890"[c - 10]);
        if (c == 87) return "Keypad 1";
        if (c == 88) return "Keypad 2";
        if (c == 89) return "Keypad 3";
        if (c == 83) return "Keypad 4";
        if (c == 84) return "Keypad 5";
        if (c == 85) return "Keypad 6";
        if (c == 79) return "Keypad 7";
        if (c == 80) return "Keypad 8";
        if (c == 81) return "Keypad 9";
        if (c == 90) return "Keypad 0";
        if (c == 86) return "Keypad +";
        if (c == 82) return "Keypad -";
        return tok;
    }
    if (low.rfind("xf86", 0) == 0) {  // XF86AudioRaiseVolume -> "Audio Raise Volume"
        std::string o;
        for (size_t i = 4; i < tok.size(); i++) {
            if (i > 4 && std::isupper(static_cast<unsigned char>(tok[i])) && !std::isupper(static_cast<unsigned char>(tok[i - 1]))) o += ' ';
            o += tok[i];
        }
        return o;
    }
    if (tok.size() == 1) return kbUpper(tok);
    return tok;
}

// ---- capturing a shortcut -------------------------------------------------------------------------------------------------------------
bool g_kbGrabbed = false;
// The `capture` submap has no binds (hyprland/binds.lua), so the compositor lets every key through to this window while it is active.
void kbGrab(bool on) {
    if (on == g_kbGrabbed) return;
    g_kbGrabbed = on;
    trRunSync({"hyprctl", "eval", on ? "hl.dispatch(hl.dsp.submap(\"capture\"))" : "hl.dispatch(hl.dsp.submap(\"reset\"))"}, "", nullptr, 1500);
}

bool kbIsModifierSym(xkb_keysym_t s) {
    return s == XKB_KEY_Shift_L || s == XKB_KEY_Shift_R || s == XKB_KEY_Control_L || s == XKB_KEY_Control_R || s == XKB_KEY_Alt_L || s == XKB_KEY_Alt_R ||
           s == XKB_KEY_Super_L || s == XKB_KEY_Super_R || s == XKB_KEY_Meta_L || s == XKB_KEY_Meta_R || s == XKB_KEY_Hyper_L || s == XKB_KEY_Hyper_R ||
           s == XKB_KEY_ISO_Level3_Shift || s == XKB_KEY_Caps_Lock || s == XKB_KEY_Num_Lock || s == XKB_KEY_Mode_switch;
}
// The modifiers held, as the table writes them.
std::string kbModsText(bool logo, bool ctrl, bool shift, bool alt) {
    std::string s;
    if (logo) s += "SUPER + ";
    if (ctrl) s += "CTRL + ";
    if (shift) s += "SHIFT + ";
    if (alt) s += "ALT + ";
    return s;
}
// The key name Hyprland takes for what was pressed: the symbol with no modifier (Shift+1 is the key "1"); the keypad by keycode.
std::string kbKeyName(const std::string& base, uint32_t code) {
    if (base.empty() || base.rfind("KP_", 0) == 0) return "code:" + std::to_string(code + 8);
    return base.size() == 1 ? kbUpper(base) : base;
}
