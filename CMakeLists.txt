cmake_minimum_required(VERSION 3.19)

project(cryo_low_vib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Test)
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

enable_testing()

qt_add_executable(test_telemetry
    tests/test_telemetry.cpp
    TelemetryStore.h
    SystemTypes.h
)
target_include_directories(test_telemetry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_telemetry PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_telemetry COMMAND test_telemetry)

qt_add_executable(test_data_logger
    tests/test_data_logger.cpp
    DataLogger.h
    DataLogger.cpp
)
target_include_directories(test_data_logger PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_data_logger PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_data_logger COMMAND test_data_logger)

qt_add_executable(test_process_state
    tests/test_process_state.cpp
    ProcessStateMachine.h
    ProcessStateMachine.cpp
    SystemTypes.h
)
target_include_directories(test_process_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_process_state PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_process_state COMMAND test_process_state)

qt_add_executable(test_trend_format
    tests/test_trend_format.cpp
    TrendChartWidget.h
    TrendChartWidget.cpp
    TelemetryStore.h
    SystemTypes.h
)
target_include_directories(test_trend_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_trend_format PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_trend_format COMMAND test_trend_format)

qt_add_executable(test_mock_system
    tests/test_mock_system.cpp
    MockSystem.h
    MockSystem.cpp
    SystemController.h
    ProcessStateMachine.h
    ProcessStateMachine.cpp
    SystemTypes.h
)
target_include_directories(test_mock_system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mock_system PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_mock_system COMMAND test_mock_system)

qt_add_executable(test_process_flow
    tests/test_process_flow.cpp
    OperatorComponents.h
    OperatorComponents.cpp
    SystemTypes.h
)
target_include_directories(test_process_flow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_process_flow PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_process_flow COMMAND test_process_flow)

qt_add_executable(test_operator_detail
    tests/test_operator_detail.cpp
    OperatorWidget.h
    OperatorWidget.cpp
    OperatorComponents.h
    OperatorComponents.cpp
    TrendChartWidget.h
    TrendChartWidget.cpp
    TelemetryStore.h
    SystemTypes.h
)
target_include_directories(test_operator_detail PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_operator_detail PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_operator_detail COMMAND test_operator_detail)

if(MSVC)
    foreach(target cryo_low_vib test_telemetry test_data_logger test_process_state test_trend_format test_mock_system test_process_flow test_operator_detail)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    endforeach()
else()
    foreach(target cryo_low_vib test_telemetry test_data_logger test_process_state test_trend_format test_mock_system test_process_flow test_operator_detail)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()
