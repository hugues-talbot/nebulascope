#pragma once
//
// SexCatalog — parser for SExtractor ASCII_HEAD catalogs. The header's
// "#  N NAME comment" lines give the column layout; every following row is
// whitespace-separated numbers. Column access is by SExtractor name
// ("X_IMAGE", "MAG_AUTO", ...), so the importer tolerates any column order
// and any default.param selection.
//
#include <QString>
#include <QHash>
#include <vector>

namespace astro {

class SexCatalog {
public:
    // Parse a catalog file; on failure returns an empty catalog and sets *err.
    static SexCatalog parse(const QString& path, QString* err = nullptr);

    bool isValid() const { return !m_rows.empty() && !m_cols.isEmpty(); }
    int  rowCount() const { return int(m_rows.size()); }
    bool has(const QString& col) const { return m_cols.contains(col); }
    // Value of column `col` in row `row`; `def` when the column is absent.
    double value(int row, const QString& col, double def = 0.0) const {
        const int c = m_cols.value(col, -1);
        if (c < 0 || row < 0 || row >= rowCount() || c >= int(m_rows[row].size())) return def;
        return m_rows[std::size_t(row)][std::size_t(c)];
    }

private:
    QHash<QString, int> m_cols;                 // name -> 0-based column index
    std::vector<std::vector<double>> m_rows;
};

} // namespace astro
