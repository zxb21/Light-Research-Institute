cmake_minimum_required(VERSION 3.19)

project(cryo_low_vib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(cryo_low_vib
    main.cpp
    MainWindow.h
    MainWindow.cpp
    OperatorWidget.h
    OperatorWidget.cpp
    OperatorComponents.h
    OperatorComponents.cpp
    EngineerWidget.h
    EngineerWidget.cpp
    SystemTypes.h
    SystemController.h
    ProcessStateMachine.h
    ProcessStateMachine.cpp
    MockSystem.h
    MockSystem.cpp
    TelemetryStore.h
    TrendChartWidget.h
    TrendChartWidget.cpp
    DataLogger.h
    DataLogger.cpp
)

qt_add_resources(cryo_low_vib "cryo_resources"
    PREFIX "/cryo"
    FILES
        resources/operator.qss
        resources/icons/dry-pump.svg
        resources/icons/molecular-pump.svg
        resources/icons/chiller.svg
        resources/icons/vacuum-gauge.svg
        resources/icons/temperature-controller.svg
        resources/icons/compressor.svg
        resources/icons/valve.svg
)

target_include_directories(cryo_low_vib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cryo_low_vib PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(cryo_low_vib PRIVATE /W4 /utf-8)
else()
    target_compile_options(cryo_low_vib PRIVATE -Wall -Wextra)
endif()
