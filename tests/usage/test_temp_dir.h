#ifndef SYJ_EDGEMIND_TEST_TEMP_DIR_H
#define SYJ_EDGEMIND_TEST_TEMP_DIR_H

#include <cstdio>
#include <cstdlib>
#include <string>

namespace syj::edgemind::test {

inline bool probe_writable_directory(const std::string& dir) {
    const std::string probe = dir + "/.syj_edgemind_write_probe";

    std::FILE* file = std::fopen(probe.c_str(), "w");
    if (file == nullptr) {
        return false;
    }

    const bool write_ok =
        std::fputs("probe\n", file) >= 0;

    const bool close_ok =
        std::fclose(file) == 0;

    const bool remove_ok =
        std::remove(probe.c_str()) == 0;

    return write_ok && close_ok && remove_ok;
}

inline std::string writable_temp_dir() {
    const char* tmpdir = std::getenv("TMPDIR");

    if (tmpdir != nullptr && *tmpdir != '\0') {
        const std::string candidate(tmpdir);

        if (probe_writable_directory(candidate)) {
            return candidate;
        }
    }

    const char* home = std::getenv("HOME");

    if (home != nullptr && *home != '\0') {
        const std::string candidate(home);

        if (probe_writable_directory(candidate)) {
            return candidate;
        }
    }

    if (probe_writable_directory("/tmp")) {
        return "/tmp";
    }

    if (probe_writable_directory(".")) {
        return ".";
    }

    return ".";
}

} // namespace syj::edgemind::test

#endif // SYJ_EDGEMIND_TEST_TEMP_DIR_H
