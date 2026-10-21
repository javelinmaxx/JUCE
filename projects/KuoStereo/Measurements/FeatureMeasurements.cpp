#include "../Source/PluginProcessor.h"
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
    float center = 12, range = 0, rate = 10, keep = 0, width = 0, guard = 0;
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
    constexpr int segmentLength = sampleRate * 2;
    Samples input (segmentLength * 4);
    Noise random;
    constexpr float frequencies[] { 100.0f, 500.0f, 1350.0f };
    for (size_t i = 0; i < input.size(); ++i)
    {
        const auto segment = static_cast<int> (i) / segmentLength;
        const auto noise = random.next();
        if (segment == 3)
            input[i] = 0.15f * noise;
        else
        {
            const auto phase = juce::MathConstants<float>::twoPi * frequencies[segment]
                             * static_cast<float> (i) / sampleRate;
            input[i] = 0.17f * std::sin (phase) + 0.015f * noise;
        }
    }
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
    constexpr const char* names[] = {
        "100 Hz + noise", "500 Hz + noise", "1350 Hz + noise", "broad noise" };
    for (size_t block = 0; block < adaptive.selected.size(); ++block)
    {
        const auto sample = std::min ((block + 1) * blockSize, input.size() - 1);
        csv << static_cast<double> (sample) / sampleRate << ','
            << names[sample / segmentLength] << ','
            << adaptive.selected[block] << ',' << adaptive.active[block] << '\n';
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
    std::ofstream csv (directory / "transient.csv");
    csv << "time_ms,input,right_normal,right_kept,velvet_side_no_guard,velvet_side_guard\n"
        << std::fixed << std::setprecision (10);
    for (int i = onset - sampleRate / 200; i < onset + sampleRate / 20; ++i)
    {
        const auto index = static_cast<size_t> (i);
        const auto side = baseline.left[index] - baseline.right[index];
        csv << 1000.0 * (i - onset) / sampleRate << ',' << input[index] << ','
            << baseline.right[index] << ',' << kept.right[index] << ','
            << unguarded.left[index] - unguarded.right[index] - side << ','
            << guarded.left[index] - guarded.right[index] - side << '\n';
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
    std::ofstream output (directory / "mono.raw", std::ios::binary);
    if (! output) throw std::runtime_error ("Cannot write mono.raw");
    for (size_t i = sampleRate / 2; i < input.size(); ++i)
    {
        const float row[] { input[i], baseline.left[i], baseline.right[i],
                            widened.left[i], widened.right[i] };
        output.write (reinterpret_cast<const char*> (row), sizeof (row));
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
        measureTransient (directory);
        measureMonoWidth (directory);
        measureScope (directory);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
