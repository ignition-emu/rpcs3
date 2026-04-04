if(USE_SYSTEM_ZLIB)
    list(REMOVE_ITEM CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})
    find_package(ZLIB)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})
else()
    if(NOT TARGET zlibstatic)
        message(FATAL_ERROR "FindZLIB: zlibstatic missing; add 3rdparty/zlib before find_package(ZLIB)")
    endif()
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB STATIC IMPORTED)
        set(_zlib_src "${CMAKE_SOURCE_DIR}/3rdparty/zlib/zlib")
        set(_zlib_bin "${CMAKE_BINARY_DIR}/3rdparty/zlib/zlib")
        set_target_properties(ZLIB::ZLIB PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_zlib_src};${_zlib_bin}")
        # Visual Studio (multi-config): MSBuild does not expand $<TARGET_FILE:...> on the link line.
        # Use CMAKE_CONFIGURATION_TYPES — CMAKE_GENERATOR_IS_MULTI_CONFIG is not reliable in find modules.
        if(CMAKE_CONFIGURATION_TYPES)
            set_target_properties(ZLIB::ZLIB PROPERTIES
                IMPORTED_LOCATION_RELEASE "${_zlib_bin}/Release/zs.lib"
                IMPORTED_LOCATION_DEBUG "${_zlib_bin}/Debug/zsd.lib"
                IMPORTED_LOCATION_RELWITHDEBINFO "${_zlib_bin}/RelWithDebInfo/zs.lib"
                IMPORTED_LOCATION_MINSIZEREL "${_zlib_bin}/MinSizeRel/zs.lib")
        else()
            set_target_properties(ZLIB::ZLIB PROPERTIES
                IMPORTED_LOCATION "$<TARGET_FILE:zlibstatic>")
        endif()
    endif()
    set(ZLIB_FOUND TRUE)
endif()
