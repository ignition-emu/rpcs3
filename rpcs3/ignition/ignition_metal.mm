// A window-less Metal surface for the embed. RPCS3's macOS Vulkan path renders
// through a CAMetalLayer obtained from an NSView (GetCAMetalLayerFromMetalView
// returns view.layer). We hand it a hidden, off-screen NSView+CAMetalLayer so
// the normal WSI swapchain works with no on-screen window and no core changes;
// frames are read back through the RSX capture path (present_frame), not the
// layer.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

extern "C" void* ignition_make_hidden_metal_view(int width, int height)
{
	const NSRect frame = NSMakeRect(0, 0, width, height);
	NSView* view = [[NSView alloc] initWithFrame:frame];
	CAMetalLayer* layer = [CAMetalLayer layer];
	layer.frame = frame;
	layer.drawableSize = CGSizeMake(width, height);
	layer.framebufferOnly = NO; // allow the presented drawable to be read/copied
	view.wantsLayer = YES;
	view.layer = layer;
	// +1 from alloc kept intentionally: the view lives for the session.
	return (void*)view;
}
#pragma GCC diagnostic pop
