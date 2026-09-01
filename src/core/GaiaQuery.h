#pragma once
//
// GaiaQuery — asynchronous cone searches against the ESA Gaia DR3 archive
// (synchronous TAP endpoint, ADQL, CSV; no authentication). Only sky
// coordinates ever leave the machine.
//
// DR3 positions are catalogued at epoch 2016.0; proper motions of bright
// nearby stars reach arcseconds per decade, so parseCsv can propagate each
// source to the frame's observation epoch (DATE-OBS) before it is matched
// or drawn.
//
#include <QObject>
#include <QString>
#include <vector>

class QNetworkAccessManager;

namespace astro {

struct GaiaSource {
    qlonglong sourceId = 0;
    double raDeg = 0, decDeg = 0;      // epoch-propagated when requested
    double gMag = 0, bpRp = 0;         // NaN when absent
    double parallaxMas = 0;            // NaN when absent
    double pmraMasYr = 0, pmdecMasYr = 0;  // mu_alpha* (x cos delta), mu_delta
};

class GaiaClient : public QObject {
    Q_OBJECT
public:
    explicit GaiaClient(QObject* parent = nullptr);

    // Cone search around (raDeg, decDeg) with the given radius, at most topN
    // sources ordered by G magnitude. epochYear > 0 propagates positions
    // from 2016.0 to that epoch. magLimit > 0 pre-filters to G < magLimit —
    // essential for wide cones in the galactic plane, where ORDER BY over a
    // million-source circle times out the sync endpoint. One finished() per
    // call; overlapping calls are queued by the network layer.
    void coneSearch(double raDeg, double decDeg, double radiusDeg, int topN,
                    double epochYear = 0.0, double magLimit = 0.0);

    // Wide-field variant: walk a BRIGHT-FIRST magnitude ladder (G < 13, 15,
    // 17) and stop as soon as topN sources come back. A dense galactic-plane
    // cone answers at the bright rung in seconds, where its unfiltered
    // ORDER BY times out the sync endpoint; a sparse field descends the
    // ladder over cheap queries. If a fainter rung fails after a shallower
    // one succeeded, the shallower result is delivered rather than an error.
    // One finished() per call.
    void coneSearchBrightest(double raDeg, double decDeg, double radiusDeg,
                             int topN, double epochYear = 0.0);

    // Pure pieces, exposed for tests (no network involved).
    static QString adqlCone(double raDeg, double decDeg, double radiusDeg,
                            int topN, double magLimit = 0.0);
    static std::vector<GaiaSource> parseCsv(const QByteArray& csv,
                                            double epochYear);
    // "2025-07-30T22:14:33" (or a plain year) -> fractional year; 0 on failure.
    static double epochYearFromIso(const QString& dateObs);

signals:
    void finished(bool ok, const std::vector<GaiaSource>& sources,
                  const QString& error);

private:
    void ladderStep(double raDeg, double decDeg, double radiusDeg, int topN,
                    double epochYear, int rung, std::vector<GaiaSource> prev);

    QNetworkAccessManager* m_net = nullptr;
};

} // namespace astro
