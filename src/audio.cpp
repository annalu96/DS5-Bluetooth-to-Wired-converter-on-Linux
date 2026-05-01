//
// Refactored for Linux (Fase 2)
//

#include "audio.h"
#include "bt.h"
#include "resample.h"
#include "usb.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

#include <opus/opus.h>
#include "utils.h"

#define INPUT_CHANNELS    4
#define OUTPUT_CHANNELS   2
#define SAMPLE_SIZE       64
#define REPORT_SIZE       398
#define REPORT_ID         0x36
// #define VOLUME_GAIN       2
#define BUFFER_LENGTH     48

using std::clamp;
using std::max;

static WDL_Resampler resampler;
static uint8_t reportSeqCounter = 0;
static uint8_t packetCounter = 0;
static bool plug_headset = false;

struct audio_raw_element {
    float data[512 * 2];
};
struct opus_element {
    uint8_t data[200];
};

static std::queue<audio_raw_element> audio_fifo;
static std::mutex audio_fifo_mutex;
static std::condition_variable audio_fifo_cv;

static std::queue<opus_element> opus_fifo;
static std::mutex opus_fifo_mutex;

static std::thread audio_thread;
static std::atomic<bool> audio_running{true};

void set_headset(bool state) {
    plug_headset = state;
}

// Stub function to replace tud_audio_read
// This will be called to pass PCM data to the audio module from the main epoll loop
void audio_receive_pcm(const int16_t* raw, uint32_t bytes_read) {
    int frames = bytes_read / (INPUT_CHANNELS * sizeof(int16_t));
    if (frames == 0) {
        return;
    }

    static float audio_buf[512 * 2];
    static uint audio_buf_pos = 0;

    WDL_ResampleSample *in_buf;
    int nframes = resampler.ResamplePrepare(frames, OUTPUT_CHANNELS, &in_buf);

    for (int i = 0; i < nframes; i++) {
        audio_buf[audio_buf_pos++] = raw[i * INPUT_CHANNELS] / 32768.0f * (volume[0] - 1.0f);
        audio_buf[audio_buf_pos++] = raw[i * INPUT_CHANNELS + 1] / 32768.0f * (volume[0] - 1.0f);
        if (audio_buf_pos == 512 * 2) {
            audio_raw_element element{};
            memcpy(element.data, audio_buf, 512 * 2 * sizeof(float));

            {
                std::lock_guard<std::mutex> lock(audio_fifo_mutex);
                if (audio_fifo.size() >= 2) {
                    audio_fifo.pop();
                }
                audio_fifo.push(element);
            }
            audio_fifo_cv.notify_one();

            audio_buf_pos = 0;
        }

        in_buf[i * 2] = (WDL_ResampleSample) raw[i * INPUT_CHANNELS + 2] / 32768.0f;
        in_buf[i * 2 + 1] = (WDL_ResampleSample) raw[i * INPUT_CHANNELS + 3] / 32768.0f;
    }

    // 3. 48kHz -> 3kHz
    static WDL_ResampleSample out_buf[SAMPLE_SIZE]; // 64 floats = 32 frames × 2ch
    int out_frames = resampler.ResampleOut(out_buf, nframes, SAMPLE_SIZE / OUTPUT_CHANNELS, OUTPUT_CHANNELS);

    static int8_t haptic_buf[SAMPLE_SIZE];
    static int haptic_buf_pos = 0;

    for (int i = 0; i < out_frames; i++) {
        int val_l = (int) (out_buf[i * 2] * 127.0f * max(volume[1], 1.0f));
        int val_r = (int) (out_buf[i * 2 + 1] * 127.0f * max(volume[1], 1.0f));
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_l, -128, 127);
        haptic_buf[haptic_buf_pos++] = (int8_t) clamp(val_r, -128, 127);

        if (haptic_buf_pos != SAMPLE_SIZE) {
            continue;
        }
        uint8_t pkt[REPORT_SIZE]{};
        pkt[0] = REPORT_ID;
        pkt[1] = reportSeqCounter << 4;
        reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
        pkt[2] = 0x11 | (1 << 7);
        pkt[3] = 7;
        pkt[4] = 0b11111110;
        pkt[5] = BUFFER_LENGTH;
        pkt[6] = BUFFER_LENGTH;
        pkt[7] = BUFFER_LENGTH;
        pkt[8] = BUFFER_LENGTH;
        pkt[9] = BUFFER_LENGTH; // buffer length
        pkt[10] = packetCounter++;
        pkt[11] = 0x12 | (1 << 7);
        pkt[12] = SAMPLE_SIZE;
        memcpy(pkt + 13, haptic_buf, SAMPLE_SIZE);

        opus_element opus_packet{};
        bool has_opus = false;
        {
            std::lock_guard<std::mutex> lock(opus_fifo_mutex);
            if (!opus_fifo.empty()) {
                opus_packet = opus_fifo.front();
                opus_fifo.pop();
                has_opus = true;
            }
        }

        if (has_opus) {
            pkt[77] = (plug_headset ? 0x16 : 0x13) | 0 << 6 | 1 << 7;
            pkt[78] = 200;
            memcpy(pkt + 79, opus_packet.data, 200);
        } else {
            // printf("[Audio] Warning: opus_fifo empty\n");
        }

        fill_output_report_checksum(pkt, sizeof(pkt));
        bt_write(pkt, sizeof(pkt));
        haptic_buf_pos = 0;
    }
}


static OpusEncoder *encoder;
static WDL_Resampler resampler_audio;

void core1_entry() {
    int error = 0;
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
    if (error != 0) {
        printf("[Audio] OpusEncoder create failed\n");
        return;
    }
    opus_encoder_ctl(encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(200 * 8 * 100));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(false));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0)); // max 4

    resampler_audio.SetMode(true, 0, false);
    resampler_audio.SetRates(51200, 48000);
    resampler_audio.SetFeedMode(true);

    while (audio_running) {
        audio_raw_element audio_element{};
        {
            std::unique_lock<std::mutex> lock(audio_fifo_mutex);
            audio_fifo_cv.wait(lock, []{ return !audio_fifo.empty() || !audio_running; });
            if (!audio_running && audio_fifo.empty()) {
                break;
            }
            audio_element = audio_fifo.front();
            audio_fifo.pop();
        }

        WDL_ResampleSample *in_buf;
        int nframes = resampler_audio.ResamplePrepare(512, 2, &in_buf);
        for (int i = 0; i < nframes * 2; i++) {
            in_buf[i] = audio_element.data[i];
        }
        static WDL_ResampleSample out_buf_dbl[480 * 2]; static float out_buf[480 * 2];
        resampler_audio.ResampleOut(out_buf_dbl, nframes, 480, 2); for (int i=0; i<480*2; i++) out_buf[i] = (float)out_buf_dbl[i];

        opus_element opus_packet{};
        opus_int32 encode_result = opus_encode_float(encoder, out_buf, 480, opus_packet.data, 200);
        if (encode_result < 0) {
            // Error handling: if encoding fails, do not queue the packet
            // printf("[Audio] Opus encode failed: %d\n", encode_result);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(opus_fifo_mutex);
            if (opus_fifo.size() >= 2) {
                opus_fifo.pop();
            }
            opus_fifo.push(opus_packet);
        }
    }
}

void audio_init() {
    resampler.SetMode(true, 0, false);
    resampler.SetRates(48000, 3000);
    resampler.SetFeedMode(true);

    // Launch thread instead of multicore core1
    audio_running = true;
    audio_thread = std::thread(core1_entry);
}

void audio_deinit() {
    audio_running = false;
    audio_fifo_cv.notify_all();
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
}
