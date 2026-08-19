#pragma once
//
// InfoPanel — orientation for the loaded image:
//   * Structure  : container, dimensions, channels, on-disk type, HDU list
//   * Statistics : per-channel min/max/median/MAD + current display clip range
//   * Header     : searchable table of FITS/XISF keyword cards
//
// Statistics and the display range update live as the StretchModel changes.
//
#include <QWidget>
#include <vector>
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "core/ImageStats.h"
#include "render/StretchModel.h"

class QLabel;
class QLineEdit;
class QTableWidget;

namespace astro {

class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(StretchModel* model, QWidget* parent = nullptr);

    void setData(const ImageData* img, const ImageHeader* hdr,
                 const std::vector<ChannelStats>& stats);

    // The panel never dictates its dock's width: content (stats table, long
    // unbreakable header values) may be wider than the panel and simply
    // clips. Without this, refilling for a new image could widen the tab
    // area it shares with the image list and shift the view grid.
    QSize minimumSizeHint() const override { return QSize(160, 120); }

private slots:
    void refresh();          // rebuild structure + stats (cheap)
    void applyFilter(const QString& text);

private:
    void rebuildTable();

    StretchModel* m_model;
    const ImageData* m_img = nullptr;
    const ImageHeader* m_hdr = nullptr;
    std::vector<ChannelStats> m_statVals;

    QLabel*       m_structure = nullptr;
    QLabel*       m_stats = nullptr;
    QLineEdit*    m_filter = nullptr;
    QTableWidget* m_table = nullptr;
};

} // namespace astro
