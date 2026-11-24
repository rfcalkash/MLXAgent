cmake_minimum_required(VERSION 3.16)

project(client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Svg Network)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

include(FetchContent)
set(JKQtPlotter_BUILD_LIB_JKQTPLOTTER OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_LIB_JKQTFASTPLOTTER OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_LIB_JKQTMATH OFF CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_LIB_JKQTMATHTEXT ON CACHE BOOL "" FORCE)
set(JKQtPlotter_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    jkqtplotter
    GIT_REPOSITORY git@github.com:jkriege2/JKQtPlotter.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(jkqtplotter)

qt_add_executable(appclient
    main.cpp
)

qt_add_qml_module(appclient
    URI MlxClient
    QML_FILES
        Main.qml
        MyFooterBar.qml
        Settings.qml
    SOURCES
        PropertyMacros.h
        fileuploader.h fileuploader.cpp
        resttransport.h resttransport.cpp
        authcontroller.h authcontroller.cpp
        modelcontroller.h modelcontroller.cpp
        ChatModel.h ChatModel.cpp
        chatcontroller.h chatcontroller.cpp
        mathformatter.h mathformatter.cpp
        appcontext.h appcontext.cpp
        webfetcher.h webfetcher.cpp
        toolcontroller.h toolcontroller.cpp
)


# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appclient PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appclient
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appclient
    PRIVATE Qt6::Quick
    Qt6::Network
    Qt6::Svg
    JKQTMathText6
)


install(TARGETS appclient
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
