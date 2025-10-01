# FaceSwap Camera Extension

A macOS virtual camera extension that provides a foundation for implementing real-time face swapping functionality using CoreMediaIO.

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Building the Project](#building-the-project)
- [Installation & Testing](#installation--testing)
- [Configuration](#configuration)
- [Implementation Guide](#implementation-guide)
- [Troubleshooting](#troubleshooting)
- [Development Tips](#development-tips)

## Overview

This project creates a CoreMediaIO Camera Extension for macOS 13.0+. The extension appears as a virtual camera device in any application that supports camera input (Zoom, Teams, FaceTime, etc.).

### Components

- **FaceSwapCamera** (Host App): SwiftUI application that installs and manages the system extension
- **FaceSwapCameraExtension**: The actual camera extension that provides video frames to applications

### Features

- ✅ **Complete Xcode Project**: Fully configured and ready to build
- ✅ Virtual camera device visible to all camera-aware applications
- ✅ System extension framework using CoreMediaIO
- ✅ Sample frame generation (animated test pattern)
- ✅ Proper sandboxing and entitlements
- ✅ App Groups for shared state between host and extension
- ✅ 1920x1080 @ 60fps video stream
- 🚧 Face swap processing (ready to implement)

## Quick Start

### Prerequisites

- macOS 13.0 or later
- Xcode 14.0 or later
- Apple Developer account (for code signing)

### 5-Minute Setup

**The Xcode project is already built and ready!** Just open and configure:

1. **Open the project**
   ```bash
   cd /Users/leone/PycharmProjects/Samson/FaceSwapCamera
   open FaceSwapCamera.xcodeproj
   ```

2. **Set your development team**
   - Select the **FaceSwapCamera** project in Project Navigator
   - For both **FaceSwapCamera** and **FaceSwapCameraExtension** targets:
     - Go to **Signing & Capabilities** tab
     - Set **Team** dropdown to your development team

3. **Build and run**
   - Press **⌘B** to build (or Product > Build)
   - Press **⌘R** to run (or Product > Run)
   - ✅ App launches with extension installer UI

### Command Line Build

```bash
cd /Users/leone/PycharmProjects/Samson/FaceSwapCamera

# Build the project
xcodebuild -project FaceSwapCamera.xcodeproj \
  -scheme FaceSwapCamera \
  -configuration Debug \
  build

# Build succeeded? Great! Now run it from Xcode or:
open build/Debug/FaceSwapCamera.app
```

## Project Structure

```
FaceSwapCamera/
├── README.md                          # This file (master documentation)
├── .gitignore                         # Git ignore rules
├── FaceSwapCamera.xcodeproj/          # Complete Xcode project
│   ├── project.pbxproj                # Project configuration
│   └── xcshareddata/xcschemes/        # Build schemes
│
├── FaceSwapCamera/                    # Host Application
│   ├── FaceSwapCameraApp.swift        # App entry point
│   ├── ContentView.swift              # Main UI with extension installer
│   └── FaceSwapCamera.entitlements    # App entitlements (sandbox, app groups)
│
└── FaceSwapCameraExtension/           # Camera Extension
    ├── main.swift                     # Extension entry point
    ├── ExtensionProvider.swift        # CMIOExtensionProvider implementation
    ├── ExtensionDeviceSource.swift    # Virtual camera device
    ├── ExtensionStreamSource.swift    # Video frame generation (60fps)
    ├── Info.plist                     # Extension configuration
    └── FaceSwapCameraExtension.entitlements  # Extension entitlements
```

## Building the Project

### Using Xcode

1. Open `FaceSwapCamera.xcodeproj`
2. Select the **FaceSwapCamera** scheme
3. Choose your Mac as the destination
4. Press **⌘B** or **Product > Build**

**Expected output**: ✅ Build Succeeded (both host app and extension compile)

### Using Command Line

```bash
# Clean build
xcodebuild -project FaceSwapCamera.xcodeproj \
  -scheme FaceSwapCamera \
  -configuration Debug \
  clean build

# Check for success
echo $?  # Should output: 0
```

### Build Verification

After building, verify the outputs:

```bash
ls -la ~/Library/Developer/Xcode/DerivedData/FaceSwapCamera-*/Build/Products/Debug/
```

You should see:
- `FaceSwapCamera.app` (host application)
- `FaceSwapCameraExtension.systemextension` (camera extension)

## Installation & Testing

### Installing the Extension

1. **Launch the host app**
   ```bash
   open ~/Library/Developer/Xcode/DerivedData/FaceSwapCamera-*/Build/Products/Debug/FaceSwapCamera.app
   ```
   Or press **⌘R** in Xcode

2. **Click "Install Extension"** in the app

3. **Approve in System Settings**
   - macOS will prompt you to open System Settings
   - Go to **Privacy & Security > System Extensions**
   - Click **Allow** for the FaceSwap Camera Extension

4. **Grant camera permissions** (if requested)

### Testing the Camera

The extension should now appear as a camera device. Test it:

**Option 1: QuickTime Player**
```bash
# Open QuickTime and start a new movie recording
open -a "QuickTime Player"
# Then: File > New Movie Recording
# Click camera dropdown > Select "FaceSwap Camera"
```

**Option 2: Any video app**
- Open Zoom, Teams, FaceTime, etc.
- Go to video settings
- Select "FaceSwap Camera" from camera list
- You should see an animated white stripe (test pattern)

### Uninstalling the Extension

If needed, uninstall the extension:

```bash
# List installed extensions
systemextensionsctl list

# Uninstall (replace with actual bundle ID)
systemextensionsctl uninstall <BUNDLE_ID> com.yourcompany.FaceSwapCamera.Extension
```

Or use the app UI (you'll need to implement an uninstall button).

## Configuration

### Bundle Identifiers

Default identifiers are set in the Xcode project:
- **Host App**: `com.yourcompany.FaceSwapCamera`
- **Extension**: `com.yourcompany.FaceSwapCamera.Extension`
- **App Group**: `group.com.yourcompany.FaceSwapCamera`

**To change these:**

1. **Update Xcode project settings**
   - Select each target
   - Change **Bundle Identifier** in General tab

2. **Update code references**
   - `ContentView.swift` line 27: Update extension identifier in `installExtension()`

3. **Update entitlements**
   - Both `.entitlements` files: Update App Group name
   - Must be identical in host and extension!

### Video Format

Current settings (in `ExtensionDeviceSource.swift`):
- **Resolution**: 1920x1080
- **Frame Rate**: 60 FPS
- **Pixel Format**: BGRA (32-bit)

To change:
```swift
// In ExtensionDeviceSource.swift, init method:
let dims = CMVideoDimensions(width: 1920, height: 1080)  // Change resolution
// In top of file:
let kFrameRate: Int = 60  // Change frame rate
```

## Implementation Guide

### Adding Face Swap Logic

Currently, the extension generates a test pattern (moving white stripe). Replace this with face swap processing in `ExtensionStreamSource.swift`:

```swift
private func generateFrame() {
    // 1. Get pixel buffer from pool (already implemented)
    var pixelBuffer: CVPixelBuffer?
    CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(...)
    
    // 2. YOUR FACE SWAP CODE HERE:
    //    - Capture from real camera (see below)
    //    - Detect faces using Vision framework
    //    - Process face swap
    //    - Write result to pixelBuffer
    
    // 3. Send buffer to clients (already implemented)
    stream.send(sampleBuffer, ...)
}
```

### Accessing Real Camera

To use the actual camera as input:

1. **Add AVFoundation**
   ```swift
   import AVFoundation
   ```

2. **Create capture session** (in `ExtensionStreamSource.init`)
   ```swift
   private var captureSession: AVCaptureSession?
   private var cameraOutput: AVCaptureVideoDataOutput?
   
   func setupCameraCapture() {
       captureSession = AVCaptureSession()
       captureSession?.sessionPreset = .hd1920x1080
       
       guard let camera = AVCaptureDevice.default(for: .video) else { return }
       guard let input = try? AVCaptureDeviceInput(device: camera) else { return }
       
       cameraOutput = AVCaptureVideoDataOutput()
       cameraOutput?.setSampleBufferDelegate(self, queue: _timerQueue)
       
       if captureSession?.canAddInput(input) == true {
           captureSession?.addInput(input)
       }
       if captureSession?.canAddOutput(cameraOutput!) == true {
           captureSession?.addOutput(cameraOutput!)
       }
       
       captureSession?.startRunning()
   }
   ```

3. **Process camera frames**
   ```swift
   extension ExtensionStreamSource: AVCaptureVideoDataOutputSampleBufferDelegate {
       func captureOutput(_ output: AVCaptureOutput, 
                         didOutput sampleBuffer: CMSampleBuffer, 
                         from connection: AVCaptureConnection) {
           // Process this frame with face swap
           // Then send to stream.send()
       }
   }
   ```

### Sharing Configuration with Host App

Use App Groups to share settings:

**In host app:**
```swift
let defaults = UserDefaults(suiteName: "group.com.yourcompany.FaceSwapCamera")
defaults?.set(true, forKey: "faceSwapEnabled")
defaults?.set(0.8, forKey: "swapIntensity")
```

**In extension:**
```swift
let defaults = UserDefaults(suiteName: "group.com.yourcompany.FaceSwapCamera")
let enabled = defaults?.bool(forKey: "faceSwapEnabled") ?? false
let intensity = defaults?.double(forKey: "swapIntensity") ?? 1.0
```

## Troubleshooting

### Build Errors

**"No such module 'SystemExtensions'"**
- Ensure deployment target is macOS 13.0+ in both targets
- Check Build Settings > macOS Deployment Target

**"Cannot find type in scope"**
- Clean build folder: **Product > Clean Build Folder** (⇧⌘K)
- Rebuild

**Code signing errors**
- Select your Team in Signing & Capabilities for both targets
- Ensure both targets use the same team
- Check that your Apple ID is signed in to Xcode

### Installation Issues

**Extension not appearing in System Settings**
- Build succeeded? Check Console.app for errors
- Try: `systemextensionsctl list` to see if extension is registered
- Restart Mac if needed

**"Installation failed" error**
- App Groups must match exactly in both entitlements
- Extension bundle ID must match the one in `ContentView.swift`
- Check code signing team is selected

**Extension approved but not in camera list**
- Restart apps that use cameras
- Check Console.app for CoreMediaIO errors
- Try reinstalling: uninstall then build & install again

### Runtime Issues

**Camera shows black screen**
- Check Console.app for frame generation errors
- Verify pixel buffer pool is created: add logging
- Ensure `stream.send()` is being called

**Camera not appearing in apps**
- Verify extension is approved: System Settings > Privacy & Security
- Some apps cache camera lists - restart them
- Try QuickTime Player first (always refreshes camera list)

**App crashes on install**
- Check extension's `main.swift` and `ExtensionProvider.swift`
- Look for force-unwrap (`!`) crashes
- Enable exception breakpoints in Xcode

### Debugging

System extensions are harder to debug. Best practices:

1. **Use extensive logging**
   ```swift
   import os.log
   let log = OSLog(subsystem: "com.yourcompany.FaceSwapCamera", category: "extension")
   os_log(.debug, log: log, "Frame generated: %d", frameCount)
   ```

2. **View logs in Console.app**
   - Open Console.app
   - Filter by process: "FaceSwapCameraExtension"
   - Or search for your subsystem name

3. **Check system logs**
   ```bash
   log stream --predicate 'subsystem == "com.yourcompany.FaceSwapCamera"' --level debug
   ```

## Development Tips

### Performance Optimization

- Frame generation runs at 60 FPS - keep processing under 16ms per frame
- Use Metal or Accelerate framework for image processing
- Profile with Instruments (Time Profiler)
- Consider reducing frame rate or resolution if needed

### Code Changes

After modifying extension code:
1. Quit all apps using the camera
2. Rebuild the project
3. May need to reinstall extension
4. Sometimes a restart is required

### Testing Workflow

1. **Development**: Use test pattern to verify camera appears
2. **Integration**: Add real camera input
3. **Processing**: Implement face detection/swap incrementally
4. **Polish**: Optimize performance and add error handling

### Security & Privacy

- Extension runs sandboxed with limited permissions
- Camera access must be explicitly granted via entitlements
- No network access by default (add if needed)
- User must approve extension installation
- All camera usage is logged by macOS

## Resources

- [CoreMediaIO Documentation](https://developer.apple.com/documentation/coremediaio)
- [System Extensions Guide](https://developer.apple.com/documentation/systemextensions)
- [Camera Extensions (WWDC 2022)](https://developer.apple.com/videos/play/wwdc2022/10022/)
- [Vision Framework](https://developer.apple.com/documentation/vision) (for face detection)

## Success Criteria

You've successfully set up the project when:

- ✅ Project builds without errors (`xcodebuild` exits with 0)
- ✅ No compiler warnings for your code
- ✅ Host app launches and shows UI
- ✅ Extension target is included in build
- ✅ Extension appears in System Settings after installation
- ✅ "FaceSwap Camera" appears in QuickTime camera list
- ✅ Test pattern (white stripe) is visible in camera preview

**Next step**: Implement your face swap logic in `ExtensionStreamSource.swift`! 🎭
