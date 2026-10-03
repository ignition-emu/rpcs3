#include "stdafx.h"
#include "shader_loading_dialog.h"
#include "Emu/emu_callbacks.h"
#include "Emu/System.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/system_progress.hpp"

#include "util/asm.hpp"

namespace rsx
{
	void shader_loading_dialog::create(const std::string& msg, const std::string& title)
	{
		dlg = g_emu_callbacks.get_msg_dialog();
		if (dlg)
		{
			dlg->type.se_normal = true;
			dlg->type.bg_invisible = true;
			dlg->type.progress_bar_count = 2;
			dlg->ProgressBarSetTaskbarIndex(-1); // -1 to combine all progressbars in the taskbar progress
			dlg->on_close = [](s32 /*status*/)
			{
				Emu.CallFromMainThread([]()
				{
					rsx_log.notice("Aborted shader loading dialog");
					Emu.Kill(false);
				});
			};

			ref_cnt++;

			Emu.CallFromMainThread([&]()
			{
				dlg->Create(msg, title);
				ref_cnt--;
			});
		}

		while (ref_cnt.load() && !Emu.IsStopped())
		{
			utils::pause();
		}
	}

	void shader_loading_dialog::update_msg(u32 index, std::string msg)
	{
		if (!dlg)
		{
			return;
		}

		ref_cnt++;

		Emu.CallFromMainThread([&, index, message = std::move(msg)]()
		{
			dlg->ProgressBarSetMsg(index, message);
			ref_cnt--;
		});
	}

	void shader_loading_dialog::inc_value(u32 index, u32 value)
	{
		if (!dlg)
		{
			return;
		}

		ref_cnt++;

		Emu.CallFromMainThread([&, index, value]()
		{
			dlg->ProgressBarInc(index, value);
			ref_cnt--;
		});
	}

	void shader_loading_dialog::set_value(u32 index, u32 value)
	{
		if (!dlg)
		{
			return;
		}

		ref_cnt++;

		Emu.CallFromMainThread([&, index, value]()
		{
			dlg->ProgressBarSetValue(index, value);
			ref_cnt--;
		});
	}

	void shader_loading_dialog::set_limit(u32 index, u32 limit)
	{
		if (!dlg)
		{
			return;
		}

		ref_cnt++;

		Emu.CallFromMainThread([&, index, limit]()
		{
			dlg->ProgressBarSetLimit(index, limit);
			ref_cnt--;
		});
	}

	void shader_loading_dialog::refresh()
	{
	}

	void shader_loading_dialog::close()
	{
		while (ref_cnt.load() && !Emu.IsStopped())
		{
			utils::pause();
		}
	}

	void shader_loading_dialog_host::create(const std::string& msg, const std::string& /*title*/)
	{
		// The first line is the phase; the rest is the dialog's "please wait".
		m_text = msg.substr(0, msg.find('\n'));
		if (m_text.ends_with('.') && !m_text.ends_with("..."))
		{
			m_text.pop_back();
		}
		publish();
	}

	void shader_loading_dialog_host::update_msg(u32 index, std::string msg)
	{
		if (index < 2)
		{
			m_msg[index] = std::move(msg);
			m_bar = index;
			publish();
		}
	}

	void shader_loading_dialog_host::inc_value(u32 index, u32 value)
	{
		if (index < 2)
		{
			m_value[index] += value;
			m_bar = index;
			publish();
		}
	}

	void shader_loading_dialog_host::set_value(u32 index, u32 value)
	{
		if (index < 2)
		{
			m_value[index] = value;
			m_bar = index;
			publish();
		}
	}

	void shader_loading_dialog_host::set_limit(u32 index, u32 limit)
	{
		if (index < 2)
		{
			m_limit[index] = limit;
			publish();
		}
	}

	void shader_loading_dialog_host::refresh()
	{
	}

	void shader_loading_dialog_host::close()
	{
		withdraw_system_progress(system_progress_source_shaders);
	}

	void shader_loading_dialog_host::publish()
	{
		const u32 limit = m_limit[m_bar];
		const u32 percent = limit ? static_cast<u32>(std::min<u64>(100, u64{m_value[m_bar]} * 100 / limit)) : 0;
		publish_system_progress({ m_text, m_msg[m_bar], percent, true, m_stoppable, system_progress_source_shaders });
	}
}
