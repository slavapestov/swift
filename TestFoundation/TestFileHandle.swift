// This source file is part of the Swift.org open source project
//
// Copyright (c) 2016, 2018, 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//

#if !DARWIN_COMPATIBILITY_TESTS // Disable until Foundation has the new FileHandle API

import Dispatch

class TestFileHandle : XCTestCase {
    var allHandles: [FileHandle] = []
    var allTemporaryFileURLs: [URL] = []
    
    let content: Data = {
        return """
        CHAPTER I.

        The Author gives some account of himself and family--His first
        inducements to travel--He is shipwrecked, and swims for his life--Gets
        safe on shore in the country of Lilliput--Is made a prisoner, and
        carried up the country

        CHAPTER II.

        The emperor of Lilliput, attended by several of the nobility, comes to
        see the Author in his confinement--The emperor's person and habits
        described--Learned men appointed to teach the Author their language--He
        gains favor by his mild disposition--His pockets are searched, and his
        sword and pistols taken from him

        CHAPTER III.

        The Author diverts the emperor, and his nobility of both sexes, in a
        very uncommon manner--The diversions of the court of Lilliput
        described--The Author has his liberty granted him upon certain
        conditions

        CHAPTER IV.

        Mildendo, the metropolis of Lilliput, described, together with the
        emperor's palace--A conversation between the Author and a principal
        secretary concerning the affairs of that empire--The Author's offers to
        serve the emperor in his wars

        CHAPTER V.

        The Author, by an extraordinary stratagem, prevents an invasion--A high
        title of honor is conferred upon him--Ambassadors arrive from the
        emperor of Blefuscu, and sue for peace
        """.data(using: .utf8)!
    }()
    
    func createFileHandle() -> FileHandle {
        let url = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent(ProcessInfo.processInfo.globallyUniqueString)
        
        expectDoesNotThrow({ try content.write(to: url) }, "Couldn't write file at \(url.path) for testing")
        
        var fh: FileHandle?
        expectDoesNotThrow({ fh = try FileHandle(forReadingFrom: url) }, "Couldn't create file handle.")
        
        allHandles.append(fh!)
        allTemporaryFileURLs.append(url)
        return fh!
    }
    
    func createFileHandleForSeekErrors() -> FileHandle {
        var fds: [Int32] = [-1, -1]
        fds.withUnsafeMutableBufferPointer { (pointer) -> Void in
            pipe(pointer.baseAddress)
        }
        
        close(fds[1])
        
        let fh = FileHandle(fileDescriptor: fds[0], closeOnDealloc: true)
        allHandles.append(fh)
        return fh
    }
    
    let seekError = NSError(domain: NSCocoaErrorDomain, code: NSFileReadUnknownError, userInfo: [ NSUnderlyingErrorKey: NSError(domain: NSPOSIXErrorDomain, code: Int(ESPIPE), userInfo: [:])])
    
    func createFileHandleForReadErrors() -> FileHandle {
        // Create a file handle where calling read returns -1.
        // Accomplish this by creating one for a directory.
        let fd = open(".", O_RDONLY)
        expectTrue(fd > 0, "We must be able to open a fd to the current directory (.)")
        let fh = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        allHandles.append(fh)
        return fh
    }
    
    let readError = NSError(domain: NSCocoaErrorDomain, code: NSFileReadUnknownError, userInfo: [ NSUnderlyingErrorKey: NSError(domain: NSPOSIXErrorDomain, code: Int(EISDIR), userInfo: [:])])
    
    override func tearDown() {
        for handle in allHandles {
            print("Closing \(handle)…")
            try? handle.close()
        }
        
        for url in allTemporaryFileURLs {
            print("Deleting \(url)…")
            try? FileManager.default.removeItem(at: url)
        }
        
        allHandles = []
        allTemporaryFileURLs = []
    }
    
    func testHandleCreationAndCleanup() {
        _ = createFileHandle()
        _ = createFileHandleForSeekErrors()
        _ = createFileHandleForReadErrors()
    }
    
    func testReadUpToCount() {
        let handle = createFileHandle()
        
        // Zero:
        expectDoesNotThrow({
            let zeroData = try handle.read(upToCount: 0)
            expectEqual(zeroData, nil, "Data should be nil")
        }, "Must not throw while reading zero data")
        
        // Max:
        expectDoesNotThrow({
            let maxData = try handle.read(upToCount: Int.max)
            expectEqual(maxData, content, "Data should be equal to the content")
        }, "Must not throw while reading Int.max data")
        
        // EOF:
        expectDoesNotThrow({
            let eof = try handle.read(upToCount: Int.max)
            expectEqual(eof, nil, "EOF should return nil")
        }, "Must not throw while reading EOF")
        
        // One byte at a time
        let onesHandle = createFileHandle()
        expectDoesNotThrow({
            for index in content.indices {
                let oneByteData = try onesHandle.read(upToCount: 1)
                let expected = content[index ..< content.index(after: index)]
                expectEqual(oneByteData, expected, "Read incorrect data at index \(index)")
            }
        }, "Must not throw while reading one byte at a time")
        
        // EOF:
        expectDoesNotThrow({
            let eof = try handle.read(upToCount: 1)
            expectEqual(eof, nil, "EOF should return nil")
        }, "Must not throw while reading one-byte-at-a-time EOF")
        
        // Errors:
        expectThrows(readError, {
            _ = try createFileHandleForReadErrors().read(upToCount: 1)
        }, "Must throw when encountering a read error")
    }
    
    func testReadToEnd() {
        let handle = createFileHandle()
        
        // To end:
        expectDoesNotThrow({
            let maxData = try handle.readToEnd()
            expectEqual(maxData, content, "Data to end should equal what was written out")
        }, "Must not throw while reading to end")
        
        // EOF:
        expectDoesNotThrow({
            let eof = try handle.readToEnd()
            expectEqual(eof, nil, "EOF should return nil")
        }, "Must not throw while reading EOF")
        
        // Errors:
        expectThrows(readError, {
            _ = try createFileHandleForReadErrors().readToEnd()
        }, "Must throw when encountering a read error")
    }
    
    func testOffset() {
        // One byte at a time:
        let handle = createFileHandle()
        var offset: UInt64 = 0
        
        for index in content.indices {
            expectDoesNotThrow({ offset = try handle.offset() }, "Reading the offset must not throw")
            expectEqual(offset, UInt64(index), "The offset must match")
            expectDoesNotThrow({ _ = try handle.read(upToCount: 1) }, "Advancing by reading must not throw")
        }
        
        expectDoesNotThrow({ offset = try handle.offset() }, "Reading the offset at EOF must not throw")
        expectEqual(offset, UInt64(content.count), "The offset at EOF must be at the end")
        
        // Error:
        expectThrows(seekError, {
            _ = try createFileHandleForSeekErrors().offset()
        }, "Must throw when encountering a seek error")
    }
    
    func createPipe() -> Pipe {
        let pipe = Pipe()
        allHandles.append(pipe.fileHandleForWriting)
        allHandles.append(pipe.fileHandleForReading)
        return pipe
    }
    
    func performWriteTest<T: DataProtocol>(with data: T, expecting expectation: Data? = nil) {
        let pipe = createPipe()
        let writer = pipe.fileHandleForWriting
        let reader = pipe.fileHandleForReading
        
        expectDoesNotThrow({ try writer.write(contentsOf: data) }, "Writing must succeed")
        expectDoesNotThrow({
            expectEqual(try reader.read(upToCount: data.count), expectation ?? content, "The content must be the same")
        }, "Reading must succeed")
    }
    
    func testWritingWithData() {
        performWriteTest(with: content)
    }
    
    func testWritingWithBuffer() {
        content.withUnsafeBytes { (buffer) in
            performWriteTest(with: buffer)
        }
    }
    
    func testWritingWithMultiregionData() {
        var expectation = Data()
        expectation.append(content)
        expectation.append(content)
        expectation.append(content)
        expectation.append(content)
        
        content.withUnsafeBytes { (buffer) in
            let data1 = DispatchData(bytes: buffer)
            let data2 = DispatchData(bytes: buffer)
            
            var multiregion1: DispatchData = .empty
            multiregion1.append(data1)
            multiregion1.append(data2)
            
            var multiregion2: DispatchData = .empty
            multiregion2.append(data1)
            multiregion2.append(data2)
            
            var longMultiregion: DispatchData = .empty
            longMultiregion.append(multiregion1)
            longMultiregion.append(multiregion2)
            
            expectTrue(longMultiregion.regions.count > 0, "The multiregion data must be actually composed of multiple regions")
            
            performWriteTest(with: longMultiregion, expecting: expectation)
        }
    }

    func test_constants() {
        XCTAssertEqual(FileHandle.readCompletionNotification.rawValue, "NSFileHandleReadCompletionNotification",
                       "\(FileHandle.readCompletionNotification.rawValue) is not equal to NSFileHandleReadCompletionNotification")
    }

    func test_nullDevice() {
        let fh = FileHandle.nullDevice

        XCTAssertEqual(fh.fileDescriptor, -1)
        fh.closeFile()
        fh.seek(toFileOffset: 10)
        XCTAssertEqual(fh.offsetInFile, 0)
        XCTAssertEqual(fh.seekToEndOfFile(), 0)
        XCTAssertEqual(fh.readData(ofLength: 15).count, 0)
        fh.synchronizeFile()

        fh.write(Data([1,2]))
        fh.seek(toFileOffset: 0)
        XCTAssertEqual(fh.availableData.count, 0)
        fh.write(Data([1,2]))
        fh.seek(toFileOffset: 0)
        XCTAssertEqual(fh.readDataToEndOfFile().count, 0)
    }

    func test_truncateFile() {
        mkstemp(template: "test_truncateFile.XXXXXX") { (fh) in
            fh.truncateFile(atOffset: 50)
            XCTAssertEqual(fh.offsetInFile, 50)

            fh.truncateFile(atOffset: 0)
            XCTAssertEqual(fh.offsetInFile, 0)

            fh.truncateFile(atOffset: 100)
            XCTAssertEqual(fh.offsetInFile, 100)

            fh.write(Data([1, 2]))
            XCTAssertEqual(fh.offsetInFile, 102)

            fh.seek(toFileOffset: 4)
            XCTAssertEqual(fh.offsetInFile, 4)

            (0..<20).forEach { fh.write(Data([$0])) }
            XCTAssertEqual(fh.offsetInFile, 24)

            fh.seekToEndOfFile()
            XCTAssertEqual(fh.offsetInFile, 102)

            fh.truncateFile(atOffset: 10)
            XCTAssertEqual(fh.offsetInFile, 10)

            fh.seek(toFileOffset: 0)
            XCTAssertEqual(fh.offsetInFile, 0)

            let data = fh.readDataToEndOfFile()
            XCTAssertEqual(data.count, 10)
            XCTAssertEqual(data, Data([0, 0, 0, 0, 0, 1, 2, 3, 4, 5]))
        }
    }
    
    func test_readabilityHandlerCloseFileRace() throws {
        for _ in 0..<10 {
            let handle = createFileHandle()
            handle.readabilityHandler = { _ = $0.offsetInFile }
            handle.closeFile()
            Thread.sleep(forTimeInterval: 0.001)
        }
    }
    
    func test_readabilityHandlerCloseFileRaceWithError() throws {
        for _ in 0..<10 {
            let handle = createFileHandle()
            handle.readabilityHandler = { _ = try? $0.offset() }
            try handle.close()
            Thread.sleep(forTimeInterval: 0.001)
        }
    }
    
    func test_fileDescriptor() throws {
        let handle = createFileHandle()
        #if !os(Windows)
        XCTAssertTrue(handle.fileDescriptor > 0, "File descriptor after opening should be valid (is \(handle.fileDescriptor))")
        #endif
        
        try handle.close()
        XCTAssertEqual(handle.fileDescriptor, -1, "File descriptor after closing should not be valid")
    }
    
    func test_availableData() {
        let handle = createFileHandle()
        
        let availableData = handle.availableData
        XCTAssertEqual(availableData, content, "Available data should be the same as input")
        
        let eofData = handle.availableData
        XCTAssertTrue(eofData.isEmpty, "Should return empty data for EOF")
    }
    
    func test_readToEndOfFileInBackgroundAndNotify() {
        let handle = createFileHandle()
        let done = expectation(forNotification: .NSFileHandleReadToEndOfFileCompletion, object: handle, notificationCenter: .default) { (notification) -> Bool in
            XCTAssertEqual(notification.userInfo as? [String: AnyHashable], [NSFileHandleNotificationDataItem: self.content], "User info was incorrect")
            return true
        }
        
        handle.readToEndOfFileInBackgroundAndNotify()
        
        wait(for: [done], timeout: 10)
    }
    
    func test_readToEndOfFileAndNotify() {
        let handle = createFileHandle()
        var readSomeData = false
        
        let done = expectation(forNotification: FileHandle.readCompletionNotification, object: handle, notificationCenter: .default) { (notification) -> Bool in
            guard let data = notification.userInfo?[NSFileHandleNotificationDataItem] as? Data else {
                XCTFail("Couldn't find the data in the user info: \(notification)")
                return true
            }
            
            if !data.isEmpty {
                readSomeData = true
                handle.readInBackgroundAndNotify()
                return false
            } else {
                return true
            }
        }
        
        handle.readInBackgroundAndNotify()
        
        wait(for: [done], timeout: 10)
        XCTAssertTrue(readSomeData, "At least some data must've been read")
    }
    
    func test_readToEndOfFileAndNotify_readError() {
        let handle = createFileHandleForReadErrors()
        
        let done = expectation(forNotification: FileHandle.readCompletionNotification, object: handle, notificationCenter: .default) { (notification) -> Bool in
            guard let error = notification.userInfo?["NSFileHandleError"] as? NSNumber else {
                XCTFail("Couldn't find the data in the user info: \(notification)")
                return true
            }
            
            XCTAssertNil(notification.userInfo?[NSFileHandleNotificationDataItem])
            XCTAssertEqual(error, NSNumber(value: EISDIR))
            return true
        }
        
        handle.readInBackgroundAndNotify()
        
        wait(for: [done], timeout: 10)
    }
    
    func test_waitForDataInBackgroundAndNotify() {
        let handle = createFileHandle()
        let done = expectation(forNotification: .NSFileHandleDataAvailable, object: handle, notificationCenter: .default) { (notification) in
            let count = notification.userInfo?.count ?? 0
            XCTAssertEqual(count, 0)
            return true
        }
        
        handle.waitForDataInBackgroundAndNotify()
        
        wait(for: [done], timeout: 10)
    }
    
    func test_readWriteHandlers() {
        for _ in 0..<100 {
            let pipe = Pipe()
            let write = pipe.fileHandleForWriting
            let read = pipe.fileHandleForReading
            
            var notificationReceived = false
            let semaphore = DispatchSemaphore(value: 0)
            let count = content.count
            read.readabilityHandler = { (handle) in
                // Check that we can reentrantly set the handler:
                handle.readabilityHandler = { (handle2) in
                    if let readData = try? handle2.read(upToCount: count) {
                        XCTAssertEqual(readData.count, count, "Should have read as much data as was sent")
                        semaphore.signal()
                    } else {
                        // EOF:
                        handle2.readabilityHandler = nil
                    }
                }
                notificationReceived = true
                if let readData = try? handle.read(upToCount: count) {
                    XCTAssertEqual(readData.count, count, "Should have read as much data as was sent")
                }
            }
            
            write.writeabilityHandler = { (handle) in
                handle.writeabilityHandler = { (handle2) in
                    handle2.writeabilityHandler = nil
                    try? handle2.write(contentsOf: self.content)
                }
                try? handle.write(contentsOf: self.content)
            }
            
            let result = semaphore.wait(timeout: .distantFuture)
            XCTAssertEqual(result, .success, "Waiting on the semaphore should not have had time to time out")
            XCTAssertTrue(notificationReceived, "Notification should be sent")
        }
    }
    
    static var allTests : [(String, (TestFileHandle) -> () throws -> ())] {
        return [
            ("testHandleCreationAndCleanup", testHandleCreationAndCleanup),
            ("testReadUpToCount", testReadUpToCount),
            ("testReadToEnd", testReadToEnd),
            ("testOffset", testOffset),
            ("testWritingWithData", testWritingWithData),
            ("testWritingWithBuffer", testWritingWithBuffer),
            ("testWritingWithMultiregionData", testWritingWithMultiregionData),
            ("test_constants", test_constants),
            ("test_nullDevice", test_nullDevice),
            ("test_truncateFile", test_truncateFile),
            ("test_readabilityHandlerCloseFileRace", test_readabilityHandlerCloseFileRace),
            ("test_readabilityHandlerCloseFileRaceWithError", test_readabilityHandlerCloseFileRaceWithError),
            ("test_fileDescriptor", test_fileDescriptor),
            ("test_availableData", test_availableData),
            ("test_readToEndOfFileInBackgroundAndNotify", test_readToEndOfFileInBackgroundAndNotify),
            ("test_readToEndOfFileAndNotify", test_readToEndOfFileAndNotify),
            ("test_readToEndOfFileAndNotify_readError", test_readToEndOfFileAndNotify_readError),
            ("test_waitForDataInBackgroundAndNotify", test_waitForDataInBackgroundAndNotify),
            ("test_readWriteHandlers", test_readWriteHandlers),
        ]
    }
}

#endif
