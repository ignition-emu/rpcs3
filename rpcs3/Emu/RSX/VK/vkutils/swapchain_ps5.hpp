#pragma once

#include "swapchain_core.h"
#include "util/logs.hpp"

namespace vk
{
	// The PS5 title presents offscreen only (g_offscreen_present): RPCS3
	// makes no window surface there.
	using swapchain_NATIVE = native_swapchain_base;

	[[maybe_unused]] static
	VkSurfaceKHR make_WSI_surface(VkInstance, display_handle_t, WSI_config*)
	{
		rsx_log.error("A PS5 title has no window surface: present offscreen");
		return VK_NULL_HANDLE;
	}
}
