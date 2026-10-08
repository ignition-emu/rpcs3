// A Vulkan driver (ICD) for the macOS module that forwards to the MoltenVK
// already in the host process.
//
// RPCS3 on macOS creates its instance through the Vulkan loader, which loads a
// driver from a manifest. Godot links MoltenVK into its executable and exports
// MoltenVK's three driver entry points, but the loader cannot dlopen an
// executable. With a second MoltenVK loaded from the bundle, the Objective-C
// runtime finds MoltenVK's classes twice (MVKBlockObserver) and warns that
// this can crash. This driver is what the bundle's manifest names instead: it
// looks the entry points up in the main executable only, and falls back to
// the bundled libMoltenVK.dylib beside it when the host has no MoltenVK of its
// own, as a command-line probe does.
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t VkResult;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_get_proc_addr)(void* object, const char* name);
typedef VkResult (*PFN_negotiate)(uint32_t* version);

static PFN_get_proc_addr instance_proc_addr;
static PFN_get_proc_addr physical_device_proc_addr;
static PFN_negotiate negotiate;

static void* bundled_moltenvk(void)
{
	Dl_info self;
	if (!dladdr((const void*)&bundled_moltenvk, &self) || !self.dli_fname)
		return NULL;
	char path[4096];
	const char* slash = strrchr(self.dli_fname, '/');
	const size_t dir = slash ? (size_t)(slash - self.dli_fname) : 0;
	if (dir + sizeof("/libMoltenVK.dylib") > sizeof(path))
		return NULL;
	memcpy(path, self.dli_fname, dir);
	memcpy(path + dir, "/libMoltenVK.dylib", sizeof("/libMoltenVK.dylib"));
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void resolve(void)
{
	if (instance_proc_addr)
		return;
	void* from = RTLD_MAIN_ONLY;
	const char* source = "the host's MoltenVK";
	if (!dlsym(from, "vk_icdGetInstanceProcAddr"))
	{
		from = bundled_moltenvk();
		source = "the bundled MoltenVK";
	}
	if (!from)
	{
		fprintf(stderr, "[ignition icd] no MoltenVK in the host and none bundled\n");
		return;
	}
	instance_proc_addr = (PFN_get_proc_addr)dlsym(from, "vk_icdGetInstanceProcAddr");
	physical_device_proc_addr = (PFN_get_proc_addr)dlsym(from, "vk_icdGetPhysicalDeviceProcAddr");
	negotiate = (PFN_negotiate)dlsym(from, "vk_icdNegotiateLoaderICDInterfaceVersion");
	fprintf(stderr, "[ignition icd] using %s\n", source);
}

__attribute__((visibility("default"))) VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* version)
{
	resolve();
	// VK_ERROR_INCOMPATIBLE_DRIVER
	return negotiate ? negotiate(version) : -9;
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void* instance, const char* name)
{
	resolve();
	return instance_proc_addr ? instance_proc_addr(instance, name) : NULL;
}

__attribute__((visibility("default"))) PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(void* instance, const char* name)
{
	resolve();
	return physical_device_proc_addr ? physical_device_proc_addr(instance, name) : NULL;
}
