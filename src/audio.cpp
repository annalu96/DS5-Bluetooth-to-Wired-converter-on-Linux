//
// Refactored for Linux (Fase 5 - ALSA integration via f_uac1)
//
// Audio is now read from the ALSA device created by the kernel's
// f_uac1 gadget driver, instead of raw FunctionFS endpoint reads.
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
#include <alsa/asoundlib.h>
#include "utils.h"

#define INPUT_CHANNELS    4
#define OUTPUT_CHANNELS   2
#define SAMPLE_SIZE       64
#define REPORT_SIZE       398
#define REPORT_ID         0x36
#define BUFFER_LENGTH     48

// ALSA capture period: number of frames per read
#define ALSA_PERIOD_FRAMES  48  // ~1ms at 48kHz

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
static std::thread alsa_capture_thread;
static std::atomic<bool> audio_running{true};

// ALSA PCM handle for capture (playback from host perspective)
static snd_pcm_t *alsa_pcm = nullptr;

void set_headset(bool state) {
    plug_headset = state;
}

// Find the ALSA device name for the UAC1 gadget
static std::string find_uac1_alsa_device() {
    // The f_uac1 kernel driver creates an ALSA card.
    // We need to find it by scanning available cards.
    // The card name is typically "UAC1Gadget" or "UAC1_Gadget".
    
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char *name = nullptr;
        if (snd_card_get_name(card, &name) == 0 && name) {
            std::string card_name(name);
            free(name);
            
            printf("[Audio] Found ALSA card %d: %s\n", card, card_name.c_str());
            
            // f_uac1 typically creates a card with "UAC1" in the name
            if (card_name.find("UAC1") != std::string::npos ||
                card_name.find("uac1") != std::string::npos ||
                card_name.find("Gadget") != std::string::npos) {
                // Return the hardware device string
                char dev[32];
                snprintf(dev, sizeof(dev), "hw:%d,0", card);
                printf("[Audio] Using ALSA device: %s (%s)\n", dev, card_name.c_str());
                return std::string(dev);
            }
        }
    }
    
    // Fallback: try to use a default name
    printf("[Audio] UAC1 ALSA card not found, falling back to 'hw:UAC1Gadget'\n");
    return "hw:UAC1Gadget";
}

// Open ALSA PCM device for capture (reading audio from host)
static int alsa_open() {
    std::string device = find_uac1_alsa_device();
    
    int err = snd_pcm_open(&alsa_pcm, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        printf("[Audio] Cannot open ALSA device '%s': %s\n", device.c_str(), snd_strerror(err));
        printf("[Audio] Audio will not be available until the gadget is bound.\n");
        return -1;
    }

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(alsa_pcm, hw_params);

    // Set parameters matching the DualSense: 4ch, 48kHz, 16-bit
    snd_pcm_hw_params_set_access(alsa_pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(alsa_pcm, hw_params, SND_PCM_FORMAT_S16_LE);

    unsigned int rate = 48000;
    snd_pcm_hw_params_set_rate_near(alsa_pcm, hw_params, &rate, 0);

    unsigned int channels = INPUT_CHANNELS;
    snd_pcm_hw_params_set_channels(alsa_pcm, hw_params, channels);

    snd_pcm_uframes_t period_size = ALSA_PERIOD_FRAMES;
    snd_pcm_hw_params_set_period_size_near(alsa_pcm, hw_params, &period_size, 0);

    snd_pcm_uframes_t buffer_size = period_size * 4;
    snd_pcm_hw_params_set_buffer_size_near(alsa_pcm, hw_params, &buffer_size);

    err = snd_pcm_hw_params(alsa_pcm, hw_params);
    if (err < 0) {
        printf("[Audio] Cannot set ALSA hw params: %s\n", snd_strerror(err));
        snd_pcm_close(alsa_pcm);
        alsa_pcm = nullptr;
        return -1;
    }

    printf("[Audio] ALSA opened: rate=%u, channels=%u, period=%lu, buffer=%lu\n",
           rate, channels, (unsigned long)period_size, (unsigned long)buffer_size);

    return 0;
}

// Process received PCM data (same logic as before, but called from ALSA thread)
static void process_pcm_data(const int16_t* raw, uint32_t frames) {
    if (frames == 0) return;

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
        }

        fill_output_report_checksum(pkt, sizeof(pkt));
        bt_write(INTERRUPT, pkt, sizeof(pkt));
        haptic_buf_pos = 0;
    }
}

// Thread that reads PCM audio from the ALSA device (f_uac1)
static void alsa_capture_entry() {
    printf("[Audio] ALSA capture thread started.\n");
    
    // If UDC binding failed, audio will never work
    if (!usb_gadget_bound) {
        printf("[Audio] UDC not bound — audio capture disabled.\n");
        return;
    }
    
    // Wait a bit for the gadget to be fully bound before trying to open ALSA
    // The UAC1 ALSA card is only created after the gadget is bound to the UDC
    int retries = 0;
    while (audio_running && alsa_pcm == nullptr) {
        if (alsa_open() == 0) {
            break;
        }
        retries++;
        if (retries > 30) {
            printf("[Audio] Gave up waiting for ALSA device after %d retries.\n", retries);
            return;
        }
        printf("[Audio] Waiting for ALSA device... (retry %d)\n", retries);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    if (!alsa_pcm) return;

    int16_t buf[ALSA_PERIOD_FRAMES * INPUT_CHANNELS];
    
    while (audio_running) {
        snd_pcm_sframes_t frames = snd_pcm_readi(alsa_pcm, buf, ALSA_PERIOD_FRAMES);
        if (frames < 0) {
            if (frames == -EPIPE) {
                // Buffer overrun
                snd_pcm_prepare(alsa_pcm);
                continue;
            } else if (frames == -EAGAIN) {
                continue;
            } else {
                printf("[Audio] ALSA read error: %s\n", snd_strerror(frames));
                // Try to recover
                int err = snd_pcm_recover(alsa_pcm, frames, 1);
                if (err < 0) {
                    printf("[Audio] ALSA recovery failed: %s\n", snd_strerror(err));
                    break;
                }
                continue;
            }
        }
        
        if (frames > 0) {
            process_pcm_data(buf, frames);
        }
    }
    
    printf("[Audio] ALSA capture thread exiting.\n");
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

    // Launch Opus encoder thread
    audio_running = true;
    audio_thread = std::thread(core1_entry);
    
    // Launch ALSA capture thread (reads PCM from f_uac1 gadget)
    alsa_capture_thread = std::thread(alsa_capture_entry);
}

void audio_deinit() {
    audio_running = false;
    audio_fifo_cv.notify_all();
    
    if (alsa_capture_thread.joinable()) {
        alsa_capture_thread.join();
    }
    if (audio_thread.joinable()) {
        audio_thread.join();
    }
    if (alsa_pcm) {
        snd_pcm_close(alsa_pcm);
        alsa_pcm = nullptr;
    }
    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
    }
}
