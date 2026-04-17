#pragma once

#include "util/types.hpp"

#include <functional>
#include <string>
#include <string_view>

enum class pup_install_result : u32
{
	ok,
	cancelled,
	path_empty,
	open_failed,
	header_read,
	header_magic,
	expected_size,
	file_entries,
	hash_mismatch,
	no_update_db,
	statfs_failed,
	disk_full,
	mount_failed,
	extract_failed,
	no_dev_flash_entries,
	no_version,
	decrypt_failed,
	firmware_exists_no_force,
};

struct pup_install_callbacks
{
	// Confirm proceeding when the incoming firmware is older than the latest known version.
	// Return true to proceed. If null, proceed by default.
	std::function<bool(std::string_view incoming_version, std::string_view latest_known_version)> confirm_old_firmware;

	// Confirm overwriting an already-installed firmware.
	// Return true to proceed. If null, proceed by default.
	std::function<bool(std::string_view installed_version, std::string_view incoming_version)> confirm_reinstall;

	// Called once after all confirmations pass, right before extraction begins.
	// Receives the total number of packages that will be processed.
	// Use this to unload in-use resources (e.g. fonts) and create a progress UI.
	std::function<void(u64 total)> on_install_starting;

	// Polling tick from the orchestrating thread (roughly every 5ms).
	// GUI implementations should update their progress widget and pump Qt events here.
	std::function<void(u64 current, u64 total)> on_progress;

	// Polling cancel hook. Return true to abort the install. If null, the install cannot be cancelled.
	std::function<bool()> is_cancelled;
};

// Install (or extract) a PS3 firmware .PUP file without touching any UI.
// When extract_only_dir is non-empty, the PUP is extracted as raw TAR contents there instead
// of being decrypted and installed into dev_flash.
// out_version receives the incoming PUP's version string when available.
// out_detail receives human-readable failure context (e.g. fs::g_tls_error, package name) on failure.
pup_install_result install_pup(
	const std::string& pup_path,
	const std::string& extract_only_dir,
	const pup_install_callbacks& callbacks,
	std::string* out_version = nullptr,
	std::string* out_detail = nullptr);

const char* pup_install_result_to_string(pup_install_result result);
