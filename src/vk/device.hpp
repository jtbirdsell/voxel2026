// Minimal Vulkan bring-up for the voxel2026 program (Phase 0 deliverable:
// "Vulkan device bring-up", and the gating prerequisite for spike #4).
//
// Deliberately narrow scope: dynamic loading of the installed Vulkan runtime
// (no SDK or link-time dependency — vulkan-1.dll / libvulkan.so are loaded at
// runtime, so every build host compiles this without Vulkan installed, and
// hosts without a driver get a clean "unavailable" instead of a link error),
// instance + discrete-GPU device selection, a capability report covering the
// architecture's tier flags, and a headless compute path (no surface, no
// swapchain — spike #4 measures compute, not presentation).
//
// Headers come from the Khronos Vulkan-Headers package (FetchContent,
// pinned); extension PRESENCE is probed by name string, so this code has no
// compile-time sensitivity to header vintage for reporting purposes.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace vk {

// The capability flags the architecture's renderer tiers key off
// (docs/architecture.md Part III §1).
struct CapabilityReport {
	bool available = false;        // loader + instance + device all came up
	std::string deviceName;
	std::uint32_t apiVersion = 0;  // VK_API_VERSION of the physical device
	std::uint32_t driverVersion = 0;
	bool discreteGpu = false;

	// Tier flags (extension presence by name).
	bool descriptorIndexing = false; // core in 1.2; reported for completeness
	bool descriptorBuffer = false;   // VK_EXT_descriptor_buffer
	bool descriptorHeap = false;     // VK_EXT_descriptor_heap (pre-KHR)
	bool meshShader = false;         // VK_EXT_mesh_shader
	bool shaderObject = false;       // VK_EXT_shader_object
	bool rayQuery = false;           // VK_KHR_ray_query
	bool accelerationStructure = false;
	bool timelineSemaphore = false;  // core in 1.2

	float timestampPeriodNs = 0.0f;  // ns per timestamp tick (0 = unsupported)
	std::uint32_t timestampValidBits = 0; // of the chosen compute family
	std::uint32_t computeQueueFamily = ~0u;

	// Graphics tier (issue #16 / ADR-0002 standing condition): set when the
	// logical device came up with a graphics queue AND VK_EXT_mesh_shader
	// enabled (instance >= 1.1 for the features2 chain, device API >= 1.2
	// for SPIR-V 1.4, extension present, meshShader feature supported).
	// When false, meshExecBlocked says which precondition failed — graphics
	// work SKIPs with a reason, never errors.
	std::uint32_t graphicsQueueFamily = ~0u;
	bool meshPipelineReady = false;
	std::string meshExecBlocked;

	std::string failureReason;       // set when available == false
};

// Owns the instance/device for the probe + compute path. Bring-up is
// best-effort: failure is a reported state, never an exception or abort —
// CI hosts without a GPU must degrade cleanly.
class Device {
public:
	// Loads the runtime, creates an instance, picks the best physical device
	// (discrete preferred), creates a logical device with one compute queue.
	// On any failure, available() is false and report().failureReason says why.
	Device();
	~Device();

	Device(const Device &) = delete;
	Device &operator=(const Device &) = delete;

	bool available() const { return m_report.available; }
	const CapabilityReport &report() const { return m_report; }

	VkDevice device() const { return m_device; }
	VkPhysicalDevice physicalDevice() const { return m_physical; }
	VkQueue computeQueue() const { return m_queue; }
	std::uint32_t computeQueueFamily() const { return m_report.computeQueueFamily; }

	// Graphics tier: null/-~0u unless report().meshPipelineReady.
	VkQueue graphicsQueue() const { return m_graphicsQueue; }
	std::uint32_t graphicsQueueFamily() const { return m_report.graphicsQueueFamily; }

	// Device-level function loading (the loader exposes only what bring-up
	// and the compute path need; extend as the program grows).
	PFN_vkVoidFunction deviceProc(const char *name) const;

	// Instance-level function loading (physical-device queries etc.).
	PFN_vkVoidFunction instanceLevelProc(const char *name) const;

private:
	CapabilityReport m_report;
	PFN_vkGetInstanceProcAddr m_gipa = nullptr; // per-instance, no globals
	VkInstance m_instance = VK_NULL_HANDLE;
	VkPhysicalDevice m_physical = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
};

} // namespace vk
