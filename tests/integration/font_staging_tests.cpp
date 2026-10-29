#include "../../src/components/font_staging.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

using auroreengine::detail::font_staging_batch;

int main() {
    try {
        const auto require = [](bool value, const char* message) {
            if(!value) throw std::runtime_error(message);
        };
        const auto rejects = [&](auto operation) {
            bool rejected = false;
            try { operation(); } catch(const std::exception&) { rejected = true; }
            require(rejected, "Invalid staging reservation was accepted");
        };
        // VMA expands flushes to whole non-coherent atoms. Two 4-byte glyph
        // batches may never touch the same 256-byte atom while either is in flight.
        font_staging_batch first(0, 4096, 256);
        const auto firstCopy = first.append(4);
        font_staging_batch second(first.reserved_end(), 4096, 256);
        const auto secondCopy = second.append(4);
        require(firstCopy == 0 && secondCopy == 256, "Successive batches share a non-coherent atom");
        require(first.flush_begin() == 0 && first.reserved_end() == 256, "First flush was not atom aligned");
        require(second.flush_begin() == 256 && second.reserved_end() == 512, "Second flush was not atom aligned");

        // Glyphs within one batch can share atoms; only separate batches need isolation.
        font_staging_batch glyphs(12, 2048, 256);
        require(glyphs.append(12) == 256, "Batch start did not skip the previous live atom");
        require(glyphs.append(264) == 268, "Glyphs gained unnecessary per-glyph padding");
        require(glyphs.reserved_end() == 768, "Final atom padding was not reserved");

        for(const auto atom : {std::uint64_t{1}, std::uint64_t{64}, std::uint64_t{256}, std::uint64_t{1024}}) {
            std::uint64_t used = 0;
            for(int i=0; i<7; ++i) {
                font_staging_batch batch(used, 16384, atom);
                const auto copy = batch.append(4 * (i+1));
                require(copy >= used && copy % atom == 0, "Batch overlaps its predecessor");
                require(batch.reserved_end() % atom == 0, "Flush end is not atom aligned");
                used = batch.reserved_end();
            }
        }
        font_staging_batch full(768, 1024, 256);
        require(full.append(256) == 768 && full.reserved_end() == 1024, "Exact-fit batch failed");
        rejects([&] { full.append(4); });
        rejects([] { font_staging_batch invalid(1, 255, 256); });
        rejects([] { font_staging_batch invalid(1025, 1024, 256); });
        rejects([] { font_staging_batch invalid(0, 1024, 0); });
        rejects([] { font_staging_batch tail(768, 1000, 256); tail.append(4); });
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        rejects([&] { font_staging_batch invalid(maximum-4, maximum, 256); });
        rejects([&] { font_staging_batch overflow(0, maximum, 1); overflow.append(maximum); overflow.append(1); });
        std::cout << "Font staging: atom isolation, flush boundaries, capacity padding and overflow checks passed\n";
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
