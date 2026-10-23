#include "leaf_validation.h"
#include "json_writer.h"
#include <bit>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace ffrs {
namespace {
template <typename T> T Read(const std::vector<uint8_t>& file, size_t offset) {
    T value {};
    std::memcpy(&value, file.data() + offset, sizeof(value));
    return value;
}
uint32_t LocalReference(int32_t a, int32_t b, int32_t c, uint32_t flags) {
    int32_t maximum = a > b ? a : b;
    const int32_t third = (flags & 1u) ? c : 1;
    if (third > maximum) maximum = third;
    return std::bit_width(static_cast<uint32_t>(maximum) | 1u);
}
struct FileDescriptor {
    int value;
    ~FileDescriptor() { if (value >= 0) close(value); }
};
}

bool ValidateLeafElf(const std::vector<uint8_t>& file, std::string& error) {
    auto fail = [&](const char* message) { error = message; return false; };
    if (file.size() != 2176) return fail("Expected the reviewed 2176-byte generated leaf library");
    if (std::memcmp(file.data(), "\177ELF\2\1", 6) != 0 ||
        Read<uint16_t>(file, 16) != 3 || Read<uint16_t>(file, 18) != 183 ||
        Read<uint64_t>(file, 24) != 0) return fail("Expected AArch64 ELF64 ET_DYN without a game entry point");
    const uint64_t phoff = Read<uint64_t>(file, 32);
    const uint16_t phsize = Read<uint16_t>(file, 54), count = Read<uint16_t>(file, 56);
    if (phsize != 56 || count == 0 || count > 16 || phoff > file.size() ||
        static_cast<uint64_t>(count) * phsize > file.size() - phoff) return fail("Invalid program header bounds");
    bool dynamic_seen = false, load_seen = false;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t p = static_cast<size_t>(phoff + i * phsize);
        const uint32_t type = Read<uint32_t>(file, p), flags = Read<uint32_t>(file, p + 4);
        const uint64_t offset = Read<uint64_t>(file, p + 8), bytes = Read<uint64_t>(file, p + 32);
        if (offset > file.size() || bytes > file.size() - offset) return fail("Segment exceeds leaf file");
        if (type == 3) return fail("ELF interpreter is prohibited for this leaf library");
        if (type == 1) {
            load_seen = true;
            if ((flags & 3u) == 3u || Read<uint64_t>(file, p + 40) < bytes)
                return fail("Invalid or writable/executable LOAD segment");
        }
        if (type != 2) continue;
        if (dynamic_seen || bytes == 0 || bytes % 16) return fail("Invalid dynamic table");
        dynamic_seen = true;
        bool terminated = false;
        for (uint64_t d = offset; d < offset + bytes; d += 16) {
            const uint64_t tag = Read<uint64_t>(file, static_cast<size_t>(d));
            if (tag == 0) { terminated = true; break; }
            // Exactly the generated freestanding library's metadata surface.
            if (tag != 14 && tag != 6 && tag != 11 && tag != 5 && tag != 10 &&
                tag != 0x6ffffef5 && tag != 4)
                return fail("Dependencies, constructors, finalizers and other dynamic actions are prohibited");
        }
        if (!terminated) return fail("Dynamic table is not terminated");
    }
    return load_seen && dynamic_seen ? true : fail("Missing LOAD or dynamic table");
}

LeafReport CompareLeafFunctions(LeafFunction original, LeafFunction reference) {
    LeafReport report;
    report.stage = "compare";
    if (!original || !reference) {
        report.status = "FAIL"; report.error = "Missing typed export"; return report;
    }
    constexpr int32_t values[] = {INT32_MIN, -128, -1, 0, 1, 2, 3, 7, 8, 15, 16, 255, 256, 65535, INT32_MAX};
    constexpr uint32_t flags[] = {0, 1, 2, 3, UINT32_MAX};
    for (int32_t a : values) for (int32_t b : values) for (int32_t c : values) for (uint32_t f : flags) {
        const uint32_t actual = original(a, b, c, f);
        const uint32_t library_expected = reference(a, b, c, f);
        const uint32_t local_expected = LocalReference(a, b, c, f);
        ++report.cases;
        if (actual != local_expected || library_expected != local_expected) {
            if (report.mismatches++ == 0) {
                report.first_inputs = {a, b, c, static_cast<int64_t>(f)};
                report.first_original = actual;
                report.first_library_reference = library_expected;
                report.first_local_reference = local_expected;
            }
        }
    }
    report.code_executed = true;
    if (report.mismatches) { report.status = "FAIL"; report.error = "Original/library/local mathematics mismatch"; }
#if defined(__ANDROID__) && defined(__aarch64__)
    else report.status = "PASS_LEAF_BEHAVIOR";
#else
    else report.status = "HOST_TEST_COMPARISON_ONLY";
#endif
    return report;
}

LeafReport VerifyLeafLibrary(const std::string& path, LeafHasher hash, void* context) {
    LeafReport report;
    report.library_path = path;
    FileDescriptor fd {open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (fd.value < 0) {
        report.status = errno == ENOENT ? "NOT_RUN" : "FAIL";
        report.error = errno == ENOENT ? "Optional app-private leaf library is absent" : std::strerror(errno);
        return report;
    }
    struct stat info {};
    report.status = "FAIL";
    if (fstat(fd.value, &info) || !S_ISREG(info.st_mode) || info.st_size != 2176) {
        report.error = "Expected a regular 2176-byte generated leaf library"; return report;
    }
    std::vector<uint8_t> file(2176);
    size_t done = 0;
    while (done < file.size()) {
        const ssize_t n = read(fd.value, file.data() + done, file.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { report.error = "Cannot read complete leaf library"; return report; }
        done += static_cast<size_t>(n);
    }
    report.stage = "preflight";
    if (!ValidateLeafElf(file, report.error)) return report;
    report.library_sha256 = hash ? hash(file, context) : "";
    if (report.library_sha256 != kLeafLibrarySha256) {
        report.error = "Leaf library SHA256 differs from the reviewed generated artifact"; return report;
    }
#if defined(__ANDROID__) && defined(__aarch64__)
    // The checked descriptor, not a second path open, names the loaded inode.
    const std::string descriptor_path = "/proc/self/fd/" + std::to_string(fd.value);
    report.stage = "dlopen";
    void* library = dlopen(descriptor_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) { const char* error = dlerror(); report.error = error ? error : "dlopen failed"; return report; }
    dlerror();
    auto original = reinterpret_cast<LeafFunction>(dlsym(library, "ffrs_leaf_694bd0"));
    const char* first_error = dlerror();
    const std::string original_error = first_error ? first_error : "";
    dlerror();
    auto reference = reinterpret_cast<LeafFunction>(dlsym(library, "ffrs_leaf_reference"));
    const char* second_error = dlerror();
    if (!original || !reference || !original_error.empty() || second_error) {
        report.stage = "dlsym";
        report.error = !original_error.empty() ? original_error : second_error ? second_error : "Missing typed export";
    } else {
        report = CompareLeafFunctions(original, reference);
        report.library_path = path;
        report.library_sha256 = kLeafLibrarySha256;
    }
    dlclose(library);
#else
    report.status = "NOT_RUN";
    report.error = "Actual leaf execution requires an Android AArch64 process";
#endif
    return report;
}

std::string SerializeLeafReport(const LeafReport& report) {
    JsonWriter out;
    out.BeginObject();
    out.Key("schema"); out.String("ffrs.android.leaf.v1");
    out.Key("status"); out.String(report.status);
    out.Key("stage"); out.String(report.stage);
    out.Key("error"); out.String(report.error);
    out.Key("scope"); out.String("One reviewed original leaf only; complete game startup/rendering NOT_RUN");
    out.Key("source_module_sha256"); out.String(kLeafModuleSha256);
    out.Key("source_module_offset"); out.String("0x694bd0");
    out.Key("source_leaf_sha256"); out.String(kLeafCodeSha256);
    out.Key("library_path"); out.String(report.library_path);
    out.Key("library_sha256"); out.String(report.library_sha256);
    out.Key("reviewed_library_sha256"); out.String(kLeafLibrarySha256);
    out.Key("architecture");
#if defined(__ANDROID__) && defined(__aarch64__)
    out.String("android-aarch64");
#else
    out.String("host-test-only");
#endif
    struct utsname runtime {};
    out.Key("runtime_machine"); out.String(uname(&runtime) == 0 ? runtime.machine : "unavailable");
    out.Key("planned_samples"); out.UInt(16875);
    out.Key("executed_samples"); out.UInt(report.cases);
    out.Key("mismatches"); out.UInt(report.mismatches);
    out.Key("leaf_code_executed"); out.Bool(report.code_executed);
    out.Key("constructors_called"); out.Bool(false);
    out.Key("comparands"); out.String("typed original export / typed library reference / local C++ integer mathematics");
    if (report.mismatches) {
        out.Key("first_mismatch"); out.BeginObject();
        out.Key("inputs"); out.BeginArray(); for (int64_t v : report.first_inputs) out.Int(v); out.EndArray();
        out.Key("original"); out.UInt(report.first_original);
        out.Key("library_reference"); out.UInt(report.first_library_reference);
        out.Key("local_reference"); out.UInt(report.first_local_reference);
        out.EndObject();
    }
    out.EndObject();
    return out.str();
}
}
