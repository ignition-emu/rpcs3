// A hidden X11 window for the embed's IGNITION_PS3_SURFACE=x11 fallback: the
// Linux counterpart of ignition_metal.mm's hidden Metal view. The window is
// never mapped; RPCS3's Vulkan swapchain presents to it and frames are read
// back through the RSX capture path, not the window. Kept in its own unit
// because Xlib's macros collide with Qt and the emulator headers.
#include <X11/Xlib.h>

extern "C" void* ignition_x11_create_window(int width, int height, unsigned long* window)
{
	Display* display = XOpenDisplay(nullptr);
	if (!display)
	{
		return nullptr;
	}
	*window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
		static_cast<unsigned int>(width), static_cast<unsigned int>(height), 0, 0, 0);
	XFlush(display);
	return display;
}

// A connection of its own for each renderer, which closes it on teardown.
extern "C" void* ignition_x11_open_display()
{
	return XOpenDisplay(nullptr);
}

extern "C" void ignition_x11_destroy_window(void* display, unsigned long window)
{
	Display* d = static_cast<Display*>(display);
	XDestroyWindow(d, window);
	XCloseDisplay(d);
}
