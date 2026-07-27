// SexCatalog tests: ASCII_HEAD header parsing, name-based column access in
// any order, tolerant defaults, and rejection of unusable input.
#include "nstest.h"
#include "core/SexCatalog.h"
#include <QFile>
#include <QTemporaryDir>

using namespace astro;

static QString writeFile(const QString& dir, const char* name, const char* text) {
    const QString p = dir + "/" + name;
    QFile f(p);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(text);
    return p;
}

NS_TEST(sexcat_parses_header_and_rows) {
    QTemporaryDir tmp;
    const QString p = writeFile(tmp.path(), "cat.txt",
        "#   1 NUMBER          Running object number\n"
        "#   2 X_IMAGE         Object position along x       [pixel]\n"
        "#   3 Y_IMAGE         Object position along y       [pixel]\n"
        "#   4 MAG_AUTO        Kron-like elliptical aperture magnitude\n"
        "#   5 FLAGS           Extraction flags\n"
        "     1   512.34   384.56  -11.2   0\n"
        "     2    10.00    20.00   -9.8   3\n"
        "     3   100.50   200.25  -13.1   0\n");
    QString err;
    SexCatalog cat = SexCatalog::parse(p, &err);
    NS_CHECK(cat.isValid());
    NS_CHECK(cat.rowCount() == 3);
    NS_CHECK(cat.has("X_IMAGE") && cat.has("FLAGS") && !cat.has("THETA_IMAGE"));
    NS_CHECK_NEAR(cat.value(0, "X_IMAGE"), 512.34, 1e-9);
    NS_CHECK_NEAR(cat.value(1, "Y_IMAGE"), 20.0, 1e-9);
    NS_CHECK_NEAR(cat.value(2, "MAG_AUTO"), -13.1, 1e-9);
    NS_CHECK_NEAR(cat.value(1, "FLAGS"), 3.0, 1e-12);
    // Absent column and out-of-range row fall back to the default.
    NS_CHECK_NEAR(cat.value(0, "THETA_IMAGE", -99.0), -99.0, 1e-12);
    NS_CHECK_NEAR(cat.value(99, "X_IMAGE", -1.0), -1.0, 1e-12);
}

NS_TEST(sexcat_column_order_independent) {
    // Same columns, different order — access by name must not care.
    QTemporaryDir tmp;
    const QString p = writeFile(tmp.path(), "cat.txt",
        "#   1 MAG_AUTO  m\n"
        "#   2 Y_IMAGE   y\n"
        "#   3 X_IMAGE   x\n"
        "  -10.0  200.0  100.0\n");
    SexCatalog cat = SexCatalog::parse(p);
    NS_CHECK(cat.isValid());
    NS_CHECK_NEAR(cat.value(0, "X_IMAGE"), 100.0, 1e-9);
    NS_CHECK_NEAR(cat.value(0, "Y_IMAGE"), 200.0, 1e-9);
    NS_CHECK_NEAR(cat.value(0, "MAG_AUTO"), -10.0, 1e-9);
}

NS_TEST(sexcat_rejects_missing_and_garbage) {
    QString err;
    SexCatalog missing = SexCatalog::parse("/nonexistent/path/cat.txt", &err);
    NS_CHECK(!missing.isValid());
    NS_CHECK(!err.isEmpty());

    QTemporaryDir tmp;
    const QString p = writeFile(tmp.path(), "junk.txt",
        "this is not\na catalog at all\n");
    SexCatalog junk = SexCatalog::parse(p);
    NS_CHECK(!junk.isValid());
}

int main() { return nstest::runAll(); }
