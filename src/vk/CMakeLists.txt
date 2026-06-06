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
FetchContent_MakeAvailable(VulkanHeaders)

# Embed the committed SPIR-V as a C array at configure time (no runtime file
# lookup, no extra tools). parity.comp is the artifact of record; regenerate
# the .spv per the comment in that file.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/parity.comp.spv PARITY_HEX HEX)
string(LENGTH "${PARITY_HEX}" PARITY_HEX_LEN)
math(EXPR PARITY_HEX_REM "${PARITY_HEX_LEN} % 8")
if(PARITY_HEX_LEN EQUAL 0 OR NOT PARITY_HEX_REM EQUAL 0)
	message(FATAL_ERROR
		"shaders/parity.comp.spv is empty or not 4-byte aligned — regenerate it "
		"(see the header comment in shaders/parity.comp)")
endif()
math(EXPR PARITY_SPIRV_WORDS "${PARITY_HEX_LEN} / 8")
# Reassemble little-endian u32 words from the byte stream.
string(REGEX REPLACE
	"([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])"
	"0x\\4\\3\\2\\1u," PARITY_SPIRV_WORDS_INITIALIZER "${PARITY_HEX}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/spirv_embed.cpp.in
	${CMAKE_CURRENT_BINARY_DIR}/spirv_embed.cpp @ONLY)

add_library(voxel2026_vk STATIC
	device.cpp
	compute.cpp
	${CMAKE_CURRENT_BINARY_DIR}/spirv_embed.cpp
)
target_include_directories(voxel2026_vk PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_vk
	PUBLIC Vulkan-Headers
	# CMAKE_DL_LIBS: dlopen/dlsym live in libdl on glibc < 2.34 (review
	# finding — ubuntu-latest only dodges this by being 24.04); expands to
	# nothing where unneeded.
	PRIVATE voxel2026_warnings ${CMAKE_DL_LIBS}
)
