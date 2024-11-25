// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Kenneth Chew <79120643+kthchew@users.noreply.github.com>
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

#import <Foundation/Foundation.h>

@protocol QuarantineRemovalServiceProtocol
/// Ask the service to remove quarantine from the file at `path`.
///
/// Some metadata of the file may be modified to prevent a sandbox escape. For example, the executable bit on a file may be removed.
/// - Parameters:
///   - path: The path of the file to remove quarantine for.
///   - reply: A boolean indicating whether quarantine was removed, and the path of the unquarantined item. Note that `NO` doesn't necessarily mean the file is currently quarantined.
- (void)removeQuarantineFromFileAt:(NSString *)path withReply:(void (^)(BOOL *, NSString *))reply;
@end
