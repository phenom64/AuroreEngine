export module dreamrender:debug;

// Minimal debug utilities module. For environments without the Vulkan DebugUtils
// extension or where names are not essential, provide no-op overloads so call
// sites compile without extra dependency or platform conditionals.

export namespace dreamrender {
    template<typename... Args>
    inline void debugName(Args&&...) noexcept {}
}

