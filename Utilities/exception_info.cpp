// Describes the exception that reached std::terminate. RPCS3 builds with -fno-exceptions,
// but libc++ and LLVM still throw (std::bad_alloc, std::system_error, ...); this one file
// is compiled with exceptions so the terminate handler can name them.
#include <exception>
#include <string>
#include <typeinfo>

std::string describe_current_exception()
{
	const std::exception_ptr ex = std::current_exception();

	if (!ex)
	{
		return {};
	}

	try
	{
		std::rethrow_exception(ex);
	}
	catch (const std::exception& e)
	{
		return std::string("Uncaught exception (") + typeid(e).name() + "): " + e.what();
	}
	catch (...)
	{
		return "Uncaught exception of a non-standard type";
	}
}

#include <unwind.h>
#include <cstdint>
#include <cstdio>

// Return addresses of the calling thread's stack, from the unwind tables (the title is
// built without frame pointers). Symbolize them against the linked ELF on the host.
std::string describe_current_stack()
{
	struct walk_state
	{
		std::string out;
		int depth = 0;
	} state;

	_Unwind_Backtrace([](_Unwind_Context* ctx, void* arg) -> _Unwind_Reason_Code
	{
		auto& st = *static_cast<walk_state*>(arg);
		char line[32];
		std::snprintf(line, sizeof(line), " %#llx", static_cast<unsigned long long>(_Unwind_GetIP(ctx)));
		st.out += line;
		return ++st.depth < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
	}, &state);

	return "Stack:" + state.out;
}
