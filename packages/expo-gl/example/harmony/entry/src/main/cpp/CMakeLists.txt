project(rnapp)
cmake_minimum_required(VERSION 3.4.1)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(NATIVERENDER_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(OH_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../oh_modules")
set(OH_MODULES_DIR "${OH_MODULE_DIR}")
set(RNOH_CPP_DIR "${OH_MODULE_DIR}/@rnoh/react-native-openharmony/src/main/include")
set(REACT_COMMON_PATCH_DIR "${RNOH_CPP_DIR}/patches/react_native_core")
set(RNOH_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(RNOH_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

set(CMAKE_CXX_STANDARD 17)
set(LOG_VERBOSITY_LEVEL 1)
set(CMAKE_ASM_FLAGS "-Wno-error=unused-command-line-argument -Qunused-arguments")
set(CMAKE_CXX_FLAGS "-fstack-protector-strong -Wl,-z,relro,-z,now,-z,noexecstack -s -fPIE -pie -DNDEBUG")
set(WITH_HITRACE_SYSTRACE 1)
add_compile_definitions(WITH_HITRACE_SYSTRACE)

set(folly_compile_options
    -DFOLLY_NO_CONFIG=1
    -DFOLLY_MOBILE=1
    -DFOLLY_USE_LIBCPP=1
    -DFOLLY_HAVE_RECVMMSG=1
    -DFOLLY_HAVE_PTHREAD=1
    -Wno-comma
    -Wno-shorten-64-to-32
    -Wno-documentation
    -faligned-new
)
add_compile_options("-Wno-unused-command-line-argument")

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${RNOH_CPP_DIR}
                    ${REACT_COMMON_PATCH_DIR}
                    ${RNOH_CPP_DIR}/third-party/folly
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/react/nativemodule/core
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/jsi
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/callinvoker
                    ${RNOH_CPP_DIR}/third-party/boost/libs/utility/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/stacktrace/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/predef/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/array/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/throw_exception/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/config/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/core/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/preprocessor/include
                    ${RNOH_CPP_DIR}/third-party/double-conversion
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/react/renderer/graphics/platform/cxx
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/runtimeexecutor
                    ${RNOH_CPP_DIR}/third-party/glog/src
                    ${RNOH_CPP_DIR}/third-party/boost/libs/mpl/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/type_traits/include
                    ${RNOH_CPP_DIR}/third-party/rn/ReactCommon/yoga
                    ${RNOH_CPP_DIR}/third-party/boost/libs/intrusive/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/assert/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/move/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/static_assert/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/container_hash/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/describe/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/mp11/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/iterator/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/detail/include
                    ${RNOH_CPP_DIR}/third-party/boost/libs/regex/include
                    ${RNOH_CPP_DIR}/patches/react_native_core/react/renderer/textlayoutmanager/platform/harmony
                    )

configure_file(
  ${RNOH_CPP_DIR}/third-party/folly/CMake/folly-config.h.cmake
  ${RNOH_CPP_DIR}/third-party/folly/folly/folly-config.h
)

file(GLOB GENERATED_CPP_FILES "./generated/*.cpp")

add_library(rnoh SHARED
    "${RNOH_CPP_DIR}/RNOHOther.cpp"
    "${RNOH_CPP_DIR}/third-party/folly/folly/lang/SafeAssert.cpp"
)

target_compile_options(rnoh PUBLIC ${folly_compile_options})
target_link_directories(rnoh PUBLIC ${OH_MODULE_DIR}/@rnoh/react-native-openharmony/libs/arm64-v8a)
target_link_libraries(rnoh PUBLIC
    rnoh_semi
    libace_napi.z.so
    libace_ndk.z.so
    librawfile.z.so
    libhilog_ndk.z.so
    libnative_vsync.so
    libnative_drawing.so
    libc++_shared.so
    libhitrace_ndk.z.so
    libdeviceinfo_ndk.z.so
    react_render_scheduler
    rrc_image
    rrc_text
    rrc_textinput
    rrc_scrollview
    react_nativemodule_core
    react_render_animations
    jsinspector
    hermes
    jsi
    logger
    react_config
    react_debug
    react_render_attributedstring
    react_render_componentregistry
    react_render_core
    react_render_debug
    react_render_graphics
    react_render_imagemanager
    react_render_mapbuffer
    react_render_mounting
    react_render_templateprocessor
    react_render_textlayoutmanager
    react_render_telemetry
    react_render_uimanager
    react_utils
    rrc_root
    rrc_view
    react_render_leakchecker
    react_render_runtimescheduler
    runtimeexecutor
)

set(RNOH_C_API_ARCH "${RNOH_C_API_ARCH}" CACHE STRING "Enable C-API architecture")
if(RNOH_C_API_ARCH STREQUAL "1" OR "$ENV{RNOH_C_API_ARCH}" STREQUAL "1")
    message("Experimental C-API architecture enabled")
    target_link_libraries(rnoh PUBLIC libqos.so)
    target_compile_definitions(rnoh PUBLIC C_API_ARCH)
endif()

# RNOH_BEGIN: add_plugin_subdirectories
# 手动注册示例（不支持autolink的插件）:
# add_subdirectory("${OH_MODULE_DIR}/@oh-rn/YOUR_PACKAGE_NAME/src/main/cpp" ./YOUR_TARGET_NAME)
# target_compile_options(YOUR_TARGET_NAME PUBLIC ${folly_compile_options)
# autolink支持:
include("${CMAKE_CURRENT_SOURCE_DIR}/autolinking.cmake")
# RNOH_END: add_plugin_subdirectories

add_library(rnoh_app SHARED
    ${GENERATED_CPP_FILES}
    "./PackageProvider.cpp"
    "${RNOH_CPP_DIR}/RNOHOther.cpp"
    "${RNOH_CPP_DIR}/RNOHAppNapiBridge.cpp"
)

target_link_libraries(rnoh_app PUBLIC rnoh)

# RNOH_BEGIN: link_plugins
# 手动注册示例（不支持autolink的插件）:
# target_link_libraries(rnoh_app PUBLIC YOUR_TARGET_NAME)
# autolink支持:
autolink_libraries(rnoh_app)
# RNOH_END: link_plugins

target_compile_options(rnoh_app PUBLIC ${folly_compile_options} -DRAW_PROPS_ENABLED -std=c++17)