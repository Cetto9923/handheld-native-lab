// Host contract: kernel I/O cannot service userspace page-guard read faults.
// Run from an owned .local directory; this is not a Vulkan or audio acceptance.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

static void *mapped;
static size_t mapped_bytes;
static volatile sig_atomic_t read_faults;
static void guard_read(int signal, siginfo_t *info, void *) {
    auto address = reinterpret_cast<uintptr_t>(info->si_addr);
    auto begin = reinterpret_cast<uintptr_t>(mapped);
    if (address < begin || address - begin >= mapped_bytes ||
        mprotect(mapped, mapped_bytes, PROT_READ)) _exit(128 + signal);
    ++read_faults;
}
static void require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "%s (errno=%d)\n", message, errno); std::exit(1); }
}
int main() {
    mapped_bytes = size_t(sysconf(_SC_PAGESIZE)) * 16;
    mapped = mmap(nullptr, mapped_bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    require(mapped != MAP_FAILED, "mmap");
    auto *source = static_cast<unsigned char *>(mapped);
    for (size_t i = 0; i < mapped_bytes; ++i) source[i] = (i * 37 + 11) & 255;
    struct sigaction action{}, previous{}, previous_bus{};
    action.sa_sigaction = guard_read; action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    require(!sigaction(SIGSEGV, &action, &previous), "sigaction");
    require(!sigaction(SIGBUS, &action, &previous_bus), "sigaction bus");
    require(!mprotect(mapped, mapped_bytes, PROT_NONE), "protect");
    FILE *negative = tmpfile(); require(negative, "negative tmpfile");
    require(!setvbuf(negative, nullptr, _IONBF, 0), "unbuffered negative");
    errno = 0;
    size_t rejected = fwrite(mapped, 1, mapped_bytes, negative);
    int rejected_errno = errno;
    require(rejected == 0 && rejected_errno == EFAULT && !read_faults,
            "kernel I/O must reject protected pages without a userspace fault");
    fclose(negative);
    std::vector<unsigned char> heap(mapped_bytes);
    std::memcpy(heap.data(), mapped, mapped_bytes);
    require(read_faults > 0, "userspace copy must service guarded read");
    for (size_t i = 0; i < mapped_bytes; ++i)
        require(heap[i] == ((i * 37 + 11) & 255), "staged byte mismatch");
    FILE *positive = tmpfile(); require(positive, "positive tmpfile");
    require(fwrite(heap.data(), 1, heap.size(), positive) == heap.size() &&
            !fflush(positive), "staged I/O");
    rewind(positive);
    std::vector<unsigned char> actual(mapped_bytes);
    require(fread(actual.data(), 1, actual.size(), positive) == actual.size() &&
            actual == heap, "persisted bytes");
    fclose(positive);
    require(!sigaction(SIGSEGV, &previous, nullptr), "restore handler");
    require(!sigaction(SIGBUS, &previous_bus, nullptr), "restore bus handler");
    require(!munmap(mapped, mapped_bytes), "unmap");
    std::printf("PASS kernel EFAULT=%d, guarded CPU read faults=%d, exact bytes=%zu\n",
                rejected_errno, int(read_faults), mapped_bytes);
}
