# The Ignition PS3 embed module: a Qt-free shared library over rpcs3_emu,
# exporting the C ABI in ignition_ps3.h. Built only when WITH_IGNITION is ON,
# and never linked into the RPCS3 executable -- it is loaded at runtime by the
# Ignition host, the same arm's-length arrangement the libretro cores use.
add_library(rpcs3_ignition SHARED
	ignition_ps3.cpp
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
)

if(WIN32)
	target_link_libraries(rpcs3_ignition PRIVATE ws2_32 Iphlpapi Winmm Psapi gdi32 setupapi)
else()
	target_link_libraries(rpcs3_ignition PRIVATE ${CMAKE_DL_LIBS})
endif()

if(APPLE)
	target_link_libraries(rpcs3_ignition PRIVATE iconv)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(rpcs3_ignition PRIVATE rt)
endif()

set_target_properties(rpcs3_ignition PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)
