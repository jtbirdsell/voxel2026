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

	// ---- Instance. The original bring-up requested exactly 1.0 (review
	// finding: a 1.1 request makes 1.0-only loaders fail with
	// VK_ERROR_INCOMPATIBLE_DRIVER for zero benefit). The mesh-execution
	// tier (issue #16) NOW has a benefit — the features2 chain at device
	// creation needs an instance >= 1.1 — so: try 1.1 first and fall back
	// to 1.0 on INCOMPATIBLE_DRIVER, which preserves the old degradation
	// contract (a 1.0-only loader still comes up, with the graphics tier
	// reported blocked instead of the whole device failing).
	auto vkCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
			iproc(VK_NULL_HANDLE, "vkCreateInstance"));
	if (!vkCreateInstance_) {
		m_report.failureReason = "vkCreateInstance not resolvable";
		return;
	}

	// Surface instance extensions (issue #21): enabled when the loader
	// advertises them — probed by NAME, requested only if present, so
	// headless hosts (CI, servers) come up exactly as before with the
	// presentation tier reported blocked instead of anything failing.
	std::vector<const char *> instanceExts;
	bool haveSurfaceExts = false;
	{
		auto vkEnumerateInstanceExtensionProperties_ =
				reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
						iproc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
		if (vkEnumerateInstanceExtensionProperties_) {
			std::uint32_t n = 0;
			vkEnumerateInstanceExtensionProperties_(nullptr, &n, nullptr);
			std::vector<VkExtensionProperties> exts(n);
			if (n > 0)
				vkEnumerateInstanceExtensionProperties_(nullptr, &n, exts.data());
#if defined(_WIN32)
			const char *platformSurface = "VK_KHR_win32_surface";
#elif defined(__APPLE__)
			const char *platformSurface = "VK_EXT_metal_surface";
#else
			const char *platformSurface = "VK_KHR_xcb_surface";
#endif
			if (hasExtension(exts, "VK_KHR_surface") &&
					hasExtension(exts, platformSurface)) {
				instanceExts.push_back("VK_KHR_surface");
				instanceExts.push_back(platformSurface);
				haveSurfaceExts = true;
			}
		}
	}

	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "voxel2026";
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = static_cast<std::uint32_t>(instanceExts.size());
	ici.ppEnabledExtensionNames = instanceExts.empty() ? nullptr : instanceExts.data();
	std::uint32_t instanceApi = VK_API_VERSION_1_1;
	VkResult instRes = vkCreateInstance_(&ici, nullptr, &m_instance);
	if (instRes == VK_ERROR_INCOMPATIBLE_DRIVER) {
		app.apiVersion = VK_API_VERSION_1_0;
		instanceApi = VK_API_VERSION_1_0;
		instRes = vkCreateInstance_(&ici, nullptr, &m_instance);
	}
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
			m_report.timestampValidBits = families[i].timestampValidBits;
			if (families[i].timestampValidBits == 0)
				m_report.timestampPeriodNs = 0.0f;
			break;
		}
	}
	if (m_report.computeQueueFamily == ~0u) {
		m_report.failureReason = "no compute-capable queue family";
		return;
	}
	for (std::uint32_t i = 0; i < qfCount; ++i) {
		if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			m_report.graphicsQueueFamily = i;
			break;
		}
	}

	// ---- Mesh-execution tier preconditions (issue #16). Each failed gate
	// records WHY, so the graphics tests can SKIP with a reason instead of
	// failing on hosts that legitimately lack the tier.
	bool wantMesh = false;
	if (instanceApi < VK_API_VERSION_1_1) {
		m_report.meshExecBlocked = "instance API < 1.1 (no features2 chain)";
	} else if (chosenProps.apiVersion < VK_MAKE_API_VERSION(0, 1, 2, 0)) {
		m_report.meshExecBlocked = "device API < 1.2 (no SPIR-V 1.4)";
	} else if (!m_report.meshShader) {
		m_report.meshExecBlocked = "VK_EXT_mesh_shader not present";
	} else if (m_report.graphicsQueueFamily == ~0u) {
		m_report.meshExecBlocked = "no graphics-capable queue family";
	} else {
		// Extension present: confirm the FEATURE bit (presence is necessary,
		// not sufficient) through the features2 chain.
		auto vkGetPhysicalDeviceFeatures2_ =
				reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
						iproc(m_instance, "vkGetPhysicalDeviceFeatures2"));
		if (!vkGetPhysicalDeviceFeatures2_) {
			m_report.meshExecBlocked = "vkGetPhysicalDeviceFeatures2 not resolvable";
		} else {
			VkPhysicalDeviceMeshShaderFeaturesEXT meshFeat{};
			meshFeat.sType =
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
			VkPhysicalDeviceFeatures2 feat2{};
			feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			feat2.pNext = &meshFeat;
			vkGetPhysicalDeviceFeatures2_(m_physical, &feat2);
			if (!meshFeat.meshShader)
				m_report.meshExecBlocked = "meshShader feature not supported";
			else
				wantMesh = true;
		}
	}

	// ---- Presentation-tier preconditions (issue #21).
	bool wantPresent = false;
	if (!haveSurfaceExts) {
		m_report.presentBlocked = "instance surface extensions unavailable";
	} else if (m_report.graphicsQueueFamily == ~0u) {
		m_report.presentBlocked = "no graphics-capable queue family";
	} else if (!hasExtension(exts, "VK_KHR_swapchain")) {
		m_report.presentBlocked = "VK_KHR_swapchain not present";
	} else {
		wantPresent = true;
	}

	// ---- Logical device: one compute queue, plus the graphics queue and
	// the tier extensions (mesh shading, swapchain) when their
	// preconditions hold. If the tiered creation fails, fall back to the
	// plain compute-only device — the compute path must never regress
	// because a graphics tier was attempted (degradation contract).
	const float priority = 1.0f;
	VkDeviceQueueCreateInfo qcis[2] = {};
	qcis[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qcis[0].queueFamilyIndex = m_report.computeQueueFamily;
	qcis[0].queueCount = 1;
	qcis[0].pQueuePriorities = &priority;
	std::uint32_t queueInfoCount = 1;
	const bool wantGraphicsQueue = wantMesh || wantPresent;
	if (wantGraphicsQueue &&
			m_report.graphicsQueueFamily != m_report.computeQueueFamily) {
		qcis[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		qcis[1].queueFamilyIndex = m_report.graphicsQueueFamily;
		qcis[1].queueCount = 1;
		qcis[1].pQueuePriorities = &priority;
		queueInfoCount = 2;
	}

	VkDeviceCreateInfo dci{};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.queueCreateInfoCount = queueInfoCount;
	dci.pQueueCreateInfos = qcis;

	if (wantMesh || wantPresent) {
		// Request exactly the verified features (meshShader only; no task
		// stage yet — ADR-0002's condition was mesh-stage execution).
		VkPhysicalDeviceMeshShaderFeaturesEXT meshEnable{};
		meshEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
		meshEnable.meshShader = VK_TRUE;
		VkPhysicalDeviceFeatures2 feat2{};
		feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		feat2.pNext = &meshEnable;
		std::vector<const char *> devExts;
		if (wantMesh)
			devExts.push_back("VK_EXT_mesh_shader");
		if (wantPresent)
			devExts.push_back("VK_KHR_swapchain");
		VkDeviceCreateInfo tierDci = dci;
		if (wantMesh)
			tierDci.pNext = &feat2; // features chain only with its extension
		tierDci.enabledExtensionCount = static_cast<std::uint32_t>(devExts.size());
		tierDci.ppEnabledExtensionNames = devExts.data();
		if (vkCreateDevice_(m_physical, &tierDci, nullptr, &m_device) == VK_SUCCESS) {
			m_report.meshPipelineReady = wantMesh;
			m_report.presentReady = wantPresent;
		} else {
			m_device = VK_NULL_HANDLE; // fall back to the plain device below
			if (wantMesh)
				m_report.meshExecBlocked = "tier-enabled vkCreateDevice failed";
			m_report.presentBlocked = "tier-enabled vkCreateDevice failed";
		}
	}
	if (m_device == VK_NULL_HANDLE) {
		dci.queueCreateInfoCount = 1; // plain path: compute queue only
		if (vkCreateDevice_(m_physical, &dci, nullptr, &m_device) != VK_SUCCESS) {
			m_report.failureReason = "vkCreateDevice failed";
			return;
		}
	}
	vkGetDeviceQueue_(m_device, m_report.computeQueueFamily, 0, &m_queue);
	// Queue index 0 in either case: same-family shares the one created
	// queue; a distinct graphics family had exactly one queue created too.
	if (m_report.meshPipelineReady || m_report.presentReady)
		vkGetDeviceQueue_(m_device, m_report.graphicsQueueFamily, 0, &m_graphicsQueue);

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
