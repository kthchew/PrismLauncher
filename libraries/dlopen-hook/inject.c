#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syslimits.h>
#include <sys/un.h>
#include <unistd.h>

// See https://github.com/apple-opensource/dyld/blob/e3f88907bebb8421f50f0943595f6874de70ebe0/include/mach-o/dyld.h
#define DYLD_INTERPOSE(_replacment, _replacee)                                                                            \
    __attribute__((used)) static struct {                                                                                 \
        const void* replacment;                                                                                           \
        const void* replacee;                                                                                             \
    } _interpose_##_replacee __attribute__((section("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, \
                                                                                (const void*)(unsigned long)&_replacee };

void* dlopen_new(const char* path, int mode)
{
    // skip for a few common system library locations, to avoid the overhead of sending the path over IPC
    const char* system = "/System";
    const char* library = "/Library";
    const char* usr = "/usr";
    if (path == NULL || strncmp(system, path, strlen(system)) == 0 || strncmp(library, path, strlen(library)) == 0 ||
        strncmp(usr, path, strlen(usr)) == 0) {
        return dlopen(path, mode);
    }
    // connect to Unix socket with path in the XPC_MIDDLEMAN_SOCKET environment variable
    const char* socket_path = getenv("XPC_MIDDLEMAN_SOCKET");
    if (socket_path == NULL) {
        return dlopen(path, mode);
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        // failed to connect? just load the library normally
        return dlopen(path, mode);
    }
    if (send(sock, path, strlen(path) + 1, 0) == -1) {
        close(sock);
        return dlopen(path, mode);
    }
    char response[sizeof(bool) + PATH_MAX];
    if (recv(sock, response, sizeof(response), 0) == -1) {
        close(sock);
        return dlopen(path, mode);
    }
    close(sock);

    return dlopen(path, mode);
}

DYLD_INTERPOSE(dlopen_new, dlopen);