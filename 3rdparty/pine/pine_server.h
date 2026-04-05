// Based on https://github.com/PCSX2/pcsx2/blob/edeb0d7bd7258c58273cc4a88a9f9a823d71e48c/pcsx2/IPC.h
// and https://github.com/PCSX2/pcsx2/blob/edeb0d7bd7258c58273cc4a88a9f9a823d71e48c/pcsx2/IPC.cpp
// Relicensed as GPLv2 for use in RPCS3 with permission from copyright owner (Govanify).

#pragma once

// IPC uses a concept of "slot" to be able to communicate with multiple
// emulators at the same time, each slot should be unique to each emulator to
// allow PnP and configurable by the end user so that several runs don't
// conflict with each others
#define IPC_DEFAULT_SLOT 28012

#include <cstring>
#include <string>
#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <sys/types.h>
#if _WIN32
#define read_portable(a, b, c) (recv(a, b, ::narrow<int>(c), 0))
#define write_portable(a, b, c) (send(a, b, ::narrow<int>(c), 0))
#define close_portable(a) (closesocket(a))
#define bzero(b, len) (memset((b), '\0', (len)), (void)0)
#include <WinSock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#define read_portable(a, b, c) (read(a, b, c))
#define write_portable(a, b, c) (write(a, b, c))
#define close_portable(a) (close(a))
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#endif

#ifdef _WIN32
constexpr SOCKET invalid_socket = INVALID_SOCKET;
#else
constexpr int invalid_socket = -1;
#endif

namespace pine
{
	/**
	 * Emulator status enum. @n
	 * A list of possible emulator statuses. @n
	 */
	enum EmuStatus : uint32_t
	{
		Running = 0,            /**< Game is running */
		Paused = 1,             /**< Game is paused */
		Shutdown = 2,           /**< Game is shutdown */
	};

	typedef unsigned long long SOCKET;

	template<typename Impl>
	class pine_server : public Impl
	{
	public:
#ifdef _WIN32
		// windows claim to have support for AF_UNIX sockets but that is a blatant lie,
		// their SDK won't even run their own examples, so we go on TCP sockets.
		SOCKET m_sock;
		// the message socket used in thread's accept().
		SOCKET m_msgsock;
#else
		// absolute path of the socket. Stored in XDG_RUNTIME_DIR, if unset /tmp
		std::string m_socket_name;
		int m_sock = 0;
		// the message socket used in thread's accept().
		int m_msgsock = 0;
#endif

		/**
		 * Maximum memory used by an IPC message request.
		 * Equivalent to 50,000 Write64 requests.
		 */
		#define MAX_IPC_SIZE 650000

		/**
		 * Maximum memory used by an IPC message reply.
		 * Equivalent to 50,000 Read64 replies.
		 */
		#define MAX_IPC_RETURN_SIZE 450000

		/**
		 * IPC return buffer.
		 * A preallocated buffer used to store all IPC replies.
		 * to the size of 50.000 MsgWrite64 IPC calls.
		 */
		std::vector<char> m_ret_buffer;

		/**
		 * IPC messages buffer.
		 * A preallocated buffer used to store all IPC messages.
		 */
		std::vector<char> m_ipc_buffer;

		/**
		 * IPC Command messages opcodes.
		 * A list of possible operations possible by the IPC.
		 * Each one of them is what we call an "opcode" and is the first
		 * byte sent by the IPC to differentiate between commands.
		 */
		enum IPCCommand : unsigned char
		{
			MsgRead8 = 0,           /**< Read 8 bit value to memory. */
			MsgRead16 = 1,          /**< Read 16 bit value to memory. */
			MsgRead32 = 2,          /**< Read 32 bit value to memory. */
			MsgRead64 = 3,          /**< Read 64 bit value to memory. */
			MsgWrite8 = 4,          /**< Write 8 bit value to memory. */
			MsgWrite16 = 5,         /**< Write 16 bit value to memory. */
			MsgWrite32 = 6,         /**< Write 32 bit value to memory. */
			MsgWrite64 = 7,         /**< Write 64 bit value to memory. */
			MsgVersion = 8,         /**< Returns RPCS3 version. */
			MsgTitle = 0xB,         /**< Returns the game title. */
			MsgID = 0xC,            /**< Returns the game ID. */
			MsgUUID = 0xD,          /**< Returns the game UUID. */
			MsgGameVersion = 0xE,   /**< Returns the game verion. */
			MsgStatus = 0xF,        /**< Returns the emulator status. */
			MsgGetSetting = 0x10,   /**< Get g_cfg value by section + key (Ignition / PCSX2-compatible). */
			MsgSetSetting = 0x11,   /**< Set g_cfg value (Ignition / PCSX2-compatible). */
			MsgApplySettings = 0x12, /**< Persist and apply settings (Ignition / PCSX2-compatible). */
			MsgListControllers = 0x13, /**< List currently visible controllers. */
			MsgGetPortBinding = 0x14, /**< Read port -> device/handler assignment. */
			MsgAutoMapPort = 0x15, /**< Stage port auto-map request. */
			MsgGetButtonBinding = 0x16, /**< Read button binding (staged first). */
			MsgSetButtonBinding = 0x17, /**< Stage button binding change. */
			MsgListPatches = 0x18, /**< List active-title patch catalog entries. */
			MsgSetPatchEnabled = 0x19, /**< Enable/disable a patch by opaque patch ID. */
			MsgPauseEmulation = 0x1A, /**< Pause emulation without suspending process execution. */
			MsgResumeEmulation = 0x1B, /**< Resume emulation after MsgPauseEmulation. */
			MsgUnimplemented = 0xFF /**< Unimplemented IPC message. */
		};

		/**
		 * IPC message buffer.
		 * A list of all needed fields to store an IPC message.
		 */
		struct IPCBuffer
		{
			usz size{};     /**< Size of the buffer. */
			char* buffer{}; /**< Buffer. */
		};

		/**
		 * IPC result codes.
		 * A list of possible result codes the IPC can send back.
		 * Each one of them is what we call an "opcode" or "tag" and is the
		 * first byte sent by the IPC to differentiate between results.
		 */
		enum IPCResult : unsigned char
		{
			IPC_OK = 0,     /**< IPC command successfully completed. */
			IPC_FAIL = 0xFF /**< IPC command failed to complete. */
		};

		/**
		 * Internal function, Parses an IPC command.
		 * buf: buffer containing the IPC command.
		 * buf_size: size of the buffer announced.
		 * ret_buffer: buffer that will be used to send the reply.
		 * return value: IPCBuffer containing a buffer with the result
		 *               of the command and its size.
		 */
		static IPCBuffer ParseCommand(char* buf, char* ret_buffer, u32 buf_size)
		{
			usz ret_cnt = 5;
			usz buf_cnt = 0;

			const auto error = [&]()
			{
				return IPCBuffer{ 5, MakeFailIPC(ret_buffer) };
			};

			const auto write_string = [&](const std::string& str)
			{
				if (!SafetyChecks(buf_cnt, 0, ret_cnt, str.size() + 1 + sizeof(u32), buf_size))
					return false;
				ToArray(ret_buffer, ::narrow<u32>(str.size() + 1), ret_cnt);
				ret_cnt += sizeof(u32);
				memcpy(&ret_buffer[ret_cnt], str.data(), str.size());
				ret_cnt += str.size();
				ret_buffer[ret_cnt++] = '\0';
				return true;
			};

			while (buf_cnt < buf_size)
			{
				if (!SafetyChecks(buf_cnt, 1, ret_cnt, 0, buf_size))
					return error();
				buf_cnt++;
				// example IPC messages: MsgRead/Write
				// refer to the client doc for more info on the format
				//         IPC Message event (1 byte)
				//         |  Memory address (4 byte)
				//         |  |           argument (VLE)
				//         |  |           |
				// format: XX YY YY YY YY ZZ ZZ ZZ ZZ
				//        reply code: 00 = OK, FF = NOT OK
				//        |  return value (VLE)
				//        |  |
				// reply: XX ZZ ZZ ZZ ZZ
				IPCCommand command = std::bit_cast<IPCCommand>(buf[buf_cnt - 1]);
				switch (command)
				{
				case MsgRead8:
				{
					if (!SafetyChecks(buf_cnt, 4, ret_cnt, 1, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<1>(a))
						return error();
					const u8 res = Impl::read8(a);
					ToArray(ret_buffer, res, ret_cnt);
					ret_cnt += 1;
					buf_cnt += 4;
					break;
				}
				case MsgRead16:
				{
					if (!SafetyChecks(buf_cnt, 4, ret_cnt, 2, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<2>(a))
						return error();
					const u16 res = Impl::read16(a);
					ToArray(ret_buffer, res, ret_cnt);
					ret_cnt += 2;
					buf_cnt += 4;
					break;
				}
				case MsgRead32:
				{
					if (!SafetyChecks(buf_cnt, 4, ret_cnt, 4, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<4>(a))
						return error();
					const u32 res = Impl::read32(a);
					ToArray(ret_buffer, res, ret_cnt);
					ret_cnt += 4;
					buf_cnt += 4;
					break;
				}
				case MsgRead64:
				{
					if (!SafetyChecks(buf_cnt, 4, ret_cnt, 8, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<8>(a))
						return error();
					u64 res = Impl::read64(a);
					ToArray(ret_buffer, res, ret_cnt);
					ret_cnt += 8;
					buf_cnt += 4;
					break;
				}
				case MsgWrite8:
				{
					if (!SafetyChecks(buf_cnt, 1 + 4, ret_cnt, 0, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<1>(a, Impl::page_writable))
						return error();
					Impl::write8(a, FromArray<u8>(&buf[buf_cnt], 4));
					buf_cnt += 5;
					break;
				}
				case MsgWrite16:
				{
					if (!SafetyChecks(buf_cnt, 2 + 4, ret_cnt, 0, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<2>(a, Impl::page_writable))
						return error();
					Impl::write16(a, FromArray<u16>(&buf[buf_cnt], 4));
					buf_cnt += 6;
					break;
				}
				case MsgWrite32:
				{
					if (!SafetyChecks(buf_cnt, 4 + 4, ret_cnt, 0, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<4>(a, Impl::page_writable))
						return error();
					Impl::write32(a, FromArray<u32>(&buf[buf_cnt], 4));
					buf_cnt += 8;
					break;
				}
				case MsgWrite64:
				{
					if (!SafetyChecks(buf_cnt, 8 + 4, ret_cnt, 0, buf_size))
						return error();
					const u32 a = FromArray<u32>(&buf[buf_cnt], 0);
					if (!Impl::template check_addr<8>(a, Impl::page_writable))
						return error();
					Impl::write64(a, FromArray<u64>(&buf[buf_cnt], 4));
					buf_cnt += 12;
					break;
				}
				case MsgVersion:
				{
					if (!write_string(Impl::get_ipc_version_string()))
						return error();
					break;
				}
				case MsgStatus:
				{
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, 4, buf_size))
						return error();
					EmuStatus status = Impl::get_status();
					ToArray(ret_buffer, status, ret_cnt);
					ret_cnt += 4;
					break;
				}
				case MsgTitle:
				{
					if (!write_string(Impl::get_title()))
						return error();
					break;
				}
				case MsgID:
				{
					if (!write_string(Impl::get_title_ID()))
						return error();
					break;
				}
				case MsgUUID:
				{
					if (!write_string(Impl::get_executable_hash()))
						return error();
					break;
				}
				case MsgGameVersion:
				{
					if (!write_string(Impl::get_app_version()))
						return error();
					break;
				}
				case MsgGetSetting:
				{
					// Wire format: [u32 section_len][section bytes][u32 key_len][key bytes]
					// Response:    [u32 value_len][value bytes] (PCSX2 / Ignition)
					if (!SafetyChecks(buf_cnt, 8, ret_cnt, 0, buf_size))
						return error();
					const u32 section_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += 4;
					if (!SafetyChecks(buf_cnt, section_len + 4, ret_cnt, 0, buf_size))
						return error();
					const std::string section(reinterpret_cast<const char*>(&buf[buf_cnt]), section_len);
					buf_cnt += section_len;
					const u32 key_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += 4;
					if (!SafetyChecks(buf_cnt, key_len, ret_cnt, 0, buf_size))
						return error();
					const std::string key(reinterpret_cast<const char*>(&buf[buf_cnt]), key_len);
					buf_cnt += key_len;
					const std::string value = Impl::pine_get_setting(section, key);
					const u32 val_len = static_cast<u32>(value.size());
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, 4 + val_len, buf_size))
						return error();
					ToArray(ret_buffer, val_len, ret_cnt);
					ret_cnt += sizeof(u32);
					if (val_len > 0)
					{
						memcpy(&ret_buffer[ret_cnt], value.data(), val_len);
						ret_cnt += val_len;
					}
					break;
				}
				case MsgSetSetting:
				{
					// Wire format: [u32 section_len][section][u32 key_len][key][u32 value_len][value]
					if (!SafetyChecks(buf_cnt, 12, ret_cnt, 0, buf_size))
						return error();
					const u32 ss_section_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += 4;
					if (!SafetyChecks(buf_cnt, ss_section_len + 4, ret_cnt, 0, buf_size))
						return error();
					const std::string ss_section(reinterpret_cast<const char*>(&buf[buf_cnt]), ss_section_len);
					buf_cnt += ss_section_len;
					const u32 ss_key_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += 4;
					if (!SafetyChecks(buf_cnt, ss_key_len + 4, ret_cnt, 0, buf_size))
						return error();
					const std::string ss_key(reinterpret_cast<const char*>(&buf[buf_cnt]), ss_key_len);
					buf_cnt += ss_key_len;
					const u32 ss_value_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += 4;
					if (!SafetyChecks(buf_cnt, ss_value_len, ret_cnt, 0, buf_size))
						return error();
					const std::string ss_value(reinterpret_cast<const char*>(&buf[buf_cnt]), ss_value_len);
					buf_cnt += ss_value_len;
					if (!Impl::pine_set_setting(ss_section, ss_key, ss_value))
						return error();
					break;
				}
				case MsgApplySettings:
				{
					if (!Impl::pine_apply_settings())
						return error();
					break;
				}
				case MsgListControllers:
				{
					const auto controllers = Impl::pine_list_controllers();
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, 4, buf_size))
						return error();
					ToArray(ret_buffer, ::narrow<u32>(controllers.size()), ret_cnt);
					ret_cnt += sizeof(u32);
					for (const auto& controller : controllers)
					{
						const u32 id_len = ::narrow<u32>(controller.device_id.size());
						const u32 name_len = ::narrow<u32>(controller.display_name.size());
						if (!SafetyChecks(buf_cnt, 0, ret_cnt, sizeof(u32) + id_len + sizeof(u32) + name_len, buf_size))
							return error();
						ToArray(ret_buffer, id_len, ret_cnt);
						ret_cnt += sizeof(u32);
						if (id_len > 0)
						{
							memcpy(&ret_buffer[ret_cnt], controller.device_id.data(), id_len);
							ret_cnt += id_len;
						}
						ToArray(ret_buffer, name_len, ret_cnt);
						ret_cnt += sizeof(u32);
						if (name_len > 0)
						{
							memcpy(&ret_buffer[ret_cnt], controller.display_name.data(), name_len);
							ret_cnt += name_len;
						}
					}
					break;
				}
				case MsgGetPortBinding:
				{
					// Wire format: [u8 port]
					if (!SafetyChecks(buf_cnt, 1, ret_cnt, 0, buf_size))
						return error();
					const u8 port = FromArray<u8>(&buf[buf_cnt], 0);
					buf_cnt += 1;

					std::string device_prefix;
					std::string controller_type;
					if (!Impl::pine_get_port_binding(port, device_prefix, controller_type))
						return error();

					const u32 prefix_len = ::narrow<u32>(device_prefix.size());
					const u32 type_len = ::narrow<u32>(controller_type.size());
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, sizeof(u32) + prefix_len + sizeof(u32) + type_len, buf_size))
						return error();

					ToArray(ret_buffer, prefix_len, ret_cnt);
					ret_cnt += sizeof(u32);
					if (prefix_len > 0)
					{
						memcpy(&ret_buffer[ret_cnt], device_prefix.data(), prefix_len);
						ret_cnt += prefix_len;
					}

					ToArray(ret_buffer, type_len, ret_cnt);
					ret_cnt += sizeof(u32);
					if (type_len > 0)
					{
						memcpy(&ret_buffer[ret_cnt], controller_type.data(), type_len);
						ret_cnt += type_len;
					}
					break;
				}
				case MsgAutoMapPort:
				{
					// Wire format: [u8 port][u32 device_id_len][device_id]
					if (!SafetyChecks(buf_cnt, 1 + 4, ret_cnt, 1, buf_size))
						return error();
					const u8 port = FromArray<u8>(&buf[buf_cnt], 0);
					buf_cnt += 1;
					const u32 device_id_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += sizeof(u32);
					if (!SafetyChecks(buf_cnt, device_id_len, ret_cnt, 1, buf_size))
						return error();
					const std::string device_id(reinterpret_cast<const char*>(&buf[buf_cnt]), device_id_len);
					buf_cnt += device_id_len;

					const u8 status = Impl::pine_auto_map_port(port, device_id);
					ToArray(ret_buffer, status, ret_cnt);
					ret_cnt += 1;
					break;
				}
				case MsgGetButtonBinding:
				{
					// Wire format: [u8 port][u32 action_len][action]
					if (!SafetyChecks(buf_cnt, 1 + 4, ret_cnt, 0, buf_size))
						return error();
					const u8 port = FromArray<u8>(&buf[buf_cnt], 0);
					buf_cnt += 1;
					const u32 action_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += sizeof(u32);
					if (!SafetyChecks(buf_cnt, action_len, ret_cnt, 0, buf_size))
						return error();
					const std::string action(reinterpret_cast<const char*>(&buf[buf_cnt]), action_len);
					buf_cnt += action_len;

					std::string binding;
					if (!Impl::pine_get_button_binding(port, action, binding))
						return error();

					const u32 binding_len = ::narrow<u32>(binding.size());
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, sizeof(u32) + binding_len, buf_size))
						return error();
					ToArray(ret_buffer, binding_len, ret_cnt);
					ret_cnt += sizeof(u32);
					if (binding_len > 0)
					{
						memcpy(&ret_buffer[ret_cnt], binding.data(), binding_len);
						ret_cnt += binding_len;
					}
					break;
				}
				case MsgSetButtonBinding:
				{
					// Wire format: [u8 port][u32 action_len][action][u32 binding_len][binding]
					if (!SafetyChecks(buf_cnt, 1 + 4, ret_cnt, 1, buf_size))
						return error();
					const u8 port = FromArray<u8>(&buf[buf_cnt], 0);
					buf_cnt += 1;

					const u32 action_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += sizeof(u32);
					if (!SafetyChecks(buf_cnt, action_len + 4, ret_cnt, 1, buf_size))
						return error();
					const std::string action(reinterpret_cast<const char*>(&buf[buf_cnt]), action_len);
					buf_cnt += action_len;

					const u32 binding_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += sizeof(u32);
					if (!SafetyChecks(buf_cnt, binding_len, ret_cnt, 1, buf_size))
						return error();
					const std::string binding(reinterpret_cast<const char*>(&buf[buf_cnt]), binding_len);
					buf_cnt += binding_len;

					const u8 status = Impl::pine_set_button_binding(port, action, binding);
					ToArray(ret_buffer, status, ret_cnt);
					ret_cnt += 1;
					break;
				}
				case MsgListPatches:
				{
					const auto patches = Impl::pine_list_patches();
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, sizeof(u32), buf_size))
						return error();
					ToArray(ret_buffer, ::narrow<u32>(patches.size()), ret_cnt);
					ret_cnt += sizeof(u32);

					for (const auto& patch : patches)
					{
						const u32 name_len = ::narrow<u32>(patch.name.size());
						const u32 desc_len = ::narrow<u32>(patch.description.size());
						const u32 place_len = ::narrow<u32>(patch.place.size());
						if (!SafetyChecks(buf_cnt, 0, ret_cnt, sizeof(u32) + name_len + sizeof(u32) + desc_len + sizeof(u32) + place_len + 2, buf_size))
							return error();

						ToArray(ret_buffer, name_len, ret_cnt);
						ret_cnt += sizeof(u32);
						if (name_len > 0)
						{
							memcpy(&ret_buffer[ret_cnt], patch.name.data(), name_len);
							ret_cnt += name_len;
						}

						ToArray(ret_buffer, desc_len, ret_cnt);
						ret_cnt += sizeof(u32);
						if (desc_len > 0)
						{
							memcpy(&ret_buffer[ret_cnt], patch.description.data(), desc_len);
							ret_cnt += desc_len;
						}

						ToArray(ret_buffer, place_len, ret_cnt);
						ret_cnt += sizeof(u32);
						if (place_len > 0)
						{
							memcpy(&ret_buffer[ret_cnt], patch.place.data(), place_len);
							ret_cnt += place_len;
						}

						ret_buffer[ret_cnt++] = patch.enabled ? 1 : 0;
						ret_buffer[ret_cnt++] = patch.global_toggleable ? 1 : 0;
					}
					break;
				}
				case MsgSetPatchEnabled:
				{
					// Wire format: [u32 patch_name_len][patch_name][u8 enabled]
					if (!SafetyChecks(buf_cnt, sizeof(u32) + 1, ret_cnt, 0, buf_size))
						return error();
					const u32 patch_name_len = FromArray<u32>(&buf[buf_cnt], 0);
					buf_cnt += sizeof(u32);
					if (!SafetyChecks(buf_cnt, patch_name_len + 1, ret_cnt, 0, buf_size))
						return error();
					const std::string patch_name(reinterpret_cast<const char*>(&buf[buf_cnt]), patch_name_len);
					buf_cnt += patch_name_len;
					const bool enabled = (FromArray<u8>(&buf[buf_cnt], 0) != 0);
					buf_cnt += 1;

					if (!Impl::pine_set_patch_enabled(patch_name, enabled))
						return error();
					break;
				}
				case MsgPauseEmulation:
				{
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, 0, buf_size))
						return error();
					if (!Impl::pine_pause_emulation())
						return error();
					break;
				}
				case MsgResumeEmulation:
				{
					if (!SafetyChecks(buf_cnt, 0, ret_cnt, 0, buf_size))
						return error();
					if (!Impl::pine_resume_emulation())
						return error();
					break;
				}
				default:
				{
					return error();
				}
				}
			}
			return IPCBuffer{ ret_cnt, MakeOkIPC(ret_buffer, ret_cnt) };
		}

		/**
		 * Formats an IPC buffer
		 * ret_buffer: return buffer to use.
		 * size: size of the IPC buffer.
		 * return value: buffer containing the status code allocated of size
		 */
		static inline char* MakeOkIPC(char* ret_buffer, usz size = 5)
		{
			ToArray(ret_buffer, ::narrow<u32>(size), 0);
			ret_buffer[4] = IPC_OK;
			return ret_buffer;
		}

		static inline char* MakeFailIPC(char* ret_buffer, usz size = 5)
		{
			ToArray(ret_buffer, ::narrow<u32>(size), 0);
			ret_buffer[4] = IPC_FAIL;
			return ret_buffer;
		}

		/**
		 * Initializes an open socket for IPC communication.
		 * return value: false if a fatal failure happened, true otherwise.
		 */
		bool StartSocket()
		{
			::pollfd poll_fd{};

			for (int pending_connection = 0; pending_connection != 1;)
			{
				if (Impl::is_aborting())
				{
					return false;
				}

				std::memset(&poll_fd, 0, sizeof(poll_fd));
				poll_fd.events  = POLLIN;
				poll_fd.revents = 0;
				poll_fd.fd      = m_sock;

#ifdef _WIN32
				// Try to wait for an incoming connection
				pending_connection = ::WSAPoll(&poll_fd, 1, 10);
#else
				pending_connection = ::poll(&poll_fd, 1, 10);
#endif
			}

			m_msgsock = accept(m_sock, 0, 0);

			if (m_msgsock == invalid_socket)
			{
				// everything else is non recoverable in our scope
				// we also mark as recoverable socket errors where it would block a
				// non blocking socket, even though our socket is blocking, in case
				// we ever have to implement a non blocking socket.
#ifdef _WIN32
				int errno_w = WSAGetLastError();
				if (!(errno_w == WSAECONNRESET || errno_w == WSAEINTR || errno_w == WSAEINPROGRESS || errno_w == WSAEMFILE || errno_w == WSAEWOULDBLOCK))
				{
#else
				if (!(errno == ECONNABORTED || errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
				{
#endif
					Impl::error("IPC: An unrecoverable error happened! Shutting down...");
					return false;
				}
			}
			return true;
		}

		// Thread used to relay IPC commands.
		void operator()()
		{
			// we allocate once buffers to not have to do mallocs for each IPC
			// request, as malloc is expansive when we optimize for µs.
			m_ret_buffer.resize(MAX_IPC_RETURN_SIZE);
			m_ipc_buffer.resize(MAX_IPC_SIZE);

			if (!StartSocket())
				return;

			while (!Impl::is_aborting())
			{
				// either int or ssize_t depending on the platform, so we have to
				// use a bunch of auto
				auto receive_length = 0;
				auto end_length = 4;

				// while we haven't received the entire packet, maybe due to
				// socket datagram splittage, we continue to read
				while (receive_length < end_length)
				{
					auto tmp_length = read_portable(m_msgsock, &m_ipc_buffer[receive_length], MAX_IPC_SIZE - receive_length);

					// we recreate the socket if an error happens
					if (tmp_length <= 0)
					{
						receive_length = 0;
						if (!StartSocket())
							return;
						break;
					}

					receive_length += tmp_length;

					// if we got at least the final size then update
					if (end_length == 4 && receive_length >= 4)
					{
						end_length = FromArray<u32>(m_ipc_buffer.data(), 0);
						// we'd like to avoid a client trying to do OOB
						if (end_length > MAX_IPC_SIZE || end_length < 4)
						{
							receive_length = 0;
							break;
						}
					}
				}

				// we remove 4 bytes to get the message size out of the IPC command
				// size in ParseCommand.
				// also, if we got a failed command, let's reset the state so we don't
				// end up deadlocking by getting out of sync, eg when a client
				// disconnects
				if (receive_length != 0)
				{
					pine_server::IPCBuffer res = ParseCommand(&m_ipc_buffer[4], m_ret_buffer.data(), static_cast<u32>(end_length) - 4);

					// if we cannot send back our answer restart the socket
					if (write_portable(m_msgsock, res.buffer, res.size) < 0)
					{
						if (!StartSocket())
							return;
					}
				}
			}
		}

		/**
		 * Converts an uint to an char* in little endian
		 * res_array: the array to modify
		 * res: the value to convert
		 * i: when to insert it into the array
		 * return value: res_array
		 * NB: implicitely inlined
		 */
		template <typename T>
		static char* ToArray(char* res_array, T res, usz i)
		{
			memcpy(res_array + i, reinterpret_cast<char*>(&res), sizeof(T));
			return res_array;
		}

		/**
		 * Converts a char* to an uint in little endian
		 * arr: the array to convert
		 * i: when to load it from the array
		 * return value: the converted value
		 * NB: implicitely inlined
		 */
		template <typename T>
		static T FromArray(char* arr, int i)
		{
			return *reinterpret_cast<T*>(arr + i);
		}

		/**
		 * Ensures an IPC message isn't too big.
		 * return value: false if checks failed, true otherwise.
		 */
		static inline bool SafetyChecks(usz command_len, usz command_size, usz reply_len, usz reply_size = 0, usz buf_size = MAX_IPC_SIZE - 1)
		{
			bool res = ((command_len + command_size) > buf_size ||
				(reply_len + reply_size) >= MAX_IPC_RETURN_SIZE);
			if (res) [[unlikely]]
				return false;
			return true;
		}

	public:
		/* Initializers */
		pine_server() noexcept
		{
#ifdef _WIN32
			WSADATA wsa;
			struct sockaddr_in server;
			m_sock = INVALID_SOCKET;
			m_msgsock = INVALID_SOCKET;

			if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			{
				Impl::error("IPC: Cannot initialize winsock! Shutting down...");
				return;
			}

			if ((m_sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			{
				Impl::error("IPC: Cannot open socket! Shutting down...");
				return;
			}

			// yes very good windows s/sun/sin/g sure is fine
			server.sin_family = AF_INET;
			// localhost only
			if (!inet_pton(server.sin_family, "127.0.0.1", &server.sin_addr.s_addr))
			{
				fmt::throw_exception("IPC: Failed to convert localhost");
			}
			server.sin_port = htons(Impl::get_port());

			if (bind(m_sock, reinterpret_cast<struct sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR)
			{
				Impl::error("IPC: Error while binding to socket! Shutting down...");
				return;
			}

#else
			char* runtime_dir = nullptr;
#ifdef __APPLE__
			runtime_dir = std::getenv("TMPDIR");
#else
			runtime_dir = std::getenv("XDG_RUNTIME_DIR");
#endif
			// fallback in case macOS or other OSes don't implement the XDG base
			// spec
			if (runtime_dir == nullptr)
				m_socket_name = "/tmp/rpcs3.sock";
			else
			{
				m_socket_name = runtime_dir;
				m_socket_name += "/rpcs3.sock";
			}

			const int slot = Impl::get_port();
			if (slot != IPC_DEFAULT_SLOT)
			{
				fmt::append(m_socket_name, ".%d", slot);
			}

			struct sockaddr_un server;

			m_sock = socket(AF_UNIX, SOCK_STREAM, 0);
			if (m_sock < 0)
			{
				Impl::error("IPC: Cannot open socket! Shutting down...");
				return;
			}
			server.sun_family = AF_UNIX;
			strcpy(server.sun_path, m_socket_name.c_str());

			// we unlink the socket so that when releasing this thread the socket gets
			// freed even if we didn't close correctly the loop
			unlink(m_socket_name.c_str());
			if (bind(m_sock, std::bit_cast<struct sockaddr*>(&server), sizeof(struct sockaddr_un)))
			{
				Impl::error("IPC: Error while binding to socket! Shutting down...");
				return;
			}
#endif

			// maximum queue of 4096 commands before refusing, approximated to the
			// nearest legal value. We do not use SOMAXCONN as windows have this idea
			// that a "reasonable" value is 5, which is not.
			listen(m_sock, 4096);
		}

		pine_server(const pine_server&) = delete;
		pine_server& operator=(const pine_server&) = delete;

		void Cleanup()
		{
#ifdef _WIN32
			WSACleanup();
#else
			unlink(m_socket_name.c_str());
#endif
			close_portable(m_sock);
			close_portable(m_msgsock);
		}

		~pine_server()
		{
			Cleanup();
		}

	}; // class pine_server
}
