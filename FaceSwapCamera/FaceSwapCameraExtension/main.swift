import Foundation
import CoreMediaIO

let providerSource = ExtensionProvider(clientQueue: nil)

CMIOExtensionProvider.startService(provider: providerSource.provider)

CFRunLoopRun()

