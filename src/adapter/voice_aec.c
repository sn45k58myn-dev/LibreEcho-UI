#include "voice_aec.h"

#include <speex/speex_echo.h>

#include <string.h>

int le_voice_aec_init(struct le_voice_aec *aec,
                      unsigned int frame_samples,
                      unsigned int filter_samples)
{
    SpeexEchoState *state;
    int sample_rate = LE_VOICE_AEC_RATE;

    if (!aec || frame_samples == 0 || filter_samples < frame_samples)
        return -1;
    memset(aec, 0, sizeof(*aec));
    state = speex_echo_state_init((int)frame_samples, (int)filter_samples);
    if (!state)
        return -1;
    if (speex_echo_ctl(state, SPEEX_ECHO_SET_SAMPLING_RATE,
                       &sample_rate) != 0) {
        speex_echo_state_destroy(state);
        return -1;
    }
    aec->echo_state = state;
    aec->frame_samples = frame_samples;
    aec->filter_samples = filter_samples;
    return 0;
}

void le_voice_aec_reset(struct le_voice_aec *aec)
{
    if (aec && aec->echo_state)
        speex_echo_state_reset((SpeexEchoState *)aec->echo_state);
}

void le_voice_aec_process(struct le_voice_aec *aec,
                          const int16_t *microphone,
                          const int16_t *reference,
                          int16_t *output)
{
    if (!aec || !aec->echo_state || !microphone || !reference || !output)
        return;
    speex_echo_cancellation((SpeexEchoState *)aec->echo_state,
                            microphone, reference, output);
}

void le_voice_aec_destroy(struct le_voice_aec *aec)
{
    if (!aec)
        return;
    if (aec->echo_state)
        speex_echo_state_destroy((SpeexEchoState *)aec->echo_state);
    memset(aec, 0, sizeof(*aec));
}

void le_voice_call_aec_reset(struct le_voice_call_aec *call)
{
    le_voice_aec_reset(&call->filter);
    memset(call->reference_delay, 0, sizeof(call->reference_delay));
    call->delay_position = call->recovery_frames = 0;
    call->gain = call->recovery_gain = LE_CALL_AEC_MAX_GAIN;
}

int le_voice_call_aec_init(struct le_voice_call_aec *call)
{
    if (le_voice_aec_init(&call->filter, LE_VOICE_AEC_FRAME_SAMPLES,
                          LE_VOICE_AEC_DEFAULT_FILTER_SAMPLES) < 0)
        return -1;
    le_voice_call_aec_reset(call);
    return 0;
}

void le_voice_call_aec_destroy(struct le_voice_call_aec *call)
{
    le_voice_aec_destroy(&call->filter);
    memset(call, 0, sizeof(*call));
}

void le_voice_call_aec_process(struct le_voice_call_aec *call,
                               const int16_t *microphone, const int16_t *reference,
                               int16_t *output)
{
    int16_t near[LE_VOICE_AEC_FRAME_SAMPLES], far[LE_VOICE_AEC_FRAME_SAMPLES];
    int peak = 0, gain = call->gain;
    unsigned int i;

    for (i = 0; i < LE_VOICE_AEC_FRAME_SAMPLES; ++i) {
        int near_abs, far_abs;
        far[i] = call->reference_delay[call->delay_position];
        call->reference_delay[call->delay_position] = reference[i];
        if (++call->delay_position == LE_CALL_AEC_DELAY_SAMPLES)
            call->delay_position = 0;
        near_abs = microphone[i] < 0 ? -(int)microphone[i] : microphone[i];
        far_abs = far[i] < 0 ? -(int)far[i] : far[i];
        if (near_abs > peak) peak = near_abs;
        if (far_abs > peak) peak = far_abs;
    }
    /* Raise both signals inside Speex so that quiet speech and its reference
     * retain precision. Reserve headroom before processing each loud frame. */
    while (gain > 1 && peak * gain > 16000)
        gain /= 2;
    if (gain == call->gain && gain < LE_CALL_AEC_MAX_GAIN) {
        int possible = LE_CALL_AEC_MAX_GAIN;
        while (possible > gain && peak * possible > 10000)
            possible /= 2;
        if (possible > gain) {
            if (possible < call->recovery_gain)
                call->recovery_gain = possible;
            /* Require 600 ms at the new level to avoid repeated filter resets. */
            if (++call->recovery_frames >= 60)
                gain = call->recovery_gain;
        } else {
            call->recovery_frames = 0;
            call->recovery_gain = LE_CALL_AEC_MAX_GAIN;
        }
    }
    if (gain != call->gain) {
        le_voice_aec_reset(&call->filter);
        call->gain = gain;
        call->recovery_frames = 0;
        call->recovery_gain = LE_CALL_AEC_MAX_GAIN;
    }
    for (i = 0; i < LE_VOICE_AEC_FRAME_SAMPLES; ++i) {
        near[i] = (int16_t)(microphone[i] * gain);
        far[i] = (int16_t)(far[i] * gain);
    }
    le_voice_aec_process(&call->filter, near, far, output);
    for (i = 0; i < LE_VOICE_AEC_FRAME_SAMPLES; ++i) {
        int sample = output[i];
        output[i] = (int16_t)((sample < 0 ? sample - gain / 2 : sample + gain / 2) / gain);
    }
}
