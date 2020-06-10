/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Module.h"
#include "Demo.h"
#include <interfaces/json/JsonData_Demo.h>

namespace WPEFramework {

namespace Plugin {

    using namespace JsonData::Demo;

    // Registration
    //

    void Demo::RegisterAll()
    {
        Register<StartParamsData,void>(_T("start"), &Demo::endpoint_start, this);
        Register<StopParamsData,void>(_T("stop"), &Demo::endpoint_stop, this);
        Property<Core::JSON::ArrayType<Core::JSON::String>>(_T("demos"), &Demo::get_demos, nullptr, this);
    }

    void Demo::UnregisterAll()
    {
        Unregister(_T("start"));
        Unregister(_T("stop"));
        Unregister(_T("demos"));
    }

    // API implementation
    //

    // Method: start - Starts a new demo
    // Return codes:
    //  - ERROR_NONE: Success
    //  - ERROR_UNAVAILABLE: Demo not found
    //  - ERROR_GENERAL: Failed to start demo
    uint32_t Demo::endpoint_start(const StartParamsData& params)
    {
        uint32_t result = Core::ERROR_NONE;
        const string& name = params.Name.Value();
        const string& command = params.Command.Value();

        TRACE_L1("name %s, command %s", name.c_str(), command.c_str());        

        return result;
    }

    // Method: stop - Stops a demo
    // Return codes:
    //  - ERROR_NONE: Success
    //  - ERROR_UNAVAILABLE: Demo not found
    uint32_t Demo::endpoint_stop(const StopParamsData& params)
    {
        uint32_t result = Core::ERROR_NONE;
        const string& name = params.Name.Value();

        TRACE_L1("name %s", name.c_str());        

        return result;
    }

    // Property: demos - List of active demos
    // Return codes:
    //  - ERROR_NONE: Success
    uint32_t Demo::get_demos(Core::JSON::ArrayType<Core::JSON::String>& response) const
    {
        TRACE_L1("get_demos");

        response.Add(Core::JSON::String("demo1"));
        response.Add(Core::JSON::String("demo2"));
        response.Add(Core::JSON::String("demo3"));

        return Core::ERROR_NONE;
    }

} // namespace Plugin

}

