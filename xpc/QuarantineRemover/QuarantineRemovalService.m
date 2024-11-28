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

#import "QuarantineRemovalService.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

bool shouldRemoveQuarantine(NSURL* url)
{
    if (![url.path hasPrefix:NSHomeDirectory()]) {
        return false;
    }
    // Avoid unquarantining directories (such as bundles, which can include applications).
    // TODO: this is denying things that need to be unquarantined
    /*NSNumber *isRegularFile;
    if (!([url getResourceValue:&isRegularFile forKey:NSURLIsRegularFileKey error:nil] && [isRegularFile boolValue])) {
        return false;
    }*/

    // If the "Open With" attribute on a file has been changed, that could potentially be dangerous. Only unquarantine a file if this
    // attribute is not set to a non-default value. Note that sandboxed processes can't choose to open a file in an app other than the
    // default app for that file.
    if (@available(macOS 12.0, *)) {
        NSString* typeIdentifier;
        [url getResourceValue:&typeIdentifier forKey:NSURLTypeIdentifierKey error:nil];
        if (typeIdentifier) {
            UTType* fileType = [UTType typeWithIdentifier:typeIdentifier];
            if ([[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:url] !=
                [[NSWorkspace sharedWorkspace] URLForApplicationToOpenContentType:fileType]) {
                return false;
            }
        }
    }

    NSSet<NSString*>* allowedExtensions = [[NSSet alloc] initWithArray:@[ @"", @"dylib", @"tmp", @"jnilib" ]];
    return [allowedExtensions containsObject:url.pathExtension];
}

@implementation QuarantineRemovalService
- (void)removeQuarantineFromFileAt:(NSString*)path withReply:(void (^)(BOOL*, NSString*))reply
{
    BOOL result = false;
    NSURL* url = [NSURL fileURLWithPath:path];
    if (!shouldRemoveQuarantine(url)) {
        reply(&result, path);
        return;
    }

    // Copy the file to a temporary location outside the sandbox, so the sandboxed code can't interfere with the below operations (for
    // example, trying to set the executable bit).
    NSError* err = nil;
    NSURL* unquarantinedCopyURL =
        [[[NSFileManager defaultManager] temporaryDirectory] URLByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
    [[NSFileManager defaultManager] copyItemAtURL:url toURL:unquarantinedCopyURL error:&err];
    if (err) {
        NSLog(@"An error occurred while copying the file to a temporary location: %@", [err localizedDescription]);
        reply(&result, path);
        return;
    }
    // Clear the executable bit on the file to prevent a malicious item from being allowed to execute in Terminal.
    NSDictionary<NSFileAttributeKey, id>* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:unquarantinedCopyURL.path
                                                                                                   error:&err];
    if (err) {
        NSLog(@"Couldn't read the file permissions: %@", [err localizedDescription]);
        reply(&result, path);
        return;
    }
    NSNumber* newPosixPerms = [NSNumber numberWithShort:[[attrs valueForKey:NSFilePosixPermissions] shortValue] & 0666];
    NSDictionary<NSFileAttributeKey, id>* newAttr = @{ NSFilePosixPermissions : newPosixPerms };
    [[NSFileManager defaultManager] setAttributes:newAttr ofItemAtPath:unquarantinedCopyURL.path error:&err];
    if (err) {
        NSLog(@"Couldn't remove executable bit: %@", [err localizedDescription]);
        reply(&result, path);
        return;
    }

    // Now that it is safe to do so, remove quarantine.
    [unquarantinedCopyURL setResourceValue:nil forKey:NSURLQuarantinePropertiesKey error:&err];
    if (err) {
        NSLog(@"Couldn't remove quarantine: %@", [err localizedDescription]);
        reply(&result, path);
        return;
    }
    // Apple says to do the above, but it instead puts some kind of "quarantine removed" flag on some systems rather than just removing it.
    // There's a macOS bug (?) that causes Gatekeeper to still deny a dynamic library with such an attribute from loading. (FB15970881)
    // Using xattr to remove the quarantine flag works around this issue, though this isn't ideal.
    NSDictionary<NSURLResourceKey, id>* quarantineAfter = [unquarantinedCopyURL resourceValuesForKeys:@[ NSURLQuarantinePropertiesKey ]
                                                                                                error:nil];
    if (quarantineAfter[NSURLQuarantinePropertiesKey] != nil) {
        NSTask* task = [[NSTask alloc] init];
        [task setLaunchPath:@"/usr/bin/xattr"];
        [task setArguments:@[ @"-d", @"com.apple.quarantine", unquarantinedCopyURL.path ]];
        [task launch];
        [task waitUntilExit];
        if ([task terminationStatus] != 0) {
            NSLog(@"Couldn't remove quarantine: %@", [err localizedDescription]);
            reply(&result, path);
            return;
        }
    }

    // Put the file back where it originally was.
    // FIXME: can error if source and destination are on different volumes - need to handle
    [[NSFileManager defaultManager] replaceItemAtURL:url
                                       withItemAtURL:unquarantinedCopyURL
                                      backupItemName:nil
                                             options:NSFileManagerItemReplacementUsingNewMetadataOnly
                                    resultingItemURL:nil
                                               error:&err];
    if (err) {
        NSLog(@"Couldn't copy back: %@", [err localizedDescription]);
        reply(&result, path);
        return;
    } else {
        result = true;
        reply(&result, path);
    }
}

@end
