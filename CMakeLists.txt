cmake_minimum_required(VERSION 3.28)

project(honey_comb LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

add_library(${PROJECT_NAME} STATIC main.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC deps/CPLUG/src/ .)
target_include_directories(${PROJECT_NAME} PRIVATE deps/glad)
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC
    CPLUG_COMPANY_NAME="not_woowoo"
    CPLUG_COMPANY_EMAIL=""
    CPLUG_PLUGIN_NAME="${PROJECT_NAME}"
    CPLUG_PLUGIN_URI="notwoowoo.github.io/plugins"
    CPLUG_PLUGIN_VERSION="1.0.0"
    CPLUG_IS_INSTRUMENT=0

    CPLUG_NUM_INPUT_BUSSES=1
    CPLUG_NUM_OUTPUT_BUSSES=1
    CPLUG_WANT_MIDI_INPUT=0
    CPLUG_WANT_MIDI_OUTPUT=0

    CPLUG_WANT_GUI=1
    CPLUG_GUI_RESIZABLE=0

    CPLUG_VST3_CATEGORIES="Fx"
    CPLUG_VST3_TUID_COMPONENT='cplg','comp','xmpl',0
    CPLUG_VST3_TUID_CONTROLLER='cplg','edit','xmpl',0

    CPLUG_CLAP_ID="com.not_woowoo.${PROJECT_NAME}"
    CPLUG_CLAP_DESCRIPTION="A Unique Comb Filter"
    CPLUG_CLAP_FEATURES=CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_STEREO

    CPLUG_NUM_PARAMS=4 # TODO: why no work clap?!?!
)
# file(WRITE ${CMAKE_BINARY_DIR}/config.h "extern const int CPLUG_NUM_PARAMS_VAR;\n")
if(UNIX)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    # target_compile_options(${PROJECT_NAME} PUBLIC -include ${CMAKE_BINARY_DIR}/config.h)
elseif(WIN32)
    # target_compile_options(${PROJECT_NAME} PUBLIC /FI ${CMAKE_BINARY_DIR}/config.h)
endif()

add_library(nanovg STATIC deps/nanovg/src/nanovg.c)
target_include_directories(nanovg PUBLIC deps/nanovg/src)
add_subdirectory(deps/glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw nanovg)

add_library(${PROJECT_NAME}_clap MODULE deps/CPLUG/src/cplug_clap.c)
target_compile_definitions(${PROJECT_NAME}_clap PUBLIC NDEBUG=1)
target_link_libraries(${PROJECT_NAME}_clap PRIVATE ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME}_clap PROPERTIES
    PREFIX ""
    SUFFIX ".clap" 
    OUTPUT_NAME ${PROJECT_NAME}
)

add_library(${PROJECT_NAME}_vst3 MODULE deps/CPLUG/src/cplug_vst3.c)
target_compile_definitions(${PROJECT_NAME}_vst3 PUBLIC NDEBUG=1)
target_link_libraries(${PROJECT_NAME}_vst3 PRIVATE ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME}_vst3 PROPERTIES
    PREFIX ""
    SUFFIX ".vst3" 
    OUTPUT_NAME ${PROJECT_NAME}
)
if(UNIX)
    target_compile_options(${PROJECT_NAME}_vst3 PRIVATE -Wno-multichar)
endif()

