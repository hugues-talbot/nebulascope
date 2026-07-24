#include "ui/ScriptRunner.h"
#include "ui/MainWindow.h"
#include "ui/ViewGrid.h"
#include "render/DisplayRenderer.h"
#include "io/ImageWriter.h"
#include <QCoreApplication>
#include <QFile>
#include <QListWidget>
#include <QTextStream>
#include <QRegularExpression>
#include <QTimer>
#include <cmath>
#include <cstdio>

namespace astro {

ScriptRunner::ScriptRunner(MainWindow* w, const QString& scriptPath, QObject* parent)
    : QObject(parent), m_w(w), m_path(scriptPath) {}

bool ScriptRunner::load() {
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "script: cannot read %s\n", m_path.toLocal8Bit().constData());
        return false;
    }
    QTextStream ts(&f);
    while (!ts.atEnd()) m_lines << ts.readLine();
    return true;
}

void ScriptRunner::start() {
    QTimer::singleShot(m_delayMs, this, &ScriptRunner::step);
}

void ScriptRunner::step() {
    while (m_pc < m_lines.size()) {
        QString line = m_lines[m_pc++].trimmed();
        const int hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0) line = line.left(hash).trimmed();
        if (line.isEmpty()) continue;

        if (line == QLatin1String("quit")) break;

        QString err;
        if (!execute(line, err)) {
            std::fprintf(stderr, "script:%d: FAIL  %s\n      %s\n",
                         m_pc, line.toLocal8Bit().constData(), err.toLocal8Bit().constData());
            ++m_failures;
        } else {
            std::printf("script:%d: ok    %s\n", m_pc, line.toLocal8Bit().constData());
        }

        if (line.startsWith(QLatin1String("sleep "))) return;   // step() rescheduled by execute
        // Yield to the event loop between commands (renders, list signals).
        QTimer::singleShot(m_delayMs, this, &ScriptRunner::step);
        return;
    }
    std::printf("script: done, %d failure(s)\n", m_failures);
    QCoreApplication::exit(m_failures);
}

bool ScriptRunner::execute(const QString& line, QString& err) {
    const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString& cmd = t[0];

    auto needArgs = [&](int n) {
        if (t.size() < n + 1) { err = QStringLiteral("expected %1 argument(s)").arg(n); return false; }
        return true;
    };

    if (cmd == QLatin1String("open")) {
        if (!needArgs(1)) return false;
        m_w->openPaths({ t.mid(1).join(QLatin1Char(' ')) });   // paths may contain spaces
        return true;
    }
    if (cmd == QLatin1String("show")) {
        if (!needArgs(1)) return false;
        const int row = t[1].toInt() - 1;
        if (row < 0 || row >= m_w->m_fileList->count()) { err = "row out of range"; return false; }
        m_w->m_fileList->setCurrentRow(row);
        return true;
    }
    if (cmd == QLatin1String("next")) { m_w->nextImage(); return true; }
    if (cmd == QLatin1String("prev")) { m_w->prevImage(); return true; }
    if (cmd == QLatin1String("split")) {
        if (!needArgs(1)) return false;
        const QStringList p = t[1].toLower().split(QLatin1Char('x'));
        if (p.size() != 2) { err = "expected RxC"; return false; }
        m_w->applySplitLayout(p[0].toInt(), p[1].toInt());
        return true;
    }
    if (cmd == QLatin1String("fn")) {
        if (!needArgs(1)) return false;
        const QString f = t[1].toLower();
        StretchFn fn;
        if (f == "linear") fn = StretchFn::Linear;
        else if (f == "log") fn = StretchFn::Log;
        else if (f == "asinh") fn = StretchFn::Asinh;
        else if (f == "ghs") fn = StretchFn::GHS;
        else { err = "unknown function"; return false; }
        m_w->m_model.setFn(fn);
        return true;
    }
    if (cmd == QLatin1String("autostf")) {
        if (m_w->m_curStats.empty()) { err = "no image shown"; return false; }
        if (t.size() > 1 && t[1] == QLatin1String("linked"))
            m_w->m_model.autoStretchLinked(m_w->m_curStats);
        else
            m_w->m_model.autoStretch(m_w->m_curStats);
        return true;
    }
    if (cmd == QLatin1String("reset")) { m_w->m_model.reset(); return true; }
    if (cmd == QLatin1String("adjust")) {
        if (!needArgs(2)) return false;
        AdjustParams a = m_w->m_model.adjust();
        const QString k = t[1].toLower();
        const double v = t[2].toDouble();
        if      (k == "brightness")  a.brightness = v;
        else if (k == "contrast")    a.contrast = v;
        else if (k == "gamma")       a.gamma = v;
        else if (k == "shadows")     a.shadows = v;
        else if (k == "highlights")  a.highlights = v;
        else if (k == "blackpoint")  a.blackpoint = v;
        else if (k == "whitepoint")  a.whitepoint = v;
        else if (k == "temperature") a.temperature = v;
        else if (k == "tint")        a.tint = v;
        else if (k == "hue")         a.hue = v;
        else if (k == "saturation")  a.saturation = v;
        else if (k == "vibrance")    a.vibrance = v;
        else { err = "unknown adjustment"; return false; }
        m_w->m_model.setAdjust(a);
        return true;
    }
    if (cmd == QLatin1String("rot90")) {
        if (!needArgs(1)) return false;
        m_w->applyTransform(t[1].toLower() == "ccw" ? MainWindow::Xform::RotCCW
                                                    : MainWindow::Xform::RotCW);
        return true;
    }
    if (cmd == QLatin1String("flip")) {
        if (!needArgs(1)) return false;
        m_w->applyTransform(t[1].toLower() == "v" ? MainWindow::Xform::FlipV
                                                  : MainWindow::Xform::FlipH);
        return true;
    }
    if (cmd == QLatin1String("rotate")) {
        if (!needArgs(1)) return false;
        m_w->pushRotateTo(t[1].toDouble());
        return true;
    }
    if (cmd == QLatin1String("export")) {
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        const QImage img = DisplayRenderer::render(m_w->m_image, m_w->m_model);
        if (!img.save(t[1])) { err = "could not write " + t[1]; return false; }
        return true;
    }
    if (cmd == QLatin1String("save")) {
        if (!needArgs(1)) return false;
        if (!m_w->m_image.isValid()) { err = "no image shown"; return false; }
        io::SaveResult sr = io::saveImage(t[1], m_w->m_image, m_w->m_header);
        if (!sr.ok) { err = sr.error; return false; }
        return true;
    }
    if (cmd == QLatin1String("assert")) return doAssert(t, err);
    if (cmd == QLatin1String("sleep")) {
        if (!needArgs(1)) return false;
        QTimer::singleShot(t[1].toInt(), this, &ScriptRunner::step);
        return true;
    }
    err = "unknown command";
    return false;
}

bool ScriptRunner::doAssert(const QStringList& t, QString& err) {
    if (t.size() < 2) { err = "assert what?"; return false; }
    const ImageData& img = m_w->m_image;
    const QString what = t[1];

    if (what == QLatin1String("size")) {
        if (t.size() < 4) { err = "assert size W H"; return false; }
        const int w = t[2].toInt(), h = t[3].toInt();
        if (img.width() != w || img.height() != h) {
            err = QStringLiteral("size is %1x%2, expected %3x%4")
                      .arg(img.width()).arg(img.height()).arg(w).arg(h);
            return false;
        }
        return true;
    }
    if (what == QLatin1String("channels")) {
        if (t.size() < 3) { err = "assert channels n"; return false; }
        if (img.channels() != t[2].toInt()) {
            err = QStringLiteral("channels is %1").arg(img.channels());
            return false;
        }
        return true;
    }
    if (what == QLatin1String("pixel")) {
        // mono: x y v [tol]   rgb: x y r g b [tol]
        if (!img.isValid()) { err = "no image"; return false; }
        if (t.size() < 5) { err = "assert pixel x y v… [tol]"; return false; }
        const int x = t[2].toInt(), y = t[3].toInt();
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) { err = "pixel out of bounds"; return false; }
        const std::size_t i = std::size_t(y) * img.width() + x;
        const int ch = img.channels() >= 3 ? 3 : 1;
        const int nvals = (ch == 3) ? 3 : 1;
        if (t.size() < 4 + nvals) { err = "not enough expected values"; return false; }
        const double tol = (t.size() > 4 + nvals) ? t[4 + nvals].toDouble() : 1e-4;
        for (int c = 0; c < nvals; ++c) {
            const double got = img.plane<float>(c)[i];
            const double want = t[4 + c].toDouble();
            if (std::fabs(got - want) > tol) {
                err = QStringLiteral("pixel(%1,%2) ch%3 = %4, expected %5 ±%6")
                          .arg(x).arg(y).arg(c).arg(got).arg(want).arg(tol);
                return false;
            }
        }
        return true;
    }
    if (what == QLatin1String("range")) {
        if (!img.isValid()) { err = "no image"; return false; }
        if (t.size() < 4) { err = "assert range min max [tol]"; return false; }
        const double tol = t.size() > 4 ? t[4].toDouble() : 1e-3;
        const float* p = img.plane<float>(0);
        float mn = p[0], mx = p[0];
        for (std::size_t i = 0; i < img.samplesPerChannel(); ++i) {
            if (std::isnan(p[i])) continue;
            mn = std::min(mn, p[i]); mx = std::max(mx, p[i]);
        }
        if (std::fabs(mn - t[2].toDouble()) > tol || std::fabs(mx - t[3].toDouble()) > tol) {
            err = QStringLiteral("range is [%1, %2]").arg(mn).arg(mx);
            return false;
        }
        return true;
    }
    err = "unknown assertion";
    return false;
}

} // namespace astro
