#include "core/GaiaQuery.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>
#include <limits>

namespace astro {

namespace {
const char* kTapSync = "https://gea.esac.esa.int/tap-server/tap/sync";

double fieldOrNan(const QStringList& row, int ix) {
    if (ix < 0 || ix >= row.size() || row[ix].trimmed().isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    bool ok = false;
    const double v = row[ix].trimmed().toDouble(&ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}
} // namespace

GaiaClient::GaiaClient(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

QString GaiaClient::adqlCone(double raDeg, double decDeg, double radiusDeg,
                             int topN, double magLimit) {
    return QStringLiteral(
        "SELECT TOP %1 source_id,ra,dec,phot_g_mean_mag,bp_rp,parallax,"
        "pmra,pmdec FROM gaiadr3.gaia_source WHERE 1=CONTAINS("
        "POINT('ICRS',ra,dec),CIRCLE('ICRS',%2,%3,%4))%5 "
        "ORDER BY phot_g_mean_mag ASC")
        .arg(topN)
        .arg(raDeg, 0, 'f', 8)
        .arg(decDeg, 0, 'f', 8)
        .arg(radiusDeg, 0, 'f', 8)
        .arg(magLimit > 0
                 ? QStringLiteral(" AND phot_g_mean_mag<%1").arg(magLimit, 0, 'f', 1)
                 : QString());
}

double GaiaClient::epochYearFromIso(const QString& dateObs) {
    const QString s = dateObs.trimmed().remove(QLatin1Char('\''));
    if (s.isEmpty()) return 0.0;
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(s.left(10), QStringLiteral("yyyy-MM-dd"));
    if (dt.isValid()) {
        // DATE-OBS is UTC by convention; reinterpret the parsed wall time as
        // UTC so the machine's own timezone cannot shift the epoch.
        dt = QDateTime(dt.date(), dt.time(), QTimeZone::UTC);
        const int y = dt.date().year();
        const QDateTime y0(QDate(y, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const QDateTime y1(QDate(y + 1, 1, 1), QTime(0, 0), QTimeZone::UTC);
        return y + double(y0.secsTo(dt)) / double(y0.secsTo(y1));
    }
    bool ok = false;
    const double y = s.toDouble(&ok);
    return ok && y > 1800.0 && y < 2200.0 ? y : 0.0;
}

std::vector<GaiaSource> GaiaClient::parseCsv(const QByteArray& csv,
                                             double epochYear) {
    std::vector<GaiaSource> out;
    const QList<QByteArray> lines = csv.split('\n');
    if (lines.isEmpty()) return out;
    // Header row: map column names to indices (order not assumed).
    const QStringList head = QString::fromUtf8(lines[0]).trimmed().split(',');
    auto ix = [&](const char* name) { return head.indexOf(QLatin1String(name)); };
    const int iId = ix("source_id"), iRa = ix("ra"), iDec = ix("dec");
    const int iG = ix("phot_g_mean_mag"), iBr = ix("bp_rp");
    const int iPlx = ix("parallax"), iPmra = ix("pmra"), iPmdec = ix("pmdec");
    if (iId < 0 || iRa < 0 || iDec < 0) return out;
    const double dt = epochYear > 0.0 ? epochYear - 2016.0 : 0.0;
    for (int li = 1; li < lines.size(); ++li) {
        const QString line = QString::fromUtf8(lines[li]).trimmed();
        if (line.isEmpty()) continue;
        const QStringList row = line.split(',');
        GaiaSource s;
        bool okId = false;
        s.sourceId = row.value(iId).toLongLong(&okId);
        s.raDeg = fieldOrNan(row, iRa);
        s.decDeg = fieldOrNan(row, iDec);
        if (!okId || !std::isfinite(s.raDeg) || !std::isfinite(s.decDeg))
            continue;
        s.gMag = fieldOrNan(row, iG);
        s.bpRp = fieldOrNan(row, iBr);
        s.parallaxMas = fieldOrNan(row, iPlx);
        s.pmraMasYr = fieldOrNan(row, iPmra);
        s.pmdecMasYr = fieldOrNan(row, iPmdec);
        // Linear proper-motion propagation 2016.0 -> epochYear. pmra is
        // mu_alpha* (already x cos delta), hence the division going back
        // to a coordinate offset.
        if (dt != 0.0 && std::isfinite(s.pmraMasYr) && std::isfinite(s.pmdecMasYr)) {
            const double cosd = std::cos(s.decDeg * M_PI / 180.0);
            if (std::fabs(cosd) > 1e-9)
                s.raDeg += s.pmraMasYr * dt / 3.6e6 / cosd;
            s.decDeg += s.pmdecMasYr * dt / 3.6e6;
        }
        out.push_back(s);
    }
    return out;
}

void GaiaClient::coneSearchBrightest(double raDeg, double decDeg,
                                     double radiusDeg, int topN,
                                     double epochYear) {
    ladderStep(raDeg, decDeg, radiusDeg, topN, epochYear, 0, {});
}

void GaiaClient::ladderStep(double raDeg, double decDeg, double radiusDeg,
                            int topN, double epochYear, int rung,
                            std::vector<GaiaSource> prev) {
    static const double kLadder[] = { 13.0, 15.0, 17.0 };
    constexpr int kRungs = 3;
    QUrl url{QString::fromLatin1(kTapSync)};
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("doQuery"));
    q.addQueryItem(QStringLiteral("LANG"), QStringLiteral("ADQL"));
    q.addQueryItem(QStringLiteral("FORMAT"), QStringLiteral("csv"));
    q.addQueryItem(QStringLiteral("QUERY"),
                   adqlCone(raDeg, decDeg, radiusDeg, topN, kLadder[rung]));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, raDeg, decDeg, radiusDeg, topN, epochYear, rung,
         prev = std::move(prev)]() mutable {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                // A shallower rung that already answered beats an error.
                if (!prev.empty()) emit finished(true, prev, QString());
                else emit finished(false, {}, reply->errorString());
                return;
            }
            std::vector<GaiaSource> srcs = parseCsv(reply->readAll(), epochYear);
            if (int(srcs.size()) >= topN || rung + 1 >= kRungs) {
                emit finished(true, srcs, QString());
                return;
            }
            ladderStep(raDeg, decDeg, radiusDeg, topN, epochYear, rung + 1,
                       std::move(srcs));
        });
}

void GaiaClient::coneSearch(double raDeg, double decDeg, double radiusDeg,
                            int topN, double epochYear, double magLimit) {
    QUrl url{QString::fromLatin1(kTapSync)};
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("doQuery"));
    q.addQueryItem(QStringLiteral("LANG"), QStringLiteral("ADQL"));
    q.addQueryItem(QStringLiteral("FORMAT"), QStringLiteral("csv"));
    q.addQueryItem(QStringLiteral("QUERY"),
                   adqlCone(raDeg, decDeg, radiusDeg, topN, magLimit));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(45000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, epochYear] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, {}, reply->errorString());
            return;
        }
        emit finished(true, parseCsv(reply->readAll(), epochYear), QString());
    });
}

} // namespace astro
