# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(PLUGIN_NAME DeviceIdentification)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

find_package(NEXUS QUIET)
find_package(BCM_HOST QUIET)
find_package(MFRFWLibs QUIET)

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(CompileSettingsDebug CONFIG REQUIRED)

add_library(${MODULE_NAME} SHARED
    DeviceIdentification.cpp
    DeviceIdentificationJsonRpc.cpp
    Module.cpp)

if(NEXUS_FOUND)
    find_package(NXCLIENT REQUIRED)
    target_link_libraries(${MODULE_NAME} 
        PRIVATE
            NEXUS::NEXUS
            NXCLIENT::NXCLIENT)
    target_sources(${MODULE_NAME} 
        PRIVATE 
            Implementation/Nexus/Nexus.cpp)
elseif (BCM_HOST_FOUND)
    target_sources(${MODULE_NAME}
        PRIVATE
            Implementation/RPI/RPI.cpp)
    target_link_libraries(${MODULE_NAME}
        PRIVATE
            BCM_HOST::BCM_HOST)
elseif (MFRFWLIBS_FOUND)
    message("Found MFRFWLibs; checking ${CMAKE_CURRENT_SOURCE_DIR}/Implementation/${WPE_PLUGIN_DEVICEID_MFRIMPL}/${WPE_PLUGIN_DEVICEID_MFRIMPL}.cpp")
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Implementation/${WPE_PLUGIN_DEVICEID_MFRIMPL}/${WPE_PLUGIN_DEVICEID_MFRIMPL}.cpp")
        target_sources(${MODULE_NAME}
            PRIVATE
	        Implementation/${WPE_PLUGIN_DEVICEID_MFRIMPL}/${WPE_PLUGIN_DEVICEID_MFRIMPL}.cpp)
        target_link_libraries(${MODULE_NAME}
            PRIVATE
                mfrfwlibs::mfrfwlibs)
    else()
	message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/Implementation/${WPE_PLUGIN_DEVICEID_MFRIMPL}/${WPE_PLUGIN_DEVICEID_MFRIMPL}.cpp not found.")
    endif()
else ()
    message(FATAL_ERROR "There is no platform backend for device identifier plugin")
endif()

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

target_link_libraries(${MODULE_NAME} 
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins)

target_include_directories(${MODULE_NAME} 
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

install(TARGETS ${MODULE_NAME} 
    DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})
