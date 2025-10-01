import CoreMediaIO
import Foundation
import IOKit.audio
import os.log

class ExtensionStreamSource: NSObject, CMIOExtensionStreamSource {
    
    private(set) var stream: CMIOExtensionStream!
    
    let device: CMIOExtensionDevice
    
    private var _streamFormat: CMIOExtensionStreamFormat
    
    private var _activeFormatIndex: Int = 0
    
    private var _availableFormats: [CMIOExtensionStreamFormat] = []
    
    private var _streaming: Bool = false
    
    private var _timer: DispatchSourceTimer?
    
    private let _timerQueue = DispatchQueue(label: "com.yourcompany.FaceSwapCamera.timerQueue", qos: .userInteractive, attributes: [], autoreleaseFrequency: .workItem, target: .global(qos: .userInteractive))
    
    private var _videoDescription: CMFormatDescription!
    
    private var _bufferPool: CVPixelBufferPool!
    
    private var _bufferAuxAttributes: NSDictionary!
    
    private var _whiteStripeStartRow: UInt32 = 0
    
    private var _whiteStripeIsAscending: Bool = true
    
    init(localizedName: String, streamID: UUID, streamFormat: CMIOExtensionStreamFormat, device: CMIOExtensionDevice) {
        self.device = device
        self._streamFormat = streamFormat
        self._availableFormats = [streamFormat]
        super.init()
        
        self.stream = CMIOExtensionStream(localizedName: localizedName, streamID: streamID, direction: .source, clockType: .hostTime, source: self)
        
        initializeBufferPool()
    }
    
    private func initializeBufferPool() {
        let dims = CMVideoFormatDescriptionGetDimensions(_streamFormat.formatDescription)
        let pixelFormat = CMFormatDescriptionGetMediaSubType(_streamFormat.formatDescription)
        
        _videoDescription = _streamFormat.formatDescription
        
        let pixelBufferAttributes: NSDictionary = [
            kCVPixelBufferWidthKey: dims.width,
            kCVPixelBufferHeightKey: dims.height,
            kCVPixelBufferPixelFormatTypeKey: pixelFormat,
            kCVPixelBufferIOSurfacePropertiesKey: [:]
        ]
        
        CVPixelBufferPoolCreate(kCFAllocatorDefault, nil, pixelBufferAttributes, &_bufferPool)
        
        let auxAttributes: NSDictionary = [kCVPixelBufferPoolAllocationThresholdKey: 5]
        _bufferAuxAttributes = auxAttributes
    }
    
    var availableProperties: Set<CMIOExtensionProperty> {
        return [.streamActiveFormatIndex, .streamFrameDuration]
    }
    
    var formats: [CMIOExtensionStreamFormat] {
        return _availableFormats
    }
    
    func streamProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionStreamProperties {
        let streamProperties = CMIOExtensionStreamProperties(dictionary: [:])
        if properties.contains(.streamActiveFormatIndex) {
            streamProperties.activeFormatIndex = 0
        }
        if properties.contains(.streamFrameDuration) {
            let frameDuration = CMTime(value: 1, timescale: Int32(kFrameRate))
            streamProperties.frameDuration = frameDuration
        }
        return streamProperties
    }
    
    func setStreamProperties(_ streamProperties: CMIOExtensionStreamProperties) throws {
        // Handle property changes
    }
    
    func authorizedToStartStream(for client: CMIOExtensionClient) -> Bool {
        return true
    }
    
    func startStream() throws {
        guard !_streaming else {
            return
        }
        
        _streaming = true
        startStreamingFrames()
    }
    
    func stopStream() throws {
        guard _streaming else {
            return
        }
        
        _streaming = false
        stopStreamingFrames()
    }
    
    func startStreaming() {
        if _streaming {
            startStreamingFrames()
        }
    }
    
    func stopStreaming() {
        stopStreamingFrames()
    }
    
    private func startStreamingFrames() {
        _timer = DispatchSource.makeTimerSource(flags: .strict, queue: _timerQueue)
        _timer!.schedule(deadline: .now(), repeating: 1.0 / Double(kFrameRate), leeway: .seconds(0))
        
        _timer!.setEventHandler { [weak self] in
            self?.generateFrame()
        }
        
        _timer!.resume()
    }
    
    private func stopStreamingFrames() {
        _timer?.cancel()
        _timer = nil
    }
    
    private func generateFrame() {
        var err: OSStatus = 0
        var pixelBuffer: CVPixelBuffer?
        
        err = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(kCFAllocatorDefault, _bufferPool, _bufferAuxAttributes, &pixelBuffer)
        if err != 0 {
            os_log(.error, "Failed to create pixel buffer: %d", err)
            return
        }
        
        guard let pixelBuffer = pixelBuffer else {
            return
        }
        
        CVPixelBufferLockBaseAddress(pixelBuffer, [])
        
        let bufferPtr = CVPixelBufferGetBaseAddress(pixelBuffer)!
        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        let rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer)
        
        memset(bufferPtr, 0, rowBytes * height)
        
        // Draw a white stripe that moves vertically
        let stripeStart = Int(_whiteStripeStartRow)
        let stripeEnd = min(stripeStart + kWhiteStripeHeight, height)
        
        for y in stripeStart..<stripeEnd {
            let rowPtr = bufferPtr.advanced(by: y * rowBytes)
            memset(rowPtr, 255, width * 4)
        }
        
        // Update stripe position
        if _whiteStripeIsAscending {
            _whiteStripeStartRow += 1
            if _whiteStripeStartRow + UInt32(kWhiteStripeHeight) >= height {
                _whiteStripeIsAscending = false
            }
        } else {
            if _whiteStripeStartRow > 0 {
                _whiteStripeStartRow -= 1
            } else {
                _whiteStripeIsAscending = true
            }
        }
        
        CVPixelBufferUnlockBaseAddress(pixelBuffer, [])
        
        let now = CMClockGetTime(CMClockGetHostTimeClock())
        let frameDuration = CMTime(value: 1, timescale: Int32(kFrameRate))
        
        var timingInfo = CMSampleTimingInfo(
            duration: frameDuration,
            presentationTimeStamp: now,
            decodeTimeStamp: .invalid
        )
        
        var sampleBuffer: CMSampleBuffer?
        err = CMSampleBufferCreateForImageBuffer(allocator: kCFAllocatorDefault, imageBuffer: pixelBuffer, dataReady: true, makeDataReadyCallback: nil, refcon: nil, formatDescription: _videoDescription, sampleTiming: &timingInfo, sampleBufferOut: &sampleBuffer)
        
        if err == 0, let sampleBuffer = sampleBuffer {
            stream.send(sampleBuffer, discontinuity: [], hostTimeInNanoseconds: UInt64(now.seconds * Double(NSEC_PER_SEC)))
        }
    }
}

