// The ignition PS3 embed: a Qt-free frontend over rpcs3_emu, exposed through the
// C ABI in ignition_ps3.h. Boots a title, services the pump, and hands the host
// video (via a hidden Metal surface), audio (a capturing backend) and input (an
// LDD pad). See docs/ps3-rpcs3-embed.md in the Ignition repo.

#include "ignition_ps3.h"

#include "Emu/System.h"
#include "util/logs.hpp"
#include "Utilities/File.h"
#include "Emu/system_config.h"
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/RSX/VK/VKGSRender.h"
#include "Emu/RSX/GSFrameBase.h"
#include "Emu/system_config.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "util/video_source.h"
#include "util/video_provider.h"
#include "Emu/Io/pad_config.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/MouseHandler.h"
#include "Emu/Io/Null/NullKeyboardHandler.h"
#include "Emu/Io/Null/NullMouseHandler.h"
#include "Emu/Audio/AudioBackend.h"
#include "Emu/Audio/audio_device_enumerator.h"
#include "Emu/Cell/Modules/sceNp.h"
#include "Input/pad_thread.h"
#include "Input/product_info.h"
#include "util/serialization.hpp"
#include "Emu/VFS.h"
#include "Emu/vfs_config.h"
#include "Emu/system_utils.hpp"
#include "Crypto/unself.h"
#include "Crypto/key_vault.h"
#include "Loader/PUP.h"
#include "Loader/TAR.h"
#include <algorithm>
#include <atomic>

#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

// rpcs3_emu calls this and leaves the frontend to define it. The GUI logs it to
// a dialog; here it goes to stderr, which is where a host running headless looks.
// Globals the emu and pad system read, defined in RPCS3's app main (rpcs3.cpp)
// which the module replaces.
// Defined at end of file; declared here for init_pad_handler above.
void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op);

cfg_input_configurations g_cfg_input_configs;
std::string g_input_config_override;

[[noreturn]] void report_fatal_error(std::string_view text, bool /*is_html*/ = false, bool /*include_help*/ = true)
{
	std::fprintf(stderr, "[rpcs3 fatal] %.*s\n", static_cast<int>(text.size()), text.data());
	std::fflush(stderr);
	std::abort();
}

// Diagnostic: mirror RPCS3's own log to stderr when IGNITION_PS3_LOG is set, so
// the embed is not blind to what the emulator reports. Off by default.
namespace
{
	struct stderr_log_listener final : logs::listener
	{
		void log(u64, const logs::message& msg, std::string_view prefix, std::string_view text) override
		{
			const auto lvl = static_cast<logs::level>(msg);
			if (lvl == logs::level::trace)
			{
				return;
			}
			const char* ch = msg->name ? msg->name : "";
			std::fprintf(stderr, "[%d] %s%s%.*s: %.*s\n", static_cast<int>(lvl),
				ch, *ch ? " " : "",
				static_cast<int>(prefix.size()), prefix.data(),
				static_cast<int>(text.size()), text.data());
		}
	};

	void maybe_install_log_listener()
	{
		static bool s_installed = false;
		if (s_installed)
		{
			return;
		}
		s_installed = true;

		// File listener, same as stock main(): RPCS3.log under the (isolated) log
		// dir, capped at a quarter of free space. Kept alive for the process.
		fs::device_stat stats{};
		const u64 cap = fs::statfs(fs::get_cache_dir(), stats) ? stats.avail_free / 4 : 128ull * 1024 * 1024;
		static std::unique_ptr<logs::listener> s_file = logs::make_file_listener(fs::get_log_dir() + "RPCS3.log", cap);

		// Optional stderr mirror for interactive debugging.
		if (const char* e = ::getenv("IGNITION_PS3_LOG"); e && e[0] == '1')
		{
			static stderr_log_listener s_listener;
			logs::listener::add(&s_listener);
		}
	}
}

// A hidden, off-screen Metal surface (ignition_metal.mm) and RPCS3's recording
// flag, both driven module-side so the Vulkan path renders with no window and
// hands frames back through present_frame -- no changes to the emulator.
extern "C" void* ignition_make_hidden_metal_view(int width, int height);
extern "C" void ignition_release_metal_view(void* view);
extern "C" char** ignition_macos_font_dirs(int* out_count);
extern atomic_t<recording_mode> g_recording_mode;

class capture_audio_backend;

struct ignition_ps3
{
	// A window-less NSView+CAMetalLayer RPCS3 renders into (see handle()).
	void* metal_view = nullptr;

	std::mutex mtx;
	// call_from_main_thread work, drained by the host each pump().
	std::deque<std::pair<std::function<void()>, atomic_t<u32>*>> work;

	// The newest presented frame, written from the RSX thread via the gs frame's
	// present_frame and read by the host on take_frame.
	std::mutex frame_mtx;
	std::vector<uint8_t> frame_data;
	uint32_t frame_w = 0, frame_h = 0, frame_pitch = 0;
	bool frame_bgra = false, frame_new = false;

	// Captured audio: read_audio pulls from cell_audio on demand (driven by the
	// host's audio clock), the same write-callback contract the real backends
	// use -- no polling thread, no intermediate ring (that double-clocked and
	// crackled). Guarded by audio_mtx.
	std::mutex audio_mtx;
	capture_audio_backend* audio_backend = nullptr;
	uint32_t audio_hz = 0;
};

// A windowless GSFrameBase. It never presents to a window: it forces the RSX to
// hand every frame back as pixels through present_frame (the offscreen/headless
// path), which land in the instance's frame buffer for the host to take.
class ignition_gs_frame : public GSFrameBase
{
public:
	void close() override {}
	void reset() override {}
	bool shown() override { return true; }
	void hide() override {}
	void show() override {}
	void toggle_fullscreen() override {}

	void delete_context(draw_context_t) override {}
	draw_context_t make_context() override { return nullptr; }
	void set_current(draw_context_t) override {}
	void flip(draw_context_t, bool) override {}

	int client_width() override { return m_width; }
	int client_height() override { return m_height; }
	f64 client_display_rate() override { return 60.0; }
	bool has_alpha() override { return false; }

	display_handle_t handle() const override;

	// Always consume: this is what makes the RSX read the frame back and call
	// present_frame every flip instead of only while recording.
	bool can_consume_frame() const override { return true; }

	void present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const override;
	void take_screenshot(std::vector<u8>&&, u32, u32, bool) override {}
	void update_title(double /*fps*/ = 0.0) override {} // added to GSFrameBase upstream; the embed has no window title

private:
	// Headless render size; updated to match the frames actually delivered.
	int m_width = 1280;
	int m_height = 720;
};

// One instance at a time, like the host's other embedded cores.
static ignition_ps3* g_inst = nullptr;

// A capturing AudioBackend: instead of playing to a device, it pulls the PS3's
// mixed output on its own thread (the same write-callback contract cubeb/coreaudio
// use) and buffers interleaved stereo s16 for the host to read. cell_audio
// downmixes to the layout the backend reports, so forcing stereo here keeps the
// host side simple.
class capture_audio_backend final : public AudioBackend
{
	std::vector<float> m_conv;

public:
	capture_audio_backend() = default;
	~capture_audio_backend() override { Close(); }

	std::string_view GetName() const override { return std::string_view("IgnitionCapture"); }

	bool Open(std::string_view, AudioFreq freq, AudioSampleSize, AudioChannelCnt, audio_channel_layout) override
	{
		Close();

		// Pull float and convert ourselves; report stereo so cell_audio downmixes.
		m_sampling_rate = freq;
		m_sample_size   = AudioSampleSize::FLOAT;
		m_layout        = audio_channel_layout::stereo;
		m_channels      = 2;

		if (g_inst)
		{
			std::lock_guard lock(g_inst->audio_mtx);
			g_inst->audio_hz = static_cast<u32>(freq);
			g_inst->audio_backend = this;
		}
		return true;
	}

	void Close() override
	{
		if (g_inst)
		{
			std::lock_guard lock(g_inst->audio_mtx);
			if (g_inst->audio_backend == this)
			{
				g_inst->audio_backend = nullptr;
			}
		}
		m_playing = false;
	}

	f64 GetCallbackFrameLen() override { return 0.005; } // 5 ms

	void Play() override  { m_playing = true; }
	void Pause() override { m_playing = false; }
	bool IsPlaying() override { return m_playing; }

	// Pull up to max_frames interleaved stereo frames from cell_audio, converted
	// to s16. Called by read_audio on the host's audio cadence -- the same
	// contract CubebBackend::data_cb uses (m_cb_mutex + m_write_callback), so
	// there is a single clock (the host device) and no drift. Returns frames
	// actually produced; the host's own buffer rides out short-term jitter.
	size_t pull_frames(int16_t* dst, size_t max_frames)
	{
		std::unique_lock lock(m_cb_mutex, std::defer_lock);
		if (!lock.try_lock_for(std::chrono::milliseconds{2}) || !m_write_callback || !m_playing)
		{
			return 0;
		}
		if (m_conv.size() < max_frames * 2)
		{
			m_conv.resize(max_frames * 2);
		}
		const u32 bytes_req = static_cast<u32>(max_frames * 2 * sizeof(float));
		u32 written = std::min(m_write_callback(bytes_req, m_conv.data()), bytes_req);
		written -= written % static_cast<u32>(2 * sizeof(float)); // whole stereo frames
		const size_t frames = written / (2 * sizeof(float));
		for (size_t i = 0; i < frames * 2; ++i)
		{
			dst[i] = static_cast<int16_t>(std::clamp(m_conv[i] * 32767.0f, -32768.0f, 32767.0f));
		}
		return frames;
	}
};

// Fills EmuCallbacks without Qt: the pump is a plain queue, and everything the
// null-renderer path does not need is a stub, exactly as headless_application
// stubs it.
static EmuCallbacks make_callbacks(ignition_ps3* self)
{
	EmuCallbacks cb{};

	cb.call_from_main_thread = [self](std::function<void()> func, atomic_t<u32>* wake_up)
	{
		std::lock_guard lock(self->mtx);
		self->work.emplace_back(std::move(func), wake_up);
	};

	cb.init_gs_render = [](utils::serial* ar)
	{
		// The embed always renders on Vulkan (into the hidden Metal surface);
		// the null renderer produces no frames, so there is nothing else to pick.
		g_fxo->init<rsx::thread, named_thread<VKGSRender>>(ar);
	};
	cb.get_gs_frame          = []() -> std::unique_ptr<GSFrameBase> { return std::make_unique<ignition_gs_frame>(); };
	cb.close_gs_frame        = []() {};
	cb.get_camera_handler    = []() -> std::shared_ptr<camera_handler_base> { return std::make_shared<null_camera_handler>(); };
	cb.get_music_handler     = []() -> std::shared_ptr<music_handler_base> { return std::make_shared<null_music_handler>(); };

	cb.get_msg_dialog                 = []() -> std::shared_ptr<MsgDialogBase> { return {}; };
	cb.get_osk_dialog                 = []() -> std::shared_ptr<OskDialogBase> { return {}; };
	cb.get_save_dialog                = []() -> std::unique_ptr<SaveDialogBase> { return {}; };
	cb.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase> { return {}; };

	cb.on_run    = [](bool) {};
	cb.on_pause  = []() {};
	cb.on_resume = []() {};
	cb.on_stop   = []() {};
	cb.on_ready  = []() {};
	cb.on_missing_fw = []() {};

	cb.enable_disc_eject  = [](bool) {};
	cb.enable_disc_insert = [](bool) {};
	cb.try_to_quit = [](bool, std::function<void()>) -> bool { return false; };
	cb.handle_taskbar_progress = [](s32, s32) {};

	cb.get_localized_string    = [](localized_string_id, const char*) -> std::string { return {}; };
	cb.get_localized_u32string = [](localized_string_id, const char*) -> std::u32string { return {}; };
	cb.get_localized_setting   = [](const cfg::_base*, u32) -> std::string { return {}; };

	cb.play_sound    = [](const std::string&, std::optional<f32>) {};
	cb.add_breakpoint = [](u32) {};
	cb.display_sleep_control_supported = []() { return false; };
	cb.enable_display_sleep = [](bool) {};
	cb.check_microphone_permissions = []() {};
	cb.make_video_source = []() -> std::unique_ptr<video_source> { return {}; };
	cb.resolve_path = [](std::string_view arg) { return std::string{arg}; };
	cb.get_database_config = [](const std::string&) -> std::string { return {}; }; // added upstream; the embed ships no game database

	// Input, audio and settings the emu calls during boot. Modelled on
	// main_application::CreateCallbacks, Qt-free: null keyboard/mouse, the
	// controller pad system with no window, a null audio backend for now
	// (real audio is a later increment), and stubs for the rest so no unset
	// std::function is ever called.
	cb.update_emu_settings = []() {};
	cb.save_emu_settings   = []() {};

	cb.init_kb_handler = []()
	{
		g_fxo->init<KeyboardHandlerBase, NullKeyboardHandler>(Emu.DeserialManager());
	};
	cb.init_mouse_handler = []()
	{
		g_fxo->init<MouseHandlerBase, NullMouseHandler>(Emu.DeserialManager());
	};
	cb.init_pad_handler = [](std::string_view title_id)
	{
		g_fxo->init<named_thread<pad_thread>>(nullptr, nullptr, title_id);
		qt_events_aware_op(0, []() { return !!pad::g_started; });
	};

	cb.get_audio = []() -> std::shared_ptr<AudioBackend> { return std::make_shared<capture_audio_backend>(); };
	cb.get_audio_enumerator = [](u64) -> std::shared_ptr<audio_device_enumerator> { return nullptr; };

	cb.get_sendmessage_dialog = []() -> std::shared_ptr<SendMessageDialogBase> { return {}; };
	cb.get_recvmessage_dialog = []() -> std::shared_ptr<RecvMessageDialogBase> { return {}; };

	cb.on_emulation_stop_no_response = [](std::shared_ptr<atomic_t<bool>>, int) {};
	cb.on_save_state_progress = [](std::shared_ptr<atomic_t<bool>>, stx::shared_ptr<utils::serial>, stx::atomic_ptr<std::string>*, std::shared_ptr<void>) {};

	cb.get_photo_path    = [](std::string_view) -> std::string { return {}; };
	cb.get_image_info    = [](const std::string&, std::string&, s32&, s32&, s32&) { return false; };
	cb.get_scaled_image  = [](const std::string&, s32, s32, s32&, s32&, u8*, bool) { return false; };
	// Same dirs Qt's QStandardPaths::FontsLocation yields, queried from the OS
	// (see ignition_macos_font_dirs) so overlay text has glyphs to draw.
	cb.get_font_dirs     = []() -> std::vector<std::string> {
		int count = 0;
		char** dirs = ignition_macos_font_dirs(&count);
		std::vector<std::string> result;
		for (int i = 0; i < count; ++i)
		{
			result.emplace_back(dirs[i]);
			free(dirs[i]);
		}
		free(dirs);
		return result;
	};
	cb.on_install_pkgs   = [](const std::vector<std::string>&) { return false; };
	cb.enable_gamemode   = [](bool) {};

	return cb;
}

extern "C" {

uint32_t ignition_ps3_abi_version(void)
{
	return IGNITION_PS3_ABI_VERSION;
}

ignition_ps3* ignition_ps3_create(const ignition_ps3_dirs* dirs)
{
	if (g_inst)
	{
		return nullptr;
	}

	maybe_install_log_listener();

	auto* self = new ignition_ps3();
	g_inst = self;

	// Create the hidden Metal surface on the caller's thread (main), and turn on
	// RPCS3's frame-capture path so the RSX hands each flip to present_frame.
	self->metal_view = ignition_make_hidden_metal_view(1280, 720);
	g_recording_mode = recording_mode::cell;

	// Root RPCS3 under Ignition's system dir rather than the global one. Set
	// before Init, which is when get_config_dir first resolves.
	if (dirs && dirs->config_dir && dirs->config_dir[0])
	{
		::setenv("RPCS3_CONFIG_DIR", dirs->config_dir, 1);
	}

	Emu.SetHasGui(false);
	Emu.SetUsr("00000001");

	// main_application detects GPUs via render_creator and tells Emu which
	// renderers are supported; the embed detects nothing, so Emu keeps its
	// default of {null} only and forces the renderer to Null at boot. That not
	// only mismatches our forced VKGSRender -- it skips the RSX overlay
	// display_manager (created only for opengl/vulkan), so there is no native
	// on-screen keyboard or message-dialog overlay. Declare Vulkan supported.
	// Only mark Vulkan as *supported* (not default): Emulator::Init ensures a
	// non-empty graphics adapter when the default is Vulkan, which we do not
	// enumerate. Supported is enough -- create() sets g_cfg.video.renderer to
	// Vulkan, and this makes Emu::Load's "is it supported" check pass instead of
	// forcing it to Null (which skipped the overlay display_manager -> no OSK).
	Emu.SetSupportedRenderers({video_renderer::null, video_renderer::vulkan});

	Emu.SetCallbacks(make_callbacks(self));
	Emu.Init();

	// Write RPCS3's own log, like main_application::InitializeEmulator does --
	// our create() otherwise skipped it, so only TTY.log appeared and the
	// emulator log (overlay/OSK/audio/etc.) was invisible for diagnosis.
	rpcs3::utils::configure_logs(Emu.IsStopped());

	// Settings the embed needs, written into the now-isolated config so boot
	// honours them: Vulkan for the offscreen path, and Write Color Buffers, which
	// some titles (e.g. Demon's Souls) need to render correctly. Decoders stay at
	// RPCS3's LLVM-recompiler defaults.
	g_cfg.video.renderer.set(video_renderer::vulkan);
	g_cfg.video.write_color_buffers.set(true);
	Emulator::SaveSettings(g_cfg.to_string(), {});

	return self;
}

// Shutdown is asynchronous in RPCS3: Kill signals the threads and an
// "Emulation Join Thread" joins them, then posts the last step (fxo reset,
// vm close, state = stopped) to the main thread. So the host must pump until
// the state is fully stopped; only then is CleanUp (g_fxo->clear) safe, and
// without it the object manager's destructor asserts at unload or exit.
void ignition_ps3_destroy(ignition_ps3* self)
{
	if (!self)
	{
		return;
	}
	if (!Emu.IsStopped(true))
	{
		Emu.Kill(false);
	}
	for (int i = 0; i < 2000 && !Emu.IsStopped(true); ++i)
	{
		ignition_ps3_pump(self);
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	if (!Emu.IsStopped(true))
	{
		std::fprintf(stderr, "[rpcs3] destroy: emulation did not stop within 10 s; leaving objects in place\n");
	}
	else
	{
		ignition_ps3_pump(self);
		Emulator::CleanUp();
	}
	if (self->metal_view)
	{
		ignition_release_metal_view(self->metal_view);
		self->metal_view = nullptr;
	}
	if (g_inst == self)
	{
		g_inst = nullptr;
	}
	delete self;
}

ignition_ps3_boot_result ignition_ps3_boot(ignition_ps3* self, const char* game_path)
{
	if (!self || !game_path)
	{
		return static_cast<ignition_ps3_boot_result>(game_boot_result::generic_error);
	}
	return static_cast<ignition_ps3_boot_result>(Emu.BootGame(game_path));
}

ignition_ps3_state ignition_ps3_state_of(const ignition_ps3*)
{
	switch (Emu.GetStatus())
	{
	case system_state::running:  return IGNITION_PS3_RUNNING;
	case system_state::paused:
	case system_state::frozen:   return IGNITION_PS3_PAUSED;
	case system_state::loading:
	case system_state::ready:
	case system_state::starting: return IGNITION_PS3_LOADING;
	case system_state::stopping: return IGNITION_PS3_STOPPING;
	case system_state::stopped:  return IGNITION_PS3_STOPPED;
	}
	return IGNITION_PS3_STOPPED;
}

void ignition_ps3_pause(ignition_ps3*)  { Emu.Pause(); }
void ignition_ps3_resume(ignition_ps3*) { Emu.Resume(); }
// Async: ask the game to exit (RPCS3 kills it if it does not respond) and let
// the host pump the shutdown through, as it pumps everything else.
void ignition_ps3_stop(ignition_ps3*)   { Emu.GracefulShutdown(false, true); }

uint32_t ignition_ps3_pump(ignition_ps3* self)
{
	if (!self)
	{
		return 0;
	}
	std::deque<std::pair<std::function<void()>, atomic_t<u32>*>> batch;
	{
		std::lock_guard lock(self->mtx);
		batch.swap(self->work);
	}
	for (auto& [func, wake_up] : batch)
	{
		func();
		if (wake_up)
		{
			*wake_up = 1;
			wake_up->notify_one();
		}
	}
	return static_cast<uint32_t>(batch.size());
}

int32_t ignition_ps3_take_frame(ignition_ps3* self, ignition_ps3_frame* out)
{
	if (!self || !out)
	{
		return 0;
	}
	std::lock_guard lock(self->frame_mtx);
	if (!self->frame_new || self->frame_data.empty())
	{
		return 0;
	}
	out->data = self->frame_data.data();
	out->pitch = self->frame_pitch;
	out->width = self->frame_w;
	out->height = self->frame_h;
	out->is_bgra = self->frame_bgra ? 1 : 0;
	self->frame_new = false;
	return 1;
}
void ignition_ps3_set_pad(ignition_ps3* self, uint32_t port, const ignition_ps3_pad* in)
{
	if (!self || !in || port >= static_cast<uint32_t>(CELL_PAD_MAX_PORT_NUM))
	{
		return;
	}

	// The pad system comes up during boot; before then there is nothing to feed.
	pad_thread* pt = pad::get_pad_thread(true);
	if (!pt)
	{
		return;
	}

	auto& pad = pt->GetPads()[port];
	if (!pad)
	{
		return;
	}

	// Present this port as a logical-device (LDD) pad the first time the host
	// touches it: a connected PS3 controller whose report the host fills in
	// directly, bypassing the physical-device handlers. Registered once --
	// InitLddPad reinitialises the port, so calling it every frame would wipe
	// the connection each time.
	if (!pad->ldd)
	{
		// Register this port as a connected logical-device (LDD) PS3 pad via the
		// real pad_thread API, not a hand-rolled pad->Init: InitLddPad also bumps
		// num_ldd_pad, which feeds pad_thread's now_connect. The RSX overlay
		// (native OSK, message dialogs) gates input on now_connect, so without
		// this the game reads our pad via cellPad but the on-screen keyboard and
		// dialogs ignore it -- e.g. you cannot enter a character name.
		std::lock_guard lock(pad::g_pad_mutex);
		pt->InitLddPad(port, nullptr);
	}

	// The host samples buttons already in cell-pad order: DIGITAL1 in the low
	// 16 bits (d-pad, Start/Select, L3/R3, PS), DIGITAL2 in the high 16
	// (face buttons, L1/L2/R1/R2). Sticks are signed and centred at zero; the
	// PS3 wants unsigned bytes centred at 128.
	const u16 digital1 = static_cast<u16>(in->buttons & 0xffff);
	const u16 digital2 = static_cast<u16>((in->buttons >> 16) & 0xffff);

	const auto to_u8 = [](int16_t v) -> u16
	{
		return static_cast<u16>(std::clamp((v >> 8) + 128, 0, 255));
	};

	auto& d = pad->ldd_data;
	d.button[CELL_PAD_BTN_OFFSET_DIGITAL1]     = digital1;
	d.button[CELL_PAD_BTN_OFFSET_DIGITAL2]     = digital2;
	d.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X]  = to_u8(in->lx);
	d.button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y]  = to_u8(in->ly);
	d.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X] = to_u8(in->rx);
	d.button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y] = to_u8(in->ry);

	// Digital pad: a held button reads as fully pressed. Games that read the
	// pressure bytes (offsets 8-19) then behave the same as with a real DS3.
	const auto press = [&](u32 offset, u16 mask, u16 bits) { d.button[offset] = (bits & mask) ? 255 : 0; };
	press(CELL_PAD_BTN_OFFSET_PRESS_RIGHT,    CELL_PAD_CTRL_RIGHT,    digital1);
	press(CELL_PAD_BTN_OFFSET_PRESS_LEFT,     CELL_PAD_CTRL_LEFT,     digital1);
	press(CELL_PAD_BTN_OFFSET_PRESS_UP,       CELL_PAD_CTRL_UP,       digital1);
	press(CELL_PAD_BTN_OFFSET_PRESS_DOWN,     CELL_PAD_CTRL_DOWN,     digital1);
	press(CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE, CELL_PAD_CTRL_TRIANGLE, digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_CIRCLE,   CELL_PAD_CTRL_CIRCLE,   digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_CROSS,    CELL_PAD_CTRL_CROSS,    digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_SQUARE,   CELL_PAD_CTRL_SQUARE,   digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_L1,       CELL_PAD_CTRL_L1,       digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_R1,       CELL_PAD_CTRL_R1,       digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_L2,       CELL_PAD_CTRL_L2,       digital2);
	press(CELL_PAD_BTN_OFFSET_PRESS_R2,       CELL_PAD_CTRL_R2,       digital2);
}
size_t ignition_ps3_read_audio(ignition_ps3* self, int16_t* buf, size_t max_frames)
{
	if (!self || !buf || !max_frames)
	{
		return 0;
	}
	std::lock_guard lock(self->audio_mtx);
	if (!self->audio_backend)
	{
		return 0;
	}
	return self->audio_backend->pull_frames(buf, max_frames);
}

uint32_t ignition_ps3_audio_rate(const ignition_ps3* self)
{
	// Written once at Open and then constant; a plain read needs no lock.
	return self ? self->audio_hz : 0;
}

// Installs a PS3 firmware PUP into dev_flash, the same extraction RPCS3's GUI
// does: parse the PUP, take file 0x300 (a TAR of dev_flash_* SELFs), decrypt
// each and extract its inner TAR into the mounted dev_flash. Zero on success.
int32_t ignition_ps3_install_firmware(ignition_ps3*, const char* pup_path)
{
	if (!pup_path)
	{
		return -1;
	}
	fs::file pup_f(pup_path);
	if (!pup_f)
	{
		return -1;
	}
	pup_object pup(std::move(pup_f));
	if (pup.operator pup_error() != pup_error::ok)
	{
		return -2;
	}
	fs::file update_files_f = pup.get_file(0x300);
	if (!update_files_f || !update_files_f.size())
	{
		return -3;
	}
	tar_object update_files(update_files_f);
	auto names = update_files.get_filenames();
	names.erase(std::remove_if(names.begin(), names.end(),
		[](const std::string& n) { return n.find("dev_flash_") == umax; }), names.end());
	if (names.empty())
	{
		return -4;
	}

	vfs::mount("/dev_flash", g_cfg_vfs.get_dev_flash());

	for (const auto& name : names)
	{
		auto stream = update_files.get_file(name);
		if (stream->m_file_handler)
		{
			stream->m_file_handler->handle_file_op(*stream, 0, stream->get_size(umax), nullptr);
		}
		fs::file inner = fs::make_stream(std::move(stream->data));
		SCEDecrypter dec(inner);
		dec.LoadHeaders();
		dec.LoadMetadata(SCEPKG_ERK, SCEPKG_RIV);
		dec.DecryptData();
		auto tar_data = dec.MakeFile();
		if (tar_data.size() < 3)
		{
			return -5;
		}
		tar_object dev_flash_tar(tar_data[2]);
		if (!dev_flash_tar.extract())
		{
			return -6;
		}
	}
	return 0;
}

int32_t ignition_ps3_save_state(ignition_ps3*, const char*) { return -1; }
int32_t ignition_ps3_load_state(ignition_ps3*, const char*) { return -1; }

} // extern "C"

// The emu (Emu/System.cpp) calls this in its boot and pause loops to run an op
// while keeping the frontend responsive; RPCS3's GUI pumps Qt events here. Ours
// drains the call_from_main_thread queue instead, so it needs no Qt. C++
// linkage, matching the declaration in Emu/System.cpp.
void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
	// Mirror the Qt frontend's version (rpcs3qt/main_window.cpp): spin until
	// wrapped_op reports it is done, servicing the call_from_main_thread queue
	// between checks. Two things must match stock or the emulator misbehaves:
	//   - the condition is `!wrapped_op()` -- wait until true, not while true;
	//   - only the main thread drains the queue. Running the emulator's
	//     main-thread work from a worker thread corrupts its state, so other
	//     threads only wait, exactly as stock's non-main path does.
	while (!wrapped_op())
	{
		if (thread_ctrl::is_main() && g_inst)
		{
			ignition_ps3_pump(g_inst);
		}

		if (repeat_duration_ms > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(repeat_duration_ms));
		}
		else
		{
			std::this_thread::yield();
		}
	}
}

// Writes the frame the RSX just handed back into the live instance's buffer.
// Called on the RSX thread; the mutex guards against the host's take_frame.
// The hidden Metal surface RPCS3 renders into; created in create() on the main
// thread, returned here on the RSX thread.
display_handle_t ignition_gs_frame::handle() const
{
	return g_inst ? g_inst->metal_view : nullptr;
}

void ignition_gs_frame::present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const
{
	if (!g_inst)
	{
		return;
	}
	std::lock_guard lock(g_inst->frame_mtx);
	g_inst->frame_data = std::move(data);
	g_inst->frame_pitch = pitch;
	g_inst->frame_w = width;
	g_inst->frame_h = height;
	g_inst->frame_bgra = is_bgra;
	g_inst->frame_new = true;
}
