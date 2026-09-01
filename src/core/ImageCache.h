#pragma once
//
// ImageCache — an LRU cache of DECODED images, byte-budgeted.
//
// The OS page cache only spares the disk read; for large masters the switch
// cost is dominated by everything after it — zlib inflation of a compressed
// XISF, byte-unshuffle, Float32 promotion, debayer, statistics. Caching the
// decoded result turns a multi-second list switch into a memcpy.
//
// Entries hold the POST-DEBAYER, DISK-FRAME decode (rotation histories are
// replayed on top by the caller, exactly as on a fresh decode), the pristine
// header, and the disk-frame statistics. Staleness is checked on every hit
// against the file's mtime + size, so an external overwrite is never served
// from memory — the auto-reload watcher additionally evicts eagerly.
//
#include "core/ImageData.h"
#include "core/ImageHeader.h"
#include "core/ImageStats.h"
#include <QDateTime>
#include <QHash>
#include <QString>
#include <list>
#include <memory>
#include <vector>

namespace astro {

class ImageCache {
public:
    struct Entry {
        std::shared_ptr<const ImageData> image;   // post-debayer, disk frame
        ImageHeader header;                        // pristine (pre display-time appends)
        std::vector<ChannelStats> stats;           // disk-frame statistics
    };

    // The decoded entry for `key`, or nullptr on miss or when `filePath` on
    // disk changed since insert (the stale entry is dropped).
    std::shared_ptr<const Entry> get(const QString& key, const QString& filePath);

    // Store a decoded entry (no-op when the budget is 0 or the entry alone
    // exceeds it). Replaces any previous entry under the same key.
    void insert(const QString& key, const QString& filePath, Entry e);

    void removeFile(const QString& filePath);     // every key of that file
    void clear();

    // Budget in bytes; evicts down immediately. 0 disables caching.
    void setBudgetBytes(qint64 b);
    qint64 budgetBytes() const { return m_budget; }
    qint64 usedBytes() const { return m_used; }
    // Presence check without touching the LRU order or the filesystem.
    bool contains(const QString& key) const { return m_map.contains(key); }

private:
    struct Node {
        QString key, filePath;
        QDateTime mtime;
        qint64 fileSize = -1;
        qint64 bytes = 0;
        std::shared_ptr<const Entry> entry;
    };
    void dropNode(std::list<Node>::iterator it);
    void evictTo(qint64 budget);

    std::list<Node> m_lru;                        // front = most recently used
    QHash<QString, std::list<Node>::iterator> m_map;
    qint64 m_budget = 0;
    qint64 m_used = 0;
};

} // namespace astro
