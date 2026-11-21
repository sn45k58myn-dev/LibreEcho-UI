#ifndef LIBREECHO_VOICE_AEC_H
#define LIBREECHO_VOICE_AEC_H

#include <stddef.h>
#include <stdint.h>

#define LE_VOICE_AEC_RATE 16000
#define LE_VOICE_AEC_FRAME_SAMPLES 160
#define LE_VOICE_AEC_DEFAULT_FILTER_SAMPLES 3200

struct le_voice_aec {
    void *echo_state;
    unsigned int frame_samples;
    unsigned int filter_samples;
};

int le_voice_aec_init(struct le_voice_aec *aec,
                      unsigned int frame_samples,
                      unsigned int filter_samples);
void le_voice_aec_reset(struct le_voice_aec *aec);
void le_voice_aec_process(struct le_voice_aec *aec,
                          const int16_t *microphone,
                          const int16_t *reference,
                          int16_t *output);
void le_voice_aec_destroy(struct le_voice_aec *aec);

/* Echo 2 intercom return path, with its own fixed-point filter and delay line. */
#define LE_CALL_AEC_DELAY_SAMPLES 480U
#define LE_CALL_AEC_MAX_GAIN 16
struct le_voice_call_aec {
    struct le_voice_aec filter;
    int16_t reference_delay[LE_CALL_AEC_DELAY_SAMPLES];
    unsigned int delay_position;
    int gain, recovery_gain;
    unsigned int recovery_frames;
};
int le_voice_call_aec_init(struct le_voice_call_aec *call);
void le_voice_call_aec_reset(struct le_voice_call_aec *call);
void le_voice_call_aec_process(struct le_voice_call_aec *call,
                               const int16_t *microphone, const int16_t *reference,
                               int16_t *output);
void le_voice_call_aec_destroy(struct le_voice_call_aec *call);

#endif
