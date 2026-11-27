#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

const char* XexProcessor::noteName (int pitchClass)
{
    return kNoteNames[((pitchClass % 12) + 12) % 12];
}

XexProcessor::XexProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout XexProcessor::createLayout()
{
    using NR = juce::NormalisableRange<float>;
    using ID = juce::ParameterID;
    using F  = juce::AudioParameterFloat;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<F> (ID { "retune",   1 }, "Retune Speed (ms)", NR (0.0f, 400.0f, 0.1f, 0.5f), 20.0f));
    layout.add (std::make_unique<F> (ID { "flex",     1 }, "Flex-Tune",         NR (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<F> (ID { "humanize", 1 }, "Humanize",          NR (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<F> (ID { "vibrato",  1 }, "Natural Vibrato",   NR (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<F> (ID { "mix",      1 }, "Mix (%)",           NR (0.0f, 100.0f, 0.1f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ID { "latency", 1 }, "Latency Mode",
        juce::StringArray { "Normal (voces desde 70 Hz)", "Bajo (desde 110 Hz)", "Ultra (desde 180 Hz)" }, 0));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<juce::AudioParameterBool> (
            ID { "note" + juce::String (i), 1 }, juce::String ("Note ") + kNoteNames[i], true));

    return layout;
}

void XexProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    currentMode = (int) apvts.getRawParameterValue ("latency")->load();
    engine.prepare (sampleRate, currentMode);
    setLatencySamples (engine.latencySamples());
}

bool XexProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void XexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Cambio de modo de latencia (reserva memoria: solo ocurre cuando el usuario lo cambia)
    const int mode = (int) apvts.getRawParameterValue ("latency")->load();
    if (mode != currentMode)
    {
        currentMode = mode;
        engine.prepare (currentSampleRate, mode);
        setLatencySamples (engine.latencySamples());
    }

    xex::Params p;
    p.retuneMs = apvts.getRawParameterValue ("retune")->load();
    p.flex     = apvts.getRawParameterValue ("flex")->load();
    p.humanize = apvts.getRawParameterValue ("humanize")->load();
    p.vibrato  = apvts.getRawParameterValue ("vibrato")->load();
    p.mix      = apvts.getRawParameterValue ("mix")->load();
    for (int i = 0; i < 12; ++i)
        p.notes[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i))->load() > 0.5f;
    engine.setParams (p);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Voz monofonica: se suma la entrada a mono y la salida va igual a todos los canales
    for (int i = 0; i < numSamples; ++i)
    {
        float in = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            in += buffer.getReadPointer (c)[i];
        in /= (float) juce::jmax (1, numChannels);

        const float out = engine.process (in);

        for (int c = 0; c < numChannels; ++c)
            buffer.getWritePointer (c)[i] = out;
    }

    const auto& t = engine.telemetry();
    telDetMidi.store (t.detMidi);
    telOutMidi.store (t.outMidi);
    telTarget.store (t.targetNote);
    telVoiced.store (t.voiced);
}

juce::AudioProcessorEditor* XexProcessor::createEditor()
{
    return new XexEditor (*this);
}

void XexProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void XexProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XexProcessor();
}
