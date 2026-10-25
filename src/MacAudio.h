/*********************************************************************************/
/*!
@file           MacAudio.h

@brief          macOS audio output latency: the size of the output device's I/O buffer.

    On macOS the latency of the internal synthesizer is set by the output device's I/O buffer
    (512 frames, about 12 ms, by default), not by FluidSynth's period settings. A process may ask
    for a smaller buffer for its own use; this does that for the default output device, and again
    whenever the default output device changes (e.g. headphones or AirPods connected).

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

#ifndef __MAC_AUDIO_H__
#define __MAC_AUDIO_H__

namespace MacAudio
{
    //! Ask for an output buffer of `frames` frames (clamped to what the device allows) on the default
    //! output device, now and after every change of the default output device.
    //! Returns the buffer size now in use, or 0 if it could not be read.
    int setOutputBufferFrames(int frames);

    //! the I/O buffer size of the default output device, in frames (0 if unknown)
    int outputBufferFrames();
}

#endif // __MAC_AUDIO_H__
