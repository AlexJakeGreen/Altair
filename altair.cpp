// Altair for Hothouse DIY DSP Platform
// Copyright (C) 2024 ajg <green@jee.org.ua>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// ### Uncomment if IntelliSense can't resolve DaisySP-LGPL classes ###
// #include "daisysp-lgpl.h"

#include "daisysp.h"
#include "hothouse.h"

#include <RTNeural/RTNeural.h>  // NOTE: Need to use older version of RTNeural, same as GuitarML/Seed
// Model Weights (edit this file to add model weights trained with Colab script)
//    The models must be GRU (gated recurrent unit) with hidden size = 9, snapshot models (not condidtioned on a parameter)
#include "all_model_data_gru9_4count.h"

#include "ImpulseResponse/ImpulseResponse.h"
#include "ImpulseResponse/ir_data.h"

#include "delayline_2tap.h"


using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::Parameter;
using daisysp::Tone;
using daisysp::ATone;
using daisysp::Balance;
using daisysp::fonepole;

Hothouse hw;

Parameter Gain, Level, Mix, filter, delayTime, delayFdbk;




bool rn_model_enabled = true;
bool ir_enabled = true;

volatile bool g_toggle_bypass_req = false;

// Bypass vars
Led led_bypass;
bool bypass = true;

float dryMix, wetMix;


float           mix_effects;

float vfilter;
Tone tone;       // Low Pass
ATone toneHP;    // High Pass
Balance bal;     // Balance for volume correction in filtering


// Impulse Response
ImpulseResponse mIR;
int   m_currentIRindex = 0;

// Delay Max Definitions (Assumes 48kHz samplerate)
#define MAX_DELAY static_cast<size_t>(48000.0f * 2.f)
DelayLine2Tap<float, MAX_DELAY> DSY_SDRAM_BSS delayLine;
// Delay with dotted eighth and triplett options
struct delay
{
    DelayLine2Tap<float, MAX_DELAY> *del;
    float                        currentDelay;
    float                        delayTarget;
    float                        feedback = 0.0;
    float                        active = false;
    float                        level = 1.0;      // Level multiplier of output
    bool                         secondTapOn = false;
    
    float Process(float in)
    {
        //set delay times
        fonepole(currentDelay, delayTarget, .0002f);
        del->SetDelay(currentDelay);

        float read = del->Read();

        float secondTap = 0.0;
        if (secondTapOn) {
            secondTap = del->ReadSecondTap();
        }

        if (active) {
            del->Write((feedback * read) + in);
        } else {
            del->Write(feedback * read); // if not active, don't write any new sound to buffer
        }

        return (read + secondTap) * level;

    }
};

delay             delay1;




// Neural Network Model
// Currently only using snapshot models, they tend to sound better and 
//   we can use input level as gain.

RTNeural::ModelT<float, 1, 1,
                 RTNeural::GRULayerT<float, 1, 9>,
                 RTNeural::DenseT<float, 9, 1>> model;

int             modelInSize;
unsigned int    modelIndex;
float           nnLevelAdjust;
int             indexMod;

// Notes: With default settings, GRU 10 is max size currently able to run on Daisy Seed
//        - Parameterized 1-knob GRU 10 is max, GRU 8 with effects is max
//        - Parameterized 2-knob/3-knob at GRU 8 is max
//        - With multi effect (reverb, etc.) added GRU 9 is recommended to allow room for processing of other effects
//        - These models should be trained using 48kHz audio data, since Daisy uses 48kHz by default.
//             Models trained with other samplerates, or running Daisy at a different samplerate will sound different.


void setup_ir() {
    mIR.Init(ir_collection[m_currentIRindex]);
}

void setup_model() {
    auto& gru = (model).template get<0>();
    auto& dense = (model).template get<1>();
    modelInSize = 1;
    gru.setWVals(model_collection[modelIndex].rec_weight_ih_l0);
    gru.setUVals(model_collection[modelIndex].rec_weight_hh_l0);
    gru.setBVals(model_collection[modelIndex].rec_bias);
    dense.setWeights(model_collection[modelIndex].lin_weight);
    dense.setBias(model_collection[modelIndex].lin_bias.data());
    model.reset();

    nnLevelAdjust = model_collection[modelIndex].levelAdjust;
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // float input_arr[1] = { 0.0 };    // Neural Net Input
    float delay_out;

    // hw.ProcessAllControls();

    float vgain = Gain.Process();
    float vmix = Mix.Process();
    float vlevel = Level.Process();
    float vfilter = filter.Process();
    float vdelayTime = delayTime.Process();
    float vdelayFdbk = delayFdbk.Process();

    // Mix and tone control
    // Set Filter Controls
    if (vfilter <= 0.5) {
        float filter_value = (vfilter * 39800.0f) + 100.0f;
        tone.SetFreq(filter_value);
    } else {
        float filter_value = (vfilter - 0.5) * 800.0f + 40.0f;
        toneHP.SetFreq(filter_value);
    }

    // Calculate mix parameters
    //    A cheap mostly energy constant crossfade from SignalSmith Blog
    //    https://signalsmith-audio.co.uk/writing/2021/cheap-energy-crossfade/
    float x2 = 1.0 - vmix;
    float A = vmix*x2;
    float B = A * (1.0 + 1.4186 * A);
    float C = B + vmix;
    float D = B + x2;

    wetMix = C * C;
    dryMix = D * D;


    // DELAY //
    if (vdelayTime < 0.01) {   // if knob < 1%, set delay to inactive
        delay1.active = false;
    } else {
        delay1.active = true;
    }

    // From 0 to 75% knob is 0 to 1 second, 75% to 100% knob is 1 to 2 seconds (for more control over 1 second range)
    if (vdelayTime <= 0.75) {
        delay1.delayTarget = 2400 + vdelayTime * 60800; // in samples 50ms to 1 second range  // Note: changing delay time with heavy reverb creates a cool modulation effect
    } else {
        delay1.delayTarget = 48000 + (vdelayTime - 0.75) * 192000; // 1 second to 2 second range
    }
    delay1.feedback = vdelayFdbk;


    // react to main-loop request
    if (g_toggle_bypass_req) {
        g_toggle_bypass_req = false;
        bypass = !bypass;
        if (!bypass) {
            model.reset();                  // clear GRU state
            // hard reset IR tail to avoid immediate overload
            // (if no .Reset() exists, re-init with the same IR)
            // mIR.Reset(); // if available
            mIR.Init(ir_collection[m_currentIRindex]);
        }
    }
    // Toggle bypass when FOOTSWITCH_2 is pressed
    // if (hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge()) {
    //     bypass = !bypass;
    //     if (!bypass) {
    //         model.reset();
    //     }
    // }

    for (size_t i = 0; i < size; ++i) {
        if (bypass) {
            // Copy left input to both outputs (mono-to-dual-mono)
            out[0][i] = out[1][i] = in[0][i];
            continue;
        }

        float x = in[0][i] * vgain;
        // Neural
        float amp_out = rn_model_enabled ? ((model.forward(&x) + x)*nnLevelAdjust) : x;

        // Guardrails to stop NaNs/denormals from breaking the audio thread
        if (!isfinite(amp_out)) amp_out = 0.0f;



        // Process Tone
        float filter_in =  amp_out;
        float filter_out;
        float balanced_out;
        
        if (vfilter <= 0.5) {
            filter_out = tone.Process(filter_in);
            balanced_out = bal.Process(filter_out, filter_in);
        } else {
            filter_out = toneHP.Process(filter_in);
            balanced_out = bal.Process(filter_out, filter_in);
        }

        delay_out = delay1.Process(balanced_out);   // Moved delay prior to IR


        // IR
        float y;
        if (ir_enabled) {
            y = mIR.Process(balanced_out * dryMix + delay_out * wetMix) * 0.2;
        } else {
            y = balanced_out * dryMix + delay_out * wetMix;
        }

        out[0][i] = y * vlevel;
        out[1][i] = y * vlevel;
    }
}

int sw_1_value = 0;
int get_sw_1() {
    switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)) {
    case Hothouse::TOGGLESWITCH_UP:
        return 2;
        break;
    case Hothouse::TOGGLESWITCH_MIDDLE:
        return 1;
        break;
    case Hothouse::TOGGLESWITCH_DOWN:
    default:
        return 0;
        break;
    }
}

int m_number = 0;
int get_sw_2() {
    switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)) {
    case Hothouse::TOGGLESWITCH_UP:
        return 2;
        break;
    case Hothouse::TOGGLESWITCH_MIDDLE:
        return 1;
        break;
    case Hothouse::TOGGLESWITCH_DOWN:
    default:
        return 0;
        break;
    }
}

int get_sw_3() {
    switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)) {
    case Hothouse::TOGGLESWITCH_UP:
        return 2;
        break;
    case Hothouse::TOGGLESWITCH_MIDDLE:
        return 1;
        break;
    case Hothouse::TOGGLESWITCH_DOWN:
    default:
        return 0;
        break;
    }
}

int main() {
    hw.Init();
    hw.SetAudioBlockSize(256);  // Number of samples handled per callback
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    float samplerate =  hw.AudioSampleRate();
    setup_ir();
    setupWeights();

    // Initialize the correct model
    modelIndex = 5;
    nnLevelAdjust = 1.0;
    indexMod = 0;
    setup_model();


    // Initialize & set params for mixers 
    mix_effects = 0.5;


    tone.Init(samplerate);
    toneHP.Init(samplerate);
    bal.Init(samplerate);
    vfilter = 0.5;

    delayLine.Init();
    delay1.del = &delayLine;
    delay1.delayTarget = 2400; // in samples
    delay1.feedback = 0.0;
    delay1.active = true;


    Gain.Init(hw.knobs[Hothouse::KNOB_1], 0.1f, 2.5f, Parameter::LINEAR);
    Mix.Init(hw.knobs[Hothouse::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    Level.Init(hw.knobs[Hothouse::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR); // lower range for quieter level
    filter.Init(hw.knobs[Hothouse::KNOB_4], 0.0f, 1.0f, Parameter::CUBE);
    delayTime.Init(hw.knobs[Hothouse::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    delayFdbk.Init(hw.knobs[Hothouse::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR); 

    led_bypass.Init(hw.seed.GetPin(Hothouse::LED_2), false);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while (true) {
        hw.DelayMs(10);
        hw.ProcessAllControls();

        if (hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge()) {
            g_toggle_bypass_req = true; // signal audio thread
        }

        int sw1 = get_sw_1();
        if (sw1 != sw_1_value) {
            setup_ir();
            m_currentIRindex = sw1;
            sw_1_value = sw1;
        }

        int m = get_sw_2() + get_sw_3();
        if (m != m_number) {
            m_number = m;
            modelIndex = m;
            setup_model();
        }

        // Toggle effect bypass LED when footswitch is pressed
        led_bypass.Set(bypass ? 0.0f : 1.0f);
        led_bypass.Update();

        // Call System::ResetToBootloader() if FOOTSWITCH_1 is pressed for 2 seconds
        hw.CheckResetToBootloader();
    }
    return 0;
}
