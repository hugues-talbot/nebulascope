// GaiaQuery tests — the pure pieces only (ADQL text, CSV parsing, epoch
// propagation). No network is ever touched in CI.
#include "nstest.h"
#include "core/GaiaQuery.h"
#include <cmath>

using namespace astro;

NS_TEST(gaia_adql_cone_text) {
    const QString q = GaiaClient::adqlCone(274.7, -13.81, 0.0014, 5);
    NS_CHECK(q.contains(QStringLiteral("TOP 5")));
    NS_CHECK(q.contains(QStringLiteral("gaiadr3.gaia_source")));
    NS_CHECK(q.contains(QStringLiteral("CIRCLE('ICRS',274.70000000,-13.81000000,0.00140000)")));
    NS_CHECK(q.contains(QStringLiteral("ORDER BY phot_g_mean_mag")));
    NS_CHECK(!q.contains(QStringLiteral("phot_g_mean_mag<")));
    const QString qm = GaiaClient::adqlCone(274.7, -13.81, 1.26, 500, 18.0);
    NS_CHECK(qm.contains(QStringLiteral(" AND phot_g_mean_mag<18.0 ")));
}

NS_TEST(gaia_epoch_from_dateobs) {
    // Mid-year ISO stamp lands near year + 0.5; plain years and quoted FITS
    // values parse too; garbage yields 0.
    const double e = GaiaClient::epochYearFromIso(QStringLiteral("2025-07-02T12:00:00"));
    NS_CHECK(std::fabs(e - 2025.5) < 0.01);
    NS_CHECK(std::fabs(GaiaClient::epochYearFromIso(QStringLiteral("'2016-01-01'")) - 2016.0) < 1e-6);
    NS_CHECK(std::fabs(GaiaClient::epochYearFromIso(QStringLiteral("2020.25")) - 2020.25) < 1e-9);
    NS_CHECK(GaiaClient::epochYearFromIso(QStringLiteral("n/a")) == 0.0);
}

NS_TEST(gaia_csv_parse_and_propagate) {
    // Column order deliberately shuffled vs the query; one row with missing
    // photometry/astrometry, one malformed row, one blank line.
    const QByteArray csv =
        "ra,source_id,dec,pmra,pmdec,phot_g_mean_mag,bp_rp,parallax\n"
        "180.00000000,4146599613565336064,0.00000000,3600.0,-7200.0,12.34,0.56,1.25\n"
        "10.50000000,4146599613565335808,-45.00000000,,,15.10,,\n"
        "not,a,valid,row,,,,\n"
        "\n";
    // dt = +10 yr: pmra 3600 mas/yr at dec 0 -> +0.01 deg in RA (cos = 1),
    // pmdec -7200 -> -0.02 deg in Dec.
    const auto ten = GaiaClient::parseCsv(csv, 2026.0);
    NS_CHECK(ten.size() == 2);
    NS_CHECK(ten[0].sourceId == 4146599613565336064LL);
    NS_CHECK(std::fabs(ten[0].raDeg - 180.01) < 1e-9);
    NS_CHECK(std::fabs(ten[0].decDeg - (-0.02)) < 1e-9);
    NS_CHECK(std::fabs(ten[0].gMag - 12.34) < 1e-9);
    NS_CHECK(std::fabs(ten[0].parallaxMas - 1.25) < 1e-9);
    // Missing proper motion: position left at catalogue epoch, NaNs kept NaN.
    NS_CHECK(std::fabs(ten[1].raDeg - 10.5) < 1e-9);
    NS_CHECK(std::isnan(ten[1].bpRp) && std::isnan(ten[1].parallaxMas));
    // epochYear 0: no propagation at all.
    const auto raw = GaiaClient::parseCsv(csv, 0.0);
    NS_CHECK(std::fabs(raw[0].raDeg - 180.0) < 1e-12);
}

int main() { return nstest::runAll(); }
