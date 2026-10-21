#include "../Source/PluginProcessor.h"
#include "SynthPad.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int sampleRate = 48000, blockSize = 256;
using Samples = std::vector<float>;

struct Noise
{
    uint32_t state = 0x4b554f31u;
    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state) / 2147483648.0f - 1.0f;
    }
};

void set (KuoStereoProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    if (parameter == nullptr) throw std::runtime_error (std::string ("Missing parameter: ") + id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

struct Settings
{
    float center = 12, range = 0, rate = 10, keep = 0, width = 0, guard = 0, guardAttack = 0;
    float low = 100, mid = 100, high = 100;
};

struct Render
{
    Samples left, right;
    std::vector<float> selected, active;
};

Render render (const Samples& input, const Settings& settings)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", settings.center);
    set (processor, "haasRange", settings.range);
    set (processor, "haasRate", settings.rate);
    set (processor, "haasKeepTransient", settings.keep);
    set (processor, "width", settings.width);
    set (processor, "transientProtect", settings.guard);
    set (processor, "guardAttack", settings.guardAttack);
    set (processor, "velvetLow", settings.low);
    set (processor, "velvetMid", settings.mid);
    set (processor, "velvetHigh", settings.high);
    processor.prepareToPlay (sampleRate, blockSize);
    Render result;
    result.left.resize (input.size());
    result.right.resize (input.size());
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (size_t start = 0; start < input.size(); start += blockSize)
    {
        buffer.clear();
        const auto count = std::min (static_cast<size_t> (blockSize), input.size() - start);
        for (size_t i = 0; i < count; ++i)
        {
            buffer.setSample (0, static_cast<int> (i), input[start + i]);
            buffer.setSample (1, static_cast<int> (i), input[start + i]);
        }
        processor.processBlock (buffer, midi);
        for (size_t i = 0; i < count; ++i)
        {
            result.left[start + i] = buffer.getSample (0, static_cast<int> (i));
            result.right[start + i] = buffer.getSample (1, static_cast<int> (i));
        }
        result.selected.push_back (processor.getSelectedDelayMs());
        result.active.push_back (processor.getCurrentDelayMs());
    }
    return result;
}

void writeRaw (const std::filesystem::path& path, const std::vector<const Samples*>& columns)
{
    std::ofstream output (path, std::ios::binary);
    if (! output) throw std::runtime_error ("Cannot write " + path.string());
    const auto count = columns.front()->size();
    for (size_t i = 0; i < count; ++i)
        for (const auto* column : columns)
            output.write (reinterpret_cast<const char*> (&(*column)[i]), sizeof (float));
}

void measureAdaptation (const std::filesystem::path& directory)
{
    Samples input (kuoTestSignal::padSamples);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = kuoTestSignal::padSample (static_cast<int> (i));
    const auto adaptive = render (input, { 12, 6, 10, 0, 0, 0 });
    const auto fixed = render (input, { 12, 0, 10, 0, 0, 0 });
    Samples adaptiveMid (input.size()), fixedMid (input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        adaptiveMid[i] = 0.5f * (adaptive.left[i] + adaptive.right[i]);
        fixedMid[i] = 0.5f * (fixed.left[i] + fixed.right[i]);
    }
    writeRaw (directory / "adaptation.raw", { &input, &adaptiveMid, &fixedMid });
    std::ofstream csv (directory / "adaptation.csv");
    csv << "time_s,content,target_ms,active_ms\n" << std::fixed << std::setprecision (9);
    for (size_t block = 0; block < adaptive.selected.size(); ++block)
    {
        const auto sample = std::min ((block + 1) * blockSize, input.size() - 1);
        csv << static_cast<double> (sample) / sampleRate << ','
            << (sample < kuoTestSignal::chordChangeSample ? "C major pad" : "A minor pad") << ','
            << adaptive.selected[block] << ',' << adaptive.active[block] << '\n';
    }
}

void measureSignedDelay (const std::filesystem::path& directory)
{
    Samples input (sampleRate / 25, 0);
    input[sampleRate / 200] = 1.0f;
    const auto positive = render (input, { 12, 0, 10, 0, 0, 0 });
    const auto zero = render (input, { 0, 0, 10, 0, 0, 0 });
    const auto negative = render (input, { -12, 0, 10, 0, 0, 0 });
    std::ofstream csv (directory / "signed_delay.csv");
    csv << "time_ms,input,positive_left,positive_right,zero_left,zero_right,"
           "negative_left,negative_right\n" << std::fixed << std::setprecision (9);
    for (size_t i = 0; i < input.size(); ++i)
        csv << 1000.0 * static_cast<double> (i) / sampleRate << ',' << input[i] << ','
            << positive.left[i] << ',' << positive.right[i] << ','
            << zero.left[i] << ',' << zero.right[i] << ','
            << negative.left[i] << ',' << negative.right[i] << '\n';
}

void measureZeroPreference (const std::filesystem::path& directory)
{
    std::ofstream csv (directory / "zero_preference.csv");
    csv << "source,selected_ms\n" << std::fixed << std::setprecision (5);
    for (const auto alreadyWide : { false, true })
    {
        KuoStereoProcessor processor;
        set (processor, "haasCenter", 0);
        set (processor, "haasRange", 3);
        set (processor, "width", 0);
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int block = 0; block < 150; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto phase = juce::MathConstants<float>::twoPi * 523.0f
                    * static_cast<float> (block * blockSize + i) / sampleRate;
                buffer.setSample (0, i, 0.2f * std::sin (phase));
                buffer.setSample (1, i, 0.2f * std::sin (phase + (alreadyWide ? 0.6f : 0.0f)));
            }
            processor.processBlock (buffer, midi);
        }
        csv << (alreadyWide ? "already-wide" : "mono") << ','
            << processor.getSelectedDelayMs() << '\n';
    }
}

void measureHaasSlew (const std::filesystem::path& directory)
{
    KuoStereoProcessor processor;
    set (processor, "haasCenter", 12);
    set (processor, "haasRange", 0);
    set (processor, "haasRate", 10);
    set (processor, "width", 0);
    processor.prepareToPlay (sampleRate, blockSize);
    set (processor, "haasCenter", 20);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::ofstream csv (directory / "haas_slew.csv");
    csv << "time_s,active_ms\n" << std::fixed << std::setprecision (9)
        << "0.000000000,12.000000000\n";
    for (int block = 0; block < 300; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        csv << static_cast<double> ((block + 1) * blockSize) / sampleRate << ','
            << processor.getCurrentDelayMs() << '\n';
    }
}

void measureTransient (const std::filesystem::path& directory)
{
    constexpr int onset = sampleRate / 10;
    Samples input (sampleRate * 2 / 5);
    Noise random;
    for (auto& sample : input) sample = 0.002f * random.next();
    input[onset] += 1.0f;
    for (int i = 1; i < 160; ++i)
        input[static_cast<size_t> (onset + i)] += 0.25f
            * std::exp (-static_cast<float> (i) / 28.0f) * random.next();
    const auto baseline = render (input, { 12, 0, 10, 0, 0, 0 });
    const auto kept = render (input, { 12, 0, 10, 100, 0, 0 });
    const auto unguarded = render (input, { 12, 0, 10, 0, 100, 0 });
    const auto guarded = render (input, { 12, 0, 10, 0, 100, 100 });
    const auto guardedFast = render (input, { 12, 0, 10, 0, 100, 100, 0.5f });
    const auto guardedSlow = render (input, { 12, 0, 10, 0, 100, 100, 5.0f });
    std::ofstream csv (directory / "transient.csv");
    csv << "time_ms,input,right_normal,right_kept,velvet_side_no_guard,velvet_side_guard,"
           "velvet_side_attack_half_ms,velvet_side_attack_five_ms\n"
        << std::fixed << std::setprecision (10);
    for (int i = onset - sampleRate / 200; i < onset + sampleRate / 20; ++i)
    {
        const auto index = static_cast<size_t> (i);
        const auto side = baseline.left[index] - baseline.right[index];
        csv << 1000.0 * (i - onset) / sampleRate << ',' << input[index] << ','
            << baseline.right[index] << ',' << kept.right[index] << ','
            << unguarded.left[index] - unguarded.right[index] - side << ','
            << guarded.left[index] - guarded.right[index] - side << ','
            << guardedFast.left[index] - guardedFast.right[index] - side << ','
            << guardedSlow.left[index] - guardedSlow.right[index] - side << '\n';
    }
    std::ofstream delay (directory / "transient_delay.csv");
    delay << "time_ms,normal_ms,kept_ms\n" << std::fixed << std::setprecision (9);
    for (size_t block = 0; block < kept.active.size(); ++block)
    {
        const auto time = 1000.0 * ((block + 1) * blockSize - onset) / sampleRate;
        if (time >= -10 && time <= 120)
            delay << time << ',' << baseline.active[block] << ',' << kept.active[block] << '\n';
    }
}

void measureGuardNoise (const std::filesystem::path& directory)
{
    Samples input (sampleRate / 8);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                   * static_cast<float> (i) / sampleRate);
    const auto unguarded = render (input, { 0, 0, 10, 0, 100, 0 });
    const auto instant = render (input, { 0, 0, 10, 0, 100, 100, 0 });
    const auto fast = render (input, { 0, 0, 10, 0, 100, 100, 0.5f });
    const auto medium = render (input, { 0, 0, 10, 0, 100, 100, 2.0f });
    const auto slow = render (input, { 0, 0, 10, 0, 100, 100, 3.0f });
    std::ofstream csv (directory / "guard_noise.csv");
    csv << "time_ms,unguarded_side,instant_guard_side,fast_guard_side,"
           "medium_guard_side,slow_guard_side\n"
        << std::fixed << std::setprecision (10);
    for (size_t i = 0; i < input.size(); ++i)
        csv << 1000.0 * static_cast<double> (i) / sampleRate << ','
            << 0.5f * (unguarded.left[i] - unguarded.right[i]) << ','
            << 0.5f * (instant.left[i] - instant.right[i]) << ','
            << 0.5f * (fast.left[i] - fast.right[i]) << ','
            << 0.5f * (medium.left[i] - medium.right[i]) << ','
            << 0.5f * (slow.left[i] - slow.right[i]) << '\n';
}

void measureHaasOnsetNoise (const std::filesystem::path& directory)
{
    Samples input (sampleRate / 8);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                   * static_cast<float> (i) / sampleRate);
    const auto normal = render (input, { 12, 0, 10, 0, 0, 0 });
    const auto kept = render (input, { 12, 0, 10, 100, 0, 0 });
    std::ofstream csv (directory / "haas_onset_noise.csv");
    csv << "time_ms,input,right_normal,right_kept\n" << std::fixed << std::setprecision (10);
    for (size_t i = 0; i < input.size(); ++i)
        csv << 1000.0 * static_cast<double> (i) / sampleRate << ','
            << input[i] << ',' << normal.right[i] << ',' << kept.right[i] << '\n';
}

void measureSpectralOnset (const std::filesystem::path& directory)
{
    Samples input (sampleRate * 6 / 5);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto frequency = i < sampleRate ? 100.0f : 5000.0f;
        input[i] = 0.18f * std::sin (juce::MathConstants<float>::twoPi * frequency
                                    * static_cast<float> (i) / sampleRate);
    }
    const auto normal = render (input, { 12, 0, 10, 0, 0, 0 });
    const auto kept = render (input, { 12, 0, 10, 100, 0, 0 });
    std::ofstream csv (directory / "spectral_onset.csv");
    csv << "time_ms,normal_ms,kept_ms\n" << std::fixed << std::setprecision (9);
    for (size_t block = 0; block < kept.active.size(); ++block)
        csv << 1000.0 * (static_cast<double> ((block + 1) * blockSize) - sampleRate) / sampleRate << ','
            << normal.active[block] << ',' << kept.active[block] << '\n';
}

void measureDetectorSelectivity (const std::filesystem::path& directory)
{
    Samples texture (sampleRate * 3 / 2);
    for (size_t i = 0; i < texture.size(); ++i)
    {
        const auto time = static_cast<float> (i) / sampleRate;
        texture[i] = 0.08f * (1.0f + 0.15f * std::sin (
            juce::MathConstants<float>::twoPi * 5.0f * time))
            * std::sin (juce::MathConstants<float>::twoPi * 700.0f * time);
    }
    auto attacked = texture;
    attacked[sampleRate] += 1.0f;
    const auto steady = render (texture, { 12, 0, 10, 100, 0, 0 });
    const auto transient = render (attacked, { 12, 0, 10, 100, 0, 0 });
    std::ofstream csv (directory / "detector_selectivity.csv");
    csv << "time_ms,steady_ms,transient_ms\n" << std::fixed << std::setprecision (8);
    for (size_t block = 0; block < steady.active.size(); ++block)
        csv << 1000.0 * (static_cast<double> ((block + 1) * blockSize) - sampleRate)
                           / sampleRate << ','
            << steady.active[block] << ',' << transient.active[block] << '\n';
}

void measureMonoWidth (const std::filesystem::path& directory)
{
    Samples input (sampleRate * 2);
    Noise random;
    float smooth = 0;
    const auto coefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 6000 / sampleRate);
    for (auto& sample : input)
    {
        smooth += coefficient * (random.next() - smooth);
        sample = 0.18f * smooth;
    }
    const auto baseline = render (input, { 12, 0, 10, 0, 0, 0 });
    const auto widened = render (input, { 12, 0, 10, 0, 100, 0 });
    const auto guarded = render (input, { 12, 0, 10, 0, 100, 100 });
    std::ofstream output (directory / "mono.raw", std::ios::binary);
    if (! output) throw std::runtime_error ("Cannot write mono.raw");
    for (size_t i = sampleRate / 2; i < input.size(); ++i)
    {
        const float row[] { input[i], baseline.left[i], baseline.right[i],
                            widened.left[i], widened.right[i], guarded.left[i], guarded.right[i] };
        output.write (reinterpret_cast<const char*> (row), sizeof (row));
    }
}

void measureVelvetBands (const std::filesystem::path& directory)
{
    std::ofstream csv (directory / "velvet_bands.csv");
    csv << "tone_hz,low_only,mid_only,high_only,all_off\n"
        << std::fixed << std::setprecision (8);
    for (const auto frequency : { 80.0f, 700.0f, 5000.0f })
    {
        Samples input (sampleRate / 2);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi * frequency
                * static_cast<float> (i) / sampleRate);
        const auto sideRms = [&] (const Settings& settings)
        {
            const auto output = render (input, settings);
            double energy = 0;
            for (size_t i = sampleRate / 4; i < input.size(); ++i)
            {
                const auto side = 0.5f * (output.left[i] - output.right[i]);
                energy += side * side;
            }
            return std::sqrt (energy / (input.size() - sampleRate / 4));
        };
        csv << frequency << ','
            << sideRms ({ 0, 0, 10, 0, 100, 0, 0, 100, 0, 0 }) << ','
            << sideRms ({ 0, 0, 10, 0, 100, 0, 0, 0, 100, 0 }) << ','
            << sideRms ({ 0, 0, 10, 0, 100, 0, 0, 0, 0, 100 }) << ','
            << sideRms ({ 0, 0, 10, 0, 100, 0, 0, 0, 0, 0 }) << '\n';
    }
}

void measureScope (const std::filesystem::path& directory)
{
    KuoStereoProcessor processor;
    set (processor, "haasRange", 0);
    set (processor, "width", 0);
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < sampleRate / blockSize; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto time = static_cast<float> (block * blockSize + i) / sampleRate;
            const auto bass = 0.25f * std::sin (juce::MathConstants<float>::twoPi * 80 * time);
            buffer.setSample (0, i, bass
                + 0.2f * std::sin (juce::MathConstants<float>::twoPi * 700 * time)
                + 0.12f * std::sin (juce::MathConstants<float>::twoPi * 5000 * time));
            buffer.setSample (1, i, bass
                + 0.2f * std::sin (juce::MathConstants<float>::twoPi * 700 * time + 0.2f)
                + 0.12f * std::sin (juce::MathConstants<float>::twoPi * 5000 * time + 0.5f));
        }
        processor.processBlock (buffer, midi);
    }
    std::ofstream csv (directory / "scope.csv");
    csv << "band,left,right\n" << std::fixed << std::setprecision (7);
    constexpr const char* names[] = { "low", "mid", "high" };
    for (int band = 0; band < 3; ++band)
    {
        std::array<juce::Point<float>, KuoStereoProcessor::scopePoints> points;
        processor.readScope (band, points);
        for (const auto& point : points)
            csv << names[band] << ',' << point.x << ',' << point.y << '\n';
    }
}
}

int main (int argc, char** argv)
{
    if (argc != 2) return 2;
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        const std::filesystem::path directory (argv[1]);
        std::filesystem::create_directories (directory);
        measureAdaptation (directory);
        measureSignedDelay (directory);
        measureZeroPreference (directory);
        measureHaasSlew (directory);
        measureTransient (directory);
        measureGuardNoise (directory);
        measureHaasOnsetNoise (directory);
        measureSpectralOnset (directory);
        measureDetectorSelectivity (directory);
        measureMonoWidth (directory);
        measureVelvetBands (directory);
        measureScope (directory);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
