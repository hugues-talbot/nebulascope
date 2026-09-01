#include "core/ImageCache.h"
#include <QFileInfo>

namespace astro {

namespace {
qint64 entryBytes(const ImageCache::Entry& e) {
    if (!e.image || !e.image->isValid()) return 0;
    return qint64(e.image->width()) * e.image->height()
         * e.image->channels() * sizeof(float);
}
} // namespace

std::shared_ptr<const ImageCache::Entry>
ImageCache::get(const QString& key, const QString& filePath) {
    auto it = m_map.find(key);
    if (it == m_map.end()) return nullptr;
    const QFileInfo fi(filePath);
    // Staleness: the file must still exist with the SAME mtime and size the
    // entry was decoded from — otherwise serving it would hide an external
    // overwrite (the page cache never has this problem; a decoded cache must
    // check it itself).
    if (!fi.exists() || fi.lastModified() != it.value()->mtime ||
        fi.size() != it.value()->fileSize) {
        dropNode(it.value());
        return nullptr;
    }
    m_lru.splice(m_lru.begin(), m_lru, it.value());     // touch: most recent
    return m_lru.front().entry;
}

void ImageCache::insert(const QString& key, const QString& filePath, Entry e) {
    const qint64 bytes = entryBytes(e);
    if (m_budget <= 0 || bytes <= 0 || bytes > m_budget) return;
    auto old = m_map.find(key);
    if (old != m_map.end()) dropNode(old.value());
    const QFileInfo fi(filePath);
    if (!fi.exists()) return;
    evictTo(m_budget - bytes);
    Node n;
    n.key = key;
    n.filePath = filePath;
    n.mtime = fi.lastModified();
    n.fileSize = fi.size();
    n.bytes = bytes;
    n.entry = std::make_shared<const Entry>(std::move(e));
    m_lru.push_front(std::move(n));
    m_map.insert(key, m_lru.begin());
    m_used += bytes;
}

void ImageCache::removeFile(const QString& filePath) {
    for (auto it = m_lru.begin(); it != m_lru.end(); ) {
        auto next = std::next(it);
        if (it->filePath == filePath) dropNode(it);
        it = next;
    }
}

void ImageCache::clear() { evictTo(0); }

void ImageCache::setBudgetBytes(qint64 b) {
    m_budget = b < 0 ? 0 : b;
    evictTo(m_budget);
}

void ImageCache::dropNode(std::list<Node>::iterator it) {
    m_used -= it->bytes;
    m_map.remove(it->key);
    m_lru.erase(it);
}

void ImageCache::evictTo(qint64 budget) {
    while (m_used > budget && !m_lru.empty())
        dropNode(std::prev(m_lru.end()));            // least recently used
}

} // namespace astro
