cmake_minimum_required(VERSION 3.20)
project(GeneralsMD CXX C)

# Win32 only: the code is full of 32-bit inline __asm, which MSVC x64 rejects outright.
if(MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure for Win32: cmake -A Win32")
endif()

set(WW ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/WWVegas)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression)

add_compile_definitions(WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(MSVC)
  # /permissive- is off on purpose: this is 2003-era code and templates here rely on
  # pre-two-phase-lookup name binding (simplevec.h et al).
  add_compile_options(/W3 /permissive /EHsc)
endif()

# ---- wwdebug --------------------------------------------------------------
add_library(wwdebug STATIC
  ${WW}/WWDebug/wwdebug.cpp
  ${WW}/WWDebug/wwmemlog.cpp
  ${WW}/WWDebug/wwprofile.cpp)
target_include_directories(wwdebug PUBLIC ${WW}/WWDebug PRIVATE ${WW}/WWLib ${WW}/Wwutil)

# ---- wwutil ---------------------------------------------------------------
add_library(wwutil STATIC
  ${WW}/Wwutil/mathutil.cpp
  ${WW}/Wwutil/miscutil.cpp)
target_include_directories(wwutil PUBLIC ${WW}/Wwutil PRIVATE ${WW}/WWLib ${WW}/WWMath)
target_link_libraries(wwutil PRIVATE wwdebug)

# ---- wwmath ---------------------------------------------------------------
add_library(wwmath STATIC
  ${WW}/WWMath/aabox.cpp          ${WW}/WWMath/aabtreecull.cpp
  ${WW}/WWMath/cardinalspline.cpp ${WW}/WWMath/catmullromspline.cpp
  ${WW}/WWMath/colmath.cpp        ${WW}/WWMath/colmathaabox.cpp
  ${WW}/WWMath/colmathaabtri.cpp  ${WW}/WWMath/colmathfrustum.cpp
  ${WW}/WWMath/colmathline.cpp    ${WW}/WWMath/colmathobbobb.cpp
  ${WW}/WWMath/colmathobbox.cpp   ${WW}/WWMath/colmathobbtri.cpp
  ${WW}/WWMath/colmathplane.cpp   ${WW}/WWMath/colmathsphere.cpp
  ${WW}/WWMath/cullsys.cpp        ${WW}/WWMath/curve.cpp
  ${WW}/WWMath/euler.cpp          ${WW}/WWMath/frustum.cpp
  ${WW}/WWMath/gridcull.cpp       ${WW}/WWMath/hermitespline.cpp
  ${WW}/WWMath/lineseg.cpp        ${WW}/WWMath/lookuptable.cpp
  ${WW}/WWMath/matrix3.cpp        ${WW}/WWMath/matrix3d.cpp
  ${WW}/WWMath/matrix4.cpp        ${WW}/WWMath/obbox.cpp
  ${WW}/WWMath/ode.cpp            ${WW}/WWMath/pot.cpp
  ${WW}/WWMath/quat.cpp           ${WW}/WWMath/tcbspline.cpp
  ${WW}/WWMath/tri.cpp            ${WW}/WWMath/v3_rnd.cpp
  ${WW}/WWMath/vehiclecurve.cpp   ${WW}/WWMath/vp.cpp
  ${WW}/WWMath/wwmath.cpp)
target_include_directories(wwmath PUBLIC ${WW}/WWMath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath PRIVATE wwdebug)

# ---- compression ----------------------------------------------------------
# zlib 1.1.4 (vendored, per README) minus maketree.c - that one is a table
# generator with its own main().  Lzhl_tcp.cpp is also skipped (socket layer,
# nothing in the game calls it).
add_library(compression STATIC
  ${COMP}/CompressionManager.cpp
  ${COMP}/EAC/btreeabout.cpp ${COMP}/EAC/btreedecode.cpp ${COMP}/EAC/btreeencode.cpp
  ${COMP}/EAC/huffabout.cpp  ${COMP}/EAC/huffdecode.cpp  ${COMP}/EAC/huffencode.cpp
  ${COMP}/EAC/refabout.cpp   ${COMP}/EAC/refdecode.cpp   ${COMP}/EAC/refencode.cpp
  ${COMP}/LZHCompress/NoxCompress.cpp
  ${COMP}/LZHCompress/CompLibSource/Huff.cpp
  ${COMP}/LZHCompress/CompLibSource/Lz.cpp
  ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
  ${COMP}/ZLib/adler32.c  ${COMP}/ZLib/compress.c ${COMP}/ZLib/crc32.c
  ${COMP}/ZLib/deflate.c  ${COMP}/ZLib/gzio.c     ${COMP}/ZLib/infblock.c
  ${COMP}/ZLib/infcodes.c ${COMP}/ZLib/inffast.c  ${COMP}/ZLib/inflate.c
  ${COMP}/ZLib/inftrees.c ${COMP}/ZLib/infutil.c  ${COMP}/ZLib/trees.c
  ${COMP}/ZLib/uncompr.c  ${COMP}/ZLib/zutil.c)
target_include_directories(compression
  PUBLIC ${COMP} ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  PRIVATE ${COMP}/LZHCompress/CompLibHeader)
# CompressionManager calls z_compress2/z_uncompress and BaseType.h owns 'Byte',
# so zlib must be built with its Z_PREFIX renames on.
target_compile_definitions(compression PRIVATE Z_PREFIX)

# Round-trip check over every codec CompressionManager exposes.
add_executable(compression_selfcheck ${COMP}/test_compression.cpp)
target_link_libraries(compression_selfcheck PRIVATE compression)

# Guards the hand-rolled Matrix3D::Get_Inverse that replaced D3DXMatrixInverse.
add_executable(wwmath_selfcheck ${WW}/WWMath/test_inverse.cpp)
target_include_directories(wwmath_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath_selfcheck PRIVATE wwmath wwsaveload wwlib wwdebug)

# ---- wwlib ----------------------------------------------------------------
add_library(wwlib STATIC
  ${WW}/WWLib/_convert.cpp   ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/argv.cpp
  ${WW}/WWLib/b64pipe.cpp    ${WW}/WWLib/b64straw.cpp   ${WW}/WWLib/base64.cpp
  ${WW}/WWLib/blit.cpp       ${WW}/WWLib/blowfish.cpp   ${WW}/WWLib/buff.cpp
  ${WW}/WWLib/bufffile.cpp   ${WW}/WWLib/chunkio.cpp    ${WW}/WWLib/convert.cpp
  ${WW}/WWLib/cpudetect.cpp  ${WW}/WWLib/crc.cpp        ${WW}/WWLib/cstraw.cpp
  ${WW}/WWLib/data.cpp       ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
  ${WW}/WWLib/Except.cpp     ${WW}/WWLib/FastAllocator.cpp
  ${WW}/WWLib/ffactory.cpp   ${WW}/WWLib/gcd_lcm.cpp    ${WW}/WWLib/hash.cpp
  ${WW}/WWLib/hsv.cpp        ${WW}/WWLib/ini.cpp        ${WW}/WWLib/int.cpp
  ${WW}/WWLib/jshell.cpp     ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp
  ${WW}/WWLib/lcw.cpp        ${WW}/WWLib/load.cpp       ${WW}/WWLib/mixfile.cpp
  ${WW}/WWLib/mono.cpp       ${WW}/WWLib/mpmath.cpp     ${WW}/WWLib/mpu.cpp
  ${WW}/WWLib/msgloop.cpp    ${WW}/WWLib/multilist.cpp  ${WW}/WWLib/mutex.cpp
  ${WW}/WWLib/nstrdup.cpp    ${WW}/WWLib/palette.cpp    ${WW}/WWLib/pcx.cpp
  ${WW}/WWLib/pipe.cpp       ${WW}/WWLib/pk.cpp         ${WW}/WWLib/ramfile.cpp
  ${WW}/WWLib/random.cpp     ${WW}/WWLib/rawfile.cpp    ${WW}/WWLib/rc4.cpp
  ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/readline.cpp   ${WW}/WWLib/realcrc.cpp
  ${WW}/WWLib/refcount.cpp   ${WW}/WWLib/registry.cpp   ${WW}/WWLib/rgb.cpp
  ${WW}/WWLib/rle.cpp        ${WW}/WWLib/rndstrng.cpp   ${WW}/WWLib/sampler.cpp
  ${WW}/WWLib/sha.cpp        ${WW}/WWLib/slnode.cpp     ${WW}/WWLib/srandom.cpp
  ${WW}/WWLib/stimer.cpp     ${WW}/WWLib/straw.cpp      ${WW}/WWLib/strtok_r.cpp
  ${WW}/WWLib/systimer.cpp   ${WW}/WWLib/tagblock.cpp   ${WW}/WWLib/TARGA.CPP
  ${WW}/WWLib/textfile.cpp   ${WW}/WWLib/tgatodxt.cpp   ${WW}/WWLib/thread.cpp
  ${WW}/WWLib/trim.cpp       ${WW}/WWLib/vector.cpp     ${WW}/WWLib/verchk.cpp
  ${WW}/WWLib/widestring.cpp ${WW}/WWLib/win.cpp        ${WW}/WWLib/WWCOMUtil.cpp
  ${WW}/WWLib/wwfile.cpp     ${WW}/WWLib/wwfont.cpp     ${WW}/WWLib/wwstring.cpp
  ${WW}/WWLib/xpipe.cpp      ${WW}/WWLib/xstraw.cpp     ${WW}/WWLib/xsurface.cpp)
target_include_directories(wwlib PUBLIC ${WW}/WWLib PRIVATE ${WW}/WWMath ${WW}/Wwutil)
target_link_libraries(wwlib PRIVATE wwdebug PUBLIC winmm)  # timeGetTime/timeBeginPeriod

# ---- wwsaveload -----------------------------------------------------------
add_library(wwsaveload STATIC
  ${WW}/WWSaveLoad/definition.cpp          ${WW}/WWSaveLoad/definitionfactory.cpp
  ${WW}/WWSaveLoad/definitionfactorymgr.cpp ${WW}/WWSaveLoad/definitionmgr.cpp
  ${WW}/WWSaveLoad/parameter.cpp           ${WW}/WWSaveLoad/persistfactory.cpp
  ${WW}/WWSaveLoad/pointerremap.cpp        ${WW}/WWSaveLoad/saveload.cpp
  ${WW}/WWSaveLoad/saveloadstatus.cpp      ${WW}/WWSaveLoad/saveloadsubsystem.cpp
  ${WW}/WWSaveLoad/twiddler.cpp            ${WW}/WWSaveLoad/wwsaveload.cpp)
target_include_directories(wwsaveload PUBLIC ${WW}/WWSaveLoad PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/Wwutil)
target_link_libraries(wwsaveload PRIVATE wwdebug wwlib wwmath)
