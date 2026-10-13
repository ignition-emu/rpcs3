#pragma once

#include "../VulkanAPI.h"
#include "swapchain.h"

#include <algorithm>
#include <atomic>
#include <vector>

#ifdef __APPLE__
#include <MoltenVK/mvk_vulkan.h>
#include <MoltenVK/mvk_private_api.h>
#endif

namespace vk
{
	// Set by a host that takes every frame through the RSX capture path
	// (GSFrameBase::present_frame) and has no window to present to. The
	// swapchain is then a set of plain images and no surface is created.
	// Set before the renderer is created.
	extern bool g_offscreen_present;

	// The size, in pixels, that such a host shows the frames at: width in the
	// high 32 bits, height in the low, 0 when it has not said. While an overlay
	// is visible, the capture path scales the frame up to fit this size before
	// it draws the overlay, so the overlay is drawn at the size it is seen at.
	extern std::atomic<u64> g_offscreen_output_size;

	class supported_extensions
	{
	private:
		std::vector<VkExtensionProperties> m_vk_exts;

	public:
		enum enumeration_class
		{
			instance = 0,
			device   = 1
		};

		supported_extensions(enumeration_class _class, const char* layer_name = nullptr, VkPhysicalDevice pdev = VK_NULL_HANDLE);

		bool is_supported(std::string_view ext) const;
	};

	class instance
	{
	private:
		std::vector<physical_device> gpus;
		VkInstance m_instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;

		PFN_vkDestroyDebugReportCallbackEXT _vkDestroyDebugReportCallback = nullptr;
		PFN_vkCreateDebugReportCallbackEXT _vkCreateDebugReportCallback = nullptr;
		VkDebugReportCallbackEXT m_debugger = nullptr;

		bool extensions_loaded = false;

	public:

		instance() = default;

		~instance();

		void destroy();

		void enable_debugging();

		bool create(const char* app_name, bool fast = false);

		void bind();

		std::vector<physical_device>& enumerate_devices();

		swapchain_base* create_swapchain(display_handle_t window_handle, vk::physical_device& dev);
	};
}
