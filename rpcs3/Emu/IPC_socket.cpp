#include "stdafx.h"
#include "System.h"
#include "Emu/IPC_config.h"
#include "Emu/system_config.h"
#include "Emu/system_utils.hpp"
#include "Emu/Io/PadHandler.h"
#include "Utilities/File.h"
#include "Utilities/bin_patch.h"
#include "IPC_socket.h"
#include "rpcs3_version.h"
#include "Input/pad_thread.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	struct canonical_action_binding
	{
		std::string_view action_id;
		cfg::string cfg_pad::*field;
	};

	constexpr std::array<canonical_action_binding, 25> g_canonical_action_bindings = {{
		{ "ls_left",  &cfg_pad::ls_left  },
		{ "ls_down",  &cfg_pad::ls_down  },
		{ "ls_right", &cfg_pad::ls_right },
		{ "ls_up",    &cfg_pad::ls_up    },
		{ "rs_left",  &cfg_pad::rs_left  },
		{ "rs_down",  &cfg_pad::rs_down  },
		{ "rs_right", &cfg_pad::rs_right },
		{ "rs_up",    &cfg_pad::rs_up    },
		{ "start",    &cfg_pad::start    },
		{ "select",   &cfg_pad::select   },
		{ "ps",       &cfg_pad::ps       },
		{ "square",   &cfg_pad::square   },
		{ "cross",    &cfg_pad::cross    },
		{ "circle",   &cfg_pad::circle   },
		{ "triangle", &cfg_pad::triangle },
		{ "left",     &cfg_pad::left     },
		{ "down",     &cfg_pad::down     },
		{ "right",    &cfg_pad::right    },
		{ "up",       &cfg_pad::up       },
		{ "l1",       &cfg_pad::l1       },
		{ "l2",       &cfg_pad::l2       },
		{ "l3",       &cfg_pad::l3       },
		{ "r1",       &cfg_pad::r1       },
		{ "r2",       &cfg_pad::r2       },
		{ "r3",       &cfg_pad::r3       },
	}};

	constexpr u8 g_min_controller_port = 1;
	constexpr u8 g_max_controller_port = 7;

	struct controller_device_desc
	{
		pad_handler handler = pad_handler::null;
		std::string handler_name;
		std::string device_name;
		std::string device_id;
		std::string display_name;
	};

	struct pending_controller_state
	{
		std::array<std::optional<std::string>, g_max_controller_port> pending_auto_map{};
		std::array<std::unordered_map<std::string, std::string>, g_max_controller_port> pending_bindings{};
	};

	pending_controller_state g_pending_controller_state;
	std::mutex g_pending_controller_mutex;

	bool is_valid_controller_port(u8 port)
	{
		return port >= g_min_controller_port && port <= g_max_controller_port;
	}

	usz to_port_index(u8 port)
	{
		return static_cast<usz>(port - g_min_controller_port);
	}

	const canonical_action_binding* find_canonical_action(std::string_view action_id)
	{
		for (const canonical_action_binding& action : g_canonical_action_bindings)
		{
			if (action.action_id == action_id)
				return &action;
		}
		return nullptr;
	}

	std::string handler_to_string(pad_handler handler)
	{
		return fmt::format("%s", handler);
	}

	std::optional<pad_handler> handler_from_string(std::string_view handler_name)
	{
		if (handler_name == "DualShock 3")
			return pad_handler::ds3;
		if (handler_name == "DualShock 4")
			return pad_handler::ds4;
		if (handler_name == "DualSense")
			return pad_handler::dualsense;
#ifdef _WIN32
		if (handler_name == "XInput")
			return pad_handler::xinput;
		if (handler_name == "MMJoystick")
			return pad_handler::mm;
#endif
#ifdef HAVE_SDL3
		if (handler_name == "SDL")
			return pad_handler::sdl;
#endif
#ifdef HAVE_LIBEVDEV
		if (handler_name == "Evdev")
			return pad_handler::evdev;
#endif
		return std::nullopt;
	}

	std::vector<pad_handler> get_controller_handlers()
	{
		std::vector<pad_handler> handlers;
		handlers.emplace_back(pad_handler::dualsense);
		handlers.emplace_back(pad_handler::ds4);
		handlers.emplace_back(pad_handler::ds3);
#ifdef _WIN32
		handlers.emplace_back(pad_handler::xinput);
		handlers.emplace_back(pad_handler::mm);
#endif
#ifdef HAVE_SDL3
		handlers.emplace_back(pad_handler::sdl);
#endif
#ifdef HAVE_LIBEVDEV
		handlers.emplace_back(pad_handler::evdev);
#endif
		return handlers;
	}

	std::string build_controller_device_id(std::string_view handler_name, std::string_view device_name)
	{
		return std::string(handler_name) + "|" + std::string(device_name);
	}

	std::optional<std::pair<std::string, std::string>> split_controller_device_id(std::string_view device_id)
	{
		const usz delimiter = device_id.find('|');
		if (delimiter == std::string_view::npos || delimiter == 0 || delimiter + 1 >= device_id.size())
			return std::nullopt;
		return std::make_pair(std::string(device_id.substr(0, delimiter)), std::string(device_id.substr(delimiter + 1)));
	}

	std::vector<controller_device_desc> enumerate_controllers()
	{
		std::vector<controller_device_desc> devices;
		std::unordered_set<std::string> seen_ids;

		for (const pad_handler handler_type : get_controller_handlers())
		{
			std::shared_ptr<PadHandlerBase> handler = pad_thread::GetHandler(handler_type);
			if (!handler)
				continue;

			handler->Init();
			const std::string handler_name = handler_to_string(handler_type);

			for (const pad_list_entry& entry : handler->list_devices())
			{
				if (entry.name.empty() || entry.is_buddy_only)
					continue;

				const std::string device_id = build_controller_device_id(handler_name, entry.name);
				if (!seen_ids.emplace(device_id).second)
					continue;

				devices.push_back(controller_device_desc{
					.handler = handler_type,
					.handler_name = handler_name,
					.device_name = entry.name,
					.device_id = device_id,
					.display_name = fmt::format("%s (%s)", entry.name, handler_name),
				});
			}
		}

		return devices;
	}

	std::optional<controller_device_desc> find_controller_by_device_id(std::string_view device_id)
	{
		const auto parts = split_controller_device_id(device_id);
		if (!parts)
			return std::nullopt;

		const auto maybe_handler = handler_from_string(parts->first);
		if (!maybe_handler)
			return std::nullopt;

		std::shared_ptr<PadHandlerBase> handler = pad_thread::GetHandler(*maybe_handler);
		if (!handler)
			return std::nullopt;

		handler->Init();
		for (const pad_list_entry& entry : handler->list_devices())
		{
			if (entry.is_buddy_only)
				continue;
			if (entry.name == parts->second)
			{
				return controller_device_desc{
					.handler = *maybe_handler,
					.handler_name = parts->first,
					.device_name = entry.name,
					.device_id = build_controller_device_id(parts->first, entry.name),
					.display_name = fmt::format("%s (%s)", entry.name, parts->first),
				};
			}
		}

		return std::nullopt;
	}

	std::string get_active_global_input_profile()
	{
		g_cfg_input_configs.load();
		std::string active_profile = g_cfg_input_configs.active_configs.get_value(g_cfg_input_configs.global_key);
		if (active_profile.empty())
			active_profile = g_cfg_input_configs.default_config;
		return active_profile;
	}

	void load_active_global_input_config()
	{
		const std::string active_profile = get_active_global_input_profile();
		g_cfg_input.load("", active_profile);
	}

	std::string active_profile_path()
	{
		return rpcs3::utils::get_input_config_dir() + get_active_global_input_profile() + ".yml";
	}

	bool has_pending_controller_changes_locked()
	{
		for (usz i = 0; i < g_max_controller_port; i++)
		{
			if (g_pending_controller_state.pending_auto_map[i].has_value())
				return true;
			if (!g_pending_controller_state.pending_bindings[i].empty())
				return true;
		}
		return false;
	}

	bool has_pending_controller_changes()
	{
		std::lock_guard lock(g_pending_controller_mutex);
		return has_pending_controller_changes_locked();
	}

	void clear_pending_controller_changes()
	{
		std::lock_guard lock(g_pending_controller_mutex);
		for (usz i = 0; i < g_max_controller_port; i++)
		{
			g_pending_controller_state.pending_auto_map[i].reset();
			g_pending_controller_state.pending_bindings[i].clear();
		}
	}

	bool apply_pending_controller_changes()
	{
		pending_controller_state staged_state;
		{
			std::lock_guard lock(g_pending_controller_mutex);
			if (!has_pending_controller_changes_locked())
				return true;
			staged_state = g_pending_controller_state;
		}

		load_active_global_input_config();

		for (u8 port = g_min_controller_port; port <= g_max_controller_port; port++)
		{
			const usz index = to_port_index(port);
			cfg_player* const player = g_cfg_input.player[index];
			if (!player)
				return false;

			if (staged_state.pending_auto_map[index].has_value())
			{
				const std::optional<controller_device_desc> device = find_controller_by_device_id(*staged_state.pending_auto_map[index]);
				if (!device)
					return false;

				std::shared_ptr<PadHandlerBase> handler = pad_thread::GetHandler(device->handler);
				if (!handler)
					return false;

				pad_thread::InitPadConfig(player->config, device->handler, handler);
				if (!player->handler.from_string(device->handler_name))
					return false;
				if (!player->device.from_string(device->device_name))
					return false;
				if (!player->buddy_device.from_string(""))
					return false;
			}

			for (const auto& [action_id, binding] : staged_state.pending_bindings[index])
			{
				const canonical_action_binding* action = find_canonical_action(action_id);
				if (!action)
					return false;
				if (!(player->config.*(action->field)).from_string(binding))
					return false;
			}
		}

		const std::string profile_path = active_profile_path();
		if (!fs::create_path(fs::get_parent_dir(profile_path)))
			return false;
		if (!g_cfg_input.cfg::node::save(profile_path))
			return false;

		if (!Emu.IsStopped())
			pad::reset(Emu.GetTitleID());

		return true;
	}

	struct patch_target_key
	{
		std::string hash;
		std::string description;
		std::string title;
		std::string serial;
		std::string app_version;
	};

	struct patch_catalog_entry
	{
		patch_target_key key;
		std::string patch_id;
		std::string description;
		std::string place;
		bool enabled = false;
	};

	std::vector<std::string> patch_catalog_file_paths()
	{
		const std::string patches_path = patch_engine::get_patches_path();
		std::vector<std::string> files;
		files.push_back(patches_path + "patch.yml");
		files.push_back(patches_path + "imported_patch.yml");

		std::vector<std::string> title_specific_files;
		for (const auto& entry : fs::dir(patches_path))
		{
			if (entry.is_directory || !entry.name.ends_with("_patch.yml"))
				continue;
			if (entry.name == "patch.yml" || entry.name == "imported_patch.yml")
				continue;
			title_specific_files.push_back(patches_path + entry.name);
		}
		std::sort(title_specific_files.begin(), title_specific_files.end());
		files.insert(files.end(), title_specific_files.begin(), title_specific_files.end());
		return files;
	}

	patch_engine::patch_map load_patch_catalog_map()
	{
		patch_engine::patch_map patches;
		for (const std::string& path : patch_catalog_file_paths())
		{
			if (!fs::is_file(path))
				continue;
			if (!patch_engine::load(patches, path))
			{
				IPC.warning("PINE patch catalog: failed to load patch file '%s'", path);
			}
		}
		return patches;
	}

	std::string classify_patch_place(std::string_view hash)
	{
		if (hash.starts_with("PPU-"))
			return "PPU";
		if (hash.starts_with("SPU-"))
			return "SPU";
		return "Other";
	}

	std::string hex_encode(std::string_view value)
	{
		static constexpr char g_hex[] = "0123456789abcdef";
		std::string encoded;
		encoded.reserve(value.size() * 2);
		for (const char raw : value)
		{
			const u8 byte = static_cast<u8>(raw);
			encoded.push_back(g_hex[(byte >> 4) & 0x0F]);
			encoded.push_back(g_hex[byte & 0x0F]);
		}
		return encoded;
	}

	std::optional<u8> hex_nibble(char c)
	{
		if (c >= '0' && c <= '9')
			return static_cast<u8>(c - '0');
		if (c >= 'a' && c <= 'f')
			return static_cast<u8>(10 + (c - 'a'));
		if (c >= 'A' && c <= 'F')
			return static_cast<u8>(10 + (c - 'A'));
		return std::nullopt;
	}

	bool hex_decode(std::string_view value, std::string& decoded)
	{
		if ((value.size() % 2) != 0)
			return false;
		decoded.clear();
		decoded.reserve(value.size() / 2);
		for (usz i = 0; i < value.size(); i += 2)
		{
			const std::optional<u8> hi = hex_nibble(value[i]);
			const std::optional<u8> lo = hex_nibble(value[i + 1]);
			if (!hi || !lo)
				return false;
			decoded.push_back(static_cast<char>((*hi << 4) | *lo));
		}
		return true;
	}

	std::string build_patch_id(const patch_target_key& key)
	{
		return fmt::format("rpcs3:{}:{}:{}:{}:{}",
			hex_encode(key.hash),
			hex_encode(key.description),
			hex_encode(key.title),
			hex_encode(key.serial),
			hex_encode(key.app_version));
	}

	bool parse_patch_id(std::string_view patch_id, patch_target_key& key)
	{
		if (!patch_id.starts_with("rpcs3:"))
			return false;

		std::array<std::string_view, 5> fields{};
		std::string_view payload = patch_id.substr(6);
		usz cursor = 0;
		for (usz i = 0; i < fields.size(); i++)
		{
			if (i + 1 == fields.size())
			{
				if (payload.find(':', cursor) != std::string_view::npos)
					return false;
				fields[i] = payload.substr(cursor);
				continue;
			}
			const usz delimiter = payload.find(':', cursor);
			if (delimiter == std::string_view::npos)
				return false;
			fields[i] = payload.substr(cursor, delimiter - cursor);
			cursor = delimiter + 1;
		}

		return hex_decode(fields[0], key.hash)
			&& hex_decode(fields[1], key.description)
			&& hex_decode(fields[2], key.title)
			&& hex_decode(fields[3], key.serial)
			&& hex_decode(fields[4], key.app_version);
	}

	std::vector<patch_catalog_entry> build_patch_catalog_entries(const patch_engine::patch_map& patches_map, std::string_view runtime_serial, std::string_view runtime_app_version)
	{
		std::vector<patch_catalog_entry> entries;

		for (const auto& [hash, container] : patches_map)
		{
			for (const auto& [description_key, patch] : container.patch_info_map)
			{
				const std::string display_description = patch.description.empty() ? description_key : patch.description;

				for (const auto& [title, serials] : patch.titles)
				{
					std::string selected_serial;
					const auto serial_exact_it = runtime_serial.empty() ? serials.end() : serials.find(std::string(runtime_serial));
					if (serial_exact_it != serials.end())
					{
						selected_serial = serial_exact_it->first;
					}
					else
					{
						const auto serial_all_it = serials.find(patch_key::all);
						if (serial_all_it == serials.end())
							continue;
						selected_serial = serial_all_it->first;
					}

					const auto serial_it = serials.find(selected_serial);
					if (serial_it == serials.end())
						continue;

					const patch_engine::patch_app_versions& app_versions = serial_it->second;
					std::string selected_app_version;
					const auto app_exact_it = runtime_app_version.empty() ? app_versions.end() : app_versions.find(std::string(runtime_app_version));
					if (app_exact_it != app_versions.end())
					{
						selected_app_version = app_exact_it->first;
					}
					else
					{
						const auto app_all_it = app_versions.find(patch_key::all);
						if (app_all_it == app_versions.end())
							continue;
						selected_app_version = app_all_it->first;
					}

					const auto app_it = app_versions.find(selected_app_version);
					if (app_it == app_versions.end())
						continue;

					patch_target_key key{
						.hash = hash,
						.description = description_key,
						.title = title,
						.serial = selected_serial,
						.app_version = selected_app_version,
					};

					entries.push_back(patch_catalog_entry{
						.key = key,
						.patch_id = build_patch_id(key),
						.description = display_description,
						.place = classify_patch_place(hash),
						.enabled = app_it->second.enabled,
					});
				}
			}
		}

		std::sort(entries.begin(), entries.end(), [](const patch_catalog_entry& lhs, const patch_catalog_entry& rhs)
		{
			if (lhs.description != rhs.description)
				return lhs.description < rhs.description;
			return lhs.patch_id < rhs.patch_id;
		});

		return entries;
	}

	bool set_patch_enabled_state(patch_engine::patch_map& patches_map, const patch_target_key& key, bool enabled)
	{
		const auto container_it = patches_map.find(key.hash);
		if (container_it == patches_map.end())
			return false;
		auto patch_it = container_it->second.patch_info_map.find(key.description);
		if (patch_it == container_it->second.patch_info_map.end())
			return false;
		auto title_it = patch_it->second.titles.find(key.title);
		if (title_it == patch_it->second.titles.end())
			return false;
		auto serial_it = title_it->second.find(key.serial);
		if (serial_it == title_it->second.end())
			return false;
		auto app_it = serial_it->second.find(key.app_version);
		if (app_it == serial_it->second.end())
			return false;
		app_it->second.enabled = enabled;
		return true;
	}

	cfg::_base* find_cfg_leaf(cfg::node& root, std::string_view section_path, std::string_view key_name)
	{
		cfg::node* cur = &root;
		std::string sec(section_path);
		size_t pos = 0;
		while (pos < sec.size())
		{
			const size_t slash = sec.find('/', pos);
			const std::string_view part = (slash == std::string::npos)
				? std::string_view(sec).substr(pos)
				: std::string_view(sec).substr(pos, slash - pos);
			if (part.empty())
				return nullptr;
			cfg::_base* next = nullptr;
			for (auto* c : cur->get_nodes())
			{
				if (std::string_view(c->get_name()) == part)
				{
					next = c;
					break;
				}
			}
			if (!next || next->get_type() != cfg::type::node)
				return nullptr;
			cur = static_cast<cfg::node*>(next);
			if (slash == std::string::npos)
				break;
			pos = slash + 1;
		}
		for (auto* c : cur->get_nodes())
		{
			if (std::string_view(c->get_name()) == key_name && c->get_type() != cfg::type::node)
				return c;
		}
		return nullptr;
	}
}

namespace IPC_socket
{
	const u8& IPC_impl::read8(u32 addr)
	{
		return vm::read8(addr);
	}

	void IPC_impl::write8(u32 addr, u8 value)
	{
		vm::write8(addr, value);
	}

	const be_t<u16>& IPC_impl::read16(u32 addr)
	{
		return vm::read16(addr);
	}

	void IPC_impl::write16(u32 addr, be_t<u16> value)
	{
		vm::write16(addr, value);
	}

	const be_t<u32>& IPC_impl::read32(u32 addr)
	{
		return vm::read32(addr);
	}

	void IPC_impl::write32(u32 addr, be_t<u32> value)
	{
		vm::write32(addr, value);
	}

	const be_t<u64>& IPC_impl::read64(u32 addr)
	{
		return vm::read64(addr);
	}

	void IPC_impl::write64(u32 addr, be_t<u64> value)
	{
		vm::write64(addr, value);
	}

	int IPC_impl::get_port()
	{
		return g_cfg_ipc.get_port();
	}

	pine::EmuStatus IPC_impl::get_status()
	{
		switch (Emu.GetStatus())
		{
		case system_state::running:
			return pine::EmuStatus::Running;
		case system_state::paused:
			return pine::EmuStatus::Paused;
		default:
			return pine::EmuStatus::Shutdown;
		}
	}

	const std::string& IPC_impl::get_title()
	{
		return Emu.GetTitle();
	}

	const std::string& IPC_impl::get_title_ID()
	{
		return Emu.GetTitleID();
	}

	const std::string& IPC_impl::get_executable_hash()
	{
		return Emu.GetExecutableHash();
	}

	const std::string& IPC_impl::get_app_version()
	{
		return Emu.GetAppVersion();
	}

	std::string IPC_impl::get_version_and_branch()
	{
		return rpcs3::get_version_and_branch();
	}

	std::string IPC_impl::get_ipc_version_string()
	{
		return std::string("RPCS3 ") + rpcs3::get_version_and_branch() + " | IGNITION_PINE:1";
	}

	std::string IPC_impl::pine_get_setting(std::string_view section, std::string_view key)
	{
		std::string result;
		Emu.BlockingCallFromMainThread([&]()
		{
			if (cfg::_base* leaf = find_cfg_leaf(g_cfg, section, key))
				result = leaf->to_string();
		});
		return result;
	}

	bool IPC_impl::pine_set_setting(std::string_view section, std::string_view key, std::string_view value)
	{
		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			cfg::_base* leaf = find_cfg_leaf(g_cfg, section, key);
			if (!leaf || leaf->get_type() == cfg::type::node)
				return;
			const bool dynamic = !Emu.IsStopped();
			if (dynamic && !leaf->get_is_dynamic())
				return;
			ok = leaf->from_string(value, dynamic);
		});
		return ok;
	}

	bool IPC_impl::pine_apply_settings()
	{
		const auto& cb = Emu.GetCallbacks().save_emu_settings;
		if (!cb)
			return false;
		cb();

		if (!has_pending_controller_changes())
			return true;

		bool controller_apply_ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			controller_apply_ok = apply_pending_controller_changes();
		});
		if (!controller_apply_ok)
			return false;

		clear_pending_controller_changes();
		return true;
	}

	bool IPC_impl::pine_pause_emulation()
	{
		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			switch (Emu.GetStatus(false))
			{
			case system_state::running:
				ok = Emu.Pause(false, false);
				break;
			case system_state::paused:
				ok = true;
				break;
			default:
				ok = false;
				break;
			}
		});
		return ok;
	}

	bool IPC_impl::pine_resume_emulation()
	{
		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			switch (Emu.GetStatus(false))
			{
			case system_state::running:
				ok = true;
				break;
			case system_state::paused:
				Emu.Resume();
				ok = true;
				break;
			default:
				ok = false;
				break;
			}
		});
		return ok;
	}

	std::vector<IPC_impl::pine_controller_entry> IPC_impl::pine_list_controllers()
	{
		std::vector<pine_controller_entry> result;
		Emu.BlockingCallFromMainThread([&]()
		{
			for (const controller_device_desc& device : enumerate_controllers())
			{
				result.push_back(pine_controller_entry{
					.device_id = device.device_id,
					.display_name = device.display_name,
				});
			}
		});
		return result;
	}

	bool IPC_impl::pine_get_port_binding(u8 port, std::string& device_prefix, std::string& controller_type)
	{
		if (!is_valid_controller_port(port))
			return false;

		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			load_active_global_input_config();
			cfg_player* const player = g_cfg_input.player[to_port_index(port)];
			if (!player)
				return;
			device_prefix = player->device.to_string();
			controller_type = player->handler.to_string();
			ok = true;
		});
		return ok;
	}

	u8 IPC_impl::pine_auto_map_port(u8 port, std::string_view device_id)
	{
		// 0 = ok, 1 = invalid port, 2 = device not found
		if (!is_valid_controller_port(port))
			return 1;

		bool device_found = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			device_found = find_controller_by_device_id(device_id).has_value();
		});
		if (!device_found)
			return 2;

		std::lock_guard lock(g_pending_controller_mutex);
		g_pending_controller_state.pending_auto_map[to_port_index(port)] = std::string(device_id);
		return 0;
	}

	bool IPC_impl::pine_get_button_binding(u8 port, std::string_view action_id, std::string& binding)
	{
		if (!is_valid_controller_port(port))
			return false;

		const canonical_action_binding* action = find_canonical_action(action_id);
		if (!action)
			return false;

		{
			std::lock_guard lock(g_pending_controller_mutex);
			const auto& pending = g_pending_controller_state.pending_bindings[to_port_index(port)];
			const auto it = pending.find(std::string(action_id));
			if (it != pending.end())
			{
				binding = it->second;
				return true;
			}
		}

		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			load_active_global_input_config();
			cfg_player* const player = g_cfg_input.player[to_port_index(port)];
			if (!player)
				return;
			binding = (player->config.*(action->field)).to_string();
			ok = true;
		});
		return ok;
	}

	u8 IPC_impl::pine_set_button_binding(u8 port, std::string_view action_id, std::string_view binding)
	{
		// 0 = ok, 1 = invalid port, 2 = invalid action
		if (!is_valid_controller_port(port))
			return 1;

		if (!find_canonical_action(action_id))
			return 2;

		std::lock_guard lock(g_pending_controller_mutex);
		g_pending_controller_state.pending_bindings[to_port_index(port)][std::string(action_id)] = std::string(binding);
		return 0;
	}

	std::vector<IPC_impl::pine_patch_entry> IPC_impl::pine_list_patches()
	{
		std::vector<pine_patch_entry> result;
		Emu.BlockingCallFromMainThread([&]()
		{
			const patch_engine::patch_map patches_map = load_patch_catalog_map();
			const std::string runtime_serial = Emu.GetTitleID();
			const std::string runtime_app_version = Emu.GetAppVersion();
			for (const patch_catalog_entry& entry : build_patch_catalog_entries(patches_map, runtime_serial, runtime_app_version))
			{
				result.push_back(pine_patch_entry{
					.name = entry.patch_id,
					.description = entry.description,
					.place = entry.place,
					.enabled = entry.enabled,
					.global_toggleable = true,
				});
			}
		});
		return result;
	}

	bool IPC_impl::pine_set_patch_enabled(std::string_view patch_name, bool enabled)
	{
		bool ok = false;
		Emu.BlockingCallFromMainThread([&]()
		{
			patch_target_key target{};
			if (!parse_patch_id(patch_name, target))
				return;

			patch_engine::patch_map patches_map = load_patch_catalog_map();
			if (!set_patch_enabled_state(patches_map, target, enabled))
				return;

			patch_engine::save_config(patches_map);
			if (!Emu.IsStopped())
			{
				IPC.notice("PINE set_patch_enabled persisted while emulation is running; full effect may require game reboot");
			}
			ok = true;
		});
		return ok;
	}

	IPC_impl& IPC_impl::operator=(thread_state)
	{
		return *this;
	}

	IPC_server_manager::IPC_server_manager(bool enabled)
	{
		// Enable IPC if needed
		set_server_enabled(enabled);
	}

	void IPC_server_manager::set_server_enabled(bool enabled)
	{
		if (enabled)
		{
			int port = g_cfg_ipc.get_port();
			if (!m_ipc_server || port != m_old_port)
			{
				IPC.notice("Starting server with port %d", port);
				m_ipc_server = std::make_unique<IPC_server>();
				m_old_port = port;
			}
		}
		else if (m_ipc_server)
		{
			IPC.notice("Stopping server");
			m_ipc_server.reset();
		}
	}
}
