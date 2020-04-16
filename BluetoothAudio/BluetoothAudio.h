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

#pragma once

#include "Module.h"

#include <interfaces/IBluetooth.h>
#include <interfaces/IBluetoothAudio.h>
#include <interfaces/JBluetoothAudioSink.h>

namespace WPEFramework {

namespace Plugin {

    class BluetoothAudio : public PluginHost::IPlugin
                         , public PluginHost::JSONRPC {
    private:
        class Config : public Core::JSON::Container {
        public:
            Config(const Config&) = delete;
            Config& operator=(const Config&) = delete;
            Config()
                : Core::JSON::Container()
                , Controller(_T("BluetoothControl"))
            {
                Add(_T("controller"), &Controller);
            }
            ~Config() = default;

        public:
            Core::JSON::String Controller;
        }; // class Config

        class AudioSink : public Exchange::IBluetoothAudioSink {
        private:
            class ServiceDiscovery : public Bluetooth::SDPSocket {
            private:
                class SDPFlow {
                public:
                    ~SDPFlow() = default;
                    SDPFlow() = delete;
                    SDPFlow(const SDPFlow&) = delete;
                    SDPFlow& operator=(const SDPFlow&) = delete;
                    SDPFlow(const TCHAR formatter[], ...)
                    {
                        va_list ap;
                        va_start(ap, formatter);
                        Trace::Format(_text, formatter, ap);
                        va_end(ap);
                    }
                    explicit SDPFlow(const string& text)
                        : _text(Core::ToString(text))
                    {
                    }

                public:
                    const char* Data() const
                    {
                        return (_text.c_str());
                    }
                    uint16_t Length() const
                    {
                        return (static_cast<uint16_t>(_text.length()));
                    }

                private:
                    std::string _text;
                }; // class SDPFlow

                class DeviceCallback : public Exchange::IBluetooth::IDevice::ICallback {
                public:
                    DeviceCallback() = delete;
                    DeviceCallback(const DeviceCallback&) = delete;
                    DeviceCallback& operator=(const DeviceCallback&) = delete;

                    DeviceCallback(ServiceDiscovery* parent)
                        : _parent(*parent)
                    {
                        ASSERT(parent != nullptr);
                    }
                    ~DeviceCallback() = default;

                public:
                    void Updated() override
                    {
                        _parent.DeviceUpdated();
                    }

                    BEGIN_INTERFACE_MAP(DeviceCallback)
                        INTERFACE_ENTRY(Exchange::IBluetooth::IDevice::ICallback)
                    END_INTERFACE_MAP

                private:
                    ServiceDiscovery& _parent;
                }; // class DeviceCallback

            public:
                ServiceDiscovery() = delete;
                ServiceDiscovery(const ServiceDiscovery&) = delete;
                ServiceDiscovery& operator=(const ServiceDiscovery&) = delete;

                static Core::NodeId Designator(const uint8_t type, const string& address)
                {
                    return (Bluetooth::Address(address.c_str()).NodeId(static_cast<Bluetooth::Address::type>(type), 0, SDPSocket::SDP_PSM));
                }

                ServiceDiscovery(AudioSink* parent, Exchange::IBluetooth::IDevice* device)
                    : Bluetooth::SDPSocket(Designator(device->Type(), device->LocalId()), Designator(device->Type(), device->RemoteId()), 255)
                    , _parent(*parent)
                    , _device(device)
                    , _callback(this)
                    , _lock()
                    , _command()
                {
                    ASSERT(parent != nullptr);
                    ASSERT(device != nullptr);

                    _device->AddRef();

                    if (_device->IsConnected() == true) {
                        TRACE(Trace::Fatal, (_T("The device is already connected")));
                    }

                    if (_device->Callback(&_callback) != Core::ERROR_NONE) {
                        TRACE(Trace::Fatal, (_T("The device is already in use")));
                    }
                }
                ~ServiceDiscovery() override
                {
                    if (SDPSocket::IsOpen() == true) {
                        SDPSocket::Close(Core::infinite);
                    }

                    if (_device->Callback(static_cast<Exchange::IBluetooth::IDevice::ICallback*>(nullptr)) != Core::ERROR_NONE) {
                        TRACE(Trace::Fatal, (_T("Could not remove the callback from the device")));
                    }

                    _device->Release();
                }

            private:
                bool Initialize() override
                {
                    return (true);
                }

                void Operational() override
                {
                    TRACE(SDPFlow, (_T("The Bluetooth device is operational; start discovery!")));

                    _explorer.Discover(CommunicationTimeout * 20, *this, { Bluetooth::Explorer::AudioSink }, [&](const uint32_t result) {
                        if (result == Core::ERROR_NONE) {
                            TRACE(SDPFlow, (_T("Found %d audio sink service(s)"), _explorer.Services().size()));
                            uint16_t cnt = 1;
                            for (auto& service : _explorer.Services()) {
                                TRACE(SDPFlow, (_T("Service %i"), cnt));
                                TRACE(SDPFlow, (_T("  Handle: 0x%08x"), service.ServiceRecordHandle()));
                                for (auto& classId : service.ServiceClassIDList()) {
                                    TRACE(SDPFlow, (_T("  Class ID: %s"), classId.ToString().c_str()));
                                }
                                for (auto& profileDescriptor : service.BluetoothProfileDescriptorList()) {
                                    TRACE(SDPFlow, (_T("  Profile: %s, version: %04x"), profileDescriptor.first.ToString().c_str(), profileDescriptor.second));
                                }
                            }
                        } else {
                            TRACE(Trace::Error, (_T("SDP discovery failed [%d]"), result));
                        }
                    });
                }

            private:
                void DeviceUpdated()
                {
                    _lock.Lock();
                    if (_device->IsConnected() == true) {
                        if (IsOpen() == false) {
                            uint32_t result = SDPSocket::Open(5000);
                            if (result != Core::ERROR_NONE) {
                                TRACE(Trace::Error, (_T("Failed to open SDP socket to %s"), _device->RemoteId().c_str()));
                            } else {
                                printf("socket open!\n");
                                TRACE(SDPFlow, (_T("Successfully opened SDP socket to %s"), _device->RemoteId().c_str()));
                            }
                        }
                    }
                    _lock.Unlock();
                }

            private:
                AudioSink& _parent;
                Exchange::IBluetooth::IDevice* _device;
                Core::Sink<DeviceCallback> _callback;
                Core::CriticalSection _lock;
                Bluetooth::SDPSocket::Command _command;
                Bluetooth::Explorer _explorer;
            }; // class ServiceDiscovery

        public:
            AudioSink(const AudioSink&) = delete;
            AudioSink& operator= (const AudioSink&) = delete;
            AudioSink(BluetoothAudio* parent)
                : _parent(*parent)
                , _sdp(nullptr)
            {
                ASSERT(parent != nullptr);
            }
            ~AudioSink() override
            {
            }

        public:
            // IBluetoothAudio overrides
            uint32_t Assign(const string& device) override;
            uint32_t Revoke(const string& device) override;

        public:
            BEGIN_INTERFACE_MAP(AudioSink)
                INTERFACE_ENTRY(Exchange::IBluetoothAudioSink)
            END_INTERFACE_MAP

        private:
            BluetoothAudio& _parent;
            ServiceDiscovery* _sdp;
        }; // class AudioSink

    public:
        BluetoothAudio(const BluetoothAudio&) = delete;
        BluetoothAudio& operator= (const BluetoothAudio&) = delete;
        BluetoothAudio()
            : _lock()
            , _service(nullptr)
            , _audioSink(nullptr)
            , _controller()

        {
        }
        ~BluetoothAudio() = default;

    public:
        // IPlugin overrides
        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override { return {}; }

    public:
        Exchange::IBluetooth* Controller() const
        {
            return (_service->QueryInterfaceByCallsign<Exchange::IBluetooth>(_controller));
        }

    public:
        BEGIN_INTERFACE_MAP(BluetoothAudio)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::IBluetoothAudioSink, _audioSink)
        END_INTERFACE_MAP

    private:
        mutable Core::CriticalSection _lock;
        PluginHost::IShell* _service;
        Exchange::IBluetoothAudioSink* _audioSink;
        string _controller;
    }; // class BluetoothAudio

} //namespace Plugin

}
