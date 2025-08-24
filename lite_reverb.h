#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_DELAY_SAMPLES 48000 // 1 сек @ 48кГц
#define NUM_DELAYS 4
#define FEEDBACK 0.7f
#define DAMP 0.4f
#define OUT_GAIN 0.35f

struct DelayLine {
    float buf[MAX_DELAY_SAMPLES];
    int size;
    int write_pos;
    int read_pos;
    float filter_state;
};

class LiteReverb {
  public:
    void Init(float sr) {
        sample_rate = sr;
        room_size = 0.5f;
        decay = 0.7f;

        UpdateDelays();
    }

    void SetRoomSize(float rs) {
        if (room_size != rs) {
            room_size = rs;
            UpdateDelays();
        }
    }

    void SetDecay(float d) {
        decay = d;
    }

    float Process(float in) {
        float acc = 0.0f;
        for (int i = 0; i < NUM_DELAYS; i++) {
            DelayLine &d = delays[i];

            // просте читання без інтерполяції
            float y = d.buf[d.read_pos];

            // LPF (демпфування високих частот у хвості)
            d.filter_state = (1.0f - DAMP) * y + DAMP * d.filter_state;
            float fb = d.filter_state * decay;

            // запис (сигнал + feedback)
            d.buf[d.write_pos] = in + fb;

            // оновлення позицій
            if (++d.write_pos >= d.size) d.write_pos = 0;
            if (++d.read_pos >= d.size) d.read_pos = 0;

            acc += y;
        }
        return acc * OUT_GAIN;
    }

  private:
    float sample_rate;
    float room_size;
    float decay;
    DelayLine delays[NUM_DELAYS];

    void UpdateDelays() {
        float min_time = 0.010f;
        float max_time = 0.100f;
        float base_time = min_time + room_size * (max_time - min_time);

        // Дві різні довжини (≈30 мс і 45 мс)
        float times[NUM_DELAYS] = {
            base_time,
            base_time * 1.3f,
            base_time * 1.7f,
            base_time * 2.1f
        };
        for (int i = 0; i < NUM_DELAYS; i++) {
            delays[i].size = (int)(sample_rate * times[i]);
            if (delays[i].size > MAX_DELAY_SAMPLES)
                delays[i].size = MAX_DELAY_SAMPLES;
            delays[i].write_pos = 0;
            delays[i].read_pos = delays[i].size / 2;
            delays[i].filter_state = 0.0f;
            memset(delays[i].buf, 0, sizeof(float) * delays[i].size);
        }
    }
};
