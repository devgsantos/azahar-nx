if (ENABLE_SWITCH_FRONTEND)
    add_library(network STATIC EXCLUDE_FROM_ALL
        artic_base/artic_base_client.h
        artic_base/artic_base_common.h
        network.h
        room.h
        room_member.h
        socket_manager.h
        switch_network_stub.cpp
        verify_user.h
    )
else()
    add_library(network STATIC EXCLUDE_FROM_ALL
        announce_multiplayer_session.cpp
        announce_multiplayer_session.h
        artic_base/artic_base_client.cpp
        artic_base/artic_base_client.h
        artic_base/artic_base_common.h
        network.cpp
        network.h
        network_settings.cpp
        network_settings.h
        packet.cpp
        packet.h
        precompiled_headers.h
        room.cpp
        room.h
        room_member.cpp
        room_member.h
        socket_manager.cpp
        socket_manager.h
        verify_user.cpp
        verify_user.h
    )
endif()

create_target_directory_groups(network)

if (ENABLE_WEB_SERVICE)
    target_link_libraries(network PRIVATE web_service)
endif()

target_link_libraries(network PRIVATE citra_common)
if (NOT ENABLE_SWITCH_FRONTEND)
    target_link_libraries(network PRIVATE enet Boost::serialization httplib)
endif()

if (CITRA_USE_PRECOMPILED_HEADERS AND NOT ENABLE_SWITCH_FRONTEND)
    target_precompile_headers(network PRIVATE precompiled_headers.h)
endif()
