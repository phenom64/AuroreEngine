module;

#include <cstdint>
#include <stdexcept>
#include <string>

export module dreamrender:debug;

import vulkan_hpp;

namespace dreamrender {

export template<class T>
void debugName(vk::Device device, T object, const std::string& name)
{
#ifndef NDEBUG
    using CType = typename T::CType;
    vk::DebugUtilsObjectNameInfoEXT name_info(object.objectType, (uint64_t)((CType)object), name.c_str());
    vk::Result result = device.setDebugUtilsObjectNameEXT(&name_info);
    if(result != vk::Result::eSuccess)
        throw std::runtime_error("naming failed: "+vk::to_string(result));
#endif
}

export enum class debug_tag : uint64_t
{
    TextureSrc
};

export template<class T>
void debugTag(vk::Device device, T object, debug_tag tag, std::string value)
{
#ifndef NDEBUG
    using CType = typename T::CType;
    vk::DebugUtilsObjectTagInfoEXT tag_info(object.objectType, (uint64_t)((CType)object), (uint64_t)tag, value.size()+1, value.c_str());
    vk::Result result = device.setDebugUtilsObjectTagEXT(&tag_info);
    if(result != vk::Result::eSuccess)
        throw std::runtime_error("tagging failed: "+vk::to_string(result));
#endif
}

}
