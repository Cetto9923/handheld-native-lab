/* The contract of the retirement rule for a readback.
 *
 * A readback copies a GPU result into the guest's memory. The copy and the buffer it writes belong to one slot: the one
 * that was recording when they were recorded. That slot's submission owns the readback, and the guest's bytes are final
 * when its submission's fence has signalled. The submissions in flight besides it are none of the readback's business.
 *
 * The rule is production code: src/graphics/nvn_vulkan/engine/xbg_readback_retire.h, which the engine uses to decide
 * which submission a readback waits for. This case drives that code, not a copy of it.
 *
 * The expectations are written as a model that is built differently from the production code: the model is given the
 * owner's own state and the number of slots, so that it structurally cannot look at the other submissions, while the
 * production function is given the whole ring. A rule that drifts away from the rule - one that waits for the newest
 * submission in flight, or one that always waits - is caught instead of being confirmed by a second copy of itself.
 */
#include "../../src/graphics/nvn_vulkan/engine/xbg_readback_retire.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <random>

static unsigned errors;

#define CHECK(x) do { if(!(x)) { fprintf(stderr, "[check-fail] line=%d %s\n", __LINE__, #x); ++errors; } } while(0)

/* The model of the rule: the decision follows from the owner and from the owner's own state alone. */
static XbgReadbackDecision model_decide(size_t count, int owner, bool owner_pending) {
    XbgReadbackDecision d{-1, false, true};
    if (owner < 0 || (size_t)owner >= count) return d;
    d.owner = owner;
    d.fallback = false;
    d.wait = owner_pending;
    return d;
}

static XbgReadbackDecision production_decide(const std::vector<bool> &pending, int owner) {
    bool flat[32] = {false};
    for (size_t i = 0; i < pending.size() && i < 32; ++i) flat[i] = pending[i];
    return xbg_readback_decide(pending.empty() ? nullptr : flat, pending.size(), owner);
}

int main(void) {
    /* The owner is in flight: the host waits for that submission's fence. */
    {
        std::vector<bool> pending{true, false, false};
        XbgReadbackDecision d = production_decide(pending, 0);
        CHECK(d.owner == 0);
        CHECK(d.wait);
        CHECK(!d.fallback);
    }
    /* The owner is not in flight: its data is final, so nothing has to be waited for. */
    {
        std::vector<bool> pending{false, true, true};
        XbgReadbackDecision d = production_decide(pending, 0);
        CHECK(d.owner == 0);
        CHECK(!d.wait);
        CHECK(!d.fallback);
    }
    /* The other submissions are in flight, the owner is not: the readback still does not wait. This is the case the
     * old full drain over-waited: the queue was not empty, but the readback's own bytes were already final. */
    {
        std::vector<bool> pending{false, true, false, true};
        XbgReadbackDecision d = production_decide(pending, 2);
        CHECK(d.owner == 2);
        CHECK(!d.wait);
        CHECK(!d.fallback);
    }
    /* No owner: the caller has to fall back to the old path. */
    {
        std::vector<bool> pending{true, true};
        CHECK(production_decide(pending, -1).fallback);
        CHECK(production_decide(pending, 2).fallback);
        CHECK(!production_decide(pending, -1).wait);
        CHECK(!production_decide(pending, 2).wait);
    }
    /* The state of the other submissions never changes the answer: only the owner's own state does. The two rings
     * agree on the owner and differ everywhere else. */
    {
        for (int owner = 0; owner < 5; ++owner) {
            for (int owner_pending = 0; owner_pending < 2; ++owner_pending) {
                std::vector<bool> a(5, false), b(5, false);
                for (int i = 0; i < 5; ++i) {
                    if (i == owner) continue;
                    a[(size_t)i] = ((i + owner) & 1) != 0;
                    b[(size_t)i] = ((i + owner + 1) & 1) != 0;
                }
                a[(size_t)owner] = owner_pending != 0;
                b[(size_t)owner] = owner_pending != 0;
                XbgReadbackDecision da = production_decide(a, owner);
                XbgReadbackDecision db = production_decide(b, owner);
                CHECK(da.owner == db.owner);
                CHECK(da.wait == db.wait);
                CHECK(da.fallback == db.fallback);
                /* The owner's own state does change the answer. */
                CHECK(da.wait == (owner_pending != 0));
            }
        }
    }
    /* The differential run: the production rule and the model answer the same on rings that were not written by hand. */
    {
        std::mt19937_64 rng(0x5eedbeefu);
        for (int round = 0; round < 2000; ++round) {
            const size_t count = 1 + rng() % 16;
            std::vector<bool> pending(count);
            for (size_t i = 0; i < count; ++i) pending[i] = (rng() & 1) != 0;
            int owner = (int)(rng() % (count + 2)) - 1;   /* -1 and count are out of range */
            XbgReadbackDecision got = production_decide(pending, owner);
            XbgReadbackDecision want = model_decide(count, owner, owner >= 0 && (size_t)owner < count && pending[(size_t)owner]);
            if (got.owner != want.owner || got.wait != want.wait || got.fallback != want.fallback) {
                fprintf(stderr, "[check-fail] round=%d owner=%d production=(%d,%d,%d) model=(%d,%d,%d) count=%zu\n",
                        round, owner, got.owner, (int)got.wait, (int)got.fallback,
                        want.owner, (int)want.wait, (int)want.fallback, count);
                ++errors;
                break;
            }
        }
    }
    if (errors) { fprintf(stderr, "xbg_readback_retire_contract errors=%u\n", errors); return 1; }
    printf("xbg_readback_retire_contract errors=0 the_readback_waits_for_its_own_submission_only\n");
    return 0;
}
