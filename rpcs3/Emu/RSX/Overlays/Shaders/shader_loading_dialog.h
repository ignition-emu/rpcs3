#pragma once

class MsgDialogBase;

namespace rsx
{
	struct shader_loading_dialog
	{
		std::shared_ptr<MsgDialogBase> dlg{};
		atomic_t<int> ref_cnt{0};

		virtual ~shader_loading_dialog() = default;
		virtual void create(const std::string& msg, const std::string& title);
		virtual void update_msg(u32 index, std::string msg);
		virtual void inc_value(u32 index, u32 value);
		virtual void set_value(u32 index, u32 value);
		virtual void set_limit(u32 index, u32 limit);
		virtual void refresh();
		virtual void close();
	};

	// For a host that draws progress itself (g_progress_drawn_by_host): no
	// dialog, the bars published as the system progress snapshot instead. The
	// bar shown is the one last moved; `stoppable` is false for a phase a stop
	// cannot interrupt.
	struct shader_loading_dialog_host : shader_loading_dialog
	{
		explicit shader_loading_dialog_host(bool stoppable) : m_stoppable(stoppable) {}

		void create(const std::string& msg, const std::string& title) override;
		void update_msg(u32 index, std::string msg) override;
		void inc_value(u32 index, u32 value) override;
		void set_value(u32 index, u32 value) override;
		void set_limit(u32 index, u32 limit) override;
		void refresh() override;
		void close() override;

	private:
		void publish();

		bool m_stoppable;
		std::string m_text;
		std::string m_msg[2];
		u32 m_value[2]{};
		u32 m_limit[2]{};
		u32 m_bar = 0;
	};
}
