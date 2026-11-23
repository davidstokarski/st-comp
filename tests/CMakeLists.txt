# Whole-plugin tests: style curves / timing, lookahead, hold, nulls, latency, channel modes, side chain, upward,
# processor stress, state, presets, multiview (with the installed ST EQ AU loaded in-process) and editor snapshots.
#   StCompPluginTests [--snapshot <dir>] [--video <dir>] [--usecases <dir> [scene ...]]
# Links the plugin's shared-code library, so it exercises the real processor.
juce_add_console_app(StCompPluginTests PRODUCT_NAME "StCompPluginTests")
target_sources(StCompPluginTests PRIVATE PluginTests.cpp UseCases.cpp)
target_compile_features(StCompPluginTests PRIVATE cxx_std_20)
target_link_libraries(StCompPluginTests PRIVATE STCOMP)
target_include_directories(StCompPluginTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Source")
target_include_directories(StCompPluginTests PRIVATE $<TARGET_PROPERTY:STCOMP,INCLUDE_DIRECTORIES>)
target_compile_definitions(StCompPluginTests PRIVATE $<TARGET_PROPERTY:STCOMP,COMPILE_DEFINITIONS> $<TARGET_PROPERTY:STCOMP,INTERFACE_COMPILE_DEFINITIONS>)
if(APPLE)
    target_link_libraries(StCompPluginTests PRIVATE "-framework AudioToolbox")
endif()
