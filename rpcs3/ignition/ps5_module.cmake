# The PS3 module for Ignition's PS5 title: rpcs3_ignition with no Qt, no HID and
# no window system, linked as the title's loader maps an extension. It stays at
# arm's length from the title: its imports are libc, libkernel and libc++ names
# and RADV's vk_icdGetInstanceProcAddr, which the title binds when it loads it.

# The overlays' English text, written from the Qt frontend's table.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(IGNITION_LOCALIZED_STRINGS "${CMAKE_CURRENT_BINARY_DIR}/ignition_localized_strings.inc")
add_custom_command(
	OUTPUT "${IGNITION_LOCALIZED_STRINGS}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/localized_strings.py"
		"${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3qt/localized_emu.h" "${IGNITION_LOCALIZED_STRINGS}"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/localized_strings.py" "${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3qt/localized_emu.h"
	VERBATIM)

add_library(rpcs3_ignition SHARED
	ignition_ps3.cpp
	ps5_module_runtime.cpp
	"${IGNITION_LOCALIZED_STRINGS}"
	${CMAKE_CURRENT_SOURCE_DIR}/../rpcs3_version.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../stdafx.cpp
	# The pad system the emu calls into. Pads come through the ABI, so the HID
	# and SDL handlers stay out.
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/pad_thread.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/product_info.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/mouse_gyro_handler.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_config.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../Input/ps_move_tracker.cpp
)

target_include_directories(rpcs3_ignition
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/..
		${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(rpcs3_ignition
	PRIVATE
		rpcs3_emu
		3rdparty::stblib
		3rdparty::yaml-cpp
		3rdparty::libusb
		3rdparty::wolfssl
		3rdparty::libcurl
		3rdparty::zlib
		3rdparty::fusion
)

# No C or C++ runtime of its own: the title binds those imports to its own.
# Only the ABI is exported, the module's references bind to its own
# definitions, and the static libraries' symbols stay local.
target_link_options(rpcs3_ignition PRIVATE
	-nostdlib
	-nodefaultlibs
	"-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/ps5_module.ld"
	"-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ignition_ps3.map"
	"-Wl,--exclude-libs,ALL"
	"-Wl,-Bsymbolic"
	"-Wl,-z,undefs"
	"-Wl,--build-id=sha1"
)
set_target_properties(rpcs3_ignition PROPERTIES
	PREFIX ""
	OUTPUT_NAME "rpcs3_ignition.ps5"
	LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ps5_module.ld;${CMAKE_CURRENT_SOURCE_DIR}/ignition_ps3.map"
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
)

target_compile_features(rpcs3_ignition PRIVATE cxx_std_23)
target_compile_definitions(rpcs3_ignition PRIVATE IGNITION_EMBED)
