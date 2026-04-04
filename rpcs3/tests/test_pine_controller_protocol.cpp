#include <gtest/gtest.h>

#include "3rdparty/pine/pine_server.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	struct mock_pine_impl
	{
		struct pine_controller_entry
		{
			std::string device_id;
			std::string display_name;
		};

		struct pine_patch_entry
		{
			std::string name;
			std::string description;
			std::string place;
			bool enabled = false;
			bool global_toggleable = true;
		};

		static inline std::vector<pine_controller_entry> controllers{};
		static inline std::vector<pine_patch_entry> patches{};
		static inline u8 last_auto_map_port = 0;
		static inline std::string last_auto_map_device{};
		static inline u8 last_set_binding_port = 0;
		static inline std::string last_set_binding_action{};
		static inline std::string last_set_binding_value{};
		static inline std::string last_set_patch_name{};
		static inline bool last_set_patch_enabled = false;

		static constexpr u8 page_writable = 0;

		static bool is_aborting() { return false; }

		template <u32 Size = 1>
		static bool check_addr(u32, u8 = 0)
		{
			return true;
		}

		static const u8& read8(u32)
		{
			static const u8 value = 0;
			return value;
		}

		static void write8(u32, u8)
		{
		}

		static const u16& read16(u32)
		{
			static const u16 value = 0;
			return value;
		}

		static void write16(u32, u16)
		{
		}

		static const u32& read32(u32)
		{
			static const u32 value = 0;
			return value;
		}

		static void write32(u32, u32)
		{
		}

		static const u64& read64(u32)
		{
			static const u64 value = 0;
			return value;
		}

		static void write64(u32, u64)
		{
		}

		template <typename... Args>
		static void error(const char*, Args&&...)
		{
		}

		static int get_port()
		{
			return IPC_DEFAULT_SLOT;
		}

		static pine::EmuStatus get_status()
		{
			return pine::EmuStatus::Shutdown;
		}

		static const std::string& get_title()
		{
			static const std::string value = "title";
			return value;
		}

		static const std::string& get_title_ID()
		{
			static const std::string value = "title_id";
			return value;
		}

		static const std::string& get_executable_hash()
		{
			static const std::string value = "hash";
			return value;
		}

		static const std::string& get_app_version()
		{
			static const std::string value = "version";
			return value;
		}

		static std::string get_version_and_branch()
		{
			return "branch";
		}

		static std::string get_ipc_version_string()
		{
			return "mock_ipc";
		}

		static std::string pine_get_setting(std::string_view, std::string_view)
		{
			return {};
		}

		static bool pine_set_setting(std::string_view, std::string_view, std::string_view)
		{
			return true;
		}

		static bool pine_apply_settings()
		{
			return true;
		}

		static std::vector<pine_controller_entry> pine_list_controllers()
		{
			return controllers;
		}

		static bool pine_get_port_binding(u8 port, std::string& device_prefix, std::string& controller_type)
		{
			if (port < 1 || port > 7)
				return false;
			device_prefix = "SDL-" + std::to_string(port - 1);
			controller_type = "XInput";
			return true;
		}

		static u8 pine_auto_map_port(u8 port, std::string_view device_id)
		{
			last_auto_map_port = port;
			last_auto_map_device = std::string(device_id);
			if (port < 1 || port > 7)
				return 1;
			if (device_id.empty())
				return 2;
			return 0;
		}

		static bool pine_get_button_binding(u8 port, std::string_view action_id, std::string& binding)
		{
			if (port < 1 || port > 7)
				return false;
			if (action_id != "cross")
				return false;
			binding = "SDL-0/FaceSouth";
			return true;
		}

		static u8 pine_set_button_binding(u8 port, std::string_view action_id, std::string_view binding)
		{
			last_set_binding_port = port;
			last_set_binding_action = std::string(action_id);
			last_set_binding_value = std::string(binding);
			if (port < 1 || port > 7)
				return 1;
			if (action_id != "cross")
				return 2;
			return 0;
		}

		static std::vector<pine_patch_entry> pine_list_patches()
		{
			return patches;
		}

		static bool pine_set_patch_enabled(std::string_view patch_name, bool enabled)
		{
			last_set_patch_name = std::string(patch_name);
			last_set_patch_enabled = enabled;
			return !patch_name.empty() && patch_name.starts_with("rpcs3:");
		}

		static void reset()
		{
			controllers.clear();
			patches.clear();
			last_auto_map_port = 0;
			last_auto_map_device.clear();
			last_set_binding_port = 0;
			last_set_binding_action.clear();
			last_set_binding_value.clear();
			last_set_patch_name.clear();
			last_set_patch_enabled = false;
		}
	};

	using mock_server = pine::pine_server<mock_pine_impl>;

	template <typename T>
	void append_le(std::vector<char>& data, T value)
	{
		const usz offset = data.size();
		data.resize(offset + sizeof(T));
		std::memcpy(data.data() + offset, &value, sizeof(T));
	}

	template <typename T>
	T read_le(const std::vector<char>& data, usz offset)
	{
		T value{};
		std::memcpy(&value, data.data() + offset, sizeof(T));
		return value;
	}

	std::vector<char> parse_packet(const std::vector<char>& request)
	{
		std::array<char, MAX_IPC_RETURN_SIZE> ret_buffer{};
		std::vector<char> mutable_request = request;
		const auto result = mock_server::ParseCommand(mutable_request.data(), ret_buffer.data(), static_cast<u32>(mutable_request.size()));
		return std::vector<char>(result.buffer, result.buffer + result.size);
	}

	u8 response_status(const std::vector<char>& response)
	{
		if (response.size() < 5)
			return 0xFF;
		return static_cast<u8>(response[4]);
	}

	std::string read_length_prefixed_string(const std::vector<char>& response, usz& offset)
	{
		const u32 len = read_le<u32>(response, offset);
		offset += sizeof(u32);
		const std::string value(response.data() + offset, response.data() + offset + len);
		offset += len;
		return value;
	}

	std::vector<char> build_auto_map_request(u8 port, std::string_view device_id)
	{
		std::vector<char> request{};
		request.push_back(static_cast<char>(mock_server::MsgAutoMapPort));
		request.push_back(static_cast<char>(port));
		append_le<u32>(request, static_cast<u32>(device_id.size()));
		request.insert(request.end(), device_id.begin(), device_id.end());
		return request;
	}

	std::vector<char> build_get_button_request(u8 port, std::string_view action)
	{
		std::vector<char> request{};
		request.push_back(static_cast<char>(mock_server::MsgGetButtonBinding));
		request.push_back(static_cast<char>(port));
		append_le<u32>(request, static_cast<u32>(action.size()));
		request.insert(request.end(), action.begin(), action.end());
		return request;
	}

	std::vector<char> build_set_button_request(u8 port, std::string_view action, std::string_view binding)
	{
		std::vector<char> request{};
		request.push_back(static_cast<char>(mock_server::MsgSetButtonBinding));
		request.push_back(static_cast<char>(port));
		append_le<u32>(request, static_cast<u32>(action.size()));
		request.insert(request.end(), action.begin(), action.end());
		append_le<u32>(request, static_cast<u32>(binding.size()));
		request.insert(request.end(), binding.begin(), binding.end());
		return request;
	}

	std::vector<char> build_set_patch_request(std::string_view patch_name, bool enabled)
	{
		std::vector<char> request{};
		request.push_back(static_cast<char>(mock_server::MsgSetPatchEnabled));
		append_le<u32>(request, static_cast<u32>(patch_name.size()));
		request.insert(request.end(), patch_name.begin(), patch_name.end());
		request.push_back(static_cast<char>(enabled ? 1 : 0));
		return request;
	}

	TEST(PineControllerProtocol, ListControllersEncodesExpectedPayload)
	{
		mock_pine_impl::reset();
		mock_pine_impl::controllers = {
			{ "XInput|Pad 1", "Pad 1 (XInput)" },
			{ "SDL|Pad 2", "Pad 2 (SDL)" },
		};

		const std::vector<char> response = parse_packet({ static_cast<char>(mock_server::MsgListControllers) });

		ASSERT_GE(response.size(), 9u);
		EXPECT_EQ(read_le<u32>(response, 0), response.size());
		EXPECT_EQ(response_status(response), mock_server::IPC_OK);

		usz offset = 5;
		const u32 count = read_le<u32>(response, offset);
		offset += sizeof(u32);
		EXPECT_EQ(count, 2u);
		EXPECT_EQ(read_length_prefixed_string(response, offset), "XInput|Pad 1");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "Pad 1 (XInput)");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "SDL|Pad 2");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "Pad 2 (SDL)");
		EXPECT_EQ(offset, response.size());
	}

	TEST(PineControllerProtocol, TruncatedControllerPacketsFailSafely)
	{
		EXPECT_EQ(response_status(parse_packet({ static_cast<char>(mock_server::MsgGetPortBinding) })), mock_server::IPC_FAIL);

		std::vector<char> auto_map_truncated = { static_cast<char>(mock_server::MsgAutoMapPort), 1, 0, 0 };
		EXPECT_EQ(response_status(parse_packet(auto_map_truncated)), mock_server::IPC_FAIL);

		std::vector<char> auto_map_payload_truncated = build_auto_map_request(1, "SDL-0");
		auto_map_payload_truncated.resize(auto_map_payload_truncated.size() - 2);
		EXPECT_EQ(response_status(parse_packet(auto_map_payload_truncated)), mock_server::IPC_FAIL);

		std::vector<char> get_binding_truncated = { static_cast<char>(mock_server::MsgGetButtonBinding), 1, 1, 0, 0 };
		EXPECT_EQ(response_status(parse_packet(get_binding_truncated)), mock_server::IPC_FAIL);

		std::vector<char> get_binding_payload_truncated = build_get_button_request(1, "cross");
		get_binding_payload_truncated.resize(get_binding_payload_truncated.size() - 1);
		EXPECT_EQ(response_status(parse_packet(get_binding_payload_truncated)), mock_server::IPC_FAIL);

		std::vector<char> set_binding_truncated_len = build_get_button_request(1, "cross");
		set_binding_truncated_len[0] = static_cast<char>(mock_server::MsgSetButtonBinding);
		EXPECT_EQ(response_status(parse_packet(set_binding_truncated_len)), mock_server::IPC_FAIL);

		std::vector<char> set_binding_payload_truncated = build_set_button_request(1, "cross", "SDL-0/FaceSouth");
		set_binding_payload_truncated.resize(set_binding_payload_truncated.size() - 4);
		EXPECT_EQ(response_status(parse_packet(set_binding_payload_truncated)), mock_server::IPC_FAIL);

		std::vector<char> set_patch_truncated = { static_cast<char>(mock_server::MsgSetPatchEnabled), 1, 0, 0, 0 };
		EXPECT_EQ(response_status(parse_packet(set_patch_truncated)), mock_server::IPC_FAIL);

		std::vector<char> set_patch_payload_truncated = build_set_patch_request("rpcs3:abcd", true);
		set_patch_payload_truncated.resize(set_patch_payload_truncated.size() - 1);
		EXPECT_EQ(response_status(parse_packet(set_patch_payload_truncated)), mock_server::IPC_FAIL);
	}

	TEST(PineControllerProtocol, SevenPortBehaviorMatchesRpcs3Contract)
	{
		mock_pine_impl::reset();

		const std::vector<char> get_port_7 = parse_packet({ static_cast<char>(mock_server::MsgGetPortBinding), 7 });
		EXPECT_EQ(response_status(get_port_7), mock_server::IPC_OK);
		usz offset = 5;
		EXPECT_EQ(read_length_prefixed_string(get_port_7, offset), "SDL-6");
		EXPECT_EQ(read_length_prefixed_string(get_port_7, offset), "XInput");

		const std::vector<char> get_port_8 = parse_packet({ static_cast<char>(mock_server::MsgGetPortBinding), 8 });
		EXPECT_EQ(response_status(get_port_8), mock_server::IPC_FAIL);

		const std::vector<char> auto_map_7 = parse_packet(build_auto_map_request(7, "XInput|Pad 1"));
		ASSERT_GE(auto_map_7.size(), 6u);
		EXPECT_EQ(response_status(auto_map_7), mock_server::IPC_OK);
		EXPECT_EQ(static_cast<u8>(auto_map_7[5]), 0);

		const std::vector<char> auto_map_8 = parse_packet(build_auto_map_request(8, "XInput|Pad 1"));
		ASSERT_GE(auto_map_8.size(), 6u);
		EXPECT_EQ(response_status(auto_map_8), mock_server::IPC_OK);
		EXPECT_EQ(static_cast<u8>(auto_map_8[5]), 1);

		const std::vector<char> get_binding_7 = parse_packet(build_get_button_request(7, "cross"));
		EXPECT_EQ(response_status(get_binding_7), mock_server::IPC_OK);
		offset = 5;
		EXPECT_EQ(read_length_prefixed_string(get_binding_7, offset), "SDL-0/FaceSouth");

		const std::vector<char> get_binding_8 = parse_packet(build_get_button_request(8, "cross"));
		EXPECT_EQ(response_status(get_binding_8), mock_server::IPC_FAIL);

		const std::vector<char> set_binding_7 = parse_packet(build_set_button_request(7, "cross", "SDL-0/FaceSouth"));
		ASSERT_GE(set_binding_7.size(), 6u);
		EXPECT_EQ(response_status(set_binding_7), mock_server::IPC_OK);
		EXPECT_EQ(static_cast<u8>(set_binding_7[5]), 0);

		const std::vector<char> set_binding_8 = parse_packet(build_set_button_request(8, "cross", "SDL-0/FaceSouth"));
		ASSERT_GE(set_binding_8.size(), 6u);
		EXPECT_EQ(response_status(set_binding_8), mock_server::IPC_OK);
		EXPECT_EQ(static_cast<u8>(set_binding_8[5]), 1);

		const std::vector<char> set_binding_bad_action = parse_packet(build_set_button_request(7, "Cross", "SDL-0/FaceSouth"));
		ASSERT_GE(set_binding_bad_action.size(), 6u);
		EXPECT_EQ(response_status(set_binding_bad_action), mock_server::IPC_OK);
		EXPECT_EQ(static_cast<u8>(set_binding_bad_action[5]), 2);
	}

	TEST(PineControllerProtocol, ListPatchesEncodesExpectedPayload)
	{
		mock_pine_impl::reset();
		mock_pine_impl::patches = {
			{ "rpcs3:01", "60 FPS", "PPU", true, true },
			{ "rpcs3:02", "Disable Motion Blur", "Other", false, true },
		};

		const std::vector<char> response = parse_packet({ static_cast<char>(mock_server::MsgListPatches) });
		ASSERT_GE(response.size(), 9u);
		EXPECT_EQ(response_status(response), mock_server::IPC_OK);

		usz offset = 5;
		const u32 count = read_le<u32>(response, offset);
		offset += sizeof(u32);
		EXPECT_EQ(count, 2u);

		EXPECT_EQ(read_length_prefixed_string(response, offset), "rpcs3:01");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "60 FPS");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "PPU");
		EXPECT_EQ(static_cast<u8>(response[offset++]), 1);
		EXPECT_EQ(static_cast<u8>(response[offset++]), 1);

		EXPECT_EQ(read_length_prefixed_string(response, offset), "rpcs3:02");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "Disable Motion Blur");
		EXPECT_EQ(read_length_prefixed_string(response, offset), "Other");
		EXPECT_EQ(static_cast<u8>(response[offset++]), 0);
		EXPECT_EQ(static_cast<u8>(response[offset++]), 1);
		EXPECT_EQ(offset, response.size());
	}

	TEST(PineControllerProtocol, SetPatchEnabledUsesOpaqueIdContract)
	{
		mock_pine_impl::reset();

		const std::vector<char> ok_response = parse_packet(build_set_patch_request("rpcs3:deadbeef", true));
		EXPECT_EQ(response_status(ok_response), mock_server::IPC_OK);
		EXPECT_EQ(mock_pine_impl::last_set_patch_name, "rpcs3:deadbeef");
		EXPECT_TRUE(mock_pine_impl::last_set_patch_enabled);

		const std::vector<char> fail_response = parse_packet(build_set_patch_request("not-an-opaque-id", false));
		EXPECT_EQ(response_status(fail_response), mock_server::IPC_FAIL);
		EXPECT_EQ(mock_pine_impl::last_set_patch_name, "not-an-opaque-id");
		EXPECT_FALSE(mock_pine_impl::last_set_patch_enabled);
	}
}
