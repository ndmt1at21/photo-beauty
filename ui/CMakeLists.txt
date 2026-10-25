# Qt Quick UI (docs/ui-tasks.md): the static QML module PhotoBeauty, which pb-ui and its tests link.
# Files are globbed, so a task only adds them; a QML file with a `pragma Singleton` line is a singleton.
# copied from snapone 6bdb5c8 (ui/CMakeLists.txt), the parts U0 needs.
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick QuickControls2 QuickTest LinguistTools)
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
# pb_core: the canvas (V1); whole, as the core's static registrations (F30's FILTER() in core/filter_*.cpp) are in
# files nothing names, which the linker would leave out of a static library. MinGW: CMake 3.28 asks ld.lld --help
# and gives --push-state, which lld's MinGW driver (llvm-mingw) does not take: the plain pair there.
if(MINGW)
  set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
endif()
target_link_libraries(pb_qml PRIVATE Qt6::Quick Qt6::QuickControls2 "$<LINK_LIBRARY:WHOLE_ARCHIVE,pb_core>")
# Wintab on Windows (I30, pen.cpp) through QWindowsApplication of qguiapplication_p.h: Qt's private API, which a Qt
# update may change (pen.cpp then no longer builds; check it with each Qt), the build tied to its Qt's DLLs, as
# ui/deploy-win.sh ships them (Qt's warning of that, said here, off).
if(WIN32)
  set(QT_NO_PRIVATE_MODULE_WARNING ON)
  find_package(Qt6 6.11 REQUIRED COMPONENTS GuiPrivate)
  target_link_libraries(pb_qml PRIVATE Qt6::GuiPrivate)
endif()

qt_add_executable(pb-ui WIN32 MACOSX_BUNDLE main.cpp)  # no console window on Windows, an .app on macOS
target_link_libraries(pb-ui PRIVATE Qt6::Quick Qt6::QuickControls2 pb_qmlplugin)
target_compile_definitions(pb-ui PRIVATE PB_VERSION="${PB_VERSION}")
# Windows (preset ui-mingw, ui/deploy-win.sh): the exe's icon, the .ico of ui/brand/ once U16 has made it, which Qt
# also gives every window as IDI_ICON1.
file(GLOB ico CONFIGURE_DEPENDS brand/*.ico)
if(WIN32 AND ico)
  list(GET ico 0 ico)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pb-ui.rc" "IDI_ICON1 ICON \"${ico}\"\n")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pb-ui.rc" PROPERTIES OBJECT_DEPENDS "${ico}")
  target_sources(pb-ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pb-ui.rc")
endif()
# English beside Vietnamese, the language of the sources (U30; snapone's decision 10): the module embeds it as
# :/i18n/pb_en.qm, which pb-ui's main.cpp loads and tests read through Language (language.h); `cmake --build build/ui
# --target update_translations` adds new and changed strings to i18n/pb_en.ts, to translate there (build/orch/api/u30.md).
set(PB_TS i18n/pb_en.ts)
qt_add_translations(pb_qml TS_FILES ${PB_TS} SOURCE_TARGETS pb_qml pb-ui SOURCES brand/splash.h
  LUPDATE_OPTIONS -no-obsolete -locations none)

# QML tests: tests/tst_*.qml, read from the source tree when they run, offscreen.
qt_add_executable(pb-ui-tests tests/main.cpp)
target_compile_definitions(pb-ui-tests PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(pb-ui-tests PRIVATE Qt6::QuickControls2 Qt6::QuickTest pb_qmlplugin)
add_test(NAME ui COMMAND pb-ui-tests)
# Every string of the sources translated, with the same placeholders and edge spaces (i18n/check.py); pb-ui takes the
# language it should at start (--lang, Thiết lập, the system's).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME i18n COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/i18n/check.py $<TARGET_FILE:Qt6::lupdate>
    ${CMAKE_CURRENT_SOURCE_DIR} ${PB_TS} --app $<TARGET_FILE:pb-ui>)
endif()
