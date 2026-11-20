#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace bardivider {

enum class Style { Line, Dot, Space };
struct Options { Style style; int margin; };
inline constexpr int maxMargin = 24;

inline std::optional<Style> parseStyle(std::string_view name) {
    if (name == "line") return Style::Line;
    if (name == "dot") return Style::Dot;
    if (name == "space") return Style::Space;
    return std::nullopt;
}

inline const char* styleName(Style style) {
    if (style == Style::Dot) return "dot";
    if (style == Style::Space) return "space";
    return "line";
}

// Store an override in its layout entry so it follows the divider when dragged.
// Plain "divider" entries continue to use the saved global defaults.
inline std::optional<Options> parse(std::string_view entry) {
    constexpr std::string_view prefix = "divider:";
    if (!entry.starts_with(prefix)) return std::nullopt;
    entry.remove_prefix(prefix.size());
    const auto separator = entry.find(':');
    if (separator == entry.npos) return std::nullopt;
    const auto style = parseStyle(entry.substr(0, separator));
    if (!style) return std::nullopt;
    entry.remove_prefix(separator + 1);
    if (entry.empty()) return std::nullopt;
    int margin = 0;
    const auto [end, error] = std::from_chars(entry.data(), entry.data() + entry.size(), margin);
    if (error != std::errc{} || end != entry.data() + entry.size() || margin < 0 || margin > maxMargin) return std::nullopt;
    return Options{*style, margin};
}

inline bool isDivider(std::string_view entry) { return entry == "divider" || parse(entry).has_value(); }

inline std::string encode(Options options) {
    return std::string("divider:") + styleName(options.style) + ':' + std::to_string(options.margin);
}

inline int extent(Style style) {
    if (style == Style::Space) return 8;
    return style == Style::Dot ? 6 : 2;
}

}  // namespace bardivider
