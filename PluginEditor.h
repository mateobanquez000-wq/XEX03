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

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& suffix);

    XexProcessor& proc;

    Knob retune, flex, humanize, vibrato, mix;

    juce::ComboBox keyBox, scaleBox, modeBox;
    juce::Label modeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    std::array<juce::TextButton, 12> noteButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 12> noteAttachments;

    juce::Rectangle<int> knobPanel, readoutArea, keysArea;

    float shownDet = 0.0f, shownOut = 0.0f;
    int   shownTarget = -1;
    bool  shownVoiced = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XexEditor)
};
