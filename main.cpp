#include <cassert>

#include <q/support/literals.hpp>
#include <q/fx/biquad.hpp>

#include <util/EffectState.h>
#include <util/Multirate.h>
#include <util/OctaveGenerator.h>
#include <util/SvFilter.h>
#include <util/Terrarium.h>

namespace q = cycfi::q;
using namespace q::literals;

Terrarium terrarium;
EffectState interface_state;
bool enable_effect = true;
SvFilter low_pass;
float filter_frequency = 500; // Todo - map to pot
float filter_q = 0;
bool sub_octave_switch = true;

//=============================================================================
void processAudioBlock(
    daisy::AudioHandle::InputBuffer in,
    daisy::AudioHandle::OutputBuffer out,
    size_t size)
{
    static const auto sample_rate = terrarium.seed.AudioSampleRate();

    static Decimator decimate;
    static Interpolator interpolate;
    static OctaveGenerator octave(sample_rate / resample_factor);
    static OctaveGenerator sub_octave(sample_rate / resample_factor);
    static q::highshelf eq1(-11, 140_Hz, sample_rate);
    static q::lowshelf eq2(5, 160_Hz, sample_rate);

    const auto& s = interface_state;

    for (size_t i = 0; i <= (size - resample_factor); i += resample_factor)
    {
        std::span<const float, resample_factor> in_chunk(
            &(in[0][i]), resample_factor);
        const auto sample = decimate(in_chunk);

        low_pass.config(filter_frequency, sample_rate, filter_q); //Todo - steeper curve, gate and compression(?)
        low_pass.update(sample);

        float octave_mix = 0;
        sub_octave.update(low_pass.lowPass());
        octave.update(sample);
        octave_mix += s.up1Level() * octave.up1();
        if(sub_octave_switch) {
            octave_mix += s.down1Level() * sub_octave.down1();
            octave_mix += s.down2Level() * sub_octave.down2();
        } else {
            octave_mix += s.down1Level() * octave.down1();
            octave_mix += s.down2Level() * octave.down2();
        }

        auto out_chunk = interpolate(octave_mix);
        for (size_t j = 0; j < out_chunk.size(); ++j)
        {
            float mix = eq2(eq1(out_chunk[j]));

            const auto dry_signal = in[0][i+j];
            mix += s.dryLevel() * dry_signal;

            out[0][i+j] = enable_effect ? mix : dry_signal;
            // out[0][i+j] = osc_signal;
            out[1][i+j] = 0;
        }
    }
}

//=============================================================================
int main()
{
    terrarium.Init(true);
    // These settings are expected by Decimator/Interpolator
    assert(terrarium.seed.AudioSampleRate() == 48000);
    assert(terrarium.seed.AudioBlockSize() % resample_factor == 0);

    auto& knob_dry = terrarium.knobs[0];
    // auto& knob_down2 = terrarium.knobs[3];
    // auto& knob_down1 = terrarium.knobs[4];
    // auto& knob_up1 = terrarium.knobs[5];

    // auto& stomp_bypass = terrarium.stomps[0];

    auto& led_enable = terrarium.leds[0];


    terrarium.seed.StartAudio(processAudioBlock);

    terrarium.Loop(100, [&](){
        interface_state.setDryRatio(knob_dry.Process());
        interface_state.setUp1Ratio(0);
        interface_state.setDown1Ratio(1);
        interface_state.setDown2Ratio(0);

        // if (stomp_bypass.RisingEdge())
        // {
        //     enable_effect = !enable_effect;
        // }

        led_enable.Set(enable_effect ? 1 : 0);
    });
}
