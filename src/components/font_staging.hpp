#pragma once

#include <cstdint>
#include <stdexcept>

namespace auroreengine::detail {

// Reserve a whole non-coherent atom envelope for each recorded upload batch.
// Later CPU writes and VMA flush expansion cannot touch a previous batch's atoms.
class font_staging_batch {
    std::uint64_t begin_, cursor_, capacity_, atom_;
    static std::uint64_t aligned(std::uint64_t value, std::uint64_t capacity, std::uint64_t atom) {
        if(!atom || value > capacity) throw std::runtime_error("Invalid font staging range");
        const auto remainder = value % atom;
        const auto padding = remainder ? atom - remainder : 0;
        if(padding > capacity - value) throw std::runtime_error("Font staging atom padding exceeds capacity");
        return value + padding;
    }
public:
    font_staging_batch(std::uint64_t used, std::uint64_t capacity, std::uint64_t atom)
        : begin_(aligned(used, capacity, atom)), cursor_(begin_), capacity_(capacity), atom_(atom) {}

    std::uint64_t append(std::uint64_t bytes) {
        if(bytes > capacity_ - cursor_) throw std::runtime_error("Font glyph staging capacity exhausted");
        const auto end = cursor_ + bytes;
        // Reserve final padding before allowing the caller to write any bytes.
        aligned(end, capacity_, atom_);
        const auto offset = cursor_;
        cursor_ = end;
        return offset;
    }
    std::uint64_t flush_begin() const { return begin_; }
    std::uint64_t reserved_end() const { return aligned(cursor_, capacity_, atom_); }
};

} // namespace auroreengine::detail
