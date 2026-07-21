#pragma once
//
// AppInfo.h — USER-MAINTAINED application metadata.
//
// ============================================================================
//  This file belongs to the project owner. It is read by the About dialog
//  (MainWindow::showAbout) and is NOT to be regenerated or rewritten by
//  code-assistant sessions. Edit freely: version, copyright, credits, links.
// ============================================================================
//
namespace appinfo {

inline constexpr const char* kVersion   = "0.9";
inline constexpr const char* kCopyright = "© 2026 Hugues Talbot";

// Extra HTML appended to the About dialog body. Add acknowledgements,
// license notes, a homepage link, etc. Empty string is fine.
inline constexpr const char* kAboutExtraHtml =
    "";

} // namespace appinfo
