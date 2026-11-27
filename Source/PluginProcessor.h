#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/XexEngine.h"

class XexProcessor : public juce::AudioProcessor
{
public:
    XexProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "XEX03"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static const char* noteName (int pitchClass);

    juce::AudioProcessorValueTreeState apvts;

    // Datos en vivo para la interfaz (se escriben en el hilo de audio)
    std::atomic<float> telDetMidi { 0.0f };
    std::atomic<float> telOutMidi { 0.0f };
    std::atomic<int>   telTarget  { -1 };
    std::atomic<bool>  telVoiced  { false };

    // Historial para el visualizador (anillo sin bloqueos, un valor cada ~10 ms; NaN = sin dato)
    static constexpr int kVisFrames = 1024;   // potencia de 2
    std::array<std::atomic<float>, kVisFrames> visDet, visOut, visTgt;
    std::atomic<uint32_t> visWrite { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    xex::Engine engine;
    double currentSampleRate = 48000.0;
    int currentMode = -1;
    int visCounter = 0, visHop = 480;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XexProcessor)
};
