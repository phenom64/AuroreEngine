/* SPDX-License-Identifier: MPL-2.0 */
#include "../../src/swapchain_policy.hpp"
#include <iostream>
int main() {
    using auroreengine::detail::choose_composite_alpha;
    for(unsigned mask=1;mask<16;++mask) {
        const auto selected=choose_composite_alpha(mask);
        if(!(selected&mask))return 1;
        if((mask&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)&&selected!=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)return 2;
    }
    if(choose_composite_alpha(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)!=VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)return 3;
    if(choose_composite_alpha(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)!=VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)return 4;
    if(choose_composite_alpha(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)!=VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)return 5;
    try {choose_composite_alpha(0);return 6;}catch(const std::runtime_error&){}
    std::cout<<"Swapchain composite-alpha capabilities passed\n";
}
