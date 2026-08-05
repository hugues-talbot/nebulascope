#pragma once
//
// ImageHeader — unified metadata for any supported container.
//
//  * `cards`      : FITS-style keyword/value/comment triples. FITS files fill
//                   these directly; XISF carries embedded FITS keywords too, so
//                   both backends populate the same list and your header
//                   inspector / WCS code is shared.
//  * `properties` : richer, typed XISF properties (camera, exposure, history,
//                   colour-management, ...). Empty for plain FITS.
//
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <vector>

namespace astro {

struct HeaderCard {
    QString key;
    QString value;
    QString comment;
};

// XISF DisplayFunction: the screen stretch (STF) the producing application —
// typically PixInsight — had active when the file was saved. Components are
// midtones / shadows / highlights per channel (RGB/K order, 4th = CIE L,
// unused here), in ABSOLUTE sample values. Purely display metadata: the
// pixels stay linear.
struct DisplayFunction {
    bool   valid = false;
    double m[4] = { 0.5, 0.5, 0.5, 0.5 };
    double s[4] = { 0, 0, 0, 0 };
    double h[4] = { 1, 1, 1, 1 };
};

struct ImageHeader {
    std::vector<HeaderCard> cards;
    QVariantMap             properties;
    DisplayFunction         displayFn;   // saved STF (XISF only; .valid gates)

    // Orientation info for the Info panel (populated by the readers).
    QString     container;    // "FITS" / "XISF"
    QString     nativeType;   // on-disk sample type, e.g. "16-bit unsigned int"
    QStringList structure;    // one line per HDU (FITS) or image/block (XISF)

    QString valueOf(const QString& key, const QString& fallback = {}) const {
        for (const auto& c : cards)
            if (c.key.compare(key, Qt::CaseInsensitive) == 0) return c.value;
        return fallback;
    }
};

} // namespace astro
