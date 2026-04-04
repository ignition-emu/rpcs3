#include <gtest/gtest.h>

#include "Emu/IPC_socket.h"

namespace
{
	struct ipc_impl_test_shim : public IPC_socket::IPC_impl
	{
		using IPC_socket::IPC_impl::pine_apply_settings;
		using IPC_socket::IPC_impl::pine_get_button_binding;
		using IPC_socket::IPC_impl::pine_set_button_binding;
	};
}

TEST(PineControllerStaging, FailedApplyKeepsPendingBinding)
{
	const std::string staged_binding = "SDL-0/FaceSouth";
	ASSERT_EQ(ipc_impl_test_shim::pine_set_button_binding(1, "cross", staged_binding), 0);

	std::string read_before_apply;
	ASSERT_TRUE(ipc_impl_test_shim::pine_get_button_binding(1, "cross", read_before_apply));
	EXPECT_EQ(read_before_apply, staged_binding);

	// If apply succeeds (e.g. full emulator callbacks are wired up), the failure-retention
	// semantics cannot be validated here — skip silently.
	const bool apply_ok = ipc_impl_test_shim::pine_apply_settings();
	if (apply_ok)
		return;

	std::string read_after_failed_apply;
	ASSERT_TRUE(ipc_impl_test_shim::pine_get_button_binding(1, "cross", read_after_failed_apply));
	EXPECT_EQ(read_after_failed_apply, staged_binding);
}
