/*********************************************************************************/
/*!
@file           MacAudio.cpp

@brief          macOS audio output latency: the size of the output device's I/O buffer.

    This file is part of the PianoBooster application

    PianoBooster is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PianoBooster is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PianoBooster.  If not, see <http://www.gnu.org/licenses/>.

*/
/*********************************************************************************/

#include "MacAudio.h"

#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <atomic>

namespace {

std::atomic<int> s_wantedFrames{0};
bool s_listening = false;

AudioObjectPropertyAddress address(AudioObjectPropertySelector selector)
{
    return {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

AudioDeviceID defaultOutputDevice()
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress property = address(kAudioHardwarePropertyDefaultOutputDevice);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &property, 0, nullptr, &size, &device) != noErr)
        return kAudioObjectUnknown;
    return device;
}

void applyBufferFrames(int frames)
{
    const AudioDeviceID device = defaultOutputDevice();
    if (device == kAudioObjectUnknown || frames <= 0)
        return;
    // stay within the range the device supports
    AudioValueRange range = {0, 0};
    UInt32 size = sizeof(range);
    AudioObjectPropertyAddress property = address(kAudioDevicePropertyBufferFrameSizeRange);
    if (AudioObjectGetPropertyData(device, &property, 0, nullptr, &size, &range) == noErr && range.mMaximum > 0)
        frames = std::max(static_cast<int>(range.mMinimum), std::min(frames, static_cast<int>(range.mMaximum)));
    UInt32 value = static_cast<UInt32>(frames);
    property = address(kAudioDevicePropertyBufferFrameSize);
    AudioObjectSetPropertyData(device, &property, 0, nullptr, sizeof(value), &value);
}

// called by CoreAudio (on its own thread) when the default output device changes
OSStatus defaultDeviceChanged(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *)
{
    applyBufferFrames(s_wantedFrames.load());
    return noErr;
}

} // namespace

int MacAudio::setOutputBufferFrames(int frames)
{
    s_wantedFrames = frames;
    applyBufferFrames(frames);
    if (!s_listening)
    {
        const AudioObjectPropertyAddress property = address(kAudioHardwarePropertyDefaultOutputDevice);
        s_listening = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &property, defaultDeviceChanged, nullptr) == noErr;
    }
    return outputBufferFrames();
}

int MacAudio::outputBufferFrames()
{
    const AudioDeviceID device = defaultOutputDevice();
    UInt32 frames = 0;
    UInt32 size = sizeof(frames);
    const AudioObjectPropertyAddress property = address(kAudioDevicePropertyBufferFrameSize);
    if (device == kAudioObjectUnknown || AudioObjectGetPropertyData(device, &property, 0, nullptr, &size, &frames) != noErr)
        return 0;
    return static_cast<int>(frames);
}
