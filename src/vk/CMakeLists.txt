# Vulkan bring-up module. Headers only — the runtime is loaded dynamically at
# first use (see device.cpp), so this builds on every host including CI
# runners with no GPU/driver, and never links a Vulkan SDK.

include(FetchContent)

# Pinned per the supply-chain policy. Only core-1.0 types and entry points are
# used (extensions are probed by NAME string), so header vintage is not
# load-bearing — this tag merely needs to exist and be modern-ish.
FetchContent_Declare(
	VulkanHeaders
	GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
	GIT_TAG        v1.3.290
)
# We consume HEADERS only. Vulkan-Headers' CMake auto-enables a C++20-modules
# target (Vulkan-Module) when it detects clang >= 16 — which the Unix
# Makefiles generator cannot build, breaking the linux-clang CI legs at
# configure time (CMP0077 makes this plain variable override their option()).
set(VULKAN_HEADERS_ENABLE_MODULE OFF)
FetchContent_MakeAvailable(VulkanHeaders)

# Embed the committed SPIR-V blobs as C arrays at configure time (no runtime
# file lookup, no extra tools). The .comp/.slang sources are the artifacts of
# record; regenerate the .spv files per the comments in those files.
function(voxel2026_embed_spirv SPV_PATH OUT_WORDS_VAR OUT_INIT_VAR)
	# Re-run configure when a regenerated .spv changes on disk (review
	# finding: file(READ) alone leaves the embedded array silently stale).
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SPV_PATH})
	file(READ ${SPV_PATH} _hex HEX)
	string(LENGTH "${_hex}" _len)
	math(EXPR _rem "${_len} % 8")
	if(_len EQUAL 0 OR NOT _rem EQUAL 0)
		message(FATAL_ERROR
			"${SPV_PATH} is empty or not 4-byte aligned — regenerate it")
	endif()
	math(EXPR _words "${_len} / 8")
	# Reassemble little-endian u32 words from the byte stream.
	string(REGEX REPLACE
		"([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])"
		"0x\\4\\3\\2\\1u," _init "${_hex}")
	set(${OUT_WORDS_VAR} "${_words}" PARENT_SCOPE)
	set(${OUT_INIT_VAR} "${_init}" PARENT_SCOPE)
endfunction()

voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/parity.comp.spv
	PARITY_SPIRV_WORDS PARITY_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/parity.slang.spv
	PARITY_SLANG_SPIRV_WORDS PARITY_SLANG_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/vct_inject.spv
	VCT_INJECT_SPIRV_WORDS VCT_INJECT_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/vct_mip.spv
	VCT_MIP_SPIRV_WORDS VCT_MIP_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/vct_trace.spv
	VCT_TRACE_SPIRV_WORDS VCT_TRACE_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/rc_march.spv
	RC_MARCH_SPIRV_WORDS RC_MARCH_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/rc_merge.spv
	RC_MERGE_SPIRV_WORDS RC_MERGE_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/rc_resolve.spv
	RC_RESOLVE_SPIRV_WORDS RC_RESOLVE_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/rc_sample.spv
	RC_SAMPLE_SPIRV_WORDS RC_SAMPLE_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/meshexec.spv
	MESHEXEC_SPIRV_WORDS MESHEXEC_SPIRV_WORDS_INITIALIZER)
voxel2026_embed_spirv(${CMAKE_CURRENT_SOURCE_DIR}/shaders/chunk.spv
	CHUNK_SPIRV_WORDS CHUNK_SPIRV_WORDS_INITIALIZER)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/spirv_embed.cpp.in
	${CMAKE_CURRENT_BINARY_DIR}/spirv_embed.cpp @ONLY)

add_library(voxel2026_vk STATIC
	device.cpp
	compute.cpp
	bench.cpp
	meshexec.cpp
	worldview.cpp
	${CMAKE_CURRENT_BINARY_DIR}/spirv_embed.cpp
)
target_include_directories(voxel2026_vk PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_vk
	PUBLIC Vulkan-Headers
	# CMAKE_DL_LIBS: dlopen/dlsym live in libdl on glibc < 2.34 (review
	# finding — ubuntu-latest only dodges this by being 24.04); expands to
	# nothing where unneeded. voxel2026_gen: the spike-4 bench voxelizes the
	# spike-1 minigen world as its test scene.
	PRIVATE voxel2026_warnings voxel2026_gen ${CMAKE_DL_LIBS}
)
