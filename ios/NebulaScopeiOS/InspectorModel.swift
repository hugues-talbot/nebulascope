import SwiftUI
import UIKit

// InspectorModel — the Swift-side single source of truth. It owns the loaded
// NBImage and the NBStretch parameters (both Objective-C++ objects), and
// republishes a rendered UIImage whenever the stretch changes. This mirrors the
// desktop StretchModel → DisplayRenderer relationship.
final class InspectorModel: ObservableObject {
    let image: NBImage
    @Published private(set) var display: UIImage?
    @Published var stretch: NBStretch          // mutated via the helpers below
    @Published var activeChannel: Int = -1     // -1 = RGB (linked), 0/1/2 = R/G/B
    @Published var logHistogram: Bool = true

    let channels: Int

    init() {
        // Open on the synthetic galaxy so the PoC runs with no file.
        let img = NBImage.sampleGalaxy(width: 1000, height: 760)
        self.image = img
        self.channels = img.channels
        self.stretch = img.autoStretch()
        render()
    }

    func render() {
        display = image.render(with: stretch)
    }

    // Histogram bins for a channel, normalized to [0,1].
    func histogram(channel: Int) -> [CGFloat] {
        let arr = image.histogram(forChannel: channel, bins: 256,
                                  logScale: logHistogram, stretch: stretch)
        return arr.map { CGFloat(truncating: $0) }
    }

    // The channel whose handles are edited (RGB edits the green as representative).
    var handleChannel: Int { activeChannel < 0 ? 1 : activeChannel }

    // Drag handlers — clamp to keep black < mid < white, then re-render.
    private let eps: Float = 0.006
    func setBlack(_ v: Float) {
        let c = activeChannel
        let ch = handleChannel
        let m = stretch.mid(forChannel: ch)
        let nv = min(max(0, v), m - eps)
        stretch.setBlack(nv, forChannel: c)          // c < 0 links all channels
        bumpAndRender()
    }
    func setMid(_ v: Float) {
        let ch = handleChannel
        let b = stretch.black(forChannel: ch)
        let w = stretch.white(forChannel: ch)
        let nv = min(max(b + eps, v), w - eps)
        stretch.setMid(nv, forChannel: activeChannel)
        bumpAndRender()
    }
    func setWhite(_ v: Float) {
        let ch = handleChannel
        let m = stretch.mid(forChannel: ch)
        let nv = max(min(1, v), m + eps)
        stretch.setWhite(nv, forChannel: activeChannel)
        bumpAndRender()
    }

    func setFunction(_ fn: NBStretchFn) { stretch.fn = fn; bumpAndRender() }
    func setColormap(_ cm: NBColormap) { stretch.colormap = cm; bumpAndRender() }
    func toggleLog() { logHistogram.toggle(); objectWillChange.send() }

    func autoStretch() { stretch = image.autoStretch(); render() }
    func reset()       { autoStretch() }

    // NBStretch is a reference type, so SwiftUI won't observe its internal
    // mutations automatically; nudge it explicitly.
    private func bumpAndRender() {
        objectWillChange.send()
        render()
    }
}
