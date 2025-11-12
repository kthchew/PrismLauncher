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

#include <qglobal.h>
#ifdef Q_OS_MACOS
#include <Foundation/Foundation.h>
#include <xpc/xpc.h>

#include "SandboxProtocol.h"
#include "XPCManager.h"

@interface XPCManagerInternal : NSObject
@property(retain) NSXPCConnection* m_connectionToService;

- (instancetype)init;
@end

@implementation XPCManagerInternal

- (instancetype)init
{
    self = [super init];
    if (!self) {
        return nil;
    }

    _m_connectionToService = [[NSXPCConnection alloc] initWithServiceName:@"org.prismlauncher.PrismLauncher.PrismSandboxService"];
    _m_connectionToService.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(SandboxServiceProtocol)];
    [_m_connectionToService resume];

    return self;
}

@end

std::pair<bool, std::string> XPCManager::askToRemoveQuarantine(char* path)
{
    __block std::pair<bool, std::string> result;
    dispatch_group_t syncGroup = dispatch_group_create();
    dispatch_group_enter(syncGroup);
    dispatch_time_t waitTime = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC * 5));

    NSString* pathStr = [NSString stringWithUTF8String:path];
    NSURL* targetURL = [NSURL fileURLWithPath:pathStr];
    NSLog(@"Asking to remove quarantine from file at path: %@", pathStr);
    id proxy = [[m_internal m_connectionToService] remoteObjectProxyWithErrorHandler:^(NSError* _Nonnull error) {
      NSLog(@"Error occurred while contacting XPC service: %@", error);
      result = std::make_pair(false, std::string(path));
      dispatch_group_leave(syncGroup);
    }];
    [proxy removeQuarantineFromLibraryAt:targetURL
                                    with:^(BOOL ok, NSURL* url) {
                                      NSLog(@"Received response from XPC service: %d, %@", ok, url);
                                      result = ok ? std::make_pair(ok, std::string([url absoluteString].UTF8String)) : std::make_pair(ok, "");
                                      dispatch_group_leave(syncGroup);
                                    }];

    // LWJGL 2 may load openal.dylib, and for... reasons... that load isn't intercepted, so just hardcode that case here preemptively
    if ([pathStr hasSuffix:@"liblwjgl.dylib"]) {
        NSLog(@"Asking to remove quarantine from file at path (LWJGL 2 workaround): %@", pathStr);
        dispatch_group_enter(syncGroup);
        NSString* openalPath = [pathStr stringByReplacingOccurrencesOfString:@"liblwjgl.dylib" withString:@"openal.dylib"];
        NSURL* openalURL = [NSURL fileURLWithPath:openalPath];
        [proxy removeQuarantineFromLibraryAt:openalURL
                                        with:^(BOOL ok, NSURL* url) {
                                          NSLog(@"Received response from XPC service: %d, %@", ok, url);
                                          dispatch_group_leave(syncGroup);
                                        }];
    }

    dispatch_group_wait(syncGroup, waitTime);
    return result;
}

bool XPCManager::removeQuarantineFromMojangJavaDirectory(NSURL* url, NSURL* manifestURL)
{
    __block bool result = false;
    dispatch_group_t syncGroup = dispatch_group_create();
    dispatch_group_enter(syncGroup);
    dispatch_time_t waitTime = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC * 5));

    id proxy = [[m_internal m_connectionToService] remoteObjectProxyWithErrorHandler:^(NSError* _Nonnull error) {
      NSLog(@"Error occurred while contacting XPC service: %@", error);
      result = false;
      dispatch_group_leave(syncGroup);
    }];

    [proxy removeQuarantineFromJavaInstallAt:url
                               downloadedFromManifestAt:manifestURL
                                                   with:^(BOOL ok) {
                                                     NSLog(@"Received response from XPC service: %d", ok);
                                                     result = ok;
                                                     dispatch_group_leave(syncGroup);
                                                   }];

    dispatch_group_wait(syncGroup, waitTime);
    return result;
}

bool XPCManager::applyDownloadQuarantineToDirectory(NSURL* url)
{
    __block bool result = false;
    dispatch_group_t syncGroup = dispatch_group_create();
    dispatch_group_enter(syncGroup);
    dispatch_time_t waitTime = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC * 10));

    id proxy = [[m_internal m_connectionToService] remoteObjectProxyWithErrorHandler:^(NSError* _Nonnull error) {
      NSLog(@"Error occurred while contacting XPC service: %@", error);
      result = false;
      dispatch_group_leave(syncGroup);
    }];
    [proxy applyQuarantineToJavaInstallAt:url
                                     with:^(BOOL ok) {
                                       NSLog(@"Received response from XPC service: %d", ok);
                                       result = ok;
                                       dispatch_group_leave(syncGroup);
                                     }];

    dispatch_group_wait(syncGroup, waitTime);
    return result;
}

QString XPCManager::getUnsandboxedTemporaryDirectory()
{
    __block QString result;
    dispatch_group_t syncGroup = dispatch_group_create();
    dispatch_group_enter(syncGroup);

    dispatch_time_t waitTime = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC * 5));

    id proxy = [[m_internal m_connectionToService] remoteObjectProxyWithErrorHandler:^(NSError* _Nonnull error) {
      NSLog(@"Error occurred while contacting XPC service: %@", error);
      result = "";
      dispatch_group_leave(syncGroup);
    }];
    [proxy getUnsandboxedUserTemporaryDirectoryWith:^(NSURL* url) {
      result = QString::fromNSString([url path]);
      dispatch_group_leave(syncGroup);
    }];

    dispatch_group_wait(syncGroup, waitTime);
    return result;
}

XPCManager::XPCManager()
{
    m_internal = [[XPCManagerInternal alloc] init];
}
#endif
