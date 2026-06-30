// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <unistd.h>

extern "C" {

struct passwd {
    char* pw_name;
    char* pw_passwd;
    unsigned int pw_uid;
    unsigned int pw_gid;
    char* pw_gecos;
    char* pw_dir;
    char* pw_shell;
};

long pread(int fd, void* buffer, std::size_t count, long offset) {
    const long original = lseek(fd, 0, SEEK_CUR);
    if (original < 0) {
        return -1;
    }
    if (lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    const long bytes_read = read(fd, buffer, count);
    lseek(fd, original, SEEK_SET);
    return bytes_read;
}

uid_t getuid() {
    return 0;
}

passwd* getpwuid(unsigned int) {
    return nullptr;
}

long sysconf(int) {
    return 1;
}

int sigprocmask(int, const void*, void*) {
    return 0;
}

} // extern "C"
