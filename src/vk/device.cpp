#include "vk/device.hpp"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace vk {

namespace {

// ---- Dynamic runtime loading ------------------------------------------------
// Loaded ONCE per process (magic-static, thread-safe init) and shared by all
// Device instances; the module handle is intentionally retained for process
// lifetime (re-dlopen-per-Device and never balancing was a review finding).

PFN_vkGetInstanceProcAddr loadRuntimeOnce(std::string &failureReason)
{
	struct Runtime {
		PFN_vkGetInstanceProcAddr gipa = nullptr;
		std::string error;

		Runtime()
		{
#if defined(_WIN32)
			HMODULE lib = ::LoadLibraryW(L"vulkan-1.dll");
			if (!lib) {
				error = "vulkan-1.dll not found (no Vulkan runtime installed)";
				return;
			}
			// FARPROC -> function pointer via memcpy: avoids the
			// object/function pointer cast GCC -Wpedantic rejects, and is
			// well-defined on every supported ABI.
			FARPROC sym = ::GetProcAddress(lib, "vkGetInstanceProcAddr");
#else
			void *lib = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
			if (!lib)
				lib = ::dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
			if (!lib) {
				error = "libvulkan.so not found (no Vulkan runtime installed)";
				return;
			}
			void *sym = ::dlsym(lib, "vkGetInstanceProcAddr");
#endif
			if (!sym) {
				error = "vkGetInstanceProcAddr missing from Vulkan runtime";
				return;
			}
			static_assert(sizeof(sym) == sizeof(gipa), "pointer size mismatch");
			std::memcpy(&gipa, &sym, sizeof(gipa));
		}
	};
	static const Runtime runtime;
	if (!runtime.gipa)
		failureReason = runtime.error;
	return runtime.gipa;
}

bool hasExtension(const std::vector<VkExtensionProperties> &exts, const char *name)
{
	for (const VkExtensionProperties &e : exts)
		if (std::strcmp(e.extensionName, name) == 0)
			return true;
	return false;
}

bool vkDisabledByEnv()
{
#if defined(_WIN32)
	// MSVC deprecates std::getenv (C4996); the Win32 call is the native way.
	char buf[8];
	return ::GetEnvironmentVariableA("VOXEL2026_VK_DISABLE", buf, sizeof(buf)) != 0;
#else
	return std::getenv("VOXEL2026_VK_DISABLE") != nullptr;
#endif
}

} // namespace

Device::Device()
{
	// Explicit CI/ops contract (review finding: SKIP-on-CI must be a
	// contract, not an accident of runner images): when set, Vulkan is
	// reported unavailable without touching any driver — the sanitizer and
	// determinism-control CI legs set this so a future runner image shipping
	// a software ICD can never silently run Mesa under TSan/ASan.
	if (vkDisabledByEnv()) {
		m_report.failureReason = "disabled by VOXEL2026_VK_DISABLE";
		return;
	}

	m_gipa = loadRuntimeOnce(m_report.failureReason);
	if (!m_gipa)
		return;

	const auto iproc = [this](VkInstance inst, const char *name) {
		return m_gipa(inst, name);
	};

	// ---- Instance. Core 1.0 is all this bring-up uses, so request exactly
	// that (review finding: a 1.1 request makes 1.0-only loaders fail with
	// VK_ERROR_INCOMPATIBLE_DRIVER for zero benefit).
	auto vkCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
			iproc(VK_NULL_HANDLE, "vkCreateInstance"));
	if (!vkCreateInstance_) {
		m_report.failureReason = "vkCreateInstance not resolvable";
		return;
	}

	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "voxel2026";
	app.apiVersion = VK_API_VERSION_1_0;
	VkInstanceCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &app;
	const VkResult instRes = vkCreateInstance_(&ici, nullptr, &m_instance);
	if (instRes != VK_SUCCESS) {
		m_report.failureReason = instRes == VK_ERROR_INCOMPATIBLE_DRIVER
				? "vkCreateInstance: incompatible driver"
				: "vkCreateInstance failed";
		return;
	}

	// ---- Instance-level functions.
	auto vkEnumeratePhysicalDevices_ = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
			iproc(m_instance, "vkEnumeratePhysicalDevices"));
	auto vkGetPhysicalDeviceProperties_ =
			reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
					iproc(m_instance, "vkGetPhysicalDeviceProperties"));
	auto vkEnumerateDeviceExtensionProperties_ =
			reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
					iproc(m_instance, "vkEnumerateDeviceExtensionProperties"));
	auto vkGetPhysicalDeviceQueueFamilyProperties_ =
			reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
					iproc(m_instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
	auto vkCreateDevice_ =
			reinterpret_cast<PFN_vkCreateDevice>(iproc(m_instance, "vkCreateDevice"));
	auto vkGetDeviceQueue_ =
			reinterpret_cast<PFN_vkGetDeviceQueue>(iproc(m_instance, "vkGetDeviceQueue"));
	if (!vkEnumeratePhysicalDevices_ || !vkGetPhysicalDeviceProperties_ ||
			!vkEnumerateDeviceExtensionProperties_ ||
			!vkGetPhysicalDeviceQueueFamilyProperties_ || !vkCreateDevice_ ||
			!vkGetDeviceQueue_) {
		m_report.failureReason = "core instance-level functions not resolvable";
		return;
	}

	// ---- Physical device (results checked — review finding).
	std::uint32_t count = 0;
	VkResult res = vkEnumeratePhysicalDevices_(m_instance, &count, nullptr);
	if ((res != VK_SUCCESS && res != VK_INCOMPLETE) || count == 0) {
		m_report.failureReason = "no Vulkan physical devices (no GPU/driver)";
		return;
	}
	std::vector<VkPhysicalDevice> devices(count);
	res = vkEnumeratePhysicalDevices_(m_instance, &count, devices.data());
	if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
		m_report.failureReason = "physical device enumeration failed";
		return;
	}
	devices.resize(count);

	VkPhysicalDeviceProperties chosenProps{};
	for (VkPhysicalDevice candidate : devices) {
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties_(candidate, &props);
		const bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		if (m_physical == VK_NULL_HANDLE || (discrete && !m_report.discreteGpu)) {
			m_physical = candidate;
			chosenProps = props;
			m_report.discreteGpu = discrete;
		}
	}

	m_report.deviceName = chosenProps.deviceName;
	m_report.apiVersion = chosenProps.apiVersion;
	m_report.driverVersion = chosenProps.driverVersion;
	m_report.timestampPeriodNs = chosenProps.limits.timestampPeriod;

	// ---- Tier flags by extension NAME (no header-vintage sensitivity).
	std::uint32_t extCount = 0;
	vkEnumerateDeviceExtensionProperties_(m_physical, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> exts(extCount);
	if (extCount > 0)
		vkEnumerateDeviceExtensionProperties_(m_physical, nullptr, &extCount, exts.data());

	m_report.descriptorIndexing = chosenProps.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0) ||
			hasExtension(exts, "VK_EXT_descriptor_indexing");
	m_report.descriptorBuffer = hasExtension(exts, "VK_EXT_descriptor_buffer");
	m_report.descriptorHeap = hasExtension(exts, "VK_EXT_descriptor_heap");
	m_report.meshShader = hasExtension(exts, "VK_EXT_mesh_shader");
	m_report.shaderObject = hasExtension(exts, "VK_EXT_shader_object");
	m_report.rayQuery = hasExtension(exts, "VK_KHR_ray_query");
	m_report.accelerationStructure = hasExtension(exts, "VK_KHR_acceleration_structure");
	m_report.timelineSemaphore = chosenProps.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0) ||
			hasExtension(exts, "VK_KHR_timeline_semaphore");

	// ---- Compute queue family. timestampPeriodNs is zeroed when the CHOSEN
	// family cannot timestamp — the report's contract is "timestamps usable
	// on the compute path", not device-wide support.
	std::uint32_t qfCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties_(m_physical, &qfCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(qfCount);
	if (qfCount > 0)
		vkGetPhysicalDeviceQueueFamilyProperties_(m_physical, &qfCount, families.data());
	for (std::uint32_t i = 0; i < qfCount; ++i) {
		if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			m_report.computeQueueFamily = i;
			if (families[i].timestampValidBits == 0)
				m_report.timestampPeriodNs = 0.0f;
			break;
		}
	}
	if (m_report.computeQueueFamily == ~0u) {
		m_report.failureReason = "no compute-capable queue family";
		return;
	}

	// ---- Logical device with one compute queue (core 1.0 path).
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo qci{};
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = m_report.computeQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &priority;
	VkDeviceCreateInfo dci{};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	if (vkCreateDevice_(m_physical, &dci, nullptr, &m_device) != VK_SUCCESS) {
		m_report.failureReason = "vkCreateDevice failed";
		return;
	}
	vkGetDeviceQueue_(m_device, m_report.computeQueueFamily, 0, &m_queue);

	m_report.available = true;
}

Device::~Device()
{
	if (!m_gipa)
		return;
	if (m_device != VK_NULL_HANDLE) {
		auto vkDestroyDevice_ = reinterpret_cast<PFN_vkDestroyDevice>(
				m_gipa(m_instance, "vkDestroyDevice"));
		if (vkDestroyDevice_)
			vkDestroyDevice_(m_device, nullptr);
	}
	if (m_instance != VK_NULL_HANDLE) {
		auto vkDestroyInstance_ = reinterpret_cast<PFN_vkDestroyInstance>(
				m_gipa(m_instance, "vkDestroyInstance"));
		if (vkDestroyInstance_)
			vkDestroyInstance_(m_instance, nullptr);
	}
}

PFN_vkVoidFunction Device::deviceProc(const char *name) const
{
	if (!m_gipa)
		return nullptr;
	auto vkGetDeviceProcAddr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
			m_gipa(m_instance, "vkGetDeviceProcAddr"));
	return vkGetDeviceProcAddr_ ? vkGetDeviceProcAddr_(m_device, name) : nullptr;
}

PFN_vkVoidFunction Device::instanceLevelProc(const char *name) const
{
	return m_gipa ? m_gipa(m_instance, name) : nullptr;
}

} // namespace vk
