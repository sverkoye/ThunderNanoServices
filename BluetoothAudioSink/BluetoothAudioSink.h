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

#include "Module.h"


namespace WPEFramework {

namespace Plugin {

    class BluetoothAudioSink : public PluginHost::IPlugin
                             , public PluginHost::JSONRPC
                             , public Exchange::IBluetoothAudioSink {
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

        class DispatchJob {
        private:
            class Job : public Core::IDispatch {
            public:
                Job(DispatchJob& parent)
                    :_parent(parent)
                {
                }
                ~Job() = default;

            public:
                void Dispatch() override
                {
                    _parent.Trigger();
                }

            private:
                DispatchJob& _parent;
            };

        public:
            using Handler = std::function<void()>;

        public:
            DispatchJob(const DispatchJob&) = delete;
            DispatchJob& operator=(const DispatchJob&) = delete;

            DispatchJob(const Handler& handler)
                : _handler(handler)
                , _job(Core::ProxyType<Job>::Create(*this))
            {
                Core::IWorkerPool::Instance().Submit(Core::ProxyType<Core::IDispatch>(_job));
            }

        private:
           ~DispatchJob() = default;

            void Trigger()
            {
                _handler();
                delete this;
            }

        private:
            Handler _handler;
            Core::ProxyType<Job> _job;
        }; // class DispatchJob

        class A2DPSink {
        private:
            class A2DPFlow {
            public:
                ~A2DPFlow() = default;
                A2DPFlow() = delete;
                A2DPFlow(const A2DPFlow&) = delete;
                A2DPFlow& operator=(const A2DPFlow&) = delete;
                A2DPFlow(const TCHAR formatter[], ...)
                {
                    va_list ap;
                    va_start(ap, formatter);
                    Trace::Format(_text, formatter, ap);
                    va_end(ap);
                }
                explicit A2DPFlow(const string& text)
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
            }; // class A2DPFlow

            class DeviceCallback : public Exchange::IBluetooth::IDevice::ICallback {
            public:
                DeviceCallback() = delete;
                DeviceCallback(const DeviceCallback&) = delete;
                DeviceCallback& operator=(const DeviceCallback&) = delete;

                DeviceCallback(A2DPSink* parent)
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
                A2DPSink& _parent;
            }; // class DeviceCallback

            static Core::NodeId Designator(const Exchange::IBluetooth::IDevice* device, const bool local, const uint16_t psm = 0)
            {
                ASSERT(device != nullptr);
                return (Bluetooth::Address((local? device->LocalId() : device->RemoteId()).c_str()).NodeId(static_cast<Bluetooth::Address::type>(device->Type()), 0 /* must be zero */, psm));
            }

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

            public:
                using ClassID = Bluetooth::SDPProfile::ClassID;

                class AudioService {
                private:
                    enum attributeid : uint16_t {
                        SupportedFeatures = 0x0311
                    };

                public:
                    enum type {
                        UNKNOWN = 0,
                        SOURCE  = 1,
                        SINK    = 2,
                        NEITHER = 3
                    };

                    enum features : uint16_t {
                        NONE        = 0,
                        HEADPHONE   = (1 << 1),
                        SPEAKER     = (1 << 2),
                        RECORDER    = (1 << 3),
                        AMPLIFIER   = (1 << 4),
                        PLAYER      = (1 << 5),
                        MICROPHONE  = (1 << 6),
                        TUNER       = (1 << 7),
                        MIXER       = (1 << 8)
                    };

                public:
                    AudioService()
                        : _l2capPsm(0)
                        , _avdtpVersion(0)
                        , _a2dpVersion(0)
                        , _features(NONE)
                        , _type(UNKNOWN)
                    {
                    }
                    AudioService(const Bluetooth::SDPProfile::Service& service)
                        : AudioService()
                    {
                        using SDPProfile = Bluetooth::SDPProfile;

                        _type = NEITHER;

                        const SDPProfile::ProfileDescriptor* a2dp = service.Profile(ClassID::AdvancedAudioDistribution);
                        ASSERT(a2dp != nullptr);
                        if (a2dp != nullptr) {
                            _a2dpVersion = a2dp->Version();
                            ASSERT(_a2dpVersion != 0);

                            const SDPProfile::ProtocolDescriptor* l2cap = service.Protocol(ClassID::L2CAP);
                            ASSERT(l2cap != nullptr);
                            if (l2cap != nullptr) {
                                SDPSocket::Record params(l2cap->Parameters());
                                params.Pop(SDPSocket::use_descriptor, _l2capPsm);
                                ASSERT(_l2capPsm != 0);

                                const SDPProfile::ProtocolDescriptor* avdtp = service.Protocol(ClassID::AVDTP);
                                ASSERT(avdtp != nullptr);
                                if (avdtp != nullptr) {
                                    SDPSocket::Record params(avdtp->Parameters());
                                    params.Pop(SDPSocket::use_descriptor, _avdtpVersion);
                                    ASSERT(_avdtpVersion != 0);

                                    // It's a A2DP service using L2CAP and AVDTP protocols; finally confirm its class ID
                                    if (service.IsClassSupported(ClassID::AudioSink)) {
                                        _type = SINK;
                                    } else if (service.IsClassSupported(ClassID::AudioSource)) {
                                        _type = SOURCE;
                                    }

                                    // This one is optional...
                                    const SDPProfile::Service::AttributeDescriptor* supportedFeatures = service.Attribute(SupportedFeatures);
                                    if (supportedFeatures != nullptr) {
                                        SDPSocket::Record value(supportedFeatures->Value());
                                        value.Pop(SDPSocket::use_descriptor, _features);
                                        if (service.IsClassSupported(ClassID::AudioSource)) {
                                            _features = static_cast<features>((static_cast<uint8_t>(_features) << 4));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ~AudioService() = default;

                public:
                    type Type() const
                    {
                        return (_type);
                    }
                    uint16_t PSM() const
                    {
                        return (_l2capPsm);
                    }
                    uint16_t TransportVersion() const
                    {
                        return (_avdtpVersion);
                    }
                    uint16_t ProfileVersion() const
                    {
                        return (_a2dpVersion);
                    }
                    features Features() const
                    {
                        return (_features);
                    }

                private:
                    Bluetooth::UUID _class;
                    uint16_t _l2capPsm;
                    uint16_t _avdtpVersion;
                    uint16_t _a2dpVersion;
                    features _features;
                    type _type;
                }; // class AudioService

            public:
                ServiceExplorer() = delete;
                ServiceExplorer(const ServiceExplorer&) = delete;
                ServiceExplorer& operator=(const ServiceExplorer&) = delete;

                ServiceExplorer(A2DPSink* parent, Exchange::IBluetooth::IDevice* device, const uint16_t psm = SDP_PSM /* a well-known PSM */)
                    : Bluetooth::SDPSocket(Designator(device, true, psm), Designator(device, false, psm), 255)
                    , _parent(*parent)
                    , _device(device)
                    , _lock()
                    , _profile(ClassID::AdvancedAudioDistribution)
                {
                    ASSERT(parent != nullptr);
                    ASSERT(device != nullptr);

                    _device->AddRef();
                }
                ~ServiceExplorer() override
                {
                    Disconnect();
                    _device->Release();
                }

            private:
                bool Initialize() override
                {
                    _audioServices.clear();
                    return (true);
                }

                void Operational() override
                {
                    TRACE(SDPFlow, (_T("Bluetooth SDP connection is operational")));
                }

            public:
                 uint32_t Connect()
                {
                    uint32_t result = Open(1000);
                    if (result != Core::ERROR_NONE) {
                        TRACE(Trace::Error, (_T("Failed to open SDP socket to %s [%d]"), _device->RemoteId().c_str(), result));
                    } else {
                        TRACE(SDPFlow, (_T("Successfully opened SDP socket to %s"), _device->RemoteId().c_str()));
                    }

                    return (result);
                }
                uint32_t Disconnect()
                {
                    uint32_t result = Core::ERROR_NONE;

                    if (IsOpen() == true) {
                        result = Close(5000);
                        if (result != Core::ERROR_NONE) {
                            TRACE(Trace::Error, (_T("Failed to close SDP socket to %s [%d]"), _device->RemoteId().c_str(), result));
                        } else {
                            TRACE(SDPFlow, (_T("Successfully closed SDP socket to %s"), _device->RemoteId().c_str()));
                        }
                    }

                    return (result);
                }
                void Discover()
                {
                    if (SDPSocket::IsOpen() == true) {
                        _profile.Discover((CommunicationTimeout * 20), *this, std::list<Bluetooth::UUID>{ ClassID::AudioSink }, [&](const uint32_t result) {
                            if (result == Core::ERROR_NONE) {
                                TRACE(SDPFlow, (_T("Service discovery complete")));

                                _lock.Lock();

                                DumpProfile();

                                if (_profile.Services().empty() == false) {
                                    for (auto const& service : _profile.Services()) {
                                        if (service.IsClassSupported(ClassID::AudioSink) == true) {
                                            _audioServices.emplace_back(service);
                                        }
                                    }

                                    new DispatchJob([this]() {
                                        _parent.AudioServices(_audioServices);
                                    });
                                } else {
                                    TRACE(Trace::Information, (_T("Not an A2DP audio sink device!")));
                                }

                                _lock.Unlock();
                            } else {
                                TRACE(Trace::Error, (_T("SDP service discovery failed [%d]"), result));
                            }
                        });
                    }
                }

            private:
                void DumpProfile() const
                {
                    TRACE(SDPFlow, (_T("Discovered %d service(s)"), _profile.Services().size()));

                    uint16_t cnt = 1;
                    for (auto const& service : _profile.Services()) {
                        TRACE(SDPFlow, (_T("Service #%i"), cnt++));
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
                        if (service.Attributes().empty() == false) {
                            TRACE(SDPFlow, (_T("  Attributes:")));
                            for (auto const& attribute : service.Attributes()) {
                                TRACE(SDPFlow, (_T("    - %04x '%s', value: %s"),
                                                attribute.second.Type(), attribute.second.Name().c_str(),
                                                attribute.second.Value().ToString().c_str()));
                            }
                        }
                    }
                }

            private:
                A2DPSink& _parent;
                Exchange::IBluetooth::IDevice* _device;
                Core::CriticalSection _lock;
                Bluetooth::SDPProfile _profile;
                std::list<AudioService> _audioServices;
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

            public:
                enum contentprotection : uint16_t {
                    NONE    = 0x0000,
                    DTC     = 0x0001,
                    SCMS_T  = 0x0002
                };

                enum mediatype : uint8_t {
                    AUDIO           = 0x00
                };

                enum audiocodec : uint8_t {
                    SBC             = 0x00, // mandatory
                    MPEG2_AUDIO     = 0x01,
                    MPEG4_AAC       = 0x02,
                    ATRAC_FAMILY    = 0x03,
                    INVALID         = 0xFE,
                    NON_A2DP        = 0xFF
                };

                class SBCAudioSEP {
                public:
                    struct Configuration {
                    public:
                        enum samplingfrequency : uint8_t {
                            HZ_48000        = 1, // mandatory for sink
                            HZ_44100        = 2, // mandatory for sink
                            HZ_32000        = 4,
                            HZ_16000        = 8
                        };

                        enum channelmode : uint8_t {
                            JOINT_STEREO    = 1, // all mandatory for sink
                            STEREO          = 2,
                            DUAL_CHANNEL    = 4,
                            MONO            = 8
                        };

                        enum blocklength : uint8_t {
                            BL16            = 1,  // all mandatory for sink
                            BL12            = 2,
                            BL8             = 4,
                            BL4             = 8,
                        };

                        enum subbands : uint8_t {
                            SB8             = 1, // all mandatory for sink
                            SB4             = 2,
                        };

                        enum allocationmethod : uint8_t {
                            LOUDNESS        = 1, // all mandatory for sink
                            SNR             = 2,
                        };

                    public:
                        Configuration()
                            : _samplingFrequency()
                            , _channelMode()
                            , _blockLength()
                            , _subBands()
                            , _allocationMethod()
                            , _minBitpool(2)
                            , _maxBitpool(250)
                        {
                        }
                        Configuration(const samplingfrequency sf, const channelmode cm, const blocklength bl,
                                      const subbands sb, const allocationmethod am, const uint8_t minBp, const uint8_t maxBp)
                            : _samplingFrequency(sf)
                            , _channelMode(cm)
                            , _blockLength(bl)
                            , _subBands(sb)
                            , _allocationMethod(am)
                            , _minBitpool(minBp)
                            , _maxBitpool(maxBp)
                        {
                        }
                        ~Configuration() = default;

                    public:
                        void Serialize(string& params) const
                        {
                            uint8_t scratchpad[16];
                            Bluetooth::Record msg(scratchpad, sizeof(scratchpad));
                            msg.Push(AUDIO);
                            msg.Push(SBC);
                            uint8_t data{};
                            data = ((static_cast<uint8_t>(_samplingFrequency) << 4) | static_cast<uint8_t>(_channelMode));
                            msg.Push(data);
                            data = ((static_cast<uint8_t>(_blockLength) << 4) | (static_cast<uint8_t>(_subBands) << 2) | static_cast<uint8_t>(_allocationMethod));
                            msg.Push(data);
                            msg.Push(_minBitpool);
                            msg.Push(_maxBitpool);
                            msg.Export(params);

                        }
                        void Deserialize(const string& params)
                        {
                            Bluetooth::Record data(params);
                            uint8_t byte{};
                            data.Pop(byte); // AUDIO
                            data.Pop(byte); // SBC
                            data.Pop(byte);
                            _samplingFrequency = (byte >> 4);
                            _channelMode = (byte & 0xFF);
                            data.Pop(byte);
                            _blockLength = (byte >> 4);
                            _subBands = ((byte >> 2) & 0x3);
                            _allocationMethod = (byte & 0x3);
                            data.Pop(_minBitpool);
                            data.Pop(_maxBitpool);
                        }
                        uint8_t SamplingFrequency() const
                        {
                            return (_samplingFrequency);
                        }
                        uint8_t ChannelMode() const
                        {
                            return (_channelMode);
                        }
                        uint8_t BlockLength() const
                        {
                            return (_blockLength);
                        }
                        uint8_t SubBands() const
                        {
                            return (_subBands);
                        }
                        uint8_t AllocationMethod() const
                        {
                            return (_allocationMethod);
                        }
                        std::pair<uint8_t, uint8_t> Bitpool() const
                        {
                            return (std::make_pair(_minBitpool, _maxBitpool));
                        }

                    private:
                        uint8_t _samplingFrequency;
                        uint8_t _channelMode;
                        uint8_t _blockLength;
                        uint8_t _subBands;
                        uint8_t _allocationMethod;
                        uint8_t _minBitpool;
                        uint8_t _maxBitpool;
                    };

                public:
                    SBCAudioSEP(AudioReceiver& parent, const Bluetooth::AVDTPProfile::StreamEndPoint& sep, const string& params)
                        : _socket(parent)
                        , _command()
                        , _seid(sep.SEID())
                        , _cp(NONE)
                        , _supported()
                        , _actuals()
                    {
                        auto cpIt = sep.Capabilities().find(Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::CONTENT_PROTECTION);
                        if (cpIt != sep.Capabilities().end()) {
                            _cp = static_cast<contentprotection>((*cpIt).second.Data()[0] | ((*cpIt).second.Data()[1] << 8));
                        }

                        _supported.Deserialize(params);
                   }

                public:
                    void Establish()
                    {
                        Configuration newConfig(Configuration::HZ_44100, Configuration::JOINT_STEREO,
                                                Configuration::BL16, Configuration::SB4, Configuration::LOUDNESS, 2, 0x35);

                        SetConfiguration(_cp, newConfig);
                    }

                private:
                    void SetConfiguration(const contentprotection cp, const Configuration& config)
                    {
                        std::map<uint8_t, string> caps;
                        string buffer;
                        uint8_t scratchpad[256];
                        Bluetooth::RecordLE msg(scratchpad, sizeof(scratchpad));

                        // Media Transport, always empty
                        caps.emplace(Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::MEDIA_TRANSPORT, string());

                        // Content Protection, select variant
                        if (cp != NONE) {
                            msg.Clear();
                            msg.Push(cp);
                            msg.Export(buffer);
                            caps.emplace(Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::CONTENT_PROTECTION, buffer);
                        }

                        // Media Codec capabilities
                        msg.Clear();
                        string params;
                        config.Serialize(params);
                        msg.Push(params);
                        msg.Export(buffer);
                        caps.emplace(Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::MEDIA_CODEC, buffer);

                        _command.SetConfiguration(_seid, 1, caps);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                GetConfiguration();
                            } else {
                                TRACE(Trace::Error, (_T("Failed to set configuration of SEID 0x%02x"), _seid));
                            }
                        });
                    }
                    void GetConfiguration()
                    {
                        _command.GetConfiguration(_seid);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                cmd.Result().ReadCapabilities([this](const uint8_t category, const string& data) {
                                    switch(category) {
                                    case Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::CONTENT_PROTECTION:
                                        _cp = static_cast<contentprotection>(data[0] | data[1] << 8);
                                        break;
                                    case Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::MEDIA_CODEC:
                                        _actuals.Deserialize(data);
                                        break;
                                    }
                                });

                                DumpConfiguration();
                                _socket.Status(Exchange::IBluetoothAudioSink::CONFIGURED);

                                Open();
                            } else {
                                TRACE(Trace::Error, (_T("Failed to read configuration of SEID 0x%02x"), _seid));
                            }
                        });
                    }
                    void Open()
                    {
                        _command.Open(_seid);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                _socket.Status(Exchange::IBluetoothAudioSink::OPEN);
                            } else {
                                TRACE(Trace::Error, (_T("Failed to open SEID 0x%02x"), _seid));
                            }
                        });
                    }
                    void Close()
                    {
                        _command.Close(_seid);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                _socket.Status(Exchange::IBluetoothAudioSink::CONFIGURED);
                            } else {
                                TRACE(Trace::Error, (_T("Failed to close SEID 0x%02x"), _seid));
                            }
                        });
                    }
                    void Start()
                    {
                        _command.Start(_seid);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                _socket.Status(Exchange::IBluetoothAudioSink::STREAMING);
                            } else {
                                TRACE(Trace::Error, (_T("Failed to start SEID 0x%02x"), _seid));
                            }
                        });
                    }
                    void Suspend()
                    {
                        _command.Start(_seid);
                        _socket.Execute(2000, _command, [&](const AVDTPSocket::Command& cmd) {
                            if ((cmd.Status() == Core::ERROR_NONE) && (cmd.Result().Status() == AVDTPSocket::Command::Message::SUCCESS)) {
                                _socket.Status(Exchange::IBluetoothAudioSink::OPEN);
                            } else {
                                TRACE(Trace::Error, (_T("Failed to suspend SEID 0x%02x"), _seid));
                            }
                        });
                    }

                public:
                    contentprotection ContentProtection() const
                    {
                        return (_cp);
                    }

                private:
                    void DumpConfiguration() const
                    {
                        static const char* cpStr[] = { "None", "DTCP", "SCMS-T" };
                        #define ELEM(name, val, prop) (_T("  [  %d] " name " [  %d]"), !!(_supported.val() & Configuration::prop), !!(_actuals.val() & Configuration::prop))
                        TRACE(AVDTPFlow, (_T("SBC configuration:")));
                        TRACE(AVDTPFlow, ELEM("Sampling frequency - 16 kHz     ", SamplingFrequency, HZ_16000));
                        TRACE(AVDTPFlow, ELEM("Sampling frequency - 32 kHz     ", SamplingFrequency, HZ_32000));
                        TRACE(AVDTPFlow, ELEM("Sampling frequency - 44.1 kHz   ", SamplingFrequency, HZ_44100));
                        TRACE(AVDTPFlow, ELEM("Sampling frequency - 48 kHz     ", SamplingFrequency, HZ_48000));
                        TRACE(AVDTPFlow, ELEM("Channel mode - Mono             ", ChannelMode, MONO));
                        TRACE(AVDTPFlow, ELEM("Channel mode - Stereo           ", ChannelMode, STEREO));
                        TRACE(AVDTPFlow, ELEM("Channel mode - Dual Channel     ", ChannelMode, DUAL_CHANNEL));
                        TRACE(AVDTPFlow, ELEM("Channel mode - Joint Stereo     ", ChannelMode, JOINT_STEREO));
                        TRACE(AVDTPFlow, ELEM("Block length - 4                ", BlockLength, BL4));
                        TRACE(AVDTPFlow, ELEM("Block length - 8                ", BlockLength, BL8));
                        TRACE(AVDTPFlow, ELEM("Block length - 12               ", BlockLength, BL12));
                        TRACE(AVDTPFlow, ELEM("Block length - 16               ", BlockLength, BL16));
                        TRACE(AVDTPFlow, ELEM("Frequency sub-bands - 4         ", SubBands, SB4));
                        TRACE(AVDTPFlow, ELEM("Frequency sub-bands - 8         ", SubBands, SB8));
                        TRACE(AVDTPFlow, ELEM("Bit allocation method - SNR     ", AllocationMethod, SNR));
                        TRACE(AVDTPFlow, ELEM("Bit allocation method - Loudness", AllocationMethod, LOUDNESS));
                        TRACE(AVDTPFlow, (_T("  [%3d] Minimal bitpool value            [%3d]"), _supported.Bitpool().first, _actuals.Bitpool().first));
                        TRACE(AVDTPFlow, (_T("  [%3d] Maximal bitpool value            [%3d]"), _supported.Bitpool().second, _actuals.Bitpool().second));
                        TRACE(AVDTPFlow, (_T("  Content protection: %s"), (_cp > SCMS_T? "<unknown>" : cpStr[_cp])));
                        #undef ELEM
                    }

                private:
                    AudioReceiver& _socket;
                    Bluetooth::AVDTPSocket::Command _command;
                    uint8_t _seid;
                    contentprotection _cp;
                    Configuration _supported;
                    Configuration _actuals;
                }; // class SBCAudioSEP

            public:
                AudioReceiver() = delete;
                AudioReceiver(const AudioReceiver&) = delete;
                AudioReceiver& operator=(const AudioReceiver&) = delete;

                AudioReceiver(A2DPSink* parent, Exchange::IBluetooth::IDevice* device)
                    : Bluetooth::AVDTPSocket(Designator(device, true), Designator(device, false), 255)
                    , _parent(*parent)
                    , _device(device)
                    , _lock()
                    , _status(Exchange::IBluetoothAudioSink::UNASSIGNED)
                    , _profile()
                    , _sbcAudioEndpoints()
                {
                    ASSERT(parent != nullptr);
                    ASSERT(device != nullptr);

                    _device->AddRef();
                    Status(DISCONNECTED);
                }
                ~AudioReceiver() override
                {
                    Disconnect();
                    _device->Release();
                }

            private:
                bool Initialize() override
                {
                    return (true);
                }
                void Operational() override
                {
                    TRACE(AVDTPFlow, (_T("Bluetooth AVDTP connection is operational")));
                    Status(Exchange::IBluetoothAudioSink::IDLE);
                }

            public:
                void Discover() {
                    _profile.Discover(2000, *this, [&](const uint32_t result) {
                        if (result == Core::ERROR_NONE) {
                            TRACE(AVDTPFlow, (_T("Stream endpoint discovery complete")));
                            DumpProfile();

                            for (auto const& sep : _profile.StreamEndPoints()) {
                                if ((sep.MediaType() == Bluetooth::AVDTPProfile::StreamEndPoint::AUDIO)
                                        && (sep.ServiceType() == Bluetooth::AVDTPProfile::StreamEndPoint::SINK)) {

                                    auto it = sep.Capabilities().find(Bluetooth::AVDTPProfile::StreamEndPoint::ServiceCapabilities::MEDIA_CODEC);
                                    if (it != sep.Capabilities().end() && ((*it).second.Data().size() > 2)) {
                                        Bluetooth::Record data((*it).second.Data());
                                        mediatype mediaType{};
                                        data.Pop(mediaType);
                                        if ((mediaType == AUDIO) && (data.Available() >= 5)) {
                                            uint8_t codec{};
                                            data.Pop(codec);

                                            if (codec == SBC) {
                                                TRACE(Trace::Information, (_T("SBC audio sink stream endpoint available! SEID: 0x%02x"), sep.SEID()));
                                                _sbcAudioEndpoints.emplace_back(*this, sep, (*it).second.Data());
                                            }
                                        }
                                    }
                                }
                            }

                            if (_sbcAudioEndpoints.empty() == true) {
                                TRACE(Trace::Information, (_T("No SBC audio sink stream endpoints available")));
                            } else {
                                _sbcAudioEndpoints.front().Establish();
                            }
                        }
                    });
                }

            public:
                uint32_t Connect(const uint16_t psm)
                {
                    RemoteNode(Designator(_device, false, psm));

                    uint32_t result = Open(1000);
                    if (result != Core::ERROR_NONE) {
                        TRACE(Trace::Error, (_T("Failed to open AVDTP socket to %s [%d]"), _device->RemoteId().c_str(), result));
                    } else {
                        TRACE(AVDTPFlow, (_T("Successfully opened AVDTP socket to %s"), _device->RemoteId().c_str()));
                    }

                    return (result);
                }
                uint32_t Disconnect()
                {
                    uint32_t result = Core::ERROR_NONE;

                    if (IsOpen() == true) {
                        result = Close(5000);
                        if (result != Core::ERROR_NONE) {
                            TRACE(Trace::Error, (_T("Failed to close AVDTP socket to %s [%d]"), _device->RemoteId().c_str(), result));
                        } else {
                            TRACE(AVDTPFlow, (_T("Successfully closed AVDTP socket to %s"), _device->RemoteId().c_str()));
                        }
                    }

                    Status(Exchange::IBluetoothAudioSink::DISCONNECTED);

                    return (result);
                }
                Exchange::IBluetoothAudioSink::status Status() const
                {
                    return (_status);
                }

            private:
                void Status(const Exchange::IBluetoothAudioSink::status newStatus)
                {
                    _lock.Lock();
                    if (_status != newStatus) {
                        _status = newStatus;
                        Core::EnumerateType<JsonData::BluetoothAudioSink::StatusType> value(static_cast<Exchange::IBluetoothAudioSink::status>(_status));
                        TRACE(Trace::Information, (_T("Audio sink status: %s"), (value.IsSet()? value.Data() : "(undefined)")));
                    }
                    _lock.Unlock();
                }
                void DumpProfile() const
                {
                    TRACE(AVDTPFlow, (_T("Discovered %d stream endpoints(s)"), _profile.StreamEndPoints().size()));

                    uint16_t cnt = 1;
                    for (auto const& sep : _profile.StreamEndPoints()) {
                        TRACE(AVDTPFlow, (_T("Stream endpoint #%i"), cnt++));
                        TRACE(AVDTPFlow, (_T("  SEID: 0x%02x"), sep.SEID()));
                        TRACE(AVDTPFlow, (_T("  Service Type: %s"), (sep.ServiceType() == Bluetooth::AVDTPProfile::StreamEndPoint::SINK? "Sink" : "Source")));
                        TRACE(AVDTPFlow, (_T("  Media Type: %s"), (sep.MediaType() == Bluetooth::AVDTPProfile::StreamEndPoint::AUDIO? "Audio"
                                                                    : (sep.MediaType() == Bluetooth::AVDTPProfile::StreamEndPoint::VIDEO? "Video" : "Multimedia"))));

                        if (sep.Capabilities().empty() == false) {
                            TRACE(AVDTPFlow, (_T("  Capabilities:")));
                            for (auto const& caps : sep.Capabilities()) {
                                TRACE(AVDTPFlow, (_T("    - %02x '%s', parameters[%d]: %s"), caps.first, caps.second.Name().c_str(),
                                                  caps.second.Data().size(), Bluetooth::Record(caps.second.Data()).ToString().c_str()));
                            }
                        }
                    }
                }

            private:
                A2DPSink& _parent;
                Exchange::IBluetooth::IDevice* _device;
                Core::CriticalSection _lock;
                Exchange::IBluetoothAudioSink::status _status;
                Bluetooth::AVDTPProfile _profile;
                std::list<SBCAudioSEP> _sbcAudioEndpoints;
            }; // class AudioReceiver

        public:
            A2DPSink(const A2DPSink&) = delete;
            A2DPSink& operator= (const A2DPSink&) = delete;
            A2DPSink(BluetoothAudioSink* parent, Exchange::IBluetooth::IDevice* device)
                : _parent(*parent)
                , _device(device)
                , _callback(this)
                , _lock()
                , _explorer(this, _device)
                , _receiver(this, _device)
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
            ~A2DPSink()
            {
                if (_device->Callback(static_cast<Exchange::IBluetooth::IDevice::ICallback*>(nullptr)) != Core::ERROR_NONE) {
                    TRACE(Trace::Fatal, (_T("Could not remove the callback from the device")));
                }

                _device->Release();
            }

        public:
            void DeviceUpdated()
            {
                if (_device->IsBonded() == true) {
                    _lock.Lock();

                    if (_device->IsConnected() == true) {
                        if (_audioService.Type() == ServiceExplorer::AudioService::UNKNOWN) {
                            TRACE(A2DPFlow, (_T("Unknown device connected, attempt audio sink discovery...")));
                            // Device features are unknown at this point, so first try service discovery
                            if (_explorer.Connect() == Core::ERROR_NONE) {
                                _explorer.Discover();
                            }
                        } else if (_audioService.Type() == ServiceExplorer::AudioService::SINK) {
                            // We already know it's an audio sink, connect to transport service right away
                            TRACE(A2DPFlow, (_T("Audio sink device connected, start audio receiver...")));
                            if (_receiver.Connect(_audioService.PSM()) == Core::ERROR_NONE) {
                                _receiver.Discover();
                            }
                        } else {
                            // It's not an audio sink device, can't do anything
                            TRACE(Trace::Information, (_T("Connected device does not feature an audio sink!")));
                        }
                    } else {
                        TRACE(A2DPFlow, (_T("Device disconnected")));
                        _audioService = ServiceExplorer::AudioService();
                        _explorer.Disconnect();
                        _receiver.Disconnect();
                    }

                    _lock.Unlock();
                }
            }

        public:
            Exchange::IBluetoothAudioSink::status Status() const
            {
                return (_receiver.Status());
            }

        private:
            void AudioServices(const std::list<ServiceExplorer::AudioService>& services)
            {
                if (services.size() > 1) {
                    TRACE(Trace::Information, (_T("More than one audio sink available, using the first one!")));
                }

                _lock.Lock();

                _explorer.Disconnect(); // done with SDP

                 _audioService = services.front(); // disregard possibility of multiple sink services for now
                TRACE(Trace::Information, (_T("Audio sink service available! A2DP v%d.%d, AVDTP v%d.%d, L2CAP PSM: %i, features: 0b%s"),
                                           (_audioService.ProfileVersion() >> 8), (_audioService.ProfileVersion() & 0xFF),
                                           (_audioService.TransportVersion() >> 8), (_audioService.TransportVersion() & 0xFF),
                                           _audioService.PSM(), std::bitset<8>(_audioService.Features()).to_string().c_str()));

                TRACE(A2DPFlow, (_T("Audio sink device discovered, start audio receiver...")));
                if (_receiver.Connect(_audioService.PSM()) == Core::ERROR_NONE) {
                    _receiver.Discover();
                }

                _lock.Unlock();
            }

        private:
            BluetoothAudioSink& _parent;
            Exchange::IBluetooth::IDevice* _device;
            Core::Sink<DeviceCallback> _callback;
            Core::CriticalSection _lock;
            ServiceExplorer::AudioService _audioService;
            ServiceExplorer _explorer;
            AudioReceiver _receiver;
        }; // class A2DPSink

    public:
        BluetoothAudioSink(const BluetoothAudioSink&) = delete;
        BluetoothAudioSink& operator= (const BluetoothAudioSink&) = delete;
        BluetoothAudioSink()
            : _lock()
            , _service(nullptr)
            , _sink(nullptr)
            , _controller()

        {
        }
        ~BluetoothAudioSink() = default;

    public:
        // IPlugin overrides
        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override { return {}; }

    public:
        // IBluetoothAudioSink overrides
        uint32_t Assign(const string& device) override;
        uint32_t Revoke(const string& device) override;
        uint32_t Status(Exchange::IBluetoothAudioSink::status& sinkStatus) const
        {
            if (_sink) {
                sinkStatus =_sink->Status();
            } else {
                sinkStatus = Exchange::IBluetoothAudioSink::UNASSIGNED;
            }
            return (Core::ERROR_NONE);
        }

    public:
        Exchange::IBluetooth* Controller() const
        {
            return (_service->QueryInterfaceByCallsign<Exchange::IBluetooth>(_controller));
        }

    public:
        BEGIN_INTERFACE_MAP(BluetoothAudioSink)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_ENTRY(Exchange::IBluetoothAudioSink)
        END_INTERFACE_MAP

    private:
        mutable Core::CriticalSection _lock;
        PluginHost::IShell* _service;
        A2DPSink* _sink;
        string _controller;
    }; // class BluetoothAudioSink

} // namespace Plugin

}
