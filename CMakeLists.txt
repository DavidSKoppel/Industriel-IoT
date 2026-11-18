cmake_minimum_required(VERSION 3.16)

project(mqtt_questdb_subscriber LANGUAGES CXX)

add_executable(mqtt_questdb_subscriber
    mqtt_questdb_subscriber.cpp
)

target_compile_features(mqtt_questdb_subscriber PRIVATE cxx_std_17)

find_package(PahoMqttCpp REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

target_link_libraries(mqtt_questdb_subscriber PRIVATE
    PahoMqttCpp::paho-mqttpp3
    PkgConfig::PQXX
)

include(CTest)
if(BUILD_TESTING)
    add_executable(batch_tests tests/batch_tests.cpp)
    target_compile_features(batch_tests PRIVATE cxx_std_17)
    target_link_libraries(batch_tests PRIVATE PahoMqttCpp::paho-mqttpp3 PkgConfig::PQXX)
    add_test(NAME sparkplug_batch_regression COMMAND batch_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/corrected-batch-fixtures.txt")
endif()
