import CoreMediaIO
import Foundation
import IOKit.audio
import os.log

class ExtensionDeviceSource: NSObject, CMIOExtensionDeviceSource {
    
    private(set) var device: CMIOExtensionDevice!
    
    private var _streamSource: ExtensionStreamSource!
    
    private var _streamingCounter: UInt32 = 0
    
    init(localizedName: String) {
        super.init()
        let deviceID = UUID()
        self.device = CMIOExtensionDevice(localizedName: localizedName, deviceID: deviceID, legacyDeviceID: nil, source: self)
        
        let dims = CMVideoDimensions(width: 1920, height: 1080)
        let pixelFormat = kCVPixelFormatType_32BGRA
        
        let streamFormat = CMIOExtensionStreamFormat(formatDescription: createFormatDescription(dims: dims, pixelFormat: pixelFormat), maxFrameDuration: CMTime(value: 1, timescale: Int32(kFrameRate)), minFrameDuration: CMTime(value: 1, timescale: Int32(kFrameRate)), validFrameDurations: nil)
        
        _streamSource = ExtensionStreamSource(localizedName: "FaceSwap Camera Stream", streamID: UUID(), streamFormat: streamFormat, device: device)
        do {
            try device.addStream(_streamSource.stream)
        } catch {
            fatalError("Failed to add stream: \(error.localizedDescription)")
        }
    }
    
    var availableProperties: Set<CMIOExtensionProperty> {
        return [.deviceTransportType, .deviceModel]
    }
    
    func deviceProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionDeviceProperties {
        let deviceProperties = CMIOExtensionDeviceProperties(dictionary: [:])
        if properties.contains(.deviceTransportType) {
            deviceProperties.transportType = kIOAudioDeviceTransportTypeVirtual
        }
        if properties.contains(.deviceModel) {
            deviceProperties.model = "FaceSwap Camera Model"
        }
        return deviceProperties
    }
    
    func setDeviceProperties(_ deviceProperties: CMIOExtensionDeviceProperties) throws {
        // Handle property changes
    }
    
    func startStreaming() {
        guard let _ = _streamSource else {
            return
        }
        
        _streamingCounter += 1
        
        _streamSource.startStreaming()
    }
    
    func stopStreaming() {
        guard _streamingCounter > 0 else {
            return
        }
        
        _streamingCounter -= 1
        
        if _streamingCounter == 0 {
            _streamSource.stopStreaming()
        }
    }
    
    private func createFormatDescription(dims: CMVideoDimensions, pixelFormat: OSType) -> CMFormatDescription {
        var formatDescription: CMFormatDescription?
        let err = CMVideoFormatDescriptionCreate(allocator: kCFAllocatorDefault, codecType: pixelFormat, width: dims.width, height: dims.height, extensions: nil, formatDescriptionOut: &formatDescription)
        guard err == noErr else {
            fatalError("Error creating format description: \(err)")
        }
        return formatDescription!
    }
}

