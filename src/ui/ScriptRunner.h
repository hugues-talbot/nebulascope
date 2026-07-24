#pragma once
//
// ScriptRunner — executes a NebulaScope test/batch script (--run file.nsc).
//
// Line-based commands, one per line; blank lines and #-comments ignored.
// Assertions print file:line-style diagnostics; the process exits with the
// number of failed assertions (0 = success), so CI can run headless scripted
// sessions:  QT_QPA_PLATFORM=offscreen nebulascope --run smoke.nsc
//
//   open <path>                 load an image (globs NOT expanded here)
//   show <n>                    select list row n (1-based)
//   next | prev                 blink forward / back
//   split <RxC>                 split the view grid
//   fn linear|log|asinh|ghs     stretch function
//   autostf [linked]            auto STF (per-channel, or linked)
//   reset                       reset stretch + adjustments
//   adjust <name> <value>       brightness contrast gamma shadows highlights
//                               blackpoint whitepoint temperature tint hue
//                               saturation vibrance
//   rot90 cw|ccw                lossless rotate
//   flip h|v                    lossless flip
//   rotate <deg>                absolute arbitrary rotation
//   export <path>               write the displayed rendition (png/jpg/tif)
//   save <path>                 write the data (fits/xisf/tiff)
//   assert size <W> <H>
//   assert channels <n>
//   assert pixel <x> <y> <v> [tol]            mono (raw data value)
//   assert pixel <x> <y> <r> <g> <b> [tol]    rgb
//   assert range <min> <max> [tol]            channel-0 data extremes
//   sleep <ms>
//   quit
//
#include <QObject>
#include <QStringList>

namespace astro {

class MainWindow;

class ScriptRunner : public QObject {
    Q_OBJECT
public:
    ScriptRunner(MainWindow* w, const QString& scriptPath, QObject* parent = nullptr);
    // Reads the file; returns false (with a message on stderr) if unreadable.
    bool load();
    // Begin executing after the event loop starts; quits the app when done.
    void start();

private:
    void step();                        // execute the next line, schedule the next
    bool execute(const QString& line, QString& err);   // one command
    bool doAssert(const QStringList& t, QString& err);

    MainWindow* m_w;
    QString m_path;
    QStringList m_lines;
    int m_pc = 0;                       // program counter (line index)
    int m_failures = 0;
    int m_delayMs = 30;                 // between commands: lets renders settle
};

} // namespace astro
