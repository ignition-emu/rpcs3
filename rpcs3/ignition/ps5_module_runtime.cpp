// The PS5 module's own exit handlers. The title's loader maps the module and
// unmaps it again, so a destructor or atexit handler of the module cannot go
// on the process's list, which runs after the module is gone. They go on this
// list, which a destructor of the module runs (the loader runs .fini_array
// before it unmaps the module).
#include <cstdlib>
#include <mutex>

namespace
{
	struct exit_handler
	{
		void (*call)(void*);
		void* argument;
		exit_handler* previous;
	};

	std::mutex& handlers_lock()
	{
		static std::mutex lock;
		return lock;
	}

	exit_handler* g_last_handler = nullptr;

	void call_plain(void* handler)
	{
		reinterpret_cast<void (*)()>(handler)();
	}

	// Runs the handlers, newest first, including any a handler adds.
	void run_handlers()
	{
		while (true)
		{
			exit_handler* handler;
			{
				std::lock_guard lock(handlers_lock());
				handler = g_last_handler;
				if (handler)
				{
					g_last_handler = handler->previous;
				}
			}

			if (!handler)
			{
				return;
			}

			handler->call(handler->argument);
			std::free(handler);
		}
	}
}

extern "C"
{
	void* __dso_handle = &__dso_handle;

	int __cxa_atexit(void (*call)(void*), void* argument, void*)
	{
		auto* handler = static_cast<exit_handler*>(std::malloc(sizeof(exit_handler)));
		if (!handler)
		{
			return -1;
		}

		std::lock_guard lock(handlers_lock());
		*handler = {call, argument, g_last_handler};
		g_last_handler = handler;
		return 0;
	}

	void __cxa_finalize(void*)
	{
		run_handlers();
	}

	int atexit(void (*handler)())
	{
		return __cxa_atexit(call_plain, reinterpret_cast<void*>(handler), nullptr);
	}
}

__attribute__((destructor)) static void run_module_exit_handlers()
{
	run_handlers();
}
