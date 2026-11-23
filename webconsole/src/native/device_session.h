#pragma once
// DISKMODE independent native hardware session. All ownership must be granted
// by an external verified stock/SampQOL handoff, never guessed from device names.
#include "hardware_bridge.h"
#include <cstdint>
#include <string>
#include <vector>
namespace dm::native {
struct OwnershipAuthority {
    virtual ~OwnershipAuthority()=default;
    // True only after an independently verified exclusive handoff, including
    // stock-service suspension and a registered rollback/restart plan.
    virtual bool granted(std::string& failure)=0;
};
struct RefuseOwnership final : OwnershipAuthority {
    bool granted(std::string& failure) override {
        failure="No verified stock/SampQOL ownership handoff";return false;
    }
};
struct ScreenBackend {
    virtual ~ScreenBackend()=default;
    // Must capture prior state and arrange restoration BEFORE activating DRM.
    virtual bool acquire(std::string& reason)=0;
    virtual bool submit(const std::vector<std::uint8_t>& xrgb8888,std::string& reason)=0;
    virtual void release() noexcept=0;
};
class DisplaySession {
public:
    DisplaySession(OwnershipAuthority& authority,ScreenBackend& backend)
        : authority_(authority),backend_(backend) {}
    ~DisplaySession(){stop();}
    DisplaySession(const DisplaySession&)=delete;
    DisplaySession& operator=(const DisplaySession&)=delete;
    bool start(std::string& reason);
    bool present(const std::vector<std::uint8_t>& frame,std::string& reason);
    void stop() noexcept;
    bool active() const{return active_;}
private:
    OwnershipAuthority& authority_;
    ScreenBackend& backend_;
    bool active_=false;
};
// An incremental parser accepts only bounded, 3-byte channel messages.
class MidiByteDecoder {
public:
    bool feed(std::uint8_t byte,SurfaceEvent& event);
    void reset(){status_=data0_=0;count_=0;sysex_=false;}
private:
    std::uint8_t status_=0,data0_=0;
    int count_=0;
    bool sysex_=false;
};
// An ALSA raw-MIDI input reader; available only with optional libasound.
// Defaults to no access and never opens an output port.
class AlsaSurfaceInput {
public:
    AlsaSurfaceInput()=default;
    ~AlsaSurfaceInput(){close();}
    AlsaSurfaceInput(const AlsaSurfaceInput&)=delete;
    AlsaSurfaceInput& operator=(const AlsaSurfaceInput&)=delete;
    // Discover an ALSA card by ID "Surface" or name "Control Surface",
    // then enumerate its input-capable raw MIDI devices/subdevices.
    bool open(OwnershipAuthority& authority,std::string& reason);
    // Nonblocking reads, calls mapper only for complete 3-byte channel messages.
    std::size_t poll(SurfaceMapper& mapper,Controller& controller,std::string& reason);
    void close() noexcept;
    bool active() const{return handle_!=nullptr;}
private:
    OwnershipAuthority* authority_=nullptr;
    void* handle_=nullptr; // snd_rawmidi_t* without forcing ALSA headers on cross builds
    MidiByteDecoder decoder_;
};
} // namespace dm::native
