# Bazarish project (c) 2026
# Loaded as CMAKE_PROJECT_INCLUDE: any earlier, the platform module puts shared suffixes back first.
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
set(ZLIB_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_LIBS ON)

# Their config packages bypass the suffix list, so pkg-config has to find them.
set(CMAKE_DISABLE_FIND_PACKAGE_harfbuzz ON)
set(CMAKE_DISABLE_FIND_PACKAGE_graphite2 ON)

# Only a static query lists private dependencies, such as expat under fontconfig.
set(PKG_CONFIG_ARGN --static)

# Last on every link line, after fontconfig (needs expat) and the xcb plugin (needs xcb-aux).
set(CMAKE_CXX_STANDARD_LIBRARIES "-L/out/prefix/lib -lxcb-util -lexpat")
