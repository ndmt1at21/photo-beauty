# Qt Quick UI (docs/ui-tasks.md): the static QML module PhotoBeauty, which pb-ui and its tests link.
# Files are globbed, so a task only adds them; a QML file with a `pragma Singleton` line is a singleton.
# copied from snapone 6bdb5c8 (ui/CMakeLists.txt), the parts U0 needs.
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick QuickControls2 QuickTest)
qt_standard_project_setup(REQUIRES 6.8)

# QML straight from the source tree, no build (CLAUDE.md): `pb-ui-tests -import build/ui/ui/live` takes the module
# from live/PhotoBeauty, links to qml/* beside a qmldir that, unlike the build's, does not prefer the compiled copy.
set(live "${CMAKE_CURRENT_BINARY_DIR}/live/PhotoBeauty")
file(REMOVE_RECURSE "${live}")
file(MAKE_DIRECTORY "${live}")
set(qmldir "module PhotoBeauty\noptional plugin pb_qmlplugin\nclassname PhotoBeautyPlugin\n")
file(GLOB PB_QML RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS qml/*.qml qml/*.js)
foreach(f ${PB_QML})
  get_filename_component(name ${f} NAME)
  file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${live}/${name}" SYMBOLIC)
  if(name MATCHES "[.]qml$")
    file(STRINGS ${f} singleton REGEX "^pragma Singleton")
    if(singleton)
      set_source_files_properties(${f} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
      string(APPEND qmldir "singleton ")
    endif()
    get_filename_component(type ${f} NAME_WE)
    string(APPEND qmldir "${type} 254.0 ${name}\n")
  endif()
endforeach()
file(WRITE "${live}/qmldir" "${qmldir}")
file(GLOB PB_UI_SOURCES CONFIGURE_DEPENDS *.cpp *.h)
list(REMOVE_ITEM PB_UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
# In QML: qrc:/qt/qml/PhotoBeauty/<path>, e.g. qrc:/qt/qml/PhotoBeauty/mock/photos/x.jpg; look.h loads the fonts.
file(GLOB PB_UI_RESOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
  fonts/*.ttf fonts/*.otf mock/photos/* brand/*.png brand/*.svg)

qt_add_library(pb_qml STATIC)
qt_add_qml_module(pb_qml URI PhotoBeauty OUTPUT_DIRECTORY PhotoBeauty  # under its URI, for qmllint
  SOURCES ${PB_UI_SOURCES} QML_FILES ${PB_QML} RESOURCES ${PB_UI_RESOURCES})
target_link_libraries(pb_qml PRIVATE Qt6::Quick Qt6::QuickControls2)

qt_add_executable(pb-ui WIN32 MACOSX_BUNDLE main.cpp)  # no console window on Windows, an .app on macOS
target_link_libraries(pb-ui PRIVATE Qt6::Quick Qt6::QuickControls2 pb_qmlplugin)
target_compile_definitions(pb-ui PRIVATE PB_VERSION="${PB_VERSION}")

# QML tests: tests/tst_*.qml, read from the source tree when they run, offscreen.
qt_add_executable(pb-ui-tests tests/main.cpp)
target_compile_definitions(pb-ui-tests PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(pb-ui-tests PRIVATE Qt6::QuickControls2 Qt6::QuickTest pb_qmlplugin)
add_test(NAME ui COMMAND pb-ui-tests)
