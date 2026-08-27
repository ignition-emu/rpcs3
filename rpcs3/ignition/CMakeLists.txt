# The Ignition PS3 embed module: a Qt-free shared library over rpcs3_emu,

# Qt is set up inside rpcs3/CMakeLists.txt, not at top level where this subdir
# is added, so find it here for AUTOMOC and the Qt6::* targets. Qt6_DIR is in
# the build environment already.
find_package(Qt6 COMPONENTS Core Gui REQUIRED)
# exporting the C ABI in ignition_ps3.h. Built only when WITH_IGNITION is ON,
# and never linked into the RPCS3 executable -- it is loaded at runtime by the
# Ignition host, the same arm's-length arrangement the libretro cores use.
add_library(rpcs3_ignition SHARED
	ignition_ps3.cpp

	# Version glue the emu references.
	${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3_version.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../stdafx.cpp

	# The pad/Input system the emu calls into. It lives in rpcs3_ui (the Qt GUI
	# lib) upstream; compiled here directly so the module links QtCore/QtGui but
	# not the GUI. gui_pad_thread and the camera sinks are left out -- pad_thread
	# does not reference them and they pull QtMultimedia/Widgets.
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/pad_thread.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/product_info.cpp
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
)

# The emu core and the non-Qt dependencies rpcs3_lib also pulls. No rpcs3_ui,
# no qt6: the frontend behaviour lives in this module's own callbacks.
target_link_libraries(rpcs3_ignition
	PRIVATE
		rpcs3_emu
		3rdparty::stblib
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
	target_link_libraries(rpcs3_ignition PRIVATE iconv "-framework CoreVideo" "-framework Foundation")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(rpcs3_ignition PRIVATE rt)
endif()

target_compile_features(rpcs3_ignition PRIVATE cxx_std_23)
target_compile_definitions(rpcs3_ignition PRIVATE IGNITION_EMBED)

set_target_properties(rpcs3_ignition PROPERTIES
	AUTOMOC ON
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)
