cmake_minimum_required(VERSION 3.28)

project(DllWrapperGenerator
    VERSION "1.0.0.0"
    DESCRIPTION "DLL wrapper layer generator"
    HOMEPAGE_URL "https://github.com/wangwenx190/dll-wrapper-generator/"
)

option(DWG_ENABLE_VCLTL "Enable VC-LTL." OFF)
option(DWG_ENABLE_YYTHUNKS "Enable YY-Thunks." OFF)

include(cmake/utils.cmake)

setup_project(
    LANGUAGES CXX RC
    MAX_WARNING
    RTTI
    EXCEPTIONS
    ENABLE_LTO
)

if(MSVC)
    if(DWG_ENABLE_VCLTL)
        include(cmake/VC-LTL.cmake)
        if("x${SupportLTL}" STREQUAL "xtrue")
            # Make sure we will always overwrite the previous settings.
            unset(CMAKE_MSVC_RUNTIME_LIBRARY)
            unset(CMAKE_MSVC_RUNTIME_LIBRARY CACHE)
            #unset(CMAKE_MSVC_RUNTIME_LIBRARY PARENT_SCOPE)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
        endif()
    endif()
    if(DWG_ENABLE_YYTHUNKS)
        unset(YYTHUNKS_TARGET_OS)
        unset(YYTHUNKS_TARGET_OS CACHE)
        #unset(YYTHUNKS_TARGET_OS PARENT_SCOPE)
        set(YYTHUNKS_TARGET_OS "WinXP" CACHE STRING "" FORCE)
        include(cmake/YY-Thunks.cmake)
    endif()
endif()

set(SYSCMDLINE_BUILD_STATIC ON)
set(SYSCMDLINE_BUILD_TESTS OFF)
set(SYSCMDLINE_INSTALL OFF)
add_subdirectory(syscmdline)

add_executable(${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION "${PROJECT_VERSION}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    syscmdline
    libclang
)

target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
)

if(WIN32)
    set(__rc_path "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.rc")
    if(NOT EXISTS "${__rc_path}")
        generate_win32_rc_file(
            PATH "${__rc_path}"
            VERSION "${PROJECT_VERSION}"
            COMPANY "wangwenx190"
            DESCRIPTION "${PROJECT_DESCRIPTION}"
            COPYRIGHT "MIT License"
            PRODUCT "DLL Wrapper Generator"
            ICONS "${CMAKE_CURRENT_SOURCE_DIR}/icons/glue.ico"
        )
    endif()
    set(__manifest_path "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.manifest")
    if(NOT EXISTS "${__manifest_path}")
        generate_win32_manifest_file(
            PATH "${__manifest_path}"
            ID "org.wangwenx190.${PROJECT_NAME}"
            VERSION "${PROJECT_VERSION}"
            DESCRIPTION "${PROJECT_DESCRIPTION}"
            VISTA_COMPAT
            WIN7_COMPAT
            WIN8_COMPAT
            WIN8_1_COMPAT
            WIN10_COMPAT
            WIN11_COMPAT
            XAML_ISLANDS_COMPAT
            UTF8_CODEPAGE
        )
    endif()
    target_sources(${PROJECT_NAME} PRIVATE
        "${__rc_path}"
        "${__manifest_path}"
    )
endif()

setup_compile_params(
    TARGETS ${PROJECT_NAME} syscmdline
    SPECTRE
    EHCONTGUARD
    PERMISSIVE
    #INTELCET # Crash on many PCs ...
    INTELJCC
    CFGUARD
    FORCE_LTO
    SECURE_CODE
)

install2(TARGETS ${PROJECT_NAME})
