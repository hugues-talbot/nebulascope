#include "ui/InfoPanel.h"
#include "core/Stretch.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontDatabase>

namespace astro {

static const char* CH_NAME[3] = { "R", "G", "B" };

static QString fmt(double v) {
    return QString::number(v, 'g', 6);
}

InfoPanel::InfoPanel(StretchModel* model, QWidget* parent)
    : QWidget(parent), m_model(model) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(10);

    auto sectionTitle = [&](const QString& t) {
        auto* l = new QLabel(t);
        l->setStyleSheet("color:#5b6876; font-size:10px; letter-spacing:1.5px; font-weight:600;");
        return l;
    };

    root->addWidget(sectionTitle("STRUCTURE"));
    m_structure = new QLabel("—");
    m_structure->setTextFormat(Qt::RichText);
    m_structure->setWordWrap(true);
    m_structure->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_structure);

    root->addWidget(sectionTitle("DATA RANGE"));
    m_stats = new QLabel("—");
    m_stats->setTextFormat(Qt::RichText);
    m_stats->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_stats);

    root->addWidget(sectionTitle("HEADER"));
    m_filter = new QLineEdit();
    m_filter->setPlaceholderText("Filter keywords…");
    m_filter->setClearButtonEnabled(true);
    root->addWidget(m_filter);

    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({ "Keyword", "Value", "Comment" });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->resizeSection(0, 90);
    m_table->horizontalHeader()->resizeSection(1, 120);
    m_table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(m_table, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &InfoPanel::applyFilter);
    connect(m_model, &StretchModel::changed, this, &InfoPanel::refresh);
}

void InfoPanel::setData(const ImageData* img, const ImageHeader* hdr,
                        const std::vector<ChannelStats>& stats) {
    m_img = img;
    m_hdr = hdr;
    m_statVals = stats;
    rebuildTable();
    refresh();
}

void InfoPanel::rebuildTable() {
    m_table->setRowCount(0);
    if (!m_hdr) return;
    m_table->setRowCount(int(m_hdr->cards.size()) + int(m_hdr->properties.size()));
    int row = 0;
    for (const auto& c : m_hdr->cards) {
        m_table->setItem(row, 0, new QTableWidgetItem(c.key));
        m_table->setItem(row, 1, new QTableWidgetItem(c.value));
        m_table->setItem(row, 2, new QTableWidgetItem(c.comment));
        ++row;
    }
    // XISF typed properties (e.g. PCL:AstrometricSolution:*) share the same
    // filterable table, marked in the comment column.
    for (auto it = m_hdr->properties.constBegin(); it != m_hdr->properties.constEnd(); ++it) {
        auto* keyItem = new QTableWidgetItem(it.key());
        keyItem->setForeground(QColor("#8fa3b8"));
        m_table->setItem(row, 0, keyItem);
        m_table->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("XISF property")));
        ++row;
    }
    applyFilter(m_filter->text());
}

void InfoPanel::applyFilter(const QString& text) {
    const QString t = text.trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool match = t.isEmpty();
        if (!match) {
            for (int c = 0; c < 3 && !match; ++c) {
                auto* it = m_table->item(r, c);
                if (it && it->text().contains(t, Qt::CaseInsensitive)) match = true;
            }
        }
        m_table->setRowHidden(r, !match);
    }
}

void InfoPanel::refresh() {
    if (!m_img || !m_img->isValid()) {
        m_structure->setText("<i>No image loaded</i>");
        m_stats->setText("—");
        return;
    }

    // ---- structure ----
    QString s;
    const QString container = m_hdr ? m_hdr->container : QString();
    const QString ntype = m_hdr ? m_hdr->nativeType : QString();
    s += QStringLiteral("<b>%1</b> &nbsp; %2 × %3 &nbsp; %4 channel%5<br>")
            .arg(container.isEmpty() ? "Image" : container)
            .arg(m_img->width()).arg(m_img->height())
            .arg(m_img->channels()).arg(m_img->channels() == 1 ? "" : "s");
    s += QStringLiteral("<span style='color:#7e8b98'>On disk:</span> %1 &nbsp; "
                        "<span style='color:#7e8b98'>In memory:</span> 32-bit float<br>")
            .arg(ntype.isEmpty() ? "—" : ntype);
    if (m_hdr && !m_hdr->structure.isEmpty()) {
        s += "<span style='color:#7e8b98'>HDUs:</span><br>";
        for (const QString& line : m_hdr->structure)
            s += "&nbsp;&nbsp;" + line.toHtmlEscaped() + "<br>";
    }
    m_structure->setText(s);

    // ---- per-channel statistics + display clip range ----
    QString t = "<table cellspacing='6' style='color:#c8d2dc'>";
    t += "<tr style='color:#5b6876'><td></td><td>min</td><td>max</td>"
         "<td>median</td><td>&sigma;(MAD)</td></tr>";
    const int n = int(m_statVals.size());
    for (int c = 0; c < n; ++c) {
        const QString name = (m_img->channels() >= 3 && c < 3) ? CH_NAME[c] : QString("L");
        const QColor col = (m_img->channels() >= 3 && c < 3)
                               ? QColor(c == 0 ? "#ff6b6b" : c == 1 ? "#3fd07f" : "#5aa9ff")
                               : QColor("#cdd7e1");
        t += QStringLiteral("<tr><td style='color:%1'><b>%2</b></td>"
                            "<td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                 .arg(col.name(), name,
                      fmt(m_statVals[c].min), fmt(m_statVals[c].max),
                      fmt(m_statVals[c].median), fmt(m_statVals[c].mad));
    }
    t += "</table>";

    // display clip range in raw units, from the model
    const StretchFn fn = m_model->fn();
    const char* fnName = fn == StretchFn::Linear ? "Linear" : fn == StretchFn::Log ? "Log"
                       : fn == StretchFn::Asinh ? "Asinh" : "GHS";
    t += QStringLiteral("<br><span style='color:#7e8b98'>Display (%1):</span><br>").arg(fnName);
    const int nc = m_img->channels();
    for (int c = 0; c < nc && c < 3; ++c) {
        const double lo = m_model->lo(c), hi = m_model->hi(c);
        const ChannelStretch cs = m_model->channel(c);
        const double blackRaw = lo + cs.black * (hi - lo);
        const double whiteRaw = lo + cs.white * (hi - lo);
        const QString name = nc >= 3 ? CH_NAME[c] : "L";
        t += QStringLiteral("&nbsp;%1 black %2 &nbsp; white %3<br>")
                 .arg(name, fmt(blackRaw), fmt(whiteRaw));
    }
    m_stats->setText(t);
}

} // namespace astro
