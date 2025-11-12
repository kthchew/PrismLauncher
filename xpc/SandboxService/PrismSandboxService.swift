// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2025 Kenneth Chew <79120643+kthchew@users.noreply.github.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import AppKit
import CryptoKit
import Foundation
import UniformTypeIdentifiers
import System
import os

// workaround for imported macro not working when using cmake
fileprivate let LC_MAIN = 0x28 | 0x80000000

struct MojangManifest: Codable {
    let files: [String: MojangManifestFileInfo]
}

struct MojangManifestFileInfo: Codable {
    let type: ManifestFileType
    /// Only present if `type` is `link`.
    let target: String?
    /// Only present if `type` is `file`.
    let executable: Bool?
    /// Only present if `type` is `file`.
    let downloads: [String: DownloadInfo]?

    enum ManifestFileType: String, Codable {
        case directory = "directory"
        case link = "link"
        case file = "file"
    }

    struct DownloadInfo: Codable {
        let sha1: String
        let size: Int
        let url: String
    }
}

/// This object implements the protocol which we have defined. It provides the actual behavior for the service. It is 'exported' by the service to make it available to the process hosting the service over an NSXPCConnection.
public class PrismSandboxService: NSObject, SandboxServiceProtocol {
    static let logger = Logger(subsystem: "org.prismlauncher.PrismLauncher.PrismSandboxService", category: "XPC")
    
    private func removeQuarantineForFile(at url: inout URL) throws {
        var values = URLResourceValues()
        values.quarantineProperties = nil
        try url.setResourceValues(values)
        
        // Apple says to do the above, but it instead puts some kind of "quarantine removed" flag on some systems rather than just removing it.
        // There's a macOS bug (?) that causes Gatekeeper to still deny a dynamic library with such an attribute from loading. (FB15970881)
        // Removing the xattr directly works around this issue, though this isn't ideal since `com.apple.quarantine` is not technically API
        let quarantineAfter = try url.resourceValues(forKeys: [.quarantinePropertiesKey])
        if quarantineAfter.quarantineProperties != nil {
            let path = url.path
            let removalResult = removexattr(path, "com.apple.quarantine", XATTR_NOFOLLOW)
            if removalResult != 0 {
                Self.logger.log("Couldn't remove com.apple.quarantine on \(path) directly")
                // don't throw here
                // failure here but not above probably means Apple changed how quarantine is stored, i.e. not com.apple.quarantine
                // hopefully by then (if that ever happens) the need for this workaround won't exist anymore anyway
            }
        }
    }
    
    private func shouldRemoveQuarantineFromLibrary(at url: URL) -> Bool {
        do {
            let resourceValues = try url.resourceValues(forKeys: [.isRegularFileKey, .isApplicationKey, .isPackageKey, .quarantinePropertiesKey, .contentTypeKey])
            
            // Avoid unquarantining directories (such as bundles, which can include applications).
            guard let isRegularFile = resourceValues.isRegularFile, isRegularFile else {
                return false
            }
            
            // Pretty sure the regular file check should handle this, but just in case:
            guard let isApplication = resourceValues.isApplication, !isApplication else {
                return false
            }
            guard let isPackage = resourceValues.isPackage, !isPackage else {
                return false
            }
            
            // Ignore the file if it is not quarantined.
            if let quarantine = resourceValues.quarantineProperties {
                let type = quarantine[kLSQuarantineTypeKey as String]
                guard type != nil else {
                    return false
                }
            }
            
            // Check if the Mach-O binary file has a `main` method (would it execute something if it were run itself?).
            // It might be unsafe in that case, and is uncommon for dynamic libraries.
            // (if this isn't a Mach-O file, this returns YES, and thus we don't remove quarantine - there's no valid reason to remove quarantine
            // from a non-Mach-O file)
            // NOTE: this check might be too strict - should this be kept or relaxed? It seems to be ok for the vanilla game at least
            guard !entrypointExistsInExecutable(at: url) else {
                return false
            }
            
            // If the "Open With" attribute on a file has been changed, that could potentially be dangerous. Only unquarantine a file if this
            // attribute is not set to a non-default value. Note that sandboxed processes can't choose to open a file in an app other than the
            // default app for that file, an app that declares it can open that file type, or certain "safe" apps (like TextEdit).
            guard let type = resourceValues.contentType else {
                return false
            }
            guard NSWorkspace.shared.urlForApplication(toOpen: url) == NSWorkspace.shared.urlForApplication(toOpen: type) else {
                return false
            }
            
            let allowedExtensions: Set<String> = ["", "dylib", "tmp", "jnilib", "so"]
            return allowedExtensions.contains(url.pathExtension)
        } catch {
            return false
        }
    }
    
    private func entrypointExistsInExecutable(at url: URL) -> Bool {
        // read and interpret the Mach-O header and file data directly
        // note: generally there's a command line tool you can use to check this, e.g. `/usr/bin/otool`, but that requires a separate process,
        // the output on that is not guaranteed to be stable, and it's unknown if it's on the system by default for all supported macOS versions
        // luckily, there's public API (mach-o/*.h) to help us parse the format and do this ourselves, though it's quite messy
        do {
            let fileHandle = try FileHandle(forReadingFrom: url)
            guard let fatHeaderData = try fileHandle.read(upToCount: MemoryLayout<fat_header>.size), fatHeaderData.count == MemoryLayout<fat_header>.size else {
                return true
            }
            let fatHeader = fatHeaderData.withUnsafeBytes { ptr in
                ptr.load(as: fat_header.self)
            }

            let archOffset = MemoryLayout<fat_header>.size
            // anything declared in mach-o/fat.h is always stored on disk in big-endian order
            var archSize: Int
            let numArch = fatHeader.nfat_arch.bigEndian;

            if fatHeader.magic == FAT_MAGIC || fatHeader.magic == FAT_CIGAM {
                archSize = MemoryLayout<fat_arch>.size
            } else if fatHeader.magic == FAT_MAGIC_64 || fatHeader.magic == FAT_CIGAM_64 {
                archSize = MemoryLayout<fat_arch_64>.size
            } else if fatHeader.magic == MH_MAGIC || fatHeader.magic == MH_CIGAM || fatHeader.magic == MH_MAGIC_64 || fatHeader.magic == MH_CIGAM_64 {
                // not a "fat" binary, just check the single architecture that is there
                return try entrypointExistsInFileHandle(fileHandle, at: 0)
            } else {
                // no idea what this file is - just say there's an entrypoint to be safe
                return true;
            }

            try fileHandle.seek(toOffset: UInt64(archOffset))
            guard let archData = try fileHandle.read(upToCount: archSize * Int(numArch)), archData.count == archSize * Int(numArch) else {
                return true
            }

            let foundEntrypoint = try archData.withUnsafeBytes { buf in
                let newBuf = buf.bindMemory(to: fat_arch.self)

                for arch in newBuf {
                    let offset = arch.offset.bigEndian
                    if try entrypointExistsInFileHandle(fileHandle, at: Int(offset)) {
                        return true
                    }
                }
                return false
            }

            return foundEntrypoint

        } catch {
            Self.logger.log("Failed to read file at \(url)")
            return true
        }
    }

    private func entrypointExistsInFileHandle(_ fileHandle: FileHandle, at offset: Int) throws -> Bool {
        try fileHandle.seek(toOffset: UInt64(offset))
        guard let headerData = try fileHandle.read(upToCount: MemoryLayout<mach_header>.size), headerData.count == MemoryLayout<mach_header>.size else {
            return true
        }
        let header = headerData.withUnsafeBytes { ptr in
            ptr.load(as: mach_header.self)
        }

        var cmdOffset: Int
        var endiannessReversed: Bool

        if header.magic == MH_MAGIC_64 || header.magic == MH_CIGAM_64 {
            cmdOffset = MemoryLayout<mach_header_64>.size
            endiannessReversed = header.magic == MH_CIGAM_64
        } else if header.magic == MH_MAGIC || header.magic == MH_CIGAM {
            cmdOffset = MemoryLayout<mach_header>.size
            endiannessReversed = header.magic == MH_CIGAM
        } else {
            // don't know what kind of binary this is, just assume this has an entry point (and thus deny quarantine removal)
            return true
        }

        let ncmds = endiannessReversed ? header.ncmds.byteSwapped : header.ncmds

        for _ in 0..<ncmds {
            try fileHandle.seek(toOffset: UInt64(offset + cmdOffset))
            guard let cmdData = try fileHandle.read(upToCount: MemoryLayout<load_command>.size), cmdData.count == MemoryLayout<load_command>.size else {
                return true
            }

            let cmd = cmdData.withUnsafeBytes { buf in
                buf.load(as: load_command.self)
            }
            let cmdType = endiannessReversed ? cmd.cmd.byteSwapped : cmd.cmd
            // found an entrypoint
            if cmdType == LC_MAIN || cmdType == LC_UNIXTHREAD {
                return true
            }

            let cmdSize = endiannessReversed ? cmd.cmdsize.byteSwapped : cmd.cmdsize
            cmdOffset += Int(cmdSize)
        }

        return false
    }
    
    private func verifyJavaRuntime(at url: URL, againstFileManifest fileManifest: [String: MojangManifestFileInfo]) -> Bool {
        // Check if there are any files in the directory that are not listed in the manifest - these might be malicious.
        guard let enumerator = FileManager.default.enumerator(at: url, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey, .isDirectoryKey]) else {
            return false
        }
        var verifiedFiles = Set<String>(minimumCapacity: fileManifest.count)
        verifiedFiles.insert(url.lastPathComponent)
        let basePath = FilePath(url.standardizedFileURL.path).removingLastComponent()
        
        for case let fileURL as URL in enumerator {
            var filePath = FilePath(fileURL.standardizedFileURL.path)
            let _ = filePath.removePrefix(basePath)
            
            guard let fileInfo = fileManifest[filePath.string] else {
                Self.logger.log("Java runtime not verified due to extraneous file not listed in manifest: \(filePath.string, privacy: .public)")
                return false
            }
            
            switch fileInfo.type {
            case .directory:
                guard let isDirectory = try? fileURL.resourceValues(forKeys: [.isDirectoryKey]).isDirectory, isDirectory else {
                    Self.logger.log("Java runtime not verified as \(filePath) is not a directory")
                    return false
                }
            case .file:
                guard let isFile = try? fileURL.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile, isFile else {
                    Self.logger.log("Java runtime not verified as \(filePath) is not a regular file")
                    return false
                }
                
                guard let expectedChecksum = fileInfo.downloads?["raw"]?.sha1 else {
                    return false
                }
                guard let fileData = try? Data(contentsOf: fileURL), fileData.count == fileInfo.downloads?["raw"]?.size else {
                    Self.logger.log("Java runtime not verified due to size mismatch for file: \(filePath)")
                    return false
                }
                
                let actualChecksum = Insecure.SHA1.hash(data: fileData)
                    .compactMap { byte in
                        String(format: "%02x", byte)
                    }
                    .joined()
                
                guard expectedChecksum == actualChecksum else {
                    Self.logger.log("Java runtime not verified due to checksum mismatch for file: \(filePath)")
                    return false
                }
            case .link:
                guard let isLink = try? fileURL.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink, isLink else {
                    Self.logger.log("Java runtime not verified as \(filePath) is not a symbolic link")
                    return false
                }
            }
            
            verifiedFiles.insert(filePath.string)
        }
        
        let missingFiles = fileManifest.keys.filter { expectedFile in
            !verifiedFiles.contains(expectedFile)
        }
        guard missingFiles.isEmpty else {
            Self.logger.log("Java runtime not verified due to missing files: \(missingFiles.joined(separator: ", "), privacy: .public)")
            return false
        }
        
        return true
    }
    
    @objc public func removeQuarantineFromLibrary(at url: URL, with reply: @escaping (Bool, URL?) -> Void) {
        Self.logger.log("removeQuarantineFromLibrary")
        do {
            let tempDir = try FileManager.default.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: url, create: true)
            var unquarantinedCopyURL = tempDir.appendingPathComponent(url.lastPathComponent)
            let unquarantinedCopyPath = unquarantinedCopyURL.path
            try FileManager.default.copyItem(at: url, to: unquarantinedCopyURL)
            
            guard shouldRemoveQuarantineFromLibrary(at: unquarantinedCopyURL) else {
                reply(false, nil)
                return
            }
            
            // Clear the executable bit on the file to prevent a malicious item from being allowed to execute in Terminal.
            let attributes = try FileManager.default.attributesOfItem(atPath: unquarantinedCopyPath)
            let newPosixPerms = (attributes[.posixPermissions] as! NSNumber).int16Value & 0o0666
            let newAttributes: [FileAttributeKey : Any] = [.posixPermissions: newPosixPerms]
            try FileManager.default.setAttributes(newAttributes, ofItemAtPath: unquarantinedCopyPath)
            
            // Now that it is safe to do so, remove quarantine.
            try removeQuarantineForFile(at: &unquarantinedCopyURL)
            // Put the file back where it originally was.
            let result = try FileManager.default.replaceItemAt(url, withItemAt: unquarantinedCopyURL, options: .usingNewMetadataOnly)
            reply(true, result)
            return
        } catch {
            Self.logger.log("Did not remove quarantine from library at \(url): \(error.localizedDescription)")
            reply(false, nil)
            return
        }
    }
    
    @objc public func removeQuarantineFromJavaInstall(at url: URL, downloadedFromManifestAt manifestURL: URL, with reply: @escaping (Bool) -> Void) {
        Self.logger.log("removeQuarantineFromJavaInstall, \(url, privacy: .public) \(manifestURL, privacy: .public)")
        guard manifestURL.scheme == "https" && manifestURL.host == "piston-meta.mojang.com" else {
            Self.logger.log("Invalid manifest URL: \(manifestURL)")
            reply(false)
            return
        }
        
        do {
            // Copy the directory to a temporary location outside the sandbox, so the sandboxed code can't interfere with the below operations.
            let tempDir = try FileManager.default.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: url, create: true)
            var unquarantinedCopyURL = tempDir.appendingPathComponent(url.lastPathComponent)
            try FileManager.default.copyItem(at: url, to: unquarantinedCopyURL)
            
            Task {
                do {
                    let (downloadedManifestURL, _) = try await URLSession.shared.download(from: manifestURL)
                    let data = try Data(contentsOf: downloadedManifestURL)
                    let jsonDecoder = JSONDecoder()
                    let manifest = try jsonDecoder.decode(MojangManifest.self, from: data)
                    let files = manifest.files

                    let verificationResult = verifyJavaRuntime(at: unquarantinedCopyURL, againstFileManifest: files)
                    if !verificationResult {
                        reply(false)
                        Self.logger.log("Mojang java runtime couldn't be verified")
                        return
                    }

                    guard let enumerator = FileManager.default.enumerator(at: unquarantinedCopyURL, includingPropertiesForKeys: [.isRegularFileKey]) else {
                        reply(false)
                        return
                    }
                    while var fileURL = enumerator.nextObject() as? URL {
                        try removeQuarantineForFile(at: &fileURL)
                    }
                    try removeQuarantineForFile(at: &unquarantinedCopyURL)

                    let _ = try FileManager.default.replaceItemAt(url, withItemAt: unquarantinedCopyURL, options: .usingNewMetadataOnly)
                    reply(true)
                } catch {
                    Self.logger.log("Error occurred while verifying manifest: \(error.localizedDescription, privacy: .public)")
                    reply(false)
                    return
                }
            }
        } catch {
            Self.logger.log("Did not remove quarantine from Java install at \(url): \(error.localizedDescription)")
            reply(false)
            return
        }
    }
    
    @objc public func applyQuarantineToJavaInstall(at url: URL, with reply: @escaping (Bool) -> Void) {
        Self.logger.log("applyQuarantineToJavaInstall \(url, privacy: .public)")
        guard let enumerator = FileManager.default.enumerator(at: url, includingPropertiesForKeys: nil) else {
            reply(false)
            return
        }
        var values = URLResourceValues()
        values.quarantineProperties = [
            kLSQuarantineAgentNameKey as String: "Prism Launcher",
            kLSQuarantineTypeKey as String: kLSQuarantineTypeOtherDownload
        ]
        
        do {
            for case var fileURL as URL in enumerator {
                try fileURL.setResourceValues(values)
            }
        } catch {
            Self.logger.log("Couldn't apply quarantine: \(error)")
        }
        
        reply(true)
    }
    
    @objc public func getUnsandboxedUserTemporaryDirectory(with reply: @escaping (URL) -> Void) {
        reply(FileManager.default.temporaryDirectory)
    }
}
