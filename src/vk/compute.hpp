// Headless compute dispatch for the Vulkan bring-up: runs the embedded
// parity kernel over 2^20 u32 elements, verifies GPU output bit-equals the
// CPU mirror, and reports GPU time from timestamp queries. This is the
// minimal end-to-end proof the device is REAL (not just enumerable) and the
// substrate spike #4's RC/VCT benchmarks will grow from.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vk/device.hpp"

namespace vk {

struct ParityResult {
	bool ran = false;        // dispatch executed and read back
	bool match = false;      // GPU output bit-equals the CPU mirror
	std::uint64_t elements = 0;
	double gpuMillis = -1.0; // from timestamp queries; -1 if unsupported
	std::string failureReason;
};

// The exact mix the kernel applies — shared so tests/tool and GPU agree by
// construction on the formula, not by copy-paste.
constexpr std::uint32_t parityMix(std::uint32_t v)
{
	return (v * 0x9E3779B9u) ^ (v >> 13);
}

// Allocate a host-visible buffer, fill with a deterministic pattern, run the
// kernel once, read back, compare against parityMix applied on the CPU.
ParityResult runParityDispatch(Device &dev);

} // namespace vk
