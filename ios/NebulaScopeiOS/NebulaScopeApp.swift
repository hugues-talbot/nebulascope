import SwiftUI

// App entry point. iPad-only (see TARGETED_DEVICE_FAMILY in project.yml).
@main
struct NebulaScopeApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .preferredColorScheme(.dark)
                .statusBarHidden(true)
        }
    }
}
