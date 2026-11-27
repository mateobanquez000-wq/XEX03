#pragma once
#include <array>
#include "PluginProcessor.h"

class XexEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit XexEditor (XexProcessor&);
    ~XexEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyScale();
    void paintGraph (juce::Graphics&, juce::Rectangle<int> area);

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& suffix);

    XexProcessor& proc;

    // fila 1: afinacion + transposicion
    Knob retune, flex, humanize, natVibrato, mix, transpose, detune, formant;
    // fila 2: vibrato creativo
    Knob vibRate, vibDelay, vibOnset, vibVar, vibDepth, vibTrem;

    juce::ComboBox keyBox, scaleBox, modeBox, vibShapeBox;
    juce::Label modeLabel, vibLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, vibShapeAttachment;

    juce::TextButton conformButton, preserveButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> conformAttachment, preserveAttachment;

    std::array<juce::TextButton, 12> noteButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 12> noteAttachments;

    juce::Rectangle<int> graphArea, panel1, panel2, readoutArea, keysArea;

    float shownDet = 0.0f, shownOut = 0.0f;
    int   shownTarget = -1;
    bool  shownVoiced = false;

    float viewCenter = 57.0f, viewSpan = 14.0f;   // zoom suavizado del visualizador

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XexEditor)
};
