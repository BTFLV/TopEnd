set(IPLUG2_DIR "${PROJECT_SOURCE_DIR}/external/iPlug2")
set(IPLUG2_CXX_STANDARD 20)
set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Deploy plugins automatically")
option(TOPEND_ENABLE_GUI_CAPTURE "Development-only offscreen GUI evidence capture" OFF)
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)
# These optional modules are not used by TopEnd's vector controls. Avoid compiling
# their layout/browser dependencies as unrelated default-build targets.
foreach(optional iPlug2_Extras_FlexBox_obj iPlug2_Extras_IWebViewControl_obj)
  if(TARGET ${optional})
    set_target_properties(${optional} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()
if(APPLE)
  include(${IPLUG2_DIR}/Scripts/cmake/AUv2.cmake)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TOPEND_ADAPTER_DIR "${CMAKE_BINARY_DIR}/iplug-adapters")
execute_process(COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/patch_iplug.py
  ${IPLUG2_DIR} ${TOPEND_ADAPTER_DIR} COMMAND_ERROR_IS_FATAL ANY)
set(adapter_overlays "CLAP|IPlug/CLAP/IPlugCLAP.cpp|IPlugCLAP.cpp"
  "VST3|Dependencies/IPlug/VST3_SDK/base/source/fstring.cpp|fstring.cpp"
  "VST3|IPlug/VST3/IPlugVST3_ProcessorBase.cpp|IPlugVST3_ProcessorBase.cpp")
if(APPLE)
  list(APPEND adapter_overlays "AUv2|IPlug/AUv2/IPlugAU.cpp|IPlugAU.cpp"
    "IGraphics|IGraphics/Platforms/IGraphicsMac_view.mm|IGraphicsMac_view.mm")
endif()
foreach(pair IN LISTS adapter_overlays)
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 api)
  list(GET parts 1 original)
  list(GET parts 2 replacement)
  get_target_property(sources iPlug2::${api} INTERFACE_SOURCES)
  list(TRANSFORM sources REPLACE "^${IPLUG2_DIR}/${original}$" "${TOPEND_ADAPTER_DIR}/${replacement}")
  set_target_properties(iPlug2::${api} PROPERTIES INTERFACE_SOURCES "${sources}")
  if(APPLE)
    set_source_files_properties(${TOPEND_ADAPTER_DIR}/${replacement} PROPERTIES LANGUAGE OBJCXX)
  endif()
endforeach()
if(NOT IPLUG2_VST3_SUPPORTED OR NOT IPLUG2_CLAP_SUPPORTED)
  message(FATAL_ERROR "Run python3 tools/bootstrap.py before configuring plugin targets")
endif()
iplug_add_plugin(TopEnd
  SOURCES TopEnd.cpp TopEnd.h config.h ../gui/Controls.h ../gui/Theme.h
  RESOURCES ${PROJECT_SOURCE_DIR}/assets/branding/topend-icon.png
            ${PROJECT_SOURCE_DIR}/assets/fonts/Lato-Regular.ttf
            ${PROJECT_SOURCE_DIR}/assets/fonts/Lato-Bold.ttf
            ${PROJECT_SOURCE_DIR}/assets/fonts/OFL.txt
            ${PROJECT_SOURCE_DIR}/assets/THIRD_PARTY_NOTICES.txt
  FORMATS AU VST3 CLAP
  LINK topend-dsp
  DEFINES SAMPLE_TYPE_FLOAT
)
if(TOPEND_ENABLE_GUI_CAPTURE)
  foreach(api vst3 clap au)
    target_compile_definitions(TopEnd-${api} PRIVATE TOPEND_ENABLE_GUI_CAPTURE)
  endforeach()
endif()
# Upstream's source-prefix helper marks interface sources as ObjC++; NanoVG's
# .m translation unit contains C and must remain Objective-C, including under Clang 21.
if(APPLE)
  set_source_files_properties(${IPLUG2_DIR}/IGraphics/Drawing/IGraphicsNanoVG_src.m
    PROPERTIES LANGUAGE OBJC)
  # Compile Metal separately for each format so Objective-C's global class
  # namespace cannot collide with another vendor or another TopEnd format.
  get_target_property(metal_sources iPlug2::IGraphics::NanoVG::Metal INTERFACE_SOURCES)
  list(REMOVE_ITEM metal_sources "$<TARGET_OBJECTS:iPlug2_IGraphics_NanoVG_Metal_obj>")
  get_target_property(metal_includes iPlug2_IGraphics_NanoVG_Metal_obj INCLUDE_DIRECTORIES)
  foreach(api vst3 clap au)
    add_library(topend-metal-${api} OBJECT ${IPLUG2_DIR}/IGraphics/Drawing/IGraphicsNanoVG_src.m)
    target_include_directories(topend-metal-${api} PRIVATE ${metal_includes})
    target_compile_options(topend-metal-${api} PRIVATE -fobjc-arc)
    target_compile_definitions(topend-metal-${api} PRIVATE IGRAPHICS_NANOVG IGRAPHICS_METAL
      MNVGbuffers=TopEnd_${api}_MNVGbuffers MNVGcontext=TopEnd_${api}_MNVGcontext MNVGtexture=TopEnd_${api}_MNVGtexture)
    list(APPEND metal_sources "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,TopEnd-${api}>:$<TARGET_OBJECTS:topend-metal-${api}>>")
  endforeach()
  set_target_properties(iPlug2::IGraphics::NanoVG::Metal PROPERTIES INTERFACE_SOURCES "${metal_sources}")
  set_target_properties(iPlug2_IGraphics_NanoVG_Metal_obj iPlug2_IGraphics_NanoVG_GL_obj PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

