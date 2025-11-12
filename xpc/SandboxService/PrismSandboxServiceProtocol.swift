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

import Foundation

/// The protocol that this service will vend as its API. This protocol will also need to be visible to the process hosting the service.
@objc public protocol SandboxServiceProtocol {
    /// Ask the service to remove quarantine from the file at `url`.
    ///
    /// Some metadata of the file may be modified to prevent a sandbox escape. For example, the executable bit on a file may be removed.
    /// - Parameters:
    ///   - url: The path of the file to remove quarantine for.
    ///   - reply: A boolean indicating whether quarantine was removed, and the path of the unquarantined item. Note that `NO` doesn't necessarily mean the file is currently quarantined.
    func removeQuarantineFromLibrary(at url: URL, with reply: @escaping (Bool, URL?) -> Void)
    /// Ask the service to remove quarantine from the directory at `path`. The directory is intended to be a Java runtime downloaded from the
    /// given manifest. The manifest must come from Mojang (piston-meta.mojang.com) and all files inside the directory must match the checksums.
    ///
    /// - Parameters:
    ///   - url: The path of a directory containing a Java runtime to remove quarantine for.
    ///   - manifestURL: A URL to a Mojang manifest that the Java runtime was downloaded from.
    ///   - reply: A boolean indicating whether quarantine was removed.
    func removeQuarantineFromJavaInstall(at url: URL, downloadedFromManifestAt manifestURL: URL, with reply: @escaping (Bool) -> Void)
    /// Apply a quarantine to all files at the provided `path` that indicates that the files were downloaded from the Internet. Unlike the
    /// typical sandbox quarantine applied by default, a download quarantine allows executables to run if they are able to get past
    /// Gatekeeper.
    ///
    /// - Parameters:
    ///   - url: The path of a directory containing files to apply quarantine to.
    ///   - reply: A boolean indicating whether quarantine was applied.
    func applyQuarantineToJavaInstall(at url: URL, with reply: @escaping (Bool) -> Void)
    /// Get the user's temporary directory for a nonsandboxed process.
    ///
    /// \param reply The path of the temporary directory.
    func getUnsandboxedUserTemporaryDirectory(with reply: @escaping (URL) -> Void)
}
