#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Utilities/Thread.h"
#include "util/logs.hpp"
#include "Emu/Memory/vm.h"
#include "3rdparty/pine/pine_server.h"

LOG_CHANNEL(IPC);

namespace IPC_socket
{
	class IPC_impl
	{
	protected:
		static constexpr u8 page_writable = vm::page_writable;

		static bool is_aborting()
		{
			return thread_ctrl::state() == thread_state::aborting;
		}

		template <u32 Size = 1>
		static bool check_addr(u32 addr, u8 flags = vm::page_readable)
		{
			return vm::check_addr<Size>(addr, flags);
		}

		static const u8& read8(u32 addr);
		static void write8(u32 addr, u8 value);
		static const be_t<u16>& read16(u32 addr);
		static void write16(u32 addr, be_t<u16> value);
		static const be_t<u32>& read32(u32 addr);
		static void write32(u32 addr, be_t<u32> value);
		static const be_t<u64>& read64(u32 addr);
		static void write64(u32 addr, be_t<u64> value);

		template<typename... Args>
		static void error(const const_str& fmt, Args&&... args)
		{
			IPC.error(fmt, std::forward<Args>(args)...);
		}

		static int get_port();
		static pine::EmuStatus get_status();
		static const std::string& get_title();
		static const std::string& get_title_ID();
		static const std::string& get_executable_hash();
		static const std::string& get_app_version();
		static std::string get_version_and_branch();
		static std::string get_ipc_version_string();
		static std::string pine_get_setting(std::string_view section, std::string_view key);
		static bool pine_set_setting(std::string_view section, std::string_view key, std::string_view value);
		static bool pine_apply_settings();
		struct pine_controller_entry
		{
			std::string device_id;
			std::string display_name;
		};
		static std::vector<pine_controller_entry> pine_list_controllers();
		static bool pine_get_port_binding(u8 port, std::string& device_prefix, std::string& controller_type);
		static u8 pine_auto_map_port(u8 port, std::string_view device_id);
		static bool pine_get_button_binding(u8 port, std::string_view action_id, std::string& binding);
		static u8 pine_set_button_binding(u8 port, std::string_view action_id, std::string_view binding);
		struct pine_patch_entry
		{
			std::string name;
			std::string description;
			std::string place;
			bool enabled = false;
			bool global_toggleable = true;
		};
		static std::vector<pine_patch_entry> pine_list_patches();
		static bool pine_set_patch_enabled(std::string_view patch_name, bool enabled);

	public:
		static auto constexpr thread_name = "IPC Server"sv;
		IPC_impl& operator=(thread_state);
	};

	class IPC_server_manager
	{
		using IPC_server = named_thread<pine::pine_server<IPC_socket::IPC_impl>>;

		std::unique_ptr<IPC_server> m_ipc_server;
		int m_old_port = 0;

	public:
		explicit IPC_server_manager(bool enabled);
		void set_server_enabled(bool enabled);
	};
}
