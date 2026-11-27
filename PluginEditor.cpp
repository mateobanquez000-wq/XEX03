#include "PluginEditor.h"

namespace
{
    const juce::Colour colBgTop  (0xff161a1f);
    const juce::Colour colBgBot  (0xff0d1013);
    const juce::Colour colPanel  (0xff1e242b);
    const juce::Colour colAccent (0xff3fd0c0);
    const juce::Colour colText   (0xffe6ece9);
    const juce::Colour colMuted  (0xff7f8b88);
    const juce::Colour colTrack  (0xff343c44);
    const juce::Colour colKeyW   (0xff2a323a);
    const juce::Colour colKeyB   (0xff1a2026);

    juce::Font makeFont (float size, bool bold = false)
    {
        auto opts = juce::FontOptions (size);
        if (bold) opts = opts.withStyle ("Bold");
        return juce::Font (opts);
    }

    struct ScaleDef { const char* name; std::vector<int> degrees; };

    const std::vector<ScaleDef>& scales()
    {
        static const std::vector<ScaleDef> s = {
            { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
            { "Minor",            { 0, 2, 3, 5, 7, 8, 10 } },
            { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
            { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 } },
            { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 } },
            { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
            { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
            { "Blues",            { 0, 3, 5, 6, 7, 10 } },
        };
        return s;
    }

    juce::String midiName (int midi)
    {
        return juce::String (XexProcessor::noteName (midi)) + juce::String (midi / 12 - 1);
    }
}

XexEditor::XexEditor (XexProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addKnob (retune,   "retune",   "RETUNE SPEED",    " ms");
    addKnob (flex,     "flex",     "FLEX-TUNE",       "");
    addKnob (humanize, "humanize", "HUMANIZE",        "");
    addKnob (vibrato,  "vibrato",  "NATURAL VIBRATO", " %");
    addKnob (mix,      "mix",      "MIX",             " %");

    for (int i = 0; i < 12; ++i)
        keyBox.addItem (XexProcessor::noteName (i), i + 1);
    for (size_t i = 0; i < scales().size(); ++i)
        scaleBox.addItem (scales()[i].name, (int) i + 1);

    keyBox.setSelectedItemIndex   ((int) proc.apvts.state.getProperty ("key", 0),   juce::dontSendNotification);
    scaleBox.setSelectedItemIndex ((int) proc.apvts.state.getProperty ("scale", 0), juce::dontSendNotification);

    keyBox.onChange = [this]
    {
        proc.apvts.state.setProperty ("key", keyBox.getSelectedItemIndex(), nullptr);
        applyScale();
    };
    scaleBox.onChange = [this]
    {
        proc.apvts.state.setProperty ("scale", scaleBox.getSelectedItemIndex(), nullptr);
        applyScale();
    };

    for (auto* box : { &keyBox, &scaleBox, &modeBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, colPanel);
        box->setColour (juce::ComboBox::textColourId, colText);
        box->setColour (juce::ComboBox::outlineColourId, colTrack);
        box->setColour (juce::ComboBox::arrowColourId, colAccent);
        addAndMakeVisible (*box);
    }

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("latency")))
        modeBox.addItemList (choice->choices, 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "latency", modeBox);

    modeLabel.setText ("LATENCIA", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    modeLabel.setColour (juce::Label::textColourId, colMuted);
    modeLabel.setFont (makeFont (12.0f, true));
    addAndMakeVisible (modeLabel);

    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteButtons[(size_t) i];
        const bool black = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
        b.setButtonText (XexProcessor::noteName (i));
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, black ? colKeyB : colKeyW);
        b.setColour (juce::TextButton::buttonOnColourId, colAccent);
        b.setColour (juce::TextButton::textColourOffId, colMuted);
        b.setColour (juce::TextButton::textColourOnId, colBgBot);
        addAndMakeVisible (b);
        noteAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.apvts, "note" + juce::String (i), b);
    }

    setSize (820, 470);
    startTimerHz (30);
}

void XexEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, colAccent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, colTrack);
    k.slider.setColour (juce::Slider::thumbColourId, colText);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colText);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colMuted);
    k.label.setFont (makeFont (11.0f, true));

    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

void XexEditor::applyScale()
{
    const int key = keyBox.getSelectedItemIndex();
    const int sc  = scaleBox.getSelectedItemIndex();
    if (key < 0 || sc < 0) return;

    const auto& degrees = scales()[(size_t) sc].degrees;
    for (int pc = 0; pc < 12; ++pc)
    {
        const int interval = ((pc - key) % 12 + 12) % 12;
        const bool on = std::find (degrees.begin(), degrees.end(), interval) != degrees.end();
        if (auto* prm = proc.apvts.getParameter ("note" + juce::String (pc)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
            prm->endChangeGesture();
        }
    }
}

void XexEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (colBgTop, 0.0f, 0.0f, colBgBot, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // cabecera
    g.setColour (colText);
    g.setFont (makeFont (26.0f, true));
    g.drawText ("XEX03", 24, 16, 200, 32, juce::Justification::centredLeft);
    g.setColour (colMuted);
    g.setFont (makeFont (12.0f));
    g.drawText ("afinador vocal  -  fase 1", 24, 46, 300, 16, juce::Justification::centredLeft);
    g.setColour (colAccent);
    g.fillRect (24, 66, 40, 2);

    // panel de perillas
    g.setColour (colPanel);
    g.fillRoundedRectangle (knobPanel.toFloat(), 10.0f);

    // lectura en vivo
    auto r = readoutArea.toFloat();
    g.setColour (colPanel);
    g.fillRoundedRectangle (r, 10.0f);

    if (! shownVoiced)
    {
        g.setColour (colMuted);
        g.setFont (makeFont (14.0f));
        g.drawText ("sin voz detectada", readoutArea, juce::Justification::centred);
    }
    else
    {
        const int detNote = (int) std::lround (shownDet);
        const float detCents = (shownDet - (float) detNote) * 100.0f;

        g.setColour (colText);
        g.setFont (makeFont (30.0f, true));
        g.drawText (midiName (detNote), readoutArea.getX() + 18, readoutArea.getY() + 8, 90, 40, juce::Justification::centredLeft);

        g.setColour (colMuted);
        g.setFont (makeFont (12.0f));
        g.drawText (juce::String (detCents, 0) + " cents", readoutArea.getX() + 18, readoutArea.getY() + 46, 100, 16, juce::Justification::centredLeft);

        if (shownTarget >= 0)
        {
            g.setColour (colMuted);
            g.drawText ("OBJETIVO", readoutArea.getX() + 130, readoutArea.getY() + 14, 80, 14, juce::Justification::centredLeft);
            g.setColour (colAccent);
            g.setFont (makeFont (22.0f, true));
            g.drawText (midiName (shownTarget), readoutArea.getX() + 130, readoutArea.getY() + 28, 80, 28, juce::Justification::centredLeft);

            // barra de desviacion (+-100 cents respecto a la nota objetivo)
            const float bx0 = (float) readoutArea.getX() + 240.0f, bx1 = (float) readoutArea.getRight() - 24.0f;
            const float cy  = (float) readoutArea.getCentreY();
            auto xOf = [&] (float cents) { return bx0 + (bx1 - bx0) * (juce::jlimit (-100.0f, 100.0f, cents) + 100.0f) / 200.0f; };

            g.setColour (colTrack);
            g.fillRoundedRectangle (bx0, cy - 2.0f, bx1 - bx0, 4.0f, 2.0f);
            for (int c = -100; c <= 100; c += 25)
                g.fillRect (xOf ((float) c) - 0.5f, cy - (c == 0 ? 12.0f : 6.0f), 1.0f, c == 0 ? 24.0f : 12.0f);

            g.setColour (colText);
            g.fillEllipse (xOf ((shownDet - (float) shownTarget) * 100.0f) - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.setColour (colAccent);
            g.fillEllipse (xOf ((shownOut - (float) shownTarget) * 100.0f) - 4.0f, cy - 4.0f, 8.0f, 8.0f);

            g.setColour (colMuted);
            g.setFont (makeFont (10.0f));
            g.drawText ("entrada", (int) bx0, (int) cy + 18, 60, 12, juce::Justification::centredLeft);
            g.drawText ("salida",  (int) bx1 - 60, (int) cy + 18, 60, 12, juce::Justification::centredRight);
        }
    }

    // marcas bajo el teclado: punto blanco = nota detectada, turquesa = objetivo
    if (shownVoiced)
    {
        const float cellW = (float) keysArea.getWidth() / 12.0f;
        const float y = (float) keysArea.getBottom() - 5.0f;
        auto dot = [&] (int midi, juce::Colour c, float size)
        {
            if (midi < 0) return;
            const int pc = ((midi % 12) + 12) % 12;
            g.setColour (c);
            g.fillEllipse ((float) keysArea.getX() + ((float) pc + 0.5f) * cellW - size * 0.5f, y - size * 0.5f, size, size);
        };
        dot ((int) std::lround (shownDet), colText, 7.0f);
        dot (shownTarget, colAccent, 5.0f);
    }
}

void XexEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (60);                       // cabecera

    auto row = area.removeFromTop (30);
    keyBox.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (10);
    scaleBox.setBounds (row.removeFromLeft (170));
    modeBox.setBounds (row.removeFromRight (210));
    modeLabel.setBounds (row.removeFromRight (80));

    area.removeFromTop (12);
    knobPanel = area.removeFromTop (170);

    auto knobs = knobPanel.reduced (10, 8);
    Knob* list[] = { &retune, &flex, &humanize, &vibrato, &mix };
    const int cellW = knobs.getWidth() / 5;
    for (auto* k : list)
    {
        auto cell = knobs.removeFromLeft (cellW);
        k->label.setBounds (cell.removeFromTop (20));
        k->slider.setBounds (cell);
    }

    area.removeFromTop (12);
    readoutArea = area.removeFromTop (70);

    area.removeFromTop (10);
    keysArea = area.removeFromTop (50);

    auto keys = keysArea.withTrimmedBottom (12);
    const int keyW = keys.getWidth() / 12;
    for (auto& b : noteButtons)
        b.setBounds (keys.removeFromLeft (keyW).reduced (2, 0));
}

void XexEditor::timerCallback()
{
    const bool  v = proc.telVoiced.load();
    const float d = proc.telDetMidi.load();
    const float o = proc.telOutMidi.load();
    const int   t = proc.telTarget.load();

    if (v != shownVoiced || t != shownTarget || std::fabs (d - shownDet) > 0.005f || std::fabs (o - shownOut) > 0.005f)
    {
        shownVoiced = v; shownDet = d; shownOut = o; shownTarget = t;
        repaint (readoutArea.getUnion (keysArea));
    }
}
