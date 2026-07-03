import SwiftUI

// ImageCanvasView — displays the rendered frame with touch inspection:
//   * pinch (MagnificationGesture) to zoom
//   * two-finger / one-finger drag to pan
//   * double-tap to reset to fit
// This replaces the desktop's mouse drag-rectangle zoom with native gestures.
struct ImageCanvasView: View {
    let image: UIImage?

    @State private var scale: CGFloat = 1
    @State private var lastScale: CGFloat = 1
    @State private var offset: CGSize = .zero
    @State private var lastOffset: CGSize = .zero

    var body: some View {
        GeometryReader { geo in
            ZStack {
                Color(red: 0.02, green: 0.027, blue: 0.04)

                if let image {
                    Image(uiImage: image)
                        .resizable()
                        .interpolation(.high)
                        .aspectRatio(contentMode: .fit)
                        .scaleEffect(scale)
                        .offset(offset)
                        .gesture(
                            SimultaneousGesture(
                                MagnificationGesture()
                                    .onChanged { v in scale = max(0.5, min(12, lastScale * v)) }
                                    .onEnded { _ in lastScale = scale },
                                DragGesture()
                                    .onChanged { v in
                                        offset = CGSize(width: lastOffset.width + v.translation.width,
                                                        height: lastOffset.height + v.translation.height)
                                    }
                                    .onEnded { _ in lastOffset = offset }
                            )
                        )
                        .onTapGesture(count: 2) {
                            withAnimation(.easeOut(duration: 0.25)) {
                                scale = 1; lastScale = 1; offset = .zero; lastOffset = .zero
                            }
                        }
                } else {
                    ProgressView().tint(.white)
                }

                // gesture hints + zoom badge
                VStack {
                    HStack {
                        hintChip("👌  Pinch to zoom · drag to pan")
                        Spacer()
                    }
                    Spacer()
                    HStack {
                        Spacer()
                        Text(String(format: "%.0f%%", scale * 100))
                            .font(.system(size: 12, weight: .medium, design: .monospaced))
                            .foregroundColor(Color(white: 0.6))
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .background(Color.black.opacity(0.5))
                            .clipShape(RoundedRectangle(cornerRadius: 8))
                    }
                }
                .padding(16)
            }
            .frame(width: geo.size.width, height: geo.size.height)
            .clipped()
        }
    }

    private func hintChip(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 12))
            .foregroundColor(Color(white: 0.68))
            .padding(.horizontal, 13).padding(.vertical, 9)
            .background(Color(red: 0.04, green: 0.07, blue: 0.11).opacity(0.82))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(red: 0.13, green: 0.2, blue: 0.27), lineWidth: 1))
            .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}
