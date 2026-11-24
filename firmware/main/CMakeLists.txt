idf_component_register(SRCS "main.c" "settings.c" "network_time.c" "weather_service.c" "transport_service.c" "map_tiles.c" "phone_setup.c" "phone_protocol.c" "credentials.c" "pairing.c" "phone_tls.c" "phone_dns.c" "device_identity.c" "identity_store.c" "device_enrollment.c" "update_policy.c" "update_service.c" INCLUDE_DIRS "."
    EMBED_TXTFILES "ota_public.pem" "device_roots.pem" "web/index.html" "web/app.js" "web/style.css" "web/location.js" "web/shortcut.js" "web/connection.js"
    REQUIRES efuse esp_hw_support esp_security esp_https_server esp_http_server station_transport station_weather station_core station_ui nvs_flash app_update esp_timer esp_netif esp_wifi esp_event espressif__esp_wifi_remote espressif__esp_hosted esp_http_client mbedtls espressif__cjson)

# Only package an Apple-signed generic shortcut, never the editable source.
set(location_shortcut "${CMAKE_CURRENT_SOURCE_DIR}/../../companion/shortcut/Weather Station Location.shortcut")
if(EXISTS "${location_shortcut}")
    # Apple exports signed shortcuts as AEA1 archives. This catches accidental
    # source-plist packaging; signature/content validation is a release step.
    file(READ "${location_shortcut}" shortcut_header LIMIT 4 HEX)
    if(NOT shortcut_header STREQUAL "41454131")
        message(FATAL_ERROR "Location shortcut must be an Apple-signed AEA1 export, not the unsigned source")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${location_shortcut}" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE STATION_HAS_LOCATION_SHORTCUT=1)
endif()
