include(${CMAKE_ROOT}/Modules/SelectLibraryConfigurations.cmake)

if(NOT TARGET unofficial::omniverse-physx-sdk)
    get_filename_component(z_vcpkg_omniverse_physx_sdk_prefix "${CMAKE_CURRENT_LIST_FILE}" PATH)
    get_filename_component(z_vcpkg_omniverse_physx_sdk_prefix "${z_vcpkg_omniverse_physx_sdk_prefix}" PATH)
    get_filename_component(z_vcpkg_omniverse_physx_sdk_prefix "${z_vcpkg_omniverse_physx_sdk_prefix}" PATH)

    get_filename_component(OMNIVERSE-PHYSX-SDK_INCLUDE_DIRS "${z_vcpkg_omniverse_physx_sdk_prefix}/include/physx" ABSOLUTE)
    get_filename_component(OMNIVERSE-PHYSX-SDK_RELEASE_LIBS_DIR "${z_vcpkg_omniverse_physx_sdk_prefix}/lib" ABSOLUTE)
    get_filename_component(OMNIVERSE-PHYSX-SDK_DEBUG_LIBS_DIR "${z_vcpkg_omniverse_physx_sdk_prefix}/debug/lib" ABSOLUTE)
    get_filename_component(OMNIVERSE-PHYSX-SDK_RELEASE_BIN_DIR "${z_vcpkg_omniverse_physx_sdk_prefix}/bin" ABSOLUTE)
    get_filename_component(OMNIVERSE-PHYSX-SDK_DEBUG_BIN_DIR "${z_vcpkg_omniverse_physx_sdk_prefix}/debug/bin" ABSOLUTE)
    get_filename_component(OMNIVERSE-PHYSX-SDK_RELEASE_TOOLS_DIR "${z_vcpkg_omniverse_physx_sdk_prefix}/tools" ABSOLUTE)

    find_library(OMNIVERSE-PHYSX-SDK_LIBRARY_RELEASE NAMES PhysX_static_64 PhysX_64 PATHS "${OMNIVERSE-PHYSX-SDK_RELEASE_LIBS_DIR}" NO_DEFAULT_PATH)
    find_library(OMNIVERSE-PHYSX-SDK_LIBRARY_DEBUG NAMES PhysX_static_64 PhysX_64 PATHS "${OMNIVERSE-PHYSX-SDK_DEBUG_LIBS_DIR}" NO_DEFAULT_PATH)

    set(OMNIVERSE-PHYSX-SDK_LIBRARIES "")
    add_library(unofficial::omniverse-physx-sdk::sdk UNKNOWN IMPORTED)

    if (WIN32 AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        set_target_properties(unofficial::omniverse-physx-sdk::sdk PROPERTIES
            IMPORTED_IMPLIB_RELEASE "${OMNIVERSE-PHYSX-SDK_LIBRARY_RELEASE}"
            IMPORTED_IMPLIB_DEBUG "${OMNIVERSE-PHYSX-SDK_LIBRARY_DEBUG}"
        )
    endif()

    set_target_properties(unofficial::omniverse-physx-sdk::sdk PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_RELEASE "${OMNIVERSE-PHYSX-SDK_LIBRARY_RELEASE}"
        IMPORTED_LOCATION_DEBUG "${OMNIVERSE-PHYSX-SDK_LIBRARY_DEBUG}"
        INTERFACE_INCLUDE_DIRECTORIES "${OMNIVERSE-PHYSX-SDK_INCLUDE_DIRS}"
    )

    target_compile_definitions(unofficial::omniverse-physx-sdk::sdk INTERFACE $<$<CONFIG:Debug>:_DEBUG>)

    set(lib_names
            PhysXExtensions
            PhysXPvdSDK
            PhysXCharacterKinematic
            PhysXCooking
            PhysXCommon
            PhysXFoundation
            PhysXVehicle
    )
    if(WIN32)
        list(APPEND lib_names PhysXVehicle2)
    endif()

    foreach(name IN LISTS lib_names)
        find_library(OMNIVERSE_${name}_LIBRARY_RELEASE
            NAMES ${name}_static_64 ${name}_64
            PATHS "${OMNIVERSE-PHYSX-SDK_RELEASE_LIBS_DIR}"
            NO_DEFAULT_PATH
            REQUIRED
        )
        find_library(OMNIVERSE_${name}_LIBRARY_DEBUG
            NAMES ${name}_static_64 ${name}_64
            PATHS "${OMNIVERSE-PHYSX-SDK_DEBUG_LIBS_DIR}"
            NO_DEFAULT_PATH
        )
        add_library(unofficial::omniverse-physx-sdk::${name} UNKNOWN IMPORTED)
        set_target_properties(unofficial::omniverse-physx-sdk::${name}
            PROPERTIES
                IMPORTED_CONFIGURATIONS "RELEASE"
                IMPORTED_LOCATION_RELEASE "${OMNIVERSE_${name}_LIBRARY_RELEASE}"
        )
        if(OMNIVERSE_${name}_LIBRARY_DEBUG)
            set_target_properties(unofficial::omniverse-physx-sdk::${name}
                PROPERTIES
                    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
                    IMPORTED_LOCATION_DEBUG "${OMNIVERSE_${name}_LIBRARY_DEBUG}"
            )
        endif()
        set_property(TARGET unofficial::omniverse-physx-sdk::sdk APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES unofficial::omniverse-physx-sdk::${name}
        )
        select_library_configurations(OMNIVERSE_${name})
    endforeach()

    if(WIN32)
        find_file(OMNIVERSE-PHYSX-SDK-GPU_LIBRARY_RELEASE NAMES PhysXGpu_64.dll PATHS "${OMNIVERSE-PHYSX-SDK_RELEASE_TOOLS_DIR}" NO_DEFAULT_PATH)
        find_file(OMNIVERSE-PHYSX-SDK-GPU_DEVICE_LIBRARY_RELEASE NAMES PhysXDevice64.dll PATHS "${OMNIVERSE-PHYSX-SDK_RELEASE_TOOLS_DIR}" NO_DEFAULT_PATH)
    elseif(UNIX)
        find_file(OMNIVERSE-PHYSX-SDK-GPU_LIBRARY_RELEASE NAMES libPhysXGpu_64.so PATHS "${OMNIVERSE-PHYSX-SDK_RELEASE_TOOLS_DIR}" NO_DEFAULT_PATH)
    endif()

    add_library(unofficial::omniverse-physx-sdk::gpu-library SHARED IMPORTED)
    set_target_properties(unofficial::omniverse-physx-sdk::gpu-library PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${OMNIVERSE-PHYSX-SDK-GPU_LIBRARY_RELEASE}"
    )
    if(WIN32)
        add_library(unofficial::omniverse-physx-sdk::gpu-device-library SHARED IMPORTED)
        set_target_properties(unofficial::omniverse-physx-sdk::gpu-device-library PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION "${OMNIVERSE-PHYSX-SDK-GPU_DEVICE_LIBRARY_RELEASE}"
        )
    endif()
endif()
