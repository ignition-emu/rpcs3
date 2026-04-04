if(USE_SYSTEM_WOLFSSL)
	message(STATUS "RPCS3: using shared wolfssl")
	pkg_check_modules(WolfSSL REQUIRED IMPORTED_TARGET wolfssl>=4.7.0)
	add_library(wolfssl INTERFACE)
	target_link_libraries(wolfssl INTERFACE PkgConfig::WolfSSL)
else()
	# TLS 1.3: required so libcurl uses wolfSSL_* APIs (not OpenSSL-compat SSL_CTX_*).
	option(WOLFSSL_TLS13 "Enable wolfSSL TLS v1.3 (default: enabled)" ON)
	# Pulls MD4, OPENSSL_EXTRA-adjacent APIs, etc., expected by bundled curl + wolfssl.
	set(WOLFSSL_CURL ON CACHE BOOL "Enable wolfSSL cURL integration" FORCE)
	set(WOLFSSL_SHA3 ON CACHE STRING "Enable wolfSSL SHA-3 support (default: enabled on x86_64/aarch64)")
	set(WOLFSSL_SHAKE256 ON CACHE STRING "Enable wolfSSL SHAKE256 support (default: enabled on x86_64/aarch64)")
	option(WOLFSSL_BASE64_ENCODE "Enable Base64 encoding (default: enabled on x86_64)" OFF)
	option(WOLFSSL_DES3 "Enable DES3 (default: disabled)" ON)
	option(WOLFSSL_PWDBASED "Enable PWDBASED (default: disabled)" ON)
	option(WOLFSSL_FAST_MATH "Enable fast math ops (default: disabled)" ON)
	option(WOLFSSL_EXAMPLES "Enable examples (default: enabled)" OFF)
	option(WOLFSSL_CRYPT_TESTS "Enable Crypt Bench/Test (default: enabled)" OFF)
	option(WOLFSSL_ASYNC_THREADS "Enable Asynchronous Threading (default: enabled)" OFF)
	option(WOLFSSL_BUILD_OUT_OF_TREE "Don't generate files in the source tree (default: yes)" ON)
	option(WOLFSSL_SNI "Enable SNI (default: disabled)" ON)
	option(WOLFSSL_OPENSSLEXTRA "Enable extra OpenSSL API, size+ (default: disabled)" ON)
	option(WOLFSSL_HARDEN "Enable Hardened build, Enables Timing Resistance and Blinding (default: enabled)" OFF)
	option(WOLFSSL_ALT_CERT_CHAINS "Enable support for Alternate certification chains (default: disabled)" ON)

	add_subdirectory(wolfssl EXCLUDE_FROM_ALL)

	# WOLFSSL_NO_OPTIONS_H skips generated options.h; consumers must match the built feature set.
	target_compile_definitions(wolfssl PUBLIC
		WOLFSSL_DES_ECB HAVE_WRITE_DUP FP_MAX_BITS=8192 WOLFSSL_NO_OPTIONS_H
		WOLFSSL_TLS13 OPENSSL_EXTRA)
endif()
