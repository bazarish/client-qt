# Bazarish project (c) 2026
# Included at the end of every project() call, which is the only place this
# holds: a top-level include runs before the platform module, and that module
# puts shared objects back at the head of the list.
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
set(ZLIB_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_LIBS ON)

# A config package names the library it was built against, and the suffix list
# above does not reach it. The two that ship one are kept out of the way, so
# they are found through pkg-config and the list applies.
set(CMAKE_DISABLE_FIND_PACKAGE_harfbuzz ON)
set(CMAKE_DISABLE_FIND_PACKAGE_graphite2 ON)
