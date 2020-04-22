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

#include <bitset>

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
            class ServiceExplorer : public Bluetooth::SDPSocket {
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

                    DeviceCallback(ServiceExplorer* parent)
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
                    ServiceExplorer& _parent;
                }; // class DeviceCallback

            public:
                ServiceExplorer() = delete;
                ServiceExplorer(const ServiceExplorer&) = delete;
                ServiceExplorer& operator=(const ServiceExplorer&) = delete;

                static Core::NodeId Designator(const uint8_t type, const string& address)
                {
                    return (Bluetooth::Address(address.c_str()).NodeId(static_cast<Bluetooth::Address::type>(type), 0, SDPSocket::SDP_PSM));
                }

                ServiceExplorer(AudioSink* parent, Exchange::IBluetooth::IDevice* device)
                    : Bluetooth::SDPSocket(Designator(device->Type(), device->LocalId()), Designator(device->Type(), device->RemoteId()), 255)
                    , _parent(*parent)
                    , _device(device)
                    , _callback(this)
                    , _lock()
                    , _command()
                    , _profile(*this)
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
                ~ServiceExplorer() override
                {
                    if (SDPSocket::IsOpen() == true) {
                        SDPSocket::Close(Core::infinite);
                    }

                    if (_device->Callback(static_cast<Exchange::IBluetooth::IDevice::ICallback*>(nullptr)) != Core::ERROR_NONE) {
                        TRACE(Trace::Fatal, (_T("Could not remove the callback from the device")));
                    }

                    _device->Release();

                    TRACE(SDPFlow, (_T("Service discovery closed")));
                }

            private:
                bool Initialize() override
                {
                    return (true);
                }

                void Operational() override
                {
                    TRACE(SDPFlow, (_T("The Bluetooth device is operational for service discovery!")));
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
                                TRACE(SDPFlow, (_T("Successfully opened SDP socket to %s"), _device->RemoteId().c_str()));
                                Discover();
                            }
                        }
                    } else {
                        if (SDPSocket::IsOpen() == true) {
                            SDPSocket::Close(Core::infinite);
                        }
                    }
                    _lock.Unlock();
                }
                void Discover()
                {
                    _profile.Discover(CommunicationTimeout * 20, Bluetooth::A2DPProfile::AudioService::SINK, [&](const uint32_t result) {
                        if (result == Core::ERROR_NONE) {
                            TRACE(SDPFlow, (_T("Service discovery complete")));

                            DumpProfile(_profile);
                            uint16_t psm = 0;

                            for (auto const& service : _profile.AudioServices()) {
                                if (service.Type() == Bluetooth::A2DPProfile::AudioService::SINK) {
                                    TRACE(Trace::Information, (_T("Audio sink service available! A2DP v%d.%d, AVDTP v%d.%d, L2CAP PSM: %i, features: 0b%s"),
                                                            (service.ProfileVersion() >> 8), (service.ProfileVersion() & 0xFF),
                                                            (service.TransportVersion() >> 8), (service.TransportVersion() & 0xFF),
                                                            service.PSM(), std::bitset<8>(service.Features()).to_string().c_str()));
                                    psm = service.PSM();
                                    break;
                                }
                            }

                            if (psm != 0) {
                                _parent.PSM(psm);
                            } else {
                                TRACE(Trace::Information, (_T("Not an A2DP audio sink device!")));
                            }

                        } else {
                            TRACE(Trace::Error, (_T("SDP service discovery failed [%d]"), result));
                        }
                    });
                }
                void DumpProfile(const Bluetooth::A2DPProfile& profile) const
                {
                    TRACE(SDPFlow, (_T("Found %d service(s) conforming to '%s' profile"), profile.Services().size(), profile.Class().Name().c_str()));

                    uint16_t cnt = 1;
                    for (auto const& service : profile.Services()) {
                        TRACE(SDPFlow, (_T("Service %i"), cnt));
                        TRACE(SDPFlow, (_T("  Handle: 0x%08x"), service.Handle()));

                        if (service.Classes().empty() == false) {
                            TRACE(SDPFlow, (_T("  Classes:")));
                            for (auto const& clazz : service.Classes()) {
                                TRACE(SDPFlow, (_T("    - %s '%s'"),
                                                clazz.Type().ToString().c_str(), clazz.Name().c_str()));
                            }
                        }
                        if (service.Profiles().empty() == false) {
                            TRACE(SDPFlow, (_T("  Profiles:")));
                            for (auto const& profile : service.Profiles()) {
                                TRACE(SDPFlow, (_T("    - %s '%s', version: %d.%d"),
                                                profile.Type().ToString().c_str(), profile.Name().c_str(),
                                                (profile.Version() >> 8), (profile.Version() & 0xFF)));
                            }
                        }
                        if (service.Protocols().empty() == false) {
                            TRACE(SDPFlow, (_T("  Protocols:")));
                            for (auto const& protocol : service.Protocols()) {
                                TRACE(SDPFlow, (_T("    - %s '%s', parameters: %s"),
                                                protocol.Type().ToString().c_str(), protocol.Name().c_str(),
                                                protocol.Parameters().ToString().c_str()));
                            }
                        }
                    }
                }

            private:
                AudioSink& _parent;
                Exchange::IBluetooth::IDevice* _device;
                Core::Sink<DeviceCallback> _callback;
                Core::CriticalSection _lock;
                Bluetooth::SDPSocket::Command _command;
                Bluetooth::A2DPProfile _profile;
            }; // class ServiceExplorer

            class AudioReceiver : public Bluetooth::AVDTPSocket {
            private:
                class AVDTPFlow {
                public:
                    ~AVDTPFlow() = default;
                    AVDTPFlow() = delete;
                    AVDTPFlow(const AVDTPFlow&) = delete;
                    AVDTPFlow& operator=(const AVDTPFlow&) = delete;
                    AVDTPFlow(const TCHAR formatter[], ...)
                    {
                        va_list ap;
                        va_start(ap, formatter);
                        Trace::Format(_text, formatter, ap);
                        va_end(ap);
                    }
                    explicit AVDTPFlow(const string& text)
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
                }; // class AVDTPFlow

                class DeviceCallback : public Exchange::IBluetooth::IDevice::ICallback {
                public:
                    DeviceCallback() = delete;
                    DeviceCallback(const DeviceCallback&) = delete;
                    DeviceCallback& operator=(const DeviceCallback&) = delete;

                    DeviceCallback(AudioReceiver* parent)
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
                    AudioReceiver& _parent;
                }; // class DeviceCallback

            public:
                AudioReceiver() = delete;
                AudioReceiver(const AudioReceiver&) = delete;
                AudioReceiver& operator=(const AudioReceiver&) = delete;

                static Core::NodeId Designator(const uint8_t type, const string& address, uint16_t psm)
                {
                    return (Bluetooth::Address(address.c_str()).NodeId(static_cast<Bluetooth::Address::type>(type), 0, psm));
                }

                AudioReceiver(AudioSink* parent, Exchange::IBluetooth::IDevice* device, const uint16_t psm)
                    : Bluetooth::AVDTPSocket(Designator(device->Type(), device->LocalId(), psm), Designator(device->Type(), device->RemoteId(), psm), 255)
                    , _parent(*parent)
                    , _device(device)
                    , _callback(this)
                    , _lock()
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
                ~AudioReceiver() override
                {
                    if (_device->Callback(static_cast<Exchange::IBluetooth::IDevice::ICallback*>(nullptr)) != Core::ERROR_NONE) {
                        TRACE(Trace::Fatal, (_T("Could not remove the callback from the device")));
                    }

                    _device->Release();

                    TRACE(AVDTPFlow, (_T("Audio transport closed")));
                }

            private:
                bool Initialize() override
                {
                    return (true);
                }

                void Operational() override
                {
                    TRACE(AVDTPFlow, (_T("The Bluetooth device is operational; start audio transmission!")));
                }

            private:
                void DeviceUpdated()
                {
                    _lock.Lock();
                    if (_device->IsConnected() == true) {
                        if (IsOpen() == false) {
                            uint32_t result = AVDTPSocket::Open(5000);
                            if (result != Core::ERROR_NONE) {
                                TRACE(Trace::Error, (_T("Failed to open AVDTP socket to %s"), _device->RemoteId().c_str()));
                            } else {
                                TRACE(AVDTPFlow, (_T("Successfully opened AVDTP socket to %s"), _device->RemoteId().c_str()));
                            }
                        }
                    } else {
                        if (AVDTPSocket::IsOpen() == true) {
                            AVDTPSocket::Close(Core::infinite);
                        }
                    }
                    _lock.Unlock();
                }

            private:
                AudioSink& _parent;
                Exchange::IBluetooth::IDevice* _device;
                Core::Sink<DeviceCallback> _callback;
                Core::CriticalSection _lock;
            }; // class AudioReceiver

        public:
            AudioSink(const AudioSink&) = delete;
            AudioSink& operator= (const AudioSink&) = delete;
            AudioSink(BluetoothAudio* parent)
                : _parent(*parent)
                , _sdp(nullptr)
                , _avdtp(nullptr)
                , _avdtpPsm(0)
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
            void PSM(const uint16_t psm)
            {
                _avdtpPsm = psm;
            }

        public:
            BEGIN_INTERFACE_MAP(AudioSink)
                INTERFACE_ENTRY(Exchange::IBluetoothAudioSink)
            END_INTERFACE_MAP

        private:
            BluetoothAudio& _parent;
            ServiceExplorer* _sdp;
            AudioReceiver* _avdtp;
            uint32_t _avdtpPsm;
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
