#include "core/SexCatalog.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace astro {

SexCatalog SexCatalog::parse(const QString& path, QString* err) {
    SexCatalog cat;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = QStringLiteral("Could not open %1").arg(path);
        return cat;
    }

    static const QRegularExpression header(
        QStringLiteral("^#\\s*(\\d+)\\s+([A-Za-z0-9_]+)"));
    static const QRegularExpression ws(QStringLiteral("\\s+"));

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        if (trimmed.startsWith(QLatin1Char('#'))) {
            const auto m = header.match(trimmed);
            if (m.hasMatch())
                cat.m_cols.insert(m.captured(2), m.captured(1).toInt() - 1);  // 1-based header
            continue;
        }

        const QStringList parts = trimmed.split(ws, Qt::SkipEmptyParts);
        std::vector<double> row;
        row.reserve(parts.size());
        bool ok = true;
        for (const QString& p : parts) {
            bool o = false;
            row.push_back(p.toDouble(&o));
            ok = ok && o;
        }
        if (ok && !row.empty()) cat.m_rows.push_back(std::move(row));
    }

    if (cat.m_cols.isEmpty() && err) *err = QStringLiteral("No ASCII_HEAD column header found (# lines)");
    else if (cat.m_rows.empty() && err) *err = QStringLiteral("No data rows in catalog");
    return cat;
}

} // namespace astro
