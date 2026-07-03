import SwiftUI

// HistogramPanel — the interactive plot. Draws the per-channel histogram (or all
// three overlaid in RGB mode) plus the transfer curve, with three oversized
// draggable handles (Black / Mid / White). Dragging writes straight back into
// the InspectorModel, which re-renders the image live.
struct HistogramPanel: View {
    @ObservedObject var model: InspectorModel

    private let rgb = Color(red: 0.35, green: 0.66, blue: 1.0)
    private let colR = Color(red: 1.0, green: 0.42, blue: 0.42)
    private let colG = Color(red: 0.25, green: 0.82, blue: 0.5)
    private let colB = Color(red: 0.35, green: 0.66, blue: 1.0)

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            channelChips
            plot
                .frame(height: 240)
                .padding(.horizontal, 20).padding(.top, 6)
            valueBoxes
            Spacer(minLength: 12)
            footer
        }
        .background(Color(red: 0.043, green: 0.063, blue: 0.086))
    }

    // MARK: sections

    private var header: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Histogram").font(.system(size: 17, weight: .bold)).foregroundColor(Color(white: 0.9))
                Spacer()
                Button(action: { model.toggleLog() }) {
                    Text(model.logHistogram ? "Log axis" : "Linear axis")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundColor(model.logHistogram ? rgb : Color(white: 0.5))
                        .padding(.horizontal, 13).padding(.vertical, 8)
                        .overlay(Capsule().stroke(model.logHistogram ? rgb.opacity(0.5) : Color(white: 0.22), lineWidth: 1))
                }
            }
            segmented
        }
        .padding(.horizontal, 20).padding(.top, 18).padding(.bottom, 14)
        .overlay(Divider().background(Color(white: 0.1)), alignment: .bottom)
    }

    private let fns: [(String, NBStretchFn)] = [("Linear", .linear), ("Log", .log), ("Asinh", .asinh), ("GHS", .GHS)]
    private var segmented: some View {
        HStack(spacing: 3) {
            ForEach(fns, id: \.0) { name, fn in
                Button(action: { model.setFunction(fn) }) {
                    Text(name)
                        .font(.system(size: 14, weight: .semibold))
                        .frame(maxWidth: .infinity).padding(.vertical, 11)
                        .foregroundColor(model.stretch.fn == fn ? .black : Color(white: 0.62))
                        .background(model.stretch.fn == fn ? rgb : Color.clear)
                        .clipShape(RoundedRectangle(cornerRadius: 9))
                }
            }
        }
        .padding(4)
        .background(Color(red: 0.04, green: 0.06, blue: 0.08))
        .overlay(RoundedRectangle(cornerRadius: 13).stroke(Color(white: 0.13), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 13))
    }

    private let chips: [(String, Int, Color)] = []
    private var channelChips: some View {
        HStack(spacing: 8) {
            chip("RGB", -1, Color(white: 0.82))
            chip("R", 0, colR); chip("G", 1, colG); chip("B", 2, colB)
            Spacer()
        }
        .padding(.horizontal, 20).padding(.top, 16).padding(.bottom, 2)
        .opacity(model.channels >= 3 ? 1 : 0.35)
        .disabled(model.channels < 3)
    }

    private func chip(_ label: String, _ ch: Int, _ color: Color) -> some View {
        let active = model.activeChannel == ch
        return Button(action: { model.activeChannel = ch; model.objectWillChange.send() }) {
            Text(label)
                .font(.system(size: 14, weight: .bold, design: .monospaced))
                .frame(minWidth: 52).padding(.vertical, 10)
                .foregroundColor(active ? .black : color)
                .background(active ? color : Color(red: 0.05, green: 0.08, blue: 0.11))
                .overlay(RoundedRectangle(cornerRadius: 11).stroke(Color(white: 0.13), lineWidth: 1))
                .clipShape(RoundedRectangle(cornerRadius: 11))
        }
    }

    // MARK: interactive plot

    private var plot: some View {
        GeometryReader { geo in
            let W = geo.size.width, H = geo.size.height
            let ch = model.handleChannel
            let b = CGFloat(model.stretch.black(forChannel: ch))
            let m = CGFloat(model.stretch.mid(forChannel: ch))
            let w = CGFloat(model.stretch.white(forChannel: ch))

            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: 12).fill(Color(red: 0.027, green: 0.043, blue: 0.063))

                // histogram fills
                if model.activeChannel < 0 && model.channels >= 3 {
                    histShape(model.histogram(channel: 0), W, H).fill(colR.opacity(0.20))
                    histShape(model.histogram(channel: 1), W, H).fill(colG.opacity(0.20))
                    histShape(model.histogram(channel: 2), W, H).fill(colB.opacity(0.20))
                } else {
                    let c = model.activeChannel < 0 ? 0 : model.activeChannel
                    histShape(model.histogram(channel: c), W, H).fill(Color(white: 0.75).opacity(0.28))
                }

                // active window shading
                Rectangle().fill(rgb.opacity(0.06))
                    .frame(width: max(0, (w - b) * W), height: H).offset(x: b * W)

                // transfer curve
                curveShape(b: b, m: m, w: w, W: W, H: H)
                    .stroke(Color(white: 0.95), lineWidth: 2)

                // handles
                handle(x: b * W, H: H, color: model.activeChannel < 0 ? Color(white: 0.82) : channelColor(ch), letter: "B") { nx in
                    model.setBlack(Float(clamp01(nx / W)))
                }
                handle(x: m * W, H: H, color: Color(white: 0.82), letter: "M") { nx in
                    model.setMid(Float(clamp01(nx / W)))
                }
                handle(x: w * W, H: H, color: model.activeChannel < 0 ? Color(white: 0.82) : channelColor(ch), letter: "W") { nx in
                    model.setWhite(Float(clamp01(nx / W)))
                }
            }
            .clipShape(RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(white: 0.1), lineWidth: 1))
            .coordinateSpace(name: "plot")
        }
    }

    private func histShape(_ bins: [CGFloat], _ W: CGFloat, _ H: CGFloat) -> Path {
        Path { p in
            guard bins.count > 1 else { return }
            p.move(to: CGPoint(x: 0, y: H))
            for (i, v) in bins.enumerated() {
                let x = CGFloat(i) / CGFloat(bins.count - 1) * W
                p.addLine(to: CGPoint(x: x, y: H - v * (H - 6)))
            }
            p.addLine(to: CGPoint(x: W, y: H)); p.closeSubpath()
        }
    }

    // Transfer curve sampled from the model's own math via a quick local MTF/shape
    // approximation (matches the C++ shapes closely enough for the legend curve).
    private func curveShape(b: CGFloat, m: CGFloat, w: CGFloat, W: CGFloat, H: CGFloat) -> Path {
        Path { p in
            let steps = 96
            let denom = max(0.0001, w - b)
            let mm = min(0.999, max(0.001, (m - b) / denom))
            for i in 0...steps {
                let t = CGFloat(i) / CGFloat(steps)
                let base = shape(t, model.stretch.fn)
                let y = mtf(base, mm)
                let px = b * W + t * denom * W
                let py = H - y * (H - 6)
                if i == 0 { p.move(to: CGPoint(x: px, y: py)) } else { p.addLine(to: CGPoint(x: px, y: py)) }
            }
        }
    }

    private func shape(_ t: CGFloat, _ fn: NBStretchFn) -> CGFloat {
        let tt = clamp01(t)
        switch fn {
        case .log:   return CGFloat(log1p(Double(tt) * 500) / log1p(500))
        case .asinh: return CGFloat(asinh(Double(tt) * 50) / asinh(50.0))
        default:     return tt      // linear & GHS approx (curve is indicative)
        }
    }
    private func mtf(_ x: CGFloat, _ m: CGFloat) -> CGFloat {
        if x <= 0 { return 0 }; if x >= 1 { return 1 }
        if m <= 0 { return 1 }; if m >= 1 { return 0 }
        if abs(x - m) < 1e-6 { return 0.5 }
        return ((m - 1) * x) / (((2 * m - 1) * x) - m)
    }

    private func handle(x: CGFloat, H: CGFloat, color: Color, letter: String,
                        onMove: @escaping (CGFloat) -> Void) -> some View {
        ZStack(alignment: .top) {
            Rectangle().fill(color).frame(width: 2, height: H).opacity(0.9)
            Text(letter)
                .font(.system(size: 13, weight: .bold, design: .monospaced))
                .foregroundColor(.black)
                .frame(width: 34, height: 34)
                .background(color)
                .clipShape(RoundedRectangle(cornerRadius: 11))
                .shadow(color: .black.opacity(0.5), radius: 4, y: 2)
                .offset(y: -15)
        }
        .frame(width: 44, height: H + 15)
        .contentShape(Rectangle())
        .position(x: x, y: H / 2)
        // Drag reported in the plot's coordinate space, so location.x IS the new
        // handle position — no translation bookkeeping needed.
        .gesture(
            DragGesture(minimumDistance: 0, coordinateSpace: .named("plot"))
                .onChanged { v in onMove(v.location.x) }
        )
    }

    private var valueBoxes: some View {
        let ch = model.handleChannel
        return HStack(spacing: 10) {
            valueBox("BLACK", model.stretch.black(forChannel: ch))
            valueBox("MID",   model.stretch.mid(forChannel: ch))
            valueBox("WHITE", model.stretch.white(forChannel: ch))
        }
        .padding(.horizontal, 20).padding(.top, 16)
    }
    private func valueBox(_ label: String, _ v: Float) -> some View {
        VStack(spacing: 3) {
            Text(label).font(.system(size: 10, weight: .semibold)).foregroundColor(Color(white: 0.42))
            Text(String(format: "%.4f", v)).font(.system(size: 14, design: .monospaced)).foregroundColor(Color(white: 0.86))
        }
        .frame(maxWidth: .infinity).padding(.vertical, 9)
        .background(Color(red: 0.05, green: 0.08, blue: 0.11))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color(white: 0.13), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private let cmaps: [(String, NBColormap, [Color])] = [
        ("Gray", .gray, [.black, .white]),
        ("Heat", .heat, [.black, .red, .orange, .yellow, .white]),
        ("Viridis", .viridis, [Color(red:0.27,green:0,blue:0.33), Color(red:0.13,green:0.57,blue:0.55), Color(red:0.99,green:0.91,blue:0.14)]),
        ("Split", .split, [.white, .gray, .black, .gray, .white])
    ]
    private var footer: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("COLORMAP").font(.system(size: 11, weight: .semibold)).kerning(1.5).foregroundColor(Color(white: 0.42))
                .opacity(model.channels == 1 ? 1 : 0.4)
            HStack(spacing: 14) {
                ForEach(cmaps, id: \.0) { name, cm, colors in
                    Button(action: { model.setColormap(cm) }) {
                        VStack(spacing: 6) {
                            RoundedRectangle(cornerRadius: 9)
                                .fill(LinearGradient(colors: colors, startPoint: .leading, endPoint: .trailing))
                                .frame(width: 62, height: 30)
                                .overlay(RoundedRectangle(cornerRadius: 9).stroke(model.stretch.colormap == cm ? rgb : Color(white: 0.13), lineWidth: 2))
                            Text(name).font(.system(size: 11, weight: .semibold)).foregroundColor(model.stretch.colormap == cm ? rgb : Color(white: 0.5))
                        }
                    }
                }
            }
            .opacity(model.channels == 1 ? 1 : 0.4)
            .disabled(model.channels != 1)

            HStack(spacing: 10) {
                Button(action: { model.autoStretch() }) {
                    Text("Auto STF").font(.system(size: 14, weight: .bold)).foregroundColor(rgb)
                        .frame(maxWidth: .infinity).padding(.vertical, 14)
                        .background(Color(red: 0.075, green: 0.125, blue: 0.18))
                        .overlay(RoundedRectangle(cornerRadius: 12).stroke(rgb.opacity(0.5), lineWidth: 1))
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                }
                Button(action: { model.reset() }) {
                    Text("Reset").font(.system(size: 14, weight: .bold)).foregroundColor(Color(white: 0.62))
                        .frame(maxWidth: .infinity).padding(.vertical, 14)
                        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(white: 0.2), lineWidth: 1))
                }
            }
        }
        .padding(20)
        .overlay(Divider().background(Color(white: 0.1)), alignment: .top)
    }

    // helpers
    private func channelColor(_ c: Int) -> Color { c == 0 ? colR : (c == 1 ? colG : colB) }
    private func clamp01(_ v: CGFloat) -> CGFloat { min(1, max(0, v)) }
}
