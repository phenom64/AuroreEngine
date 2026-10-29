#include "../../src/queue_family_policy.hpp"
#include <iostream>
#include <vector>

using namespace auroreengine::detail;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if(!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
}
template<class F> bool throws(F&& f) {
    try { f(); } catch(const std::runtime_error&) { return true; }
    return false;
}
queue_family_selection select(const std::vector<queue_family_caps>& f, bool surface) {
    return select_queue_families(f, surface);
}
// Every family index returned must exist and support the role it is used for.
void check_roles(const std::vector<queue_family_caps>& f, const queue_family_selection& s, bool surface, const char* what) {
    const bool ok = s.graphics && s.present && *s.graphics < f.size() && *s.present < f.size()
        && f[*s.graphics].graphics && (!surface || f[*s.present].present);
    check(ok, what);
}
}

int main() {
    // Common family exists after the "last graphics" and "last present" families: must be chosen.
    {
        const std::vector<queue_family_caps> f{
            {.graphics = true},                              // 0: graphics only
            {.graphics = true, .present = true},             // 1: both
            {.graphics = true},                              // 2: graphics only (last graphics)
            {.present = true},                               // 3: present only (last present)
        };
        const auto s = select(f, true);
        check_roles(f, s, true, "later common family: valid roles");
        check(s.graphics == 1u && s.present == 1u, "later common family: both roles use family 1");
        const auto sh = choose_swapchain_sharing(*s.graphics, *s.present);
        check(!sh.concurrent && sh.count == 0, "common family: exclusive sharing");
    }
    // Several common families: last wins (historical order); present-only after it is ignored.
    {
        const std::vector<queue_family_caps> f{
            {.graphics = true, .present = true},
            {.graphics = true, .present = true},
            {.present = true},
        };
        const auto s = select(f, true);
        check(s.graphics == 1u && s.present == 1u, "multiple common families: last common");
    }
    // Single combined family (typical desktop device) is unchanged.
    {
        const std::vector<queue_family_caps> f{{.graphics = true, .transfer = true, .present = true}};
        const auto s = select(f, true);
        check(s.graphics == 0u && s.present == 0u, "single family: graphics==present==0");
        check(!s.transfer.has_value(), "single family: no dedicated transfer family");
    }
    // Split-only families: keep both, share concurrently with two unique indices.
    {
        const std::vector<queue_family_caps> f{
            {.graphics = true, .transfer = true},            // 0
            {.transfer = true},                              // 1 (dedicated transfer)
            {.present = true},                               // 2
        };
        const auto s = select(f, true);
        check_roles(f, s, true, "split-only: valid roles");
        check(s.graphics == 0u && s.present == 2u, "split-only: separate graphics/present preserved");
        check(s.transfer == 1u, "split-only: dedicated transfer family preserved");
        const auto sh = choose_swapchain_sharing(*s.graphics, *s.present);
        check(sh.concurrent && sh.count == 2, "split-only: concurrent sharing over two indices");
        check(sh.indices[0] == 0u && sh.indices[1] == 2u && sh.indices[0] != sh.indices[1],
              "split-only: indices are graphics then present, unique");
    }
    // Duplicate index handling.
    {
        const auto same = choose_swapchain_sharing(3, 3);
        check(!same.concurrent && same.count == 0, "equal indices never listed twice / never concurrent");
        const auto diff = choose_swapchain_sharing(5, 2);
        check(diff.concurrent && diff.count == 2 && diff.indices[0] == 5u && diff.indices[1] == 2u,
              "different indices listed in graphics, present order");
    }
    // Failures.
    {
        check(throws([]{ select({}, true); }), "no families (surface) throws");
        check(throws([]{ select({}, false); }), "no families (headless) throws");
        check(throws([]{ select({{.transfer = true, .present = true}}, true); }),
              "no graphics family throws even when something can present");
        check(throws([]{ select({{.transfer = true}}, false); }), "headless without graphics throws");
        check(throws([]{ select({{.graphics = true}, {.transfer = true}}, true); }),
              "no present-capable family with a surface throws");
    }
    // Headless: graphics-only selection; surface support is ignored; present aliases graphics.
    {
        const std::vector<queue_family_caps> f{
            {.graphics = true},
            {.present = true},                               // must not be chosen without a surface
            {.graphics = true, .present = true},
            {.transfer = true},
        };
        const auto s = select(f, false);
        check(s.graphics == 2u && s.present == 2u, "headless: present aliases last graphics family");
        check(s.transfer == 3u, "headless: dedicated transfer preserved");
        const std::vector<queue_family_caps> g{{.graphics = true}};
        const auto t = select(g, false);
        check(t.graphics == 0u && t.present == 0u, "headless graphics-only device selects it for both");
    }
    if(failures) return 1;
    std::cout << "Queue-family policy passed\n";
}
