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



using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::SaiHandle;
using daisy::Parameter;

Hothouse hw;

Parameter Gain;




bool rn_model_enabled = true;
bool ir_enabled = true;

volatile bool g_toggle_bypass_req = false;

// Bypass vars
Led led_bypass;
bool bypass = true;

// Impulse Response
ImpulseResponse mIR;
int   m_currentIRindex = 0;


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

    // hw.ProcessAllControls();

    float vgain = Gain.Process();

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
        // light saturation (prevents IR from exploding on transients)
        //amp_out = fminf(fmaxf(amp_out, -1.0f), 1.0f);
        
        // IR
        float y = ir_enabled ? mIR.Process(amp_out) : amp_out;
        
        // Output clamp (keep it simple; you can retune later)
        //if (!isfinite(y)) y = 0.0f;
        //y = fminf(fmaxf(y, -1.0f), 1.0f);
        
        out[0][i] = y;
        out[1][i] = y;
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

    setup_ir();
    setupWeights();

    // Initialize the correct model
    modelIndex = 5;
    nnLevelAdjust = 1.0;
    indexMod = 0;
    setup_model();

    Gain.Init(hw.knobs[Hothouse::KNOB_1], 0.1f, 2.5f, Parameter::LINEAR);

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
