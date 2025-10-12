import SwiftUI
import SystemExtensions

struct ContentView: View {
    @StateObject private var extensionManager = ExtensionManager()
    
    var body: some View {
        VStack(spacing: 20) {
            Text("FaceSwap Camera Extension")
                .font(.largeTitle)
            
            Text(extensionManager.statusMessage)
                .foregroundColor(extensionManager.statusColor)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
            
            Button("Install Extension") {
                extensionManager.installExtension()
            }
            .disabled(extensionManager.isInstalled)
            
            Button("Open System Settings") {
                openSystemSettings()
            }
        }
        .padding()
        .frame(width: 500, height: 350)
    }
    
    func openSystemSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera") {
            NSWorkspace.shared.open(url)
        }
    }
}

class ExtensionManager: NSObject, ObservableObject {
    @Published var statusMessage = "Camera Extension not installed"
    @Published var isInstalled = false
    @Published var statusColor: Color = .orange
    
    func installExtension() {
        print("Starting installation request for identifier: com.yourcompany.FaceSwapCamera.Extension")
        statusMessage = "Requesting installation..."
        statusColor = .blue
        
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: "com.yourcompany.FaceSwapCamera.Extension",
            queue: .main
        )
        print("Request created: \(request)")
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
        print("Request submitted")
    }
}

extension ExtensionManager: OSSystemExtensionRequestDelegate {
    func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        DispatchQueue.main.async {
            print("✅ Extension installation succeeded")
            self.statusMessage = "✅ Extension installed successfully!\nGo to System Settings > Privacy & Security to approve."
            self.statusColor = .green
            self.isInstalled = true
        }
    }
    
    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        DispatchQueue.main.async {
            print("❌ Extension installation failed: \(error.localizedDescription)")
            self.statusMessage = "❌ Installation failed: \(error.localizedDescription)"
            self.statusColor = .red
        }
    }
    
    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        DispatchQueue.main.async {
            print("⏳ Extension needs user approval")
            self.statusMessage = "⏳ Awaiting approval in System Settings...\nGo to System Settings > Privacy & Security"
            self.statusColor = .orange
        }
    }
    
    func request(_ request: OSSystemExtensionRequest, 
                 actionForReplacingExtension existing: OSSystemExtensionProperties, 
                 withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        print("🔄 Replacing existing extension")
        return .replace
    }
}

#Preview {
    ContentView()
}

