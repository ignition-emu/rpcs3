// The ignition PS3 embed: a Qt-free frontend over rpcs3_emu, exposed through the
// C ABI in ignition_ps3.h. First increment -- prove the module builds, links
// rpcs3_emu, boots a title and services the pump. Video, audio and input are
// stubbed here and land in later increments (see docs/ps3-rpcs3-embed.md).

#include "ignition_ps3.h"

#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/Io/Null/null_camera_handler.h"
#include "Emu/Io/Null/null_music_handler.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"

#include <cstdio>
#include <deque>
#include <mutex>
#include <utility>

// rpcs3_emu calls this and leaves the frontend to define it. The GUI logs it to
// a dialog; here it goes to stderr, which is where a host running headless looks.
[[noreturn]] void report_fatal_error(std::string_view text, bool /*is_html*/ = false, bool /*include_help*/ = true)
{
	std::fprintf(stderr, "[rpcs3 fatal] %.*s\n", static_cast<int>(text.size()), text.data());
	std::fflush(stderr);
	std::abort();
}

struct ignition_ps3
{
	std::mutex mtx;
	// call_from_main_thread work, drained by the host each pump().
	std::deque<std::pair<std::function<void()>, atomic_t<u32>*>> work;
};

// One instance at a time, like the host's other embedded cores.
static ignition_ps3* g_inst = nullptr;

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
		g_fxo->init<rsx::thread, named_thread<NullGSRender>>(ar);
	};
	cb.get_gs_frame          = []() -> std::unique_ptr<GSFrameBase> { return {}; };
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

	return cb;
}

extern "C" {

uint32_t ignition_ps3_abi_version(void)
{
	return IGNITION_PS3_ABI_VERSION;
}

ignition_ps3* ignition_ps3_create(const ignition_ps3_dirs* /*dirs*/)
{
	if (g_inst)
	{
		return nullptr;
	}

	auto* self = new ignition_ps3();
	g_inst = self;

	Emu.SetHasGui(false);
	Emu.SetUsr("00000001");
	Emu.SetCallbacks(make_callbacks(self));
	Emu.Init();

	return self;
}

void ignition_ps3_destroy(ignition_ps3* self)
{
	if (!self)
	{
		return;
	}
	Emu.Kill();
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
	case system_state::running: return IGNITION_PS3_RUNNING;
	case system_state::paused:  return IGNITION_PS3_PAUSED;
	case system_state::loading:
	case system_state::starting: return IGNITION_PS3_LOADING;
	default: return IGNITION_PS3_STOPPED;
	}
}

void ignition_ps3_pause(ignition_ps3*)  { Emu.Pause(); }
void ignition_ps3_resume(ignition_ps3*) { Emu.Resume(); }
void ignition_ps3_stop(ignition_ps3*)   { Emu.GracefulShutdown(false); }

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

// Video, input and audio: stubbed until their increments land.
int32_t ignition_ps3_take_frame(ignition_ps3*, ignition_ps3_frame*) { return 0; }
void ignition_ps3_set_pad(ignition_ps3*, uint32_t, const ignition_ps3_pad*) {}
size_t ignition_ps3_read_audio(ignition_ps3*, int16_t*, size_t) { return 0; }
uint32_t ignition_ps3_audio_rate(const ignition_ps3*) { return 0; }

int32_t ignition_ps3_save_state(ignition_ps3*, const char*) { return -1; }
int32_t ignition_ps3_load_state(ignition_ps3*, const char*) { return -1; }

} // extern "C"
