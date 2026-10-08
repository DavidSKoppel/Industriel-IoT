cmake_minimum_required(VERSION 3.16)

project(mqtt_questdb_subscriber LANGUAGES CXX)

add_executable(mqtt_questdb_subscriber
    mqtt_questdb_subscriber.cpp
)

target_compile_features(mqtt_questdb_subscriber PRIVATE cxx_std_17)

find_package(PahoMqttCpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

target_link_libraries(mqtt_questdb_subscriber PRIVATE
    PahoMqttCpp::paho-mqttpp3
    nlohmann_json::nlohmann_json
    PkgConfig::PQXX
)
