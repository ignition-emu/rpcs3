#pragma once

#include "util/types.hpp"
#include <functional>
#include <vector>

#include "display.h"

class GSFrameBase
{
public:
	GSFrameBase() = default;
	GSFrameBase(const GSFrameBase&) = delete;
	virtual ~GSFrameBase() = default;

	virtual void close() = 0;
	virtual void reset() = 0;
	virtual bool shown() = 0;
	virtual void hide() = 0;
	virtual void show() = 0;
	virtual void toggle_fullscreen() = 0;

	virtual void delete_context(draw_context_t ctx) = 0;
	virtual draw_context_t make_context() = 0;
	virtual void set_current(draw_context_t ctx) = 0;
	virtual void flip(draw_context_t ctx, bool skip_frame = false) = 0;
	virtual int client_width() = 0;
	virtual int client_height() = 0;
	virtual f64 client_display_rate() = 0;
	virtual bool has_alpha() = 0;

	virtual display_handle_t handle() const = 0;

	virtual bool can_consume_frame() const = 0;
	virtual void present_frame(std::vector<u8>&& data, u32 pitch, u32 width, u32 height, bool is_bgra) const = 0;
	virtual void take_screenshot(std::vector<u8>&& sshot_data, u32 sshot_width, u32 sshot_height, bool is_bgra) = 0;

	// An embedding host's buffers, which the renderer can write a captured
	// frame into on its GPU. acquire_frame_slot gives one that holds `bytes`,
	// or null. A frame written into it is passed with present_frame_slot and a
	// check that is true once the GPU has finished writing; a slot left unused
	// is given back with release_frame_slot. settle_frame_slots says every
	// check is true, before the renderer that answers them is destroyed.
	virtual u8* acquire_frame_slot(usz /*bytes*/) const { return nullptr; }
	virtual usz frame_slot_bytes() const { return 0; }
	virtual void present_frame_slot(u8* /*slot*/, std::function<bool()> /*written*/, u32 /*pitch*/, u32 /*width*/, u32 /*height*/, bool /*is_bgra*/) const {}
	virtual void release_frame_slot(u8* /*slot*/) const {}
	virtual void settle_frame_slots() const {}

	virtual void update_title(double fps = 0.0) = 0;
};
