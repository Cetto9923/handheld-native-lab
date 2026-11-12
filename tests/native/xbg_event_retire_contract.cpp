/* The contract of the retirement rule for a deferred event.
 *
 * A deferred event is published when the GPU has finished the submission that carries its signal. When the guest reads
 * the value of an event, every signal it has already issued for that address has to be published first, otherwise the
 * guest would read a value older than the one it signalled. The newest signal to that address therefore fixes the
 * submission that has to retire; the submissions queued after it stay in flight.
 *
 * The rule is production code: src/graphics/nvn_vulkan/engine/xbg_event_retire.h, which the engine uses to choose the
 * submission it retires when the guest reads an event. This case drives that code, not a copy of it.
 *
 * The expectations are written as a model that is built differently from the production code (the signals to an address
 * are collected first, and the newest of them is the one with the greatest order), so that a production rule that drifts
 * away from the rule is caught instead of being confirmed by a second copy of itself.
 */
#include "../../src/graphics/nvn_vulkan/engine/xbg_event_retire.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <random>

static unsigned errors;

#define CHECK(x) do { if(!(x)) { fprintf(stderr, "[check-fail] line=%d %s\n", __LINE__, #x); ++errors; } } while(0)

/* The model of the rule: the signals to an address are collected, and the newest of them is the one with the greatest
 * order. The index the model answers is the first one with that order, so that a tie is answered the same way. */
static long model_newest(const std::vector<XbgEventSignal> &signals, uint64_t addr) {
    std::vector<uint64_t> orders;
    for (const auto &s : signals) if (s.addr == addr) orders.push_back(s.order);
    if (orders.empty()) return -1;
    uint64_t newest = orders[0];
    for (uint64_t o : orders) if (o > newest) newest = o;
    for (size_t i = 0; i < signals.size(); ++i) if (signals[i].addr == addr && signals[i].order == newest) return (long)i;
    return -1;
}

static long production_newest(const std::vector<XbgEventSignal> &signals, uint64_t addr) {
    return xbg_event_newest_signal(signals.data(), signals.size(), addr);
}

int main(void) {
    /* No signals at all, and signals to another address: the guest has not signalled this one. */
    CHECK(production_newest({}, 0x1000) == -1);
    {
        std::vector<XbgEventSignal> s{{0x2000, 1}, {0x2000, 2}};
        CHECK(production_newest(s, 0x1000) == -1);
    }
    /* One signal to the address: that submission is the one that has to retire. */
    {
        std::vector<XbgEventSignal> s{{0x1000, 7}};
        CHECK(production_newest(s, 0x1000) == 0);
    }
    /* Several signals to the same address (the same event signalled again): the newest one decides, because it is the
     * value the guest is entitled to read. */
    {
        std::vector<XbgEventSignal> s{{0x1000, 3}, {0x2000, 4}, {0x1000, 9}};
        CHECK(production_newest(s, 0x1000) == 2);
        CHECK(production_newest(s, 0x2000) == 1);
    }
    /* The order of the list does not decide: the order of the signals does. */
    {
        std::vector<XbgEventSignal> s{{0x1000, 9}, {0x1000, 3}};
        CHECK(production_newest(s, 0x1000) == 0);
    }
    /* A tie in the order is answered with the first of the two, so that the answer does not depend on the list. */
    {
        std::vector<XbgEventSignal> s{{0x1000, 5}, {0x1000, 5}};
        CHECK(production_newest(s, 0x1000) == 0);
    }
    /* The differential run: the production rule and the model answer the same on lists that were not written by hand. */
    {
        std::mt19937_64 rng(0x5eed1234u);
        for (int round = 0; round < 2000; ++round) {
            const size_t count = rng() % 33;
            const uint64_t addresses = 1 + rng() % 4;
            std::vector<XbgEventSignal> s;
            uint64_t order = 0;
            for (size_t i = 0; i < count; ++i) {
                order += 1 + rng() % 3;
                s.push_back(XbgEventSignal{0x1000 + (rng() % addresses), order});
            }
            if (count && (rng() & 1)) std::shuffle(s.begin(), s.end(), rng);
            for (uint64_t a = 0; a < addresses; ++a) {
                const uint64_t addr = 0x1000 + a;
                const long got = production_newest(s, addr);
                const long want = model_newest(s, addr);
                if (got != want) {
                    fprintf(stderr, "[check-fail] round=%d addr=%#llx production=%ld model=%ld size=%zu\n",
                            round, (unsigned long long)addr, got, want, s.size());
                    ++errors;
                    break;
                }
                if (got >= 0) CHECK(s[(size_t)got].addr == addr);
            }
        }
    }
    if (errors) { fprintf(stderr, "xbg_event_retire_contract errors=%u\n", errors); return 1; }
    printf("xbg_event_retire_contract errors=0 newest_signal_to_an_address=the_last_one_issued\n");
    return 0;
}
