#include <cmath>
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
    for (int i = 0; i < kVisFrames; ++i)
    {
        visDet[(size_t) i].store (std::nanf (""));
        visOut[(size_t) i].store (std::nanf (""));
        visTgt[(size_t) i].store (std::nanf (""));
    }
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

    // Fase 2: transposicion, formantes y vibrato creativo
    layout.add (std::make_unique<F> (ID { "transpose", 1 }, "Transpose (st)",   NR (-12.0f, 12.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (ID { "conform", 1 }, "Conform to Scale", false));
    layout.add (std::make_unique<F> (ID { "detune",    1 }, "Detune (cents)",   NR (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<F> (ID { "formant",   1 }, "Formant (st)",     NR (-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (ID { "fpreserve", 1 }, "Formant Preserve", true));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        ID { "vibshape", 1 }, "Vibrato Shape", juce::StringArray { "Off", "Sine", "Sawtooth", "Square" }, 0));
    layout.add (std::make_unique<F> (ID { "vibrate",  1 }, "Vibrato Rate (Hz)",    NR (0.5f, 12.0f, 0.01f), 5.5f));
    layout.add (std::make_unique<F> (ID { "vibdelay", 1 }, "Vibrato Delay (ms)",   NR (0.0f, 1500.0f, 1.0f), 300.0f));
    layout.add (std::make_unique<F> (ID { "vibonset", 1 }, "Vibrato Onset (ms)",   NR (0.0f, 1500.0f, 1.0f), 400.0f));
    layout.add (std::make_unique<F> (ID { "vibvar",   1 }, "Vibrato Variation",    NR (0.0f, 100.0f, 0.1f), 20.0f));
    layout.add (std::make_unique<F> (ID { "vibdepth", 1 }, "Vibrato Depth (cents)", NR (0.0f, 100.0f, 0.1f), 30.0f));
    layout.add (std::make_unique<F> (ID { "vibtrem",  1 }, "Tremolo",              NR (0.0f, 100.0f, 0.1f), 0.0f));

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
    visHop = juce::jmax (1, (int) std::lround (sampleRate / 100.0));
    visCounter = 0;
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
    p.transpose       = apvts.getRawParameterValue ("transpose")->load();
    p.conform         = apvts.getRawParameterValue ("conform")->load() > 0.5f;
    p.detune          = apvts.getRawParameterValue ("detune")->load();
    p.formant         = apvts.getRawParameterValue ("formant")->load();
    p.formantPreserve = apvts.getRawParameterValue ("fpreserve")->load() > 0.5f;
    p.vibShape        = (int) apvts.getRawParameterValue ("vibshape")->load();
    p.vibRate         = apvts.getRawParameterValue ("vibrate")->load();
    p.vibDelay        = apvts.getRawParameterValue ("vibdelay")->load();
    p.vibOnset        = apvts.getRawParameterValue ("vibonset")->load();
    p.vibVariation    = apvts.getRawParameterValue ("vibvar")->load();
    p.vibDepth        = apvts.getRawParameterValue ("vibdepth")->load();
    p.vibTrem         = apvts.getRawParameterValue ("vibtrem")->load();
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

        if (++visCounter >= visHop)
        {
            visCounter = 0;
            const auto& tel = engine.telemetry();
            const uint32_t w = visWrite.load (std::memory_order_relaxed);
            const size_t slot = (size_t) (w & (uint32_t) (kVisFrames - 1));
            visDet[slot].store (tel.voiced ? tel.detMidi : std::nanf (""), std::memory_order_relaxed);
            visOut[slot].store (tel.voiced ? tel.outMidi : std::nanf (""), std::memory_order_relaxed);
            visTgt[slot].store (tel.voiced && tel.targetNote >= 0 ? (float) tel.targetNote : std::nanf (""), std::memory_order_relaxed);
            visWrite.store (w + 1, std::memory_order_release);
        }
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
