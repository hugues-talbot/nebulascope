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
#include <QVariantMap>
#include <vector>

namespace astro {

struct HeaderCard {
    QString key;
    QString value;
    QString comment;
};

struct ImageHeader {
    std::vector<HeaderCard> cards;
    QVariantMap             properties;

    QString valueOf(const QString& key, const QString& fallback = {}) const {
        for (const auto& c : cards)
            if (c.key.compare(key, Qt::CaseInsensitive) == 0) return c.value;
        return fallback;
    }
};

} // namespace astro
