/*
Copyright (c) 2012-2024 R. Danbrook
Copyright (c) 2020-2024 Rupert Carmichael
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdio>

#include <SDL.h>

#include <samplerate.h>

#include <jg/jg.h>

#include "audiomanager.h"
#include "jgmanager.h"

#include "logdriver.h"

namespace {

jg_audioinfo_t* audinfo{nullptr};
jg_audioinfo_t micinfo{JG_SAMPFMT_INT16, 48000, 1, 800, NULL};

SDL_AudioSpec spec, obtained;
SDL_AudioDeviceID dev;

SDL_AudioSpec spec_in, obtained_in;
SDL_AudioDeviceID dev_in;

SRC_STATE *srcstate{nullptr};
SRC_DATA srcdata;
float *fltbuf_in{nullptr};
float *fltbuf_out{nullptr};

int16_t *buf_in{nullptr};
int16_t *buf_out{nullptr};

size_t bufstart{0};
size_t bufend{0};
size_t bufsamples{0};

size_t spf{0};
int ffspeed{1};

void audio_cb_sdl(void *data, uint8_t *stream, int len) {
    AudioManager *audiomgr = static_cast<AudioManager*>(data);
    int16_t *out = (int16_t*)stream;
    for (int i = 0; i < len / sizeof(int16_t); i++) {
        out[i] = audiomgr->dequeue();
    }
}

void audio_cb_input(void *data, uint8_t *stream, int len) {
    JGManager *jgm = static_cast<JGManager*>(data);
    micinfo.buf = (void*)stream;
    jgm->data_push(JG_DATA_AUDIO, 0, &micinfo, len / sizeof(int16_t));
}

};

AudioManager::AudioManager(JGManager& jgm, SettingManager& setmgr)
        : jgm(jgm), setmgr(setmgr) {
    jgm.set_audio_cb(setmgr.get_setting("a_mute")->val ?
                     AudioManager::null_queue : AudioManager::queue);

    // Initialize audio buffers
    buf_in = new int16_t[BUFSIZE];
    std::fill(buf_in, buf_in + BUFSIZE, 0);
    buf_out = new int16_t[BUFSIZE];
    std::fill(buf_out, buf_out + BUFSIZE, 0);

    fltbuf_in = new float[BUFSIZE];
    std::fill(fltbuf_in, fltbuf_in + BUFSIZE, 0);
    fltbuf_out = new float[BUFSIZE];
    std::fill(fltbuf_out, fltbuf_out + BUFSIZE, 0);

    bufend = bufstart = bufsamples = 0;

    audinfo = jgm.get_audioinfo();
    audinfo->buf = &buf_in[0];
    spf = audinfo->spf;

    // Resampler
    int err;
    srcstate = src_new(setmgr.get_setting("a_rsqual")->val, audinfo->channels, &err);
    srcdata.data_in = fltbuf_in;
    srcdata.data_out = fltbuf_out;
    srcdata.output_frames = audinfo->spf;
    srcdata.src_ratio = 1.0;

    // SDL Audio Output
    spec.freq = audinfo->rate;
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    spec.format = AUDIO_S16MSB;
    #else
    spec.format = AUDIO_S16LSB;
    #endif
    spec.channels = audinfo->channels;
    spec.silence = 0;
    spec.samples = 512;
    spec.userdata = this;
    spec.callback = audio_cb_sdl;

    dev = SDL_OpenAudioDevice(NULL, 0, &spec, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!dev) {
        LogDriver::log(LogLevel::Warn, "Error opening audio device");
    }

    SDL_PauseAudioDevice(dev, 1);  // Setting to 0 unpauses
}

AudioManager::~AudioManager() {
    if (dev) {
        SDL_CloseAudioDevice(dev);
    }

    if (srcstate) {
        srcstate = src_delete(srcstate);
    }

    if (buf_in) {
        delete[] buf_in;
    }

    if (buf_out) {
        delete[] buf_out;
    }

    if (fltbuf_in) {
        delete[] fltbuf_in;
    }

    if (fltbuf_out) {
        delete[] fltbuf_out;
    }
}

int16_t AudioManager::dequeue() {
    if (bufsamples == 0) {
        return 0;
    }

    int16_t sample = buf_out[bufstart];
    bufstart = (bufstart + 1) % BUFSIZE;
    bufsamples--;
    return sample;
}

void AudioManager::queue(size_t in_size) {
    spf = audinfo->spf;
    while ((bufsamples + spf) >= BUFSIZE) {
        SDL_Delay(1);
    }

    size_t numsamples = in_size / ffspeed;

    src_short_to_float_array(buf_in, fltbuf_in, numsamples);
    srcdata.input_frames = numsamples;
    srcdata.end_of_input = 0;

    SDL_LockAudioDevice(dev);

    size_t frames_queued = bufsamples / spf;

    if (frames_queued < 3) {
        size_t step = 3 - frames_queued;
        srcdata.output_frames = numsamples + step;
        srcdata.src_ratio = (audinfo->rate + (JGManager::get_frametime() * step)) /
                            (audinfo->rate * 1.0);
    }
    else {
        srcdata.output_frames = numsamples;
        srcdata.src_ratio = 1.0;
    }

    src_process(srcstate, &srcdata);

    for (int i = 0; i < srcdata.output_frames_gen; i++) {
        fltbuf_out[i] *= 32768;
        buf_out[bufend] = fltbuf_out[i] >= 32767.0 ? 32767 :
                          fltbuf_out[i] <= -32768.0 ? -32768 :
                          fltbuf_out[i];
        bufend = (bufend + 1) % BUFSIZE;
        bufsamples++;
        if (bufsamples >= BUFSIZE - 1) {
            break;
        }
    }

    SDL_UnlockAudioDevice(dev);
}

void AudioManager::null_queue(size_t in_size) {
    return;
}

void AudioManager::rehash() {
    if (srcstate) {
        srcstate = src_delete(srcstate);
    }
    int err;
    srcstate = src_new(setmgr.get_setting("a_rsqual")->val, audinfo->channels, &err);
}

void AudioManager::set_speed(int speed) {
    spf = (audinfo->rate / jgm.get_frametime()) / speed;
    ffspeed = speed;
}

void AudioManager::pause(bool p) {
    SDL_PauseAudioDevice(dev, p);
    if (dev_in && jgm.get_coreinfo()->hints & JG_HINT_INPUT_AUDIO) {
        SDL_PauseAudioDevice(dev_in, p);
    }
    else if (!p && jgm.get_coreinfo()->hints & JG_HINT_INPUT_AUDIO) {
        // Discover any audio recording devices
        int miccount = SDL_GetNumAudioDevices(1);
        std::string micname{};

        for (int i = 0; i < miccount; ++i) {
            micname = std::string{SDL_GetAudioDeviceName(i, 1)};
        }

        if (miccount) {
            spec_in.channels = micinfo.channels;
            spec_in.freq = micinfo.rate;
            spec_in.silence = 0;
            spec_in.samples = 512;
            spec_in.userdata = &jgm;
            spec_in.format = spec.format;
            spec_in.callback = audio_cb_input;
            dev_in = SDL_OpenAudioDevice(micname.c_str(), 1, &spec_in, &obtained_in,
                                         SDL_AUDIO_ALLOW_ANY_CHANGE);
            SDL_PauseAudioDevice(dev_in, 0); // Unpause
            LogDriver::log(LogLevel::Debug, "Microphone: " + micname);
        }
    }
}

void AudioManager::mute(bool m) {
    jgm.set_audio_cb(m ? AudioManager::null_queue : AudioManager::queue);
}
