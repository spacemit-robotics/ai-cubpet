cmake_minimum_required(VERSION 3.16)
project(ai_cubpet LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AI_CUBPET_BUILD_PERIPHERALS_DEMO "Build peripheral CLI demo" OFF)
option(AI_CUBPET_BUILD_VOICE_DEMO "Build local VAD/ASR/DOA voice-control demo" ON)
option(AI_CUBPET_BUILD_UI_DEMO "Build DDS Qt UI process" ON)
option(AI_CUBPET_BUILD_TEST_TOOLS "Build and install development smoke-test tools" OFF)
option(AI_CUBPET_USE_AEC "Enable WebRTC AEC dependency for the voice demo" ON)
option(AI_CUBPET_USE_DDS "Enable DDS UI publisher for the voice demo" ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv|RISCV")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mstrict-align -fno-aggressive-loop-optimizations")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mstrict-align -fno-aggressive-loop-optimizations")
    add_compile_definitions(FFTW_NO_SIMD)
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(AI_CUBPET_PREFIX_HINTS
    ${CMAKE_INSTALL_PREFIX}
    ${CMAKE_PREFIX_PATH}
)
if(DEFINED ENV{PREFIX} AND NOT "$ENV{PREFIX}" STREQUAL "")
    list(APPEND AI_CUBPET_PREFIX_HINTS "$ENV{PREFIX}")
endif()
list(APPEND AI_CUBPET_PREFIX_HINTS "${REPO_ROOT}/output/staging")

find_package(motor CONFIG QUIET)
find_package(Components QUIET)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp)
endif()

function(ai_cubpet_link_nlohmann_json target)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
    elseif(NLOHMANN_JSON_INCLUDE_DIR)
        target_include_directories(${target} PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "Required dependency not found: nlohmann/json.hpp")
    endif()
endfunction()

if(AI_CUBPET_BUILD_PERIPHERALS_DEMO OR AI_CUBPET_BUILD_VOICE_DEMO)
    pkg_check_modules(LIBGPIOD QUIET libgpiod)
    if(NOT LIBGPIOD_FOUND)
        find_library(LIBGPIOD_LIBRARY
            NAMES gpiod
            HINTS ${AI_CUBPET_PREFIX_HINTS}
            PATH_SUFFIXES lib
        )
        if(NOT LIBGPIOD_LIBRARY)
            message(FATAL_ERROR "Required dependency not found: libgpiod")
        endif()
    endif()
endif()

function(ai_cubpet_resolve_component out_var name)
    if(TARGET components::${name})
        set(${out_var} components::${name} PARENT_SCOPE)
        return()
    endif()
    if(TARGET ai_cubpet_${name})
        set(${out_var} ai_cubpet_${name} PARENT_SCOPE)
        return()
    endif()

    find_library(${out_var}
        NAMES ${name}
        HINTS ${AI_CUBPET_PREFIX_HINTS}
        PATH_SUFFIXES lib
        NO_DEFAULT_PATH
    )
    if(NOT ${out_var})
        find_library(${out_var} NAMES ${name})
    endif()
    if(NOT ${out_var})
        message(FATAL_ERROR "Required component library not found: ${name}")
    endif()

    get_filename_component(_lib_dir "${${out_var}}" DIRECTORY)
    get_filename_component(_prefix "${_lib_dir}/.." ABSOLUTE)

    add_library(ai_cubpet_${name} UNKNOWN IMPORTED)
    set_target_properties(ai_cubpet_${name} PROPERTIES
        IMPORTED_LOCATION "${${out_var}}"
        INTERFACE_INCLUDE_DIRECTORIES "${_prefix}/include"
    )
    set(${out_var} ai_cubpet_${name} PARENT_SCOPE)
endfunction()

function(ai_cubpet_find_library out_var)
    cmake_parse_arguments(ARG "REQUIRED" "" "NAMES" ${ARGN})
    find_library(${out_var}
        NAMES ${ARG_NAMES}
        HINTS ${AI_CUBPET_PREFIX_HINTS}
        PATH_SUFFIXES lib
        NO_DEFAULT_PATH
    )
    if(NOT ${out_var})
        find_library(${out_var} NAMES ${ARG_NAMES})
    endif()
    if(ARG_REQUIRED AND NOT ${out_var})
        message(FATAL_ERROR "Required library not found: ${ARG_NAMES}")
    endif()
    if(NOT ${out_var})
        set(${out_var} "" PARENT_SCOPE)
    else()
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endif()
endfunction()

function(ai_cubpet_find_include out_var header)
    find_path(${out_var}
        NAMES ${header}
        HINTS ${AI_CUBPET_PREFIX_HINTS}
        PATH_SUFFIXES include
        NO_DEFAULT_PATH
    )
    if(NOT ${out_var})
        find_path(${out_var} NAMES ${header})
    endif()
    if(NOT ${out_var})
        message(FATAL_ERROR "Required include not found: ${header}")
    endif()
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

if(AI_CUBPET_USE_DDS)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cyclonedds.cmake)
    find_package(CycloneDDS-CXX REQUIRED CONFIG
        PATHS "${AI_CUBPET_CYCLONEDDS_INSTALL_PREFIX}"
        NO_DEFAULT_PATH)
    if(NOT TARGET ToyCommand_cubpet)
        idlcxx_generate(TARGET ToyCommand_cubpet
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/idl/ToyCommand.idl
            WARNINGS no-implicit-extensibility)
    endif()
endif()

if(AI_CUBPET_BUILD_PERIPHERALS_DEMO)
    ai_cubpet_resolve_component(MISC_IO_TARGET misc_io)
    ai_cubpet_resolve_component(PM_TARGET pm)
    ai_cubpet_resolve_component(WIFI_TARGET wifi)
    ai_cubpet_resolve_component(NFC_TARGET nfc)
    ai_cubpet_resolve_component(MOTOR_TARGET motor)
    ai_cubpet_resolve_component(LIGHT_SENSOR_TARGET light_sensor)

    ai_cubpet_find_library(STK_ALPS_LIB NAMES stk_alps_lib REQUIRED)

    add_executable(ai-cubpet-peripherals
        examples/peripherals.c
        src/cubpet_peripheral_config.cpp
    )

    target_include_directories(ai-cubpet-peripherals PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(ai-cubpet-peripherals PRIVATE
        ${MISC_IO_TARGET}
        ${PM_TARGET}
        ${WIFI_TARGET}
        ${NFC_TARGET}
        -Wl,--whole-archive
        ${MOTOR_TARGET}
        -Wl,--no-whole-archive
        ${LIGHT_SENSOR_TARGET}
        ${STK_ALPS_LIB}
        Threads::Threads
        dl
        rt
        m
        stdc++
    )
    ai_cubpet_link_nlohmann_json(ai-cubpet-peripherals)

    if(LIBGPIOD_FOUND)
        target_include_directories(ai-cubpet-peripherals PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
        target_link_libraries(ai-cubpet-peripherals PRIVATE ${LIBGPIOD_LIBRARIES})
        target_compile_options(ai-cubpet-peripherals PRIVATE ${LIBGPIOD_CFLAGS_OTHER})
    else()
        target_link_libraries(ai-cubpet-peripherals PRIVATE ${LIBGPIOD_LIBRARY})
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ai-cubpet-peripherals PRIVATE -Wall -Wextra)
        target_link_options(ai-cubpet-peripherals PRIVATE -no-pie)
    endif()

    set_target_properties(ai-cubpet-peripherals PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    install(TARGETS ai-cubpet-peripherals RUNTIME DESTINATION bin)
endif()

if(AI_CUBPET_BUILD_VOICE_DEMO)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    pkg_check_modules(SNDFILE REQUIRED sndfile)
    pkg_check_modules(FFTW3F REQUIRED fftw3f)
    pkg_check_modules(SAMPLERATE QUIET samplerate)
    ai_cubpet_find_library(SPACEMIT_AUDIO_LIB NAMES spacemit_audio REQUIRED)
    ai_cubpet_find_library(AUDIO_RESAMPLER_LIB NAMES audio_resampler REQUIRED)
    ai_cubpet_find_include(AUDIO_INCLUDE_DIR audio_base.hpp)

    ai_cubpet_find_library(ASR_LIB NAMES asr stt REQUIRED)
    ai_cubpet_find_library(SENSEVOICE_LIB NAMES sensevoice REQUIRED)
    ai_cubpet_find_library(ZIPFORMER_LIB NAMES zipformer REQUIRED)
    ai_cubpet_find_library(KALDI_FBANK_LIB NAMES kaldi-native-fbank-core REQUIRED)
    ai_cubpet_find_library(KISSFFT_FLOAT_LIB NAMES kissfft-float kissfft_float kissfft REQUIRED)
    ai_cubpet_find_library(SPACEMIT_EP_LIB NAMES spacemit_ep)
    ai_cubpet_find_include(ASR_INCLUDE_DIR asr_service.h)

    ai_cubpet_find_library(VAD_LIB NAMES vad REQUIRED)
    ai_cubpet_find_library(SILERO_VAD_LIB NAMES silero_vad REQUIRED)
    ai_cubpet_find_include(VAD_INCLUDE_DIR vad_service.h)

    ai_cubpet_find_library(DOA_LIB NAMES sound_locator REQUIRED)
    ai_cubpet_find_include(DOA_INCLUDE_DIR doa_service.h)

    ai_cubpet_find_library(ONNXRUNTIME_LIB NAMES onnxruntime REQUIRED)
    ai_cubpet_find_include(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h)
    ai_cubpet_resolve_component(MOTOR_TARGET motor)

    if(AI_CUBPET_USE_AEC)
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/webrtc_audio_processing.cmake)
    endif()

    set(AI_CUBPET_VOICE_SOURCES
        src/cubpet_audio_pipeline.cpp
        src/cubpet_doa_runtime.cpp
        src/cubpet_keywords.cpp
        src/cubpet_motor_actions.cpp
        src/cubpet_peripheral_config.cpp
        src/cubpet_ui_publisher.cpp
        src/cubpet_voice_demo.cpp
    )
    if(NOT AI_CUBPET_USE_DDS)
        list(REMOVE_ITEM AI_CUBPET_VOICE_SOURCES src/cubpet_ui_publisher.cpp)
    endif()
    if(AI_CUBPET_USE_AEC)
        list(APPEND AI_CUBPET_VOICE_SOURCES
            src/cubpet_aec_pipeline.cpp
        )
    endif()

    add_executable(ai-cubpet
        examples/main.cpp
        ${AI_CUBPET_VOICE_SOURCES}
    )

    target_include_directories(ai-cubpet PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${AUDIO_INCLUDE_DIR}
        ${ASR_INCLUDE_DIR}
        ${VAD_INCLUDE_DIR}
        ${DOA_INCLUDE_DIR}
        ${ONNXRUNTIME_INCLUDE_DIR}
        ${PORTAUDIO_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
        ${FFTW3F_INCLUDE_DIRS}
        ${SAMPLERATE_INCLUDE_DIRS}
    )
    if(AI_CUBPET_USE_DDS)
        target_include_directories(ai-cubpet PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}
        )
        target_compile_definitions(ai-cubpet PRIVATE AI_CUBPET_USE_DDS)
    endif()

    target_link_libraries(ai-cubpet PRIVATE
        ${SPACEMIT_AUDIO_LIB}
        ${AUDIO_RESAMPLER_LIB}
        ${ASR_LIB}
        ${SENSEVOICE_LIB}
        ${ZIPFORMER_LIB}
        ${KALDI_FBANK_LIB}
        ${KISSFFT_FLOAT_LIB}
        ${SPACEMIT_EP_LIB}
        ${VAD_LIB}
        ${SILERO_VAD_LIB}
        ${DOA_LIB}
        -Wl,--whole-archive
        ${MOTOR_TARGET}
        -Wl,--no-whole-archive
        ${ONNXRUNTIME_LIB}
        CURL::libcurl
        ${PORTAUDIO_LIBRARIES}
        ${SNDFILE_LIBRARIES}
        ${FFTW3F_LIBRARIES}
        ${SAMPLERATE_LIBRARIES}
        Threads::Threads
        dl
        rt
        m
    )
    ai_cubpet_link_nlohmann_json(ai-cubpet)

    target_link_directories(ai-cubpet PRIVATE
        ${PORTAUDIO_LIBRARY_DIRS}
        ${SNDFILE_LIBRARY_DIRS}
        ${FFTW3F_LIBRARY_DIRS}
        ${SAMPLERATE_LIBRARY_DIRS}
    )

    if(LIBGPIOD_FOUND)
        target_include_directories(ai-cubpet PRIVATE ${LIBGPIOD_INCLUDE_DIRS})
        target_link_libraries(ai-cubpet PRIVATE ${LIBGPIOD_LIBRARIES})
        target_compile_options(ai-cubpet PRIVATE ${LIBGPIOD_CFLAGS_OTHER})
    else()
        target_link_libraries(ai-cubpet PRIVATE ${LIBGPIOD_LIBRARY})
    endif()

    if(AI_CUBPET_USE_AEC)
        add_dependencies(ai-cubpet webrtc_ap_ep)
        target_compile_definitions(ai-cubpet PRIVATE AI_CUBPET_USE_WEBRTC_AEC)
        target_include_directories(ai-cubpet PRIVATE ${WEBRTC_INCLUDE_DIRS})
        target_link_libraries(ai-cubpet PRIVATE ${WEBRTC_LIB})
    endif()
    if(AI_CUBPET_USE_DDS)
        target_link_libraries(ai-cubpet PRIVATE
            CycloneDDS-CXX::ddscxx
            ToyCommand_cubpet
        )
        add_dependencies(ai-cubpet ToyCommand_cubpet)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ai-cubpet PRIVATE -Wall -Wextra)
        target_link_options(ai-cubpet PRIVATE -no-pie)
    endif()

    set_target_properties(ai-cubpet PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    if(APPLE)
        set_target_properties(ai-cubpet PROPERTIES
            INSTALL_RPATH "@executable_path/../lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    else()
        set_target_properties(ai-cubpet PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()

    if(AI_CUBPET_USE_AEC)
        get_filename_component(_WEBRTC_LIB_DIR "${WEBRTC_LIB}" DIRECTORY)
        install(FILES "${_WEBRTC_LIB_DIR}/libwebrtc-audio-processing-2.so.1"
            DESTINATION lib)
        install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
            libwebrtc-audio-processing-2.so.1
            \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/lib/libwebrtc-audio-processing-2.so)")
    endif()
    if(AI_CUBPET_USE_DDS)
        # CycloneDDS is fetched, built, and installed into CMAKE_INSTALL_PREFIX
        # by cmake/cyclonedds.cmake so SDK staging/rootfs carry the runtime libs.
    endif()

    install(TARGETS ai-cubpet RUNTIME DESTINATION bin)
endif()

if(AI_CUBPET_BUILD_UI_DEMO)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)

    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
    function(ai_cubpet_set_qt_host_tool tool_target tool_variable)
        if(DEFINED ${tool_variable})
            if(NOT EXISTS "${${tool_variable}}")
                message(FATAL_ERROR "Configured host Qt tool ${tool_variable}=${${tool_variable}} does not exist")
            endif()
            if(TARGET Qt5::${tool_target})
                set_property(TARGET Qt5::${tool_target}
                    PROPERTY IMPORTED_LOCATION "${${tool_variable}}")
            endif()
        endif()
    endfunction()

    if(CMAKE_CROSSCOMPILING)
        ai_cubpet_set_qt_host_tool(moc SROBOTIS_QT_MOC_EXECUTABLE)
        ai_cubpet_set_qt_host_tool(uic SROBOTIS_QT_UIC_EXECUTABLE)
        ai_cubpet_set_qt_host_tool(rcc SROBOTIS_QT_RCC_EXECUTABLE)
    endif()

    if(AI_CUBPET_BUILD_TEST_TOOLS)
        add_executable(ai-cubpet-ui-demo
            ui/main_standalone.cpp
            ui/src/toy_display_widget.cpp
            ui/inc/toy_display_widget.hpp
        )

        target_include_directories(ai-cubpet-ui-demo PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/inc
        )

        target_link_libraries(ai-cubpet-ui-demo PRIVATE
            Qt5::Core
            Qt5::Gui
            Qt5::Widgets
        )

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(ai-cubpet-ui-demo PRIVATE -Wall -Wextra)
            target_link_options(ai-cubpet-ui-demo PRIVATE -no-pie)
        endif()

        set_target_properties(ai-cubpet-ui-demo PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
        if(APPLE)
            set_target_properties(ai-cubpet-ui-demo PROPERTIES
                INSTALL_RPATH "@executable_path/../lib"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        else()
            set_target_properties(ai-cubpet-ui-demo PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        endif()

        install(TARGETS ai-cubpet-ui-demo RUNTIME DESTINATION bin)
    endif()

    if(AI_CUBPET_USE_DDS)
        add_executable(ai-cubpet-ui
            ui/main_dds.cpp
            ui/src/toy_display_widget.cpp
            ui/inc/toy_display_widget.hpp
            ui/src/cubpet_ui_dds_subscriber.cpp
            ui/inc/cubpet_ui_dds_subscriber.hpp
        )

        target_include_directories(ai-cubpet-ui PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/inc
            ${CMAKE_CURRENT_BINARY_DIR}
        )

        target_link_libraries(ai-cubpet-ui PRIVATE
            Qt5::Core
            Qt5::Gui
            Qt5::Widgets
            CycloneDDS-CXX::ddscxx
            ToyCommand_cubpet
        )
        add_dependencies(ai-cubpet-ui ToyCommand_cubpet)

        set(_AI_CUBPET_UI_TARGETS ai-cubpet-ui)

        if(AI_CUBPET_BUILD_TEST_TOOLS)
            add_executable(ai-cubpet-dds-test-pub
                ui/dds_test_publisher.cpp
            )

            target_include_directories(ai-cubpet-dds-test-pub PRIVATE
                ${CMAKE_CURRENT_BINARY_DIR}
            )

            target_link_libraries(ai-cubpet-dds-test-pub PRIVATE
                CycloneDDS-CXX::ddscxx
                ToyCommand_cubpet
            )
            add_dependencies(ai-cubpet-dds-test-pub ToyCommand_cubpet)
            list(APPEND _AI_CUBPET_UI_TARGETS ai-cubpet-dds-test-pub)
        endif()

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(ai-cubpet-ui PRIVATE -Wall -Wextra)
            target_link_options(ai-cubpet-ui PRIVATE -no-pie)
            if(AI_CUBPET_BUILD_TEST_TOOLS)
                target_compile_options(ai-cubpet-dds-test-pub PRIVATE -Wall -Wextra)
                target_link_options(ai-cubpet-dds-test-pub PRIVATE -no-pie)
            endif()
        endif()

        set_target_properties(${_AI_CUBPET_UI_TARGETS} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
        if(APPLE)
            set_target_properties(${_AI_CUBPET_UI_TARGETS} PROPERTIES
                INSTALL_RPATH "@executable_path/../lib"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        else()
            set_target_properties(${_AI_CUBPET_UI_TARGETS} PROPERTIES
                INSTALL_RPATH "$ORIGIN/../lib"
                BUILD_WITH_INSTALL_RPATH TRUE
            )
        endif()

        install(TARGETS ai-cubpet-ui RUNTIME DESTINATION bin)
        if(AI_CUBPET_BUILD_TEST_TOOLS)
            install(TARGETS ai-cubpet-dds-test-pub RUNTIME DESTINATION bin)
        endif()
    endif()
endif()

add_executable(ai_cubpet_daemon
    examples/ai_cubpet_daemon.cpp
)

ai_cubpet_link_nlohmann_json(ai_cubpet_daemon)
target_link_libraries(ai_cubpet_daemon PRIVATE CURL::libcurl)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ai_cubpet_daemon PRIVATE -Wall -Wextra)
    target_link_options(ai_cubpet_daemon PRIVATE -no-pie)
endif()

set_target_properties(ai_cubpet_daemon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
if(APPLE)
    set_target_properties(ai_cubpet_daemon PROPERTIES
        INSTALL_RPATH "@executable_path/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
else()
    set_target_properties(ai_cubpet_daemon PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS ai_cubpet_daemon RUNTIME DESTINATION bin)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY idl/ DESTINATION share/ai-cubpet/idl)
install(DIRECTORY config/boards/ DESTINATION share/ai-cubpet/boards
    FILES_MATCHING
        PATTERN "*.json")

set(AI_CUBPET_GIF_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/gif"
    CACHE PATH "Directory containing ai-cubpet GIF assets")
install(CODE "file(MAKE_DIRECTORY
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share/ai-cubpet/gif\")")
if(EXISTS "${AI_CUBPET_GIF_ASSET_DIR}")
    install(DIRECTORY "${AI_CUBPET_GIF_ASSET_DIR}/"
        DESTINATION share/ai-cubpet/gif
        FILES_MATCHING
            PATTERN "*.gif"
            PATTERN "*.png")
endif()

set(AI_CUBPET_AUDIO_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
    CACHE PATH "Directory containing ai-cubpet audio assets")
install(CODE "file(MAKE_DIRECTORY
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/share/ai-cubpet/audio\")")
if(EXISTS "${AI_CUBPET_AUDIO_ASSET_DIR}")
    install(DIRECTORY "${AI_CUBPET_AUDIO_ASSET_DIR}/"
        DESTINATION share/ai-cubpet/audio
        FILES_MATCHING
            PATTERN "*.wav"
            PATTERN "*.mp3"
            PATTERN "*.ogg")
endif()
