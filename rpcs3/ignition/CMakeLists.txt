# The Ignition PS3 embed module: a Qt-free shared library over rpcs3_emu,

# Added from rpcs3/CMakeLists.txt, so it inherits that directory's compile
# options; Qt6::Core and Gui are found here for AUTOMOC and the link.
find_package(Qt6 COMPONENTS Core Gui REQUIRED)
# exporting the C ABI in ignition_ps3.h. Built only when WITH_IGNITION is ON,
# and never linked into the RPCS3 executable -- it is loaded at runtime by the
# Ignition host, the same arm's-length arrangement the libretro cores use.
add_library(rpcs3_ignition SHARED
	ignition_ps3.cpp

	# Version glue the emu references.
	${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3_version.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../stdafx.cpp

	# Localized overlay text (OSK, message/save/trophy dialogs). QtCore-only, the
	# same source gui_application feeds get_localized_string from.
	${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3qt/localized_emu.cpp

	# The pad/Input system the emu calls into. It lives in rpcs3_ui (the Qt GUI
	# lib) upstream; compiled here directly so the module links QtCore/QtGui but
	# not the GUI. gui_pad_thread and the camera sinks are left out -- pad_thread
	# does not reference them and they pull QtMultimedia/Widgets.
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/pad_thread.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/product_info.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/mouse_gyro_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ds3_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ds4_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/dualsense_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/skateboard_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/hid_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/sdl_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/sdl_instance.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_config.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_calibration.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_tracker.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/evdev_joystick_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/xinput_pad_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/mm_joystick_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/raw_mouse_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/raw_mouse_config.cpp
)

# rpcs3_emu propagates its own PUBLIC includes; this is for ignition_ps3.h and
# the Emu/... paths the sources reach for.
target_include_directories(rpcs3_ignition
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/..
		${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3qt
)

# The emu core and the non-Qt dependencies rpcs3_lib also pulls. No rpcs3_ui,
# no qt6: the frontend behaviour lives in this module's own callbacks.
target_link_libraries(rpcs3_ignition
	PRIVATE
		rpcs3_emu
		3rdparty::stblib
		3rdparty::yaml-cpp
		3rdparty::libevdev
		3rdparty::hidapi
		3rdparty::libusb
		3rdparty::wolfssl
		3rdparty::libcurl
		3rdparty::zlib
		3rdparty::opencv
		3rdparty::fusion
		Qt6::Core
		Qt6::Gui
)

if(WIN32)
	target_link_libraries(rpcs3_ignition PRIVATE ws2_32 Iphlpapi Winmm Psapi gdi32 setupapi)
else()
	target_link_libraries(rpcs3_ignition PRIVATE ${CMAKE_DL_LIBS})
endif()

if(APPLE)
	target_sources(rpcs3_ignition PRIVATE ignition_metal.mm)
	target_link_libraries(rpcs3_ignition PRIVATE iconv "-framework CoreVideo" "-framework Foundation" "-framework AppKit" "-framework QuartzCore" "-framework Metal")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(rpcs3_ignition PRIVATE rt)

	# HAVE_X11/HAVE_WAYLAND come from rpcs3/CMakeLists.txt. X11 is what
	# rpcs3_emu's X11 swapchain links, and the hidden-window fallback.
	if(X11_FOUND)
		target_sources(rpcs3_ignition PRIVATE ignition_x11.cpp)
		target_link_libraries(rpcs3_ignition PRIVATE X11::X11)
	endif()
	if(WAYLAND_FOUND)
		target_include_directories(rpcs3_ignition SYSTEM PRIVATE ${WAYLAND_INCLUDE_DIR})
	endif()

	# Export the ABI and nothing else, bind the module's own references to its
	# own definitions, and refuse to link with anything left unresolved: a
	# shared object otherwise defers that to the host's dlopen.
	target_link_options(rpcs3_ignition PRIVATE
		"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ignition_ps3.map"
		"-Wl,--exclude-libs,ALL"
		"-Wl,-Bsymbolic"
		"-Wl,--no-undefined"
		# The __start_/__stop_ symbols the linker makes for named sections
		# (protobuf's pb_defaults) are exported by default.
		"-Wl,-z,start-stop-visibility=hidden"
		# The C++ runtime linked in and kept local (the version script hides it
		# with everything else), so the module asks nothing of the host's
		# libstdc++: the build image's is newer than Ubuntu 22.04's, and a host
		# process has usually loaded its own already (Mesa, Godot). Only C
		# crosses the ABI.
		"-static-libstdc++"
		"-static-libgcc")
	set_target_properties(rpcs3_ignition PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ignition_ps3.map")
endif()

target_compile_features(rpcs3_ignition PRIVATE cxx_std_23)
target_compile_definitions(rpcs3_ignition PRIVATE IGNITION_EMBED)

set_target_properties(rpcs3_ignition PROPERTIES
	AUTOMOC ON
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)
