#include "io/ImageWriter.h"
#include "io/FitsWriter.h"
#include "io/XisfWriter.h"
#include "io/TiffWriter.h"

namespace astro::io {

std::vector<ImageWriter*> registeredWriters() {
    static FitsWriter fits;
    static XisfWriter xisf;
    static TiffWriter tiff;
    static std::vector<ImageWriter*> writers { &fits, &xisf, &tiff };
    return writers;
}

ImageWriter* writerForFile(const QString& path) {
    for (ImageWriter* w : registeredWriters())
        if (w->canWrite(path)) return w;
    return nullptr;
}

SaveResult saveImage(const QString& path, const ImageData& image,
                     const ImageHeader& header, const SaveOptions& opts) {
    if (ImageWriter* w = writerForFile(path))
        return w->save(path, image, header, opts);
    SaveResult res;
    res.error = QStringLiteral("Unsupported output format: %1").arg(path);
    return res;
}

} // namespace astro::io
