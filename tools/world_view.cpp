// First world view (issue #21) — the developer-hardware demo over the
// connected belt: generate -> Contract-1 blob -> SQLite store -> unpack ->
// greedy mesh on the job system -> world-view renderer -> swapchain.
//
// Issue #23 made it WALKABLE: G drops into walk mode — a deterministic
// player box (sim::playerStep over the SolidityWorld adapter, fixed-dt
// accumulator) with gravity, ground contact, and jumping; F returns to the
// free-fly camera. The camera-frame trig that turns WASD into a world-space
// move vector lives HERE, on the presentation side — the deterministic
// kernel only ever sees the resulting input floats (the replay-determinism
// claim is pinned by tests/test_player.cpp over scripted inputs, not by
// this interactive loop).
//
// Issue #24 made the world DYNAMIC: a camera-centered streaming window
// (world::Streamer) replaces the fixed pre-loaded region — chunks
// load-or-generate under a per-frame admission budget, mesh neighbor-aware
// on the job system, and evict behind you; the world is now unbounded in
// x/z. On every changed pump the scene re-uploads wholesale (the measured
// v1 cost printed in the summary; issue #25 tracks the incremental
// device-local pools that replace it) and the walk-mode solidity world
// rebuilds over the current residents.
//
// Windows-only dev tool (the offscreen renderer is the CI-validated path;
// this is the human-facing one). Controls: WASD move, arrow keys look,
// G walk / F fly, SPACE jump (walk), Q/E down/up (fly), ESC quit.
// --frames N exits after N frames (automation); --db PATH persists the
// world between runs (default: in-memory); --radius N sets the window
// radius in chunks (default 5).
//
// Issue #22 upgraded the loop to the persistent WorldRenderer: created-once
// pipeline/depth/geometry (device-local, staged upload), reused command
// slots, semaphore-paced acquire/render/present, GPU time from timestamp
// queries. The old v1 host-serial path survives as the one-shot
// renderWorldView* functions (the CI-validated reference the equivalence
// gate compares against).

#if !defined(_WIN32)
#include <cstdio>
int main()
{
	std::puts("world_view is a Windows-only demo in v1 (the offscreen renderer "
			  "is the portable, CI-validated path)");
	return 0;
}
#else

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "jobs/jobs.hpp"
#include "sim/player.hpp"
#include "store/sqlite_store.hpp"
#include "vk/device.hpp"
#include "vk/worldrenderer.hpp"
#include "vk/worldview.hpp"
#include "world/solidity.hpp"
#include "world/stream.hpp"

namespace {

bool opaqueNonZero(const world::CellValue &c)
{
	return c.content != 0;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, w, l);
}

bool keyDown(int virtualKey)
{
	return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

// Camera eye above the player box center (presentation only — never enters
// the simulation). Box center rests at floor + 0.4375, so eyes sit ~1.6
// above the feet.
constexpr float kEyeAboveCenter = 1.1625f;

constexpr sim::Vec3 kSpawn{64.0f, 40.0f, 64.0f}; // above the region center

} // namespace

int main(int argc, char **argv)
{
	std::uint64_t maxFrames = ~0ull;
	std::string dbPath = ":memory:";
	std::int32_t radius = 5;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
			maxFrames = std::strtoull(argv[++i], nullptr, 10);
		else if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc)
			dbPath = argv[++i];
		else if (std::strcmp(argv[i], "--radius") == 0 && i + 1 < argc)
			radius = static_cast<std::int32_t>(std::strtol(argv[++i], nullptr, 10));
	}
	radius = radius < 1 ? 1 : (radius > 16 ? 16 : radius);

	vk::Device dev;
	if (!dev.available()) {
		std::printf("Vulkan unavailable: %s\n", dev.report().failureReason.c_str());
		return 0;
	}
	if (!dev.report().presentReady) {
		std::printf("presentation tier unavailable: %s\n",
				dev.report().presentBlocked.c_str());
		return 0;
	}

	// ---- The streamer (issue #24): camera-centered window over the belt,
	// meshed neighbor-aware on the job system. Replaces the fixed 8x2x8
	// region — the world is unbounded in x/z now.
	world::SqliteStore store(dbPath);
	if (!store.ok()) {
		std::printf("store unavailable: %s\n", store.error().c_str());
		return 1;
	}
	jobs::JobSystem js;
	world::Streamer streamer(store, gen::kStandardSeed,
			{.radius = radius, .cyMin = 0, .cyMax = 1, .budget = 8},
			&opaqueNonZero);
	const auto chunkOf = [](float v) {
		return static_cast<std::int32_t>(std::floor(v / 16.0f));
	};

	// ---- Window.
	const HINSTANCE hinst = GetModuleHandleW(nullptr);
	WNDCLASSW wc{};
	wc.lpfnWndProc = wndProc;
	wc.hInstance = hinst;
	wc.lpszClassName = L"voxel2026_world_view";
	wc.hCursor = LoadCursorA(nullptr, IDC_ARROW); // ANSI macro w/o UNICODE define
	RegisterClassW(&wc);
	const std::uint32_t winW = 1280, winH = 720;
	RECT rect{0, 0, static_cast<LONG>(winW), static_cast<LONG>(winH)};
	AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
			L"voxel2026 — first walkable world (G walk / F fly, WASD, SPACE jump, ESC)",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
			rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hinst,
			nullptr);
	if (!hwnd) {
		std::puts("window creation failed");
		return 1;
	}
	ShowWindow(hwnd, SW_SHOW);

	// ---- Surface + swapchain via the dynamic loader.
	const auto ip = [&dev](const char *name) { return dev.instanceLevelProc(name); };
	const auto dp = [&dev](const char *name) { return dev.deviceProc(name); };
#define LOAD_I(fn) auto fn##_ = reinterpret_cast<PFN_##fn>(ip(#fn))
#define LOAD_D(fn) auto fn##_ = reinterpret_cast<PFN_##fn>(dp(#fn))
	LOAD_I(vkCreateWin32SurfaceKHR);
	LOAD_I(vkGetPhysicalDeviceSurfaceSupportKHR);
	LOAD_I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	LOAD_I(vkGetPhysicalDeviceSurfaceFormatsKHR);
	LOAD_I(vkDestroySurfaceKHR);
	LOAD_D(vkCreateSwapchainKHR);
	LOAD_D(vkGetSwapchainImagesKHR);
	LOAD_D(vkAcquireNextImageKHR);
	LOAD_D(vkQueuePresentKHR);
	LOAD_D(vkDestroySwapchainKHR);
	LOAD_D(vkCreateImageView);
	LOAD_D(vkDestroyImageView);
	LOAD_D(vkCreateSemaphore);
	LOAD_D(vkDestroySemaphore);
	LOAD_D(vkQueueWaitIdle);
#undef LOAD_I
#undef LOAD_D
	if (!vkCreateWin32SurfaceKHR_ || !vkGetPhysicalDeviceSurfaceSupportKHR_ ||
			!vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ ||
			!vkGetPhysicalDeviceSurfaceFormatsKHR_ || !vkDestroySurfaceKHR_ ||
			!vkCreateSwapchainKHR_ || !vkGetSwapchainImagesKHR_ ||
			!vkAcquireNextImageKHR_ || !vkQueuePresentKHR_ || !vkDestroySwapchainKHR_ ||
			!vkCreateImageView_ || !vkDestroyImageView_ || !vkCreateSemaphore_ ||
			!vkDestroySemaphore_ || !vkQueueWaitIdle_) {
		std::puts("surface/swapchain entry points not resolvable");
		return 1;
	}

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkWin32SurfaceCreateInfoKHR sci{};
	sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	sci.hinstance = hinst;
	sci.hwnd = hwnd;
	if (vkCreateWin32SurfaceKHR_(dev.instance(), &sci, nullptr, &surface) !=
			VK_SUCCESS) {
		std::puts("surface creation failed");
		return 1;
	}

	VkBool32 supported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR_(dev.physicalDevice(),
			dev.graphicsQueueFamily(), surface, &supported);
	if (!supported) {
		std::puts("graphics queue family cannot present to this surface");
		vkDestroySurfaceKHR_(dev.instance(), surface, nullptr);
		return 1;
	}

	VkSurfaceCapabilitiesKHR caps{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(dev.physicalDevice(), surface, &caps);
	std::uint32_t fmtCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR_(dev.physicalDevice(), surface, &fmtCount,
			nullptr);
	std::vector<VkSurfaceFormatKHR> formats(fmtCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR_(dev.physicalDevice(), surface, &fmtCount,
			formats.data());
	VkSurfaceFormatKHR chosen = formats.empty()
			? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
					  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
			: formats[0];
	for (const VkSurfaceFormatKHR &f : formats)
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
			chosen = f;
			break;
		}
	const VkExtent2D extent = caps.currentExtent.width != 0xFFFFFFFFu
			? caps.currentExtent
			: VkExtent2D{winW, winH};

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkSwapchainCreateInfoKHR scci{};
	scci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	scci.surface = surface;
	scci.minImageCount = caps.minImageCount + (caps.maxImageCount == 0 ||
											  caps.maxImageCount > caps.minImageCount
									? 1
									: 0);
	scci.imageFormat = chosen.format;
	scci.imageColorSpace = chosen.colorSpace;
	scci.imageExtent = extent;
	scci.imageArrayLayers = 1;
	scci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	scci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scci.preTransform = caps.currentTransform;
	scci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	scci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
	scci.clipped = VK_TRUE;
	if (vkCreateSwapchainKHR_(dev.device(), &scci, nullptr, &swapchain) != VK_SUCCESS) {
		std::puts("swapchain creation failed");
		vkDestroySurfaceKHR_(dev.instance(), surface, nullptr);
		return 1;
	}
	std::uint32_t imageCount = 0;
	vkGetSwapchainImagesKHR_(dev.device(), swapchain, &imageCount, nullptr);
	std::vector<VkImage> images(imageCount);
	vkGetSwapchainImagesKHR_(dev.device(), swapchain, &imageCount, images.data());
	std::vector<VkImageView> views(imageCount, VK_NULL_HANDLE);
	for (std::uint32_t i = 0; i < imageCount; ++i) {
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = images[i];
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = chosen.format;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCreateImageView_(dev.device(), &vci, nullptr, &views[i]);
	}
	// Pacing (issue #22): per-slot acquire semaphores (lockstep with the
	// renderer's internal command slots) + per-image render-finished
	// semaphores; the renderer's slot fences cap frames in flight.
	VkSemaphore imageAvailable[vk::WorldRenderer::kFramesInFlight] = {};
	std::vector<VkSemaphore> renderFinished(imageCount, VK_NULL_HANDLE);
	{
		VkSemaphoreCreateInfo semci{};
		semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (auto &s : imageAvailable)
			vkCreateSemaphore_(dev.device(), &semci, nullptr, &s);
		for (auto &s : renderFinished)
			vkCreateSemaphore_(dev.device(), &semci, nullptr, &s);
	}

	vk::WorldRenderer renderer(dev, chosen.format, extent.width, extent.height);
	if (!renderer.ok()) {
		std::printf("renderer init failed: %s\n", renderer.error().c_str());
		return 1;
	}

	// Scene + solidity rebuild over the current residents. Chunk and mesh
	// pointers point into the streamer's map nodes (stable until eviction);
	// uploadScene copies everything device-side before returning, and the
	// solidity world is rebuilt wholesale (cheap: a pointer map).
	std::vector<vk::ChunkDraw> draws;
	world::SolidityWorld solidity;
	std::uint64_t uploads = 0;
	double uploadMsAccum = 0;
	const auto rebuildScene = [&]() -> bool {
		draws.clear();
		solidity = world::SolidityWorld{};
		for (const auto &[key, res] : streamer.residents()) {
			solidity.addChunk(key.cx, key.cy, key.cz, &res.chunk);
			if (res.meshed && res.mesh.quadCount > 0)
				draws.push_back({{static_cast<float>(key.cx * 16),
										 static_cast<float>(key.cy * 16),
										 static_cast<float>(key.cz * 16)},
						&res.mesh});
		}
		const auto t0 = std::chrono::steady_clock::now();
		const bool ok = renderer.uploadScene(draws);
		uploadMsAccum += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - t0)
								 .count();
		++uploads;
		return ok;
	};

	// ---- Fly/walk loop.
	vk::WorldCamera cam;
	cam.eye = {64, 56, 150};

	// Prime the window at the spawn so the first frame has a world; from
	// here on, pump() streams under the per-frame admission budget.
	{
		streamer.setCenter(chunkOf(cam.eye.x), chunkOf(cam.eye.z));
		const auto t0 = std::chrono::steady_clock::now();
		int pumps = 0;
		while (!streamer.quiescent() && pumps < 4096) {
			streamer.pump(&js);
			++pumps;
		}
		std::uint64_t quads = 0;
		for (const auto &[key, res] : streamer.residents())
			quads += res.mesh.quadCount;
		std::printf("primed: %zu chunks, %llu quads, %.1f ms on %u threads "
					"(radius %d, %d pumps)\n",
				streamer.residents().size(), static_cast<unsigned long long>(quads),
				std::chrono::duration<double, std::milli>(
						std::chrono::steady_clock::now() - t0)
						.count(),
				js.threadCount(), radius, pumps);
	}
	if (!rebuildScene()) {
		std::printf("scene upload failed: %s\n", renderer.error().c_str());
		return 1;
	}

	std::uint64_t streamedIn = 0, streamedFromStore = 0, streamedOut = 0,
			remeshed = 0, changedPumps = 0;
	double pumpMsAccum = 0, pumpMsMax = 0;
	float yaw = -1.5708f; // facing -Z
	float pitch = -0.35f;
	bool walkMode = false;
	bool prevG = false, prevF = false;
	sim::Entity player;
	player.halfExtents = sim::kPlayerHalfExtents;
	const sim::StepParams simParams{};
	const sim::WalkParams walkParams{};
	float simAccum = 0.0f;
	std::uint64_t frames = 0;
	double frameMsAccum = 0;
	auto last = std::chrono::steady_clock::now();
	bool running = true;
	std::puts("controls: WASD move, arrows look, G walk / F fly, SPACE jump, ESC quits");
	while (running && frames < maxFrames) {
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT)
				running = false;
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		if (!running || keyDown(VK_ESCAPE))
			break;

		const auto now = std::chrono::steady_clock::now();
		const float dt =
				std::chrono::duration<float>(now - last).count();
		last = now;

		const float lookSpeed = 1.6f * dt;
		if (keyDown(VK_LEFT))
			yaw -= lookSpeed;
		if (keyDown(VK_RIGHT))
			yaw += lookSpeed;
		if (keyDown(VK_UP))
			pitch += lookSpeed;
		if (keyDown(VK_DOWN))
			pitch -= lookSpeed;
		pitch = pitch < -1.5f ? -1.5f : (pitch > 1.5f ? 1.5f : pitch);
		const vk::Float3 fwd{std::cos(pitch) * std::cos(yaw), std::sin(pitch),
				std::cos(pitch) * std::sin(yaw)};
		const vk::Float3 right{-std::sin(yaw), 0, std::cos(yaw)};

		// Mode toggles (edge-triggered). Entering walk mode drops the player
		// at the camera; entering fly mode keeps the camera where it is.
		const bool gNow = keyDown('G'), fNow = keyDown('F');
		if (gNow && !prevG && !walkMode) {
			walkMode = true;
			player.pos = {cam.eye.x, cam.eye.y - kEyeAboveCenter, cam.eye.z};
			player.vel = {};
			player.onGround = false;
			simAccum = 0.0f;
			std::puts("walk mode (G): WASD walk, SPACE jump, F to fly");
		}
		if (fNow && !prevF && walkMode) {
			walkMode = false;
			std::puts("fly mode (F): WASD fly, Q/E down/up, G to walk");
		}
		prevG = gNow;
		prevF = fNow;

		if (walkMode) {
			// Camera-frame trig -> world-space intent, normalized on the
			// PRESENTATION side; the deterministic kernel only ever sees the
			// resulting floats. Holding SPACE re-jumps on landing (kernel
			// semantics — see player.hpp).
			float ix = 0.0f, iz = 0.0f;
			const float fx = std::cos(yaw), fz = std::sin(yaw);
			if (keyDown('W')) {
				ix += fx;
				iz += fz;
			}
			if (keyDown('S')) {
				ix -= fx;
				iz -= fz;
			}
			if (keyDown('D')) {
				ix += -std::sin(yaw);
				iz += std::cos(yaw);
			}
			if (keyDown('A')) {
				ix -= -std::sin(yaw);
				iz -= std::cos(yaw);
			}
			const float len = std::sqrt(ix * ix + iz * iz);
			if (len > 1.0f) {
				ix /= len;
				iz /= len;
			}
			const sim::PlayerInput in{ix, iz, keyDown(VK_SPACE)};

			// Fixed-dt accumulator: render rate varies, simulation ticks do
			// not. Input is sampled once per FRAME and held for every tick
			// the accumulator releases (catch-up ticks reuse it). The clamp
			// bounds catch-up work after a stall (no spiral of death);
			// walking is interactive here, replay-determinism is the test
			// suite's claim.
			simAccum += dt > 0.25f ? 0.25f : dt;
			while (simAccum >= simParams.dt) {
				sim::playerStep(player, in, walkParams, simParams,
						sim::WorldOffset{}, &world::SolidityWorld::solidAt,
						&solidity);
				simAccum -= simParams.dt;
			}
			if (player.pos.y < -16.0f) { // walked off the region into the void
				player.pos = kSpawn;
				player.vel = {};
				std::puts("fell out of the world - respawned");
			}
			cam.eye = {player.pos.x, player.pos.y + kEyeAboveCenter,
					player.pos.z};
		} else {
			const float moveSpeed = 40.0f * dt;
			if (keyDown('W')) {
				cam.eye.x += fwd.x * moveSpeed;
				cam.eye.y += fwd.y * moveSpeed;
				cam.eye.z += fwd.z * moveSpeed;
			}
			if (keyDown('S')) {
				cam.eye.x -= fwd.x * moveSpeed;
				cam.eye.y -= fwd.y * moveSpeed;
				cam.eye.z -= fwd.z * moveSpeed;
			}
			if (keyDown('A')) {
				cam.eye.x -= right.x * moveSpeed;
				cam.eye.z -= right.z * moveSpeed;
			}
			if (keyDown('D')) {
				cam.eye.x += right.x * moveSpeed;
				cam.eye.z += right.z * moveSpeed;
			}
			if (keyDown('Q'))
				cam.eye.y -= moveSpeed;
			if (keyDown('E'))
				cam.eye.y += moveSpeed;
		}
		cam.target = {cam.eye.x + fwd.x, cam.eye.y + fwd.y, cam.eye.z + fwd.z};

		// Streaming slice (issue #24): follow the camera, admit under the
		// budget, rebuild scene + solidity only on change. The walk step
		// above used last frame's solidity — safe, because the window is
		// centered on the camera and the chunks underfoot have been resident
		// since priming.
		{
			streamer.setCenter(chunkOf(cam.eye.x), chunkOf(cam.eye.z));
			const auto pumpT0 = std::chrono::steady_clock::now();
			const world::PumpStats st = streamer.pump(&js);
			if (st.changed()) {
				const double ms = std::chrono::duration<double, std::milli>(
						std::chrono::steady_clock::now() - pumpT0)
										  .count();
				pumpMsAccum += ms;
				pumpMsMax = ms > pumpMsMax ? ms : pumpMsMax;
				streamedIn += st.loaded;
				streamedFromStore += st.loadedFromStore;
				streamedOut += st.evicted;
				remeshed += st.meshed;
				++changedPumps;
				if (!rebuildScene()) {
					std::printf("scene upload failed: %s\n",
							renderer.error().c_str());
					break;
				}
			}
		}

		// Paced presentation (issue #22): semaphore-chained acquire ->
		// render -> present; the renderer's internal slot fences bound the
		// frames in flight.
		const std::uint32_t slot = static_cast<std::uint32_t>(
				frames % vk::WorldRenderer::kFramesInFlight);
		std::uint32_t imageIndex = 0;
		const VkResult acq = vkAcquireNextImageKHR_(dev.device(), swapchain, ~0ull,
				imageAvailable[slot], VK_NULL_HANDLE, &imageIndex);
		if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
			break;

		std::string err;
		if (!renderer.renderFrameAsync(cam, views[imageIndex], imageAvailable[slot],
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					renderFinished[imageIndex], err)) {
			std::printf("render failed: %s\n", err.c_str());
			break;
		}

		VkPresentInfoKHR present{};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &renderFinished[imageIndex];
		present.swapchainCount = 1;
		present.pSwapchains = &swapchain;
		present.pImageIndices = &imageIndex;
		vkQueuePresentKHR_(dev.graphicsQueue(), &present);

		frameMsAccum +=
				std::chrono::duration<double, std::milli>(
						std::chrono::steady_clock::now() - now)
						.count();
		++frames;
	}

	// Two numbers, named for what they are: the GPU SPAN from timestamp
	// queries (TOP..BOTTOM of the submission — in the semaphore-paced loop
	// it can include in-queue waits; the offscreen test path measures pure
	// spans), and loop time (FIFO-capped at the display refresh: pacing,
	// not cost).
	if (frames > 0)
		std::printf("frames: %llu | GPU span %.2f ms (timestamped) | loop %.1f ms "
					"(FIFO-paced)\n",
				static_cast<unsigned long long>(frames), renderer.lastGpuMillis(),
				frameMsAccum / double(frames));
	// Streaming budgets, named for what they include: pump = belt slice +
	// neighbor-aware meshing on the frame thread (averaged over CHANGED
	// pumps only; quiescent pumps are no-ops and the priming fill is
	// excluded); per-pump load shows the admission budget actually holding;
	// upload = the wholesale scene re-upload (the measured v1 cost;
	// incremental device-local pools are the named refinement).
	if (streamedIn + streamedOut + remeshed > 0)
		std::printf("streamed: %llu in (%llu from store) / %llu out / %llu "
					"meshed over %llu pumps (%.1f loaded/pump) | pump avg %.2f "
					"max %.2f ms | upload avg %.2f ms (%llu uploads)\n",
				static_cast<unsigned long long>(streamedIn),
				static_cast<unsigned long long>(streamedFromStore),
				static_cast<unsigned long long>(streamedOut),
				static_cast<unsigned long long>(remeshed),
				static_cast<unsigned long long>(changedPumps),
				changedPumps > 0 ? double(streamedIn) / double(changedPumps) : 0.0,
				changedPumps > 0 ? pumpMsAccum / double(changedPumps) : 0.0,
				pumpMsMax, uploads > 0 ? uploadMsAccum / double(uploads) : 0.0,
				static_cast<unsigned long long>(uploads));

	// Teardown: drain the queue (in-flight frames + presents), then destroy.
	vkQueueWaitIdle_(dev.graphicsQueue());
	for (VkSemaphore s : imageAvailable)
		if (s)
			vkDestroySemaphore_(dev.device(), s, nullptr);
	for (VkSemaphore s : renderFinished)
		if (s)
			vkDestroySemaphore_(dev.device(), s, nullptr);
	for (VkImageView v : views)
		if (v)
			vkDestroyImageView_(dev.device(), v, nullptr);
	vkDestroySwapchainKHR_(dev.device(), swapchain, nullptr);
	vkDestroySurfaceKHR_(dev.instance(), surface, nullptr);
	DestroyWindow(hwnd);
	return 0;
}
#endif
