#pragma once

#include <ostream>

// Lightweight no-op debug stream for Vulkan translation units
// Define VULKAN_ENABLE_DEBUG_OUTPUT to enable printing during development

struct VulkanNullStream {
    template<typename T>
    VulkanNullStream& operator<<(T const&) { return *this; }
    VulkanNullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
};

#ifndef VULKAN_ENABLE_DEBUG_OUTPUT
static VulkanNullStream VULKAN_DBG;
#else
#include <iostream>
#define VULKAN_DBG std::cout
#endif
