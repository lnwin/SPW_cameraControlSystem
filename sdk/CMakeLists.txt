cmake_minimum_required(VERSION 3.22)

project(spwcam_sdk
    VERSION     2.0.0
    DESCRIPTION "SPWater Camera SDK"
    LANGUAGES   CXX C
)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── output directories ────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── options ───────────────────────────────────────────────────────────────────
option(SPWCAM_BUILD_EXAMPLES "Build example programs"  ON)
option(SPWCAM_BUILD_TESTS    "Build unit / ABI tests"  ON)

# ── third-party paths (override on command line or via cmake-gui) ─────────────
set(QT_ROOT          "C:/Qt/6.4.3/msvc2019_64"                         CACHE PATH "Qt6 installation root")
set(GST_ROOT         "E:/ThirdParty/Gstreamer/1.0/msvc_x86_64"        CACHE PATH "GStreamer root")
set(FFMPEG_ROOT      "E:/ThirdParty/ffmpeg-8.0-full_build-shared"      CACHE PATH "FFmpeg root")

# ── find Qt6 ──────────────────────────────────────────────────────────────────
list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)
qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── GStreamer (manual, no pkg-config needed on Windows) ───────────────────────
# NOTE: deliberately NOT adding "${GST_ROOT}/include" itself -- that directory
# also contains FFmpeg 7.x headers (libavcodec/ etc.) shipped for gstlibav,
# which would shadow the FFmpeg 8.0 headers below and silently produce an
# AVCodecContext layout mismatch at runtime (encoder open fails).
set(GST_INCLUDE_DIRS
    "${GST_ROOT}/include/gstreamer-1.0"
    "${GST_ROOT}/include/glib-2.0"
    "${GST_ROOT}/lib/glib-2.0/include"
)
set(GST_LIBRARIES
    "${GST_ROOT}/lib/gstreamer-1.0.lib"
    "${GST_ROOT}/lib/gstapp-1.0.lib"
    "${GST_ROOT}/lib/gstbase-1.0.lib"
    "${GST_ROOT}/lib/gstvideo-1.0.lib"
    "${GST_ROOT}/lib/gstrtsp-1.0.lib"
    "${GST_ROOT}/lib/gstsdp-1.0.lib"
    "${GST_ROOT}/lib/glib-2.0.lib"
    "${GST_ROOT}/lib/gobject-2.0.lib"
)

# ── FFmpeg (manual) ───────────────────────────────────────────────────────────
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARIES
    "${FFMPEG_ROOT}/lib/avcodec.lib"
    "${FFMPEG_ROOT}/lib/avformat.lib"
    "${FFMPEG_ROOT}/lib/avutil.lib"
    "${FFMPEG_ROOT}/lib/swscale.lib"
    "${FFMPEG_ROOT}/lib/swresample.lib"
)

# ── parent project source root ────────────────────────────────────────────────
# The SDK references the upstream core files directly (no copy / no modification).
set(UPSTREAM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ── spwcam_sdk DLL ────────────────────────────────────────────────────────────
add_library(spwcam_sdk SHARED
    # adapter / glue
    src/spwcam_core.cpp
    src/spwcam_device.cpp
    src/spwcam_stream.cpp
    src/spwcam_record.cpp
    src/qt_worker.cpp

    # upstream core (unchanged originals)
    ${UPSTREAM_SRC}/rtspviewerqt.cpp
    ${UPSTREAM_SRC}/videorecorder.cpp
    ${UPSTREAM_SRC}/udpserver.cpp
)

target_compile_definitions(spwcam_sdk PRIVATE
    SPWCAM_BUILDING_DLL
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    QT_NO_DEBUG_OUTPUT   # suppress qDebug from upstream in Release
)

if(MSVC)
    # sources are UTF-8 (some contain Chinese comments); avoid C4819 on CJK code pages
    target_compile_options(spwcam_sdk PRIVATE /utf-8)
endif()

target_include_directories(spwcam_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(spwcam_sdk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${UPSTREAM_SRC}           # for rtspviewerqt.h, videorecorder.h, etc.
    ${FFMPEG_INCLUDE_DIRS}    # FFmpeg first: must win over any other libav* headers
    ${GST_INCLUDE_DIRS}
)

# Guard against the header/DLL mismatch described above: the compile-time
# libavcodec major version must match the DLL we ship (avcodec-62 = FFmpeg 8).
file(READ "${FFMPEG_ROOT}/include/libavcodec/version_major.h" _avc_ver_h)
string(REGEX MATCH "LIBAVCODEC_VERSION_MAJOR[ \t]+([0-9]+)" _m "${_avc_ver_h}")
set(SPWCAM_AVCODEC_MAJOR "${CMAKE_MATCH_1}")
if(NOT EXISTS "${FFMPEG_ROOT}/bin/avcodec-${SPWCAM_AVCODEC_MAJOR}.dll")
    message(FATAL_ERROR "FFmpeg headers (avcodec ${SPWCAM_AVCODEC_MAJOR}) do not match the DLLs in ${FFMPEG_ROOT}/bin")
endif()
target_compile_definitions(spwcam_sdk PRIVATE SPWCAM_EXPECT_AVCODEC_MAJOR=${SPWCAM_AVCODEC_MAJOR})

target_link_libraries(spwcam_sdk PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Gui
    ${GST_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    Iphlpapi
    Ws2_32
)

# Windows: export all symbols automatically (alternative to __declspec)
# — we already use explicit __declspec(dllexport) in the header, so this
#   is just belt-and-suspenders for any Qt MOC symbols.
set_target_properties(spwcam_sdk PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    OUTPUT_NAME "spwcam_sdk"
    PREFIX ""
)

# ── examples ──────────────────────────────────────────────────────────────────
if(SPWCAM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── tests ─────────────────────────────────────────────────────────────────────
if(SPWCAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS spwcam_sdk
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/spwcam
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
