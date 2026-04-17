#include "stdafx.h"

#include "PUP_install.h"
#include "PUP.h"
#include "TAR.h"

#include "Crypto/unself.h"
#include "Crypto/key_vault.h"

#include "Emu/System.h"
#include "Emu/VFS.h"
#include "Emu/vfs_config.h"

#include "util/sysinfo.hpp"
#include "Utilities/Thread.h"

#include <algorithm>
#include <chrono>
#include <thread>

LOG_CHANNEL(pup_install_log, "PUP");

namespace
{
	pup_install_result map_pup_error(pup_error err)
	{
		switch (err)
		{
		case pup_error::ok:                return pup_install_result::ok;
		case pup_error::stream:            return pup_install_result::file_entries;
		case pup_error::header_read:       return pup_install_result::header_read;
		case pup_error::header_magic:      return pup_install_result::header_magic;
		case pup_error::header_file_count: return pup_install_result::file_entries;
		case pup_error::expected_size:     return pup_install_result::expected_size;
		case pup_error::file_entries:      return pup_install_result::file_entries;
		case pup_error::hash_mismatch:     return pup_install_result::hash_mismatch;
		}
		return pup_install_result::file_entries;
	}
}

pup_install_result install_pup(
	const std::string& pup_path,
	const std::string& extract_only_dir,
	const pup_install_callbacks& cb,
	std::string* out_version,
	std::string* out_detail)
{
	using namespace std::chrono_literals;

	const auto set_detail = [&](std::string_view s)
	{
		if (out_detail) *out_detail = std::string(s);
	};

	if (pup_path.empty())
	{
		pup_install_log.error("Firmware install: provided path is empty.");
		return pup_install_result::path_empty;
	}

	Emu.GracefulShutdown(false);

	fs::file pup_f(pup_path);
	if (!pup_f)
	{
		const std::string err_detail = fmt::format("%s", fs::g_tls_error);
		pup_install_log.error("Firmware install: could not open PUP '%s' (%s)", pup_path, err_detail);
		set_detail(err_detail);
		return pup_install_result::open_failed;
	}

	pup_object pup(std::move(pup_f));

	if (const pup_error err = pup.operator pup_error(); err != pup_error::ok)
	{
		if (!pup.get_formatted_error().empty())
		{
			pup_install_log.error("Firmware install: PUP error (%s)", pup.get_formatted_error());
			set_detail(pup.get_formatted_error());
		}
		else
		{
			pup_install_log.error("Firmware install: PUP file is invalid.");
		}
		return map_pup_error(err);
	}

	fs::file update_files_f = pup.get_file(0x300);
	const usz update_files_size = update_files_f ? update_files_f.size() : 0;

	if (!update_files_size)
	{
		pup_install_log.error("Firmware install: could not find installation packages database in PUP.");
		return pup_install_result::no_update_db;
	}

	fs::device_stat dev_stat{};
	if (!fs::statfs(g_cfg_vfs.get_dev_flash(), dev_stat))
	{
		const std::string err_detail = fmt::format("statfs failed for '%s'", g_cfg_vfs.get_dev_flash());
		pup_install_log.error("Firmware install: %s", err_detail);
		set_detail(err_detail);
		return pup_install_result::statfs_failed;
	}

	if (dev_stat.avail_free < update_files_size)
	{
		const std::string err_detail = fmt::format("out of disk space ('%s', needed: %d bytes)", g_cfg_vfs.get_dev_flash(), update_files_size - dev_stat.avail_free);
		pup_install_log.error("Firmware install: %s", err_detail);
		set_detail(err_detail);
		return pup_install_result::disk_full;
	}

	tar_object update_files(update_files_f);

	if (!extract_only_dir.empty())
	{
		if (!vfs::mount("/pup_extract", extract_only_dir + '/'))
		{
			const std::string err_detail = fmt::format("failed to mount '%s'", extract_only_dir);
			pup_install_log.error("Firmware extract: %s", err_detail);
			set_detail(err_detail);
			return pup_install_result::mount_failed;
		}

		if (!update_files.extract("/pup_extract", true))
		{
			pup_install_log.error("Firmware extract: TAR contents are invalid.");
			return pup_install_result::extract_failed;
		}

		pup_install_log.success("Extracted PUP file to %s", extract_only_dir);
		return pup_install_result::ok;
	}

	auto update_filenames = update_files.get_filenames();

	update_filenames.erase(std::remove_if(
		update_filenames.begin(), update_filenames.end(), [](const std::string& s) { return s.find("dev_flash_") == umax; }),
		update_filenames.end());

	if (update_filenames.empty())
	{
		pup_install_log.error("Firmware install: no dev_flash_* packages were found in PUP.");
		return pup_install_result::no_dev_flash_entries;
	}

	static constexpr std::string_view cur_version = "4.93";

	std::string version_string;

	if (fs::file version = pup.get_file(0x100))
	{
		version_string = version.to_string();
	}

	if (const usz version_pos = version_string.find('\n'); version_pos != umax)
	{
		version_string.erase(version_pos);
	}

	if (version_string.empty())
	{
		pup_install_log.error("Firmware install: no version data was found in PUP.");
		return pup_install_result::no_version;
	}

	if (out_version) *out_version = version_string;

	if (version_string < cur_version)
	{
		const bool proceed = !cb.confirm_old_firmware || cb.confirm_old_firmware(version_string, cur_version);
		if (!proceed)
		{
			pup_install_log.notice("Firmware install: aborted by old-firmware confirmation (incoming=%s, latest=%s).", version_string, cur_version);
			return pup_install_result::cancelled;
		}
	}

	if (const std::string installed = utils::get_firmware_version(); !installed.empty())
	{
		pup_install_log.warning("Firmware install: reinstalling (old=%s, new=%s)", installed, version_string);

		const bool proceed = cb.confirm_reinstall ? cb.confirm_reinstall(installed, version_string) : true;
		if (!proceed)
		{
			pup_install_log.warning("Firmware install: aborted by reinstall confirmation.");
			return pup_install_result::firmware_exists_no_force;
		}
	}

	if (cb.on_install_starting)
	{
		cb.on_install_starting(update_filenames.size());
	}

	// Used by tar_object::extract() as destination directory
	vfs::mount("/dev_flash", g_cfg_vfs.get_dev_flash());

	atomic_t<pup_install_result> worker_result(pup_install_result::ok);
	std::string worker_detail;

	// Synchronization variable; -1 is sentinel for cancelled/failed.
	atomic_t<uint> progress(0);
	{
		named_thread worker("Firmware Installer", [&]
		{
			for (const auto& update_filename : update_filenames)
			{
				auto update_file_stream = update_files.get_file(update_filename);

				if (update_file_stream->m_file_handler)
				{
					// Forcefully read all the data
					update_file_stream->m_file_handler->handle_file_op(*update_file_stream, 0, update_file_stream->get_size(umax), nullptr);
				}

				fs::file update_file = fs::make_stream(std::move(update_file_stream->data));

				SCEDecrypter self_dec(update_file);
				self_dec.LoadHeaders();
				self_dec.LoadMetadata(SCEPKG_ERK, SCEPKG_RIV);
				self_dec.DecryptData();

				auto dev_flash_tar_f = self_dec.MakeFile();
				if (dev_flash_tar_f.size() < 3)
				{
					pup_install_log.error("Firmware install: PUP contents are invalid (package=%s).", update_filename);
					worker_detail = fmt::format("could not decrypt package '%s'", update_filename);
					worker_result = pup_install_result::decrypt_failed;
					progress = -1;
					return;
				}

				tar_object dev_flash_tar(dev_flash_tar_f[2]);
				if (!dev_flash_tar.extract())
				{
					pup_install_log.error("Firmware install: TAR contents are invalid. (package=%s)", update_filename);
					worker_detail = fmt::format("could not extract package '%s' (anti-virus interference?)", update_filename);
					worker_result = pup_install_result::extract_failed;
					progress = -1;
					return;
				}

				if (!progress.try_inc(::narrow<uint>(update_filenames.size())))
				{
					// Installation was cancelled (progress set to -1 from outside)
					return;
				}
			}
		});

		// Poll for completion or cancellation.
		while (true)
		{
			const uint value = progress.load();

			if (static_cast<int>(value) < 0)
			{
				break; // failed or cancelled
			}

			if (cb.on_progress)
			{
				cb.on_progress(value, update_filenames.size());
			}

			if (value >= update_filenames.size())
			{
				break;
			}

			if (cb.is_cancelled && cb.is_cancelled())
			{
				progress = -1;
				break;
			}

			std::this_thread::sleep_for(5ms);
		}

		// Join the worker thread (destructor of named_thread joins).
		worker();
	}

	update_files_f.close();

	const bool success = (progress == update_filenames.size());

	if (success && cb.on_progress)
	{
		cb.on_progress(update_filenames.size(), update_filenames.size());
	}

	// Remount /dev_flash etc. — needed in both GUI and headless paths so the
	// freshly installed firmware becomes visible to the running process.
	Emu.Init();

	if (!success)
	{
		if (!worker_detail.empty()) set_detail(worker_detail);

		pup_install_result r = worker_result.load();
		if (r == pup_install_result::ok)
		{
			// Worker didn't set a specific reason: either cancelled via is_cancelled, or exited early.
			r = pup_install_result::cancelled;
		}
		return r;
	}

	pup_install_log.success("Successfully installed PS3 firmware version %s.", version_string);
	return pup_install_result::ok;
}

const char* pup_install_result_to_string(pup_install_result result)
{
	switch (result)
	{
	case pup_install_result::ok: return "ok";
	case pup_install_result::cancelled: return "cancelled";
	case pup_install_result::path_empty: return "path_empty";
	case pup_install_result::open_failed: return "open_failed";
	case pup_install_result::header_read: return "header_read";
	case pup_install_result::header_magic: return "header_magic";
	case pup_install_result::expected_size: return "expected_size";
	case pup_install_result::file_entries: return "file_entries";
	case pup_install_result::hash_mismatch: return "hash_mismatch";
	case pup_install_result::no_update_db: return "no_update_db";
	case pup_install_result::statfs_failed: return "statfs_failed";
	case pup_install_result::disk_full: return "disk_full";
	case pup_install_result::mount_failed: return "mount_failed";
	case pup_install_result::extract_failed: return "extract_failed";
	case pup_install_result::no_dev_flash_entries: return "no_dev_flash_entries";
	case pup_install_result::no_version: return "no_version";
	case pup_install_result::decrypt_failed: return "decrypt_failed";
	case pup_install_result::firmware_exists_no_force: return "firmware_exists_no_force";
	}
	return "unknown";
}
