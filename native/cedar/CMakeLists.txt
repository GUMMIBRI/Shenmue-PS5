cmake_minimum_required(VERSION 3.24)
project(cedar_platform_scaffold VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Original scaffold only: upstream research is deliberately not a source input.
add_library(cedar_miniz_inflate STATIC third_party/miniz/miniz_tinfl.c)
set_source_files_properties(third_party/miniz/miniz_tinfl.c PROPERTIES LANGUAGE CXX)
target_include_directories(cedar_miniz_inflate PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
target_compile_definitions(cedar_miniz_inflate PUBLIC MINIZ_NO_MALLOC MINIZ_NO_STDIO
    MINIZ_NO_TIME MINIZ_NO_DEFLATE_APIS MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_APIS
    MINIZ_HAS_64BIT_REGISTERS=1 MINIZ_LITTLE_ENDIAN=1 MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0)
add_library(cedar_platform STATIC src/platform.cpp src/tad.cpp src/payload.cpp src/dds.cpp
    src/gzip_decode.cpp src/pkf.cpp src/pvr.cpp src/pvr_dds.cpp src/texture_resource.cpp
    src/package_texture.cpp src/mt5.cpp src/mt5_geometry.cpp src/mt7.cpp src/model_textures.cpp src/graphics_settings.cpp
    src/dialogue_progress.cpp src/dialogue_body.cpp src/dialogue_machine.cpp src/dialogue_expression.cpp
    src/afs.cpp src/srf.cpp src/spsd.cpp src/spsd_aica.cpp src/aica_adpcm.cpp
    src/world_clock.cpp src/game_state.cpp src/reference_game_state.cpp src/game_events.cpp src/game_context.cpp
    src/actor_world.cpp src/dialogue_dispatch.cpp src/dialogue_dispatch_host.cpp src/scene_resource.cpp
    src/world_collision.cpp)
target_link_libraries(cedar_platform PRIVATE cedar_miniz_inflate)
target_include_directories(cedar_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(cedar_platform PUBLIC cxx_std_17)
set_target_properties(cedar_platform PROPERTIES CXX_EXTENSIONS OFF)

if(CEDAR_PS5)
    target_compile_definitions(cedar_platform PUBLIC CEDAR_PS5=1)
    add_library(cedar_ps5 STATIC src/ps5_audio.cpp src/ps5_input.cpp src/ps5_process.cpp src/posix_assets.cpp
        src/save_store.cpp src/graphics_preferences.cpp src/tac_reader.cpp src/afs_reader.cpp src/archive_set.cpp src/archive_mount.cpp src/archive_texture.cpp src/asset_stream.cpp src/startup_resources.cpp src/startup_scene.cpp)
    target_link_libraries(cedar_ps5 PUBLIC cedar_platform)
else()
    # Host uses std::filesystem; the native target uses descriptor-relative I/O.
    add_library(cedar_assets STATIC src/assets.cpp)
    target_include_directories(cedar_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(cedar_assets PUBLIC cxx_std_17)
    set_target_properties(cedar_assets PROPERTIES CXX_EXTENSIONS OFF)
endif()

set(CEDAR_SDL3_INCLUDE_DIR "" CACHE PATH "Existing licensed SDL3 include directory (optional)")
set(CEDAR_GPU_SHADER_INCLUDE_DIR "" CACHE PATH "Generated original Cedar SPIR-V header directory (optional)")
set(CEDAR_POST_SHADER_INCLUDE_DIR "" CACHE PATH "Generated post-processing SPIR-V header directory (optional)")
if(CEDAR_SDL3_INCLUDE_DIR)
    if(NOT EXISTS "${CEDAR_SDL3_INCLUDE_DIR}/SDL3/SDL.h")
        message(FATAL_ERROR "CEDAR_SDL3_INCLUDE_DIR must contain SDL3/SDL.h")
    endif()
    add_library(cedar_presentation STATIC src/presentation.cpp src/sdl_events.cpp src/spsd_voice.cpp)
    target_include_directories(cedar_presentation PUBLIC "${CEDAR_SDL3_INCLUDE_DIR}")
    target_link_libraries(cedar_presentation PUBLIC cedar_platform)
    if(CEDAR_GPU_SHADER_INCLUDE_DIR)
        if(NOT EXISTS "${CEDAR_GPU_SHADER_INCLUDE_DIR}/cedar_mesh_shaders.hpp")
            message(FATAL_ERROR "Compile original GPU shaders before configuring cedar_gpu")
        endif()
        add_library(cedar_gpu STATIC src/gpu_context.cpp src/gpu_mesh.cpp src/gpu_texture.cpp
            src/gpu_renderer.cpp src/gpu_pack.cpp src/actor_render.cpp src/scene_draws.cpp src/scene_actors.cpp)
        target_include_directories(cedar_gpu PUBLIC "${CEDAR_SDL3_INCLUDE_DIR}" PRIVATE "${CEDAR_GPU_SHADER_INCLUDE_DIR}")
        target_link_libraries(cedar_gpu PUBLIC cedar_platform)
        if(CEDAR_POST_SHADER_INCLUDE_DIR)
            if(NOT EXISTS "${CEDAR_POST_SHADER_INCLUDE_DIR}/cedar_post_shaders.hpp")
                message(FATAL_ERROR "Compile post shaders before configuring cedar_post")
            endif()
            add_library(cedar_post STATIC src/fsr1.cpp src/display_effects.cpp src/movie_effects.cpp
                src/render_layout.cpp src/scene_target.cpp src/post_processor.cpp src/graphics_apply.cpp src/post_blit.cpp
                src/gpu_frame.cpp)
            target_include_directories(cedar_post PUBLIC "${CEDAR_SDL3_INCLUDE_DIR}"
                PRIVATE "${CEDAR_POST_SHADER_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsr1")
            target_link_libraries(cedar_post PUBLIC cedar_gpu cedar_platform)
        endif()
    endif()
    if(CEDAR_PS5)
        add_library(cedar_session STATIC src/ps5_session.cpp src/application.cpp)
        target_link_libraries(cedar_session PUBLIC cedar_ps5 cedar_presentation)
        if(TARGET cedar_gpu)
            add_library(cedar_scene STATIC src/scene_session.cpp)
            target_link_libraries(cedar_scene PUBLIC cedar_ps5 cedar_gpu)
        endif()
    endif()
endif()

foreach(component cedar_platform cedar_assets cedar_ps5 cedar_presentation cedar_session cedar_gpu cedar_post cedar_scene)
    if(TARGET ${component})
        if(MSVC)
            target_compile_options(${component} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${component} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()

# Intentionally no executable, install/deploy target, network fetch or game boot.
