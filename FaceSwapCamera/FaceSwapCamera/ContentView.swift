import SwiftUI
import SystemExtensions

struct ContentView: View {
    @State private var extensionInstalled = false
    @State private var statusMessage = "Camera Extension not installed"
    
    var body: some View {
        VStack(spacing: 20) {
            Text("FaceSwap Camera Extension")
                .font(.largeTitle)
            
            Text(statusMessage)
                .foregroundColor(extensionInstalled ? .green : .orange)
            
            Button("Install Extension") {
                installExtension()
            }
            .disabled(extensionInstalled)
            
            Button("Open System Settings") {
                openSystemSettings()
            }
        }
        .padding()
        .frame(width: 400, height: 300)
    }
    
    func installExtension() {
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: "com.yourcompany.FaceSwapCamera.Extension",
            queue: .main
        )
        request.delegate = ExtensionDelegate.shared
        OSSystemExtensionManager.shared.submitRequest(request)
    }
    
    func openSystemSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera") {
            NSWorkspace.shared.open(url)
        }
    }
}

class ExtensionDelegate: NSObject, OSSystemExtensionRequestDelegate {
    static let shared = ExtensionDelegate()
    
    func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        print("Extension installation succeeded")
    }
    
    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        print("Extension installation failed: \(error.localizedDescription)")
    }
    
    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        print("Extension needs user approval")
    }
    
    func request(_ request: OSSystemExtensionRequest, 
                 actionForReplacingExtension existing: OSSystemExtensionProperties, 
                 withExtension ext: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        return .replace
    }
}

#Preview {
    ContentView()
}

