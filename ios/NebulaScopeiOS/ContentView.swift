import SwiftUI

// ContentView — the top-level iPad layout: a slim toolbar, the image canvas and
// histogram side-sheet, and a filmstrip stub. Mirrors the HTML mock, but driven
// by the real C++ core through InspectorModel.
struct ContentView: View {
    @StateObject private var model = InspectorModel()

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            HStack(spacing: 0) {
                ImageCanvasView(image: model.display)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                HistogramPanel(model: model)
                    .frame(width: 452)
            }
            filmstrip
        }
        .background(Color(red: 0.039, green: 0.055, blue: 0.075))
        .ignoresSafeArea(.container, edges: .bottom)
    }

    private var toolbar: some View {
        HStack(spacing: 14) {
            RoundedRectangle(cornerRadius: 7)
                .fill(LinearGradient(colors: [Color(red: 0.35, green: 0.66, blue: 1), Color(red: 0.54, green: 0.42, blue: 1)], startPoint: .topLeading, endPoint: .bottomTrailing))
                .frame(width: 26, height: 26)
            Text("NebulaScope").font(.system(size: 16, weight: .bold)).foregroundColor(Color(white: 0.92))
            Text("iPad · proof of concept").font(.system(size: 12, design: .monospaced)).foregroundColor(Color(white: 0.42))
            Spacer()
            // live pixel-ish readout of the image centre, just to show data flow
            pixelReadout
        }
        .padding(.horizontal, 20).frame(height: 60)
        .background(Color(red: 0.047, green: 0.067, blue: 0.09))
        .overlay(Divider().background(Color(white: 0.1)), alignment: .bottom)
    }

    private var pixelReadout: some View {
        var r: Float = 0, g: Float = 0, b: Float = 0
        _ = model.image.pixel(atX: model.image.width / 2, y: model.image.height / 2, outR: &r, outG: &g, outB: &b)
        return HStack(spacing: 10) {
            label("R", String(format: "%.3f", r), Color(red: 1, green: 0.54, blue: 0.54))
            label("G", String(format: "%.3f", g), Color(red: 0.5, green: 0.88, blue: 0.65))
            label("B", String(format: "%.3f", b), Color(red: 0.56, green: 0.75, blue: 0.96))
        }
        .padding(.horizontal, 16).frame(height: 44)
        .background(Color(red: 0.067, green: 0.094, blue: 0.125))
        .overlay(RoundedRectangle(cornerRadius: 11).stroke(Color(white: 0.15), lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 11))
    }
    private func label(_ k: String, _ v: String, _ c: Color) -> some View {
        HStack(spacing: 5) {
            Text(k).font(.system(size: 12, design: .monospaced)).foregroundColor(Color(white: 0.42))
            Text(v).font(.system(size: 12, design: .monospaced)).foregroundColor(c)
        }
    }

    private var filmstrip: some View {
        HStack(spacing: 13) {
            Text("LIBRARY").font(.system(size: 11, weight: .semibold)).kerning(1.5)
                .foregroundColor(Color(white: 0.42)).rotationEffect(.degrees(-90)).frame(width: 20)
            ForEach(0..<6, id: \.self) { i in
                VStack(spacing: 6) {
                    RoundedRectangle(cornerRadius: 12)
                        .fill(RadialGradient(colors: [Color(red: 0.85, green: 0.76, blue: 0.6), Color(red: 0.06, green: 0.05, blue: 0.08)], center: .center, startRadius: 2, endRadius: 60))
                        .frame(width: 112, height: 70)
                        .overlay(RoundedRectangle(cornerRadius: 12).stroke(i == 0 ? Color(red: 0.35, green: 0.66, blue: 1) : Color(white: 0.13), lineWidth: 2))
                    Text(i == 0 ? "Sample" : "slot \(i)").font(.system(size: 11, design: .monospaced)).foregroundColor(Color(white: i == 0 ? 0.86 : 0.42))
                }
            }
            Spacer()
        }
        .padding(.horizontal, 20).frame(height: 118)
        .background(Color(red: 0.047, green: 0.067, blue: 0.09))
        .overlay(Divider().background(Color(white: 0.1)), alignment: .top)
    }
}
