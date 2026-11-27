#include <cmath>
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
    const juce::Colour colIn     (0xffff6b6b);   // entrada detectada
    const juce::Colour colTgt    (0xff4aa3ff);   // nota objetivo
    const juce::Colour colOut    (0xff6df2a0);   // salida corregida

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

    void styleToggle (juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, colKeyW);
        b.setColour (juce::TextButton::buttonOnColourId, colAccent);
        b.setColour (juce::TextButton::textColourOffId, colMuted);
        b.setColour (juce::TextButton::textColourOnId, colBgBot);
    }
}

XexEditor::XexEditor (XexProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addKnob (retune,     "retune",    "RETUNE SPEED", " ms");
    addKnob (flex,       "flex",      "FLEX-TUNE",    "");
    addKnob (humanize,   "humanize",  "HUMANIZE",     "");
    addKnob (natVibrato, "vibrato",   "NATURAL VIB.", " %");
    addKnob (mix,        "mix",       "MIX",          " %");
    addKnob (transpose,  "transpose", "TRANSPOSE",    " st");
    addKnob (detune,     "detune",    "DETUNE",       " c");
    addKnob (formant,    "formant",   "FORMANT",      " st");

    addKnob (vibRate,  "vibrate",  "RATE",      " Hz");
    addKnob (vibDelay, "vibdelay", "DELAY",     " ms");
    addKnob (vibOnset, "vibonset", "ONSET",     " ms");
    addKnob (vibVar,   "vibvar",   "VARIATION", " %");
    addKnob (vibDepth, "vibdepth", "DEPTH",     " c");
    addKnob (vibTrem,  "vibtrem",  "TREMOLO",   " %");

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

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("latency")))
        modeBox.addItemList (choice->choices, 1);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("vibshape")))
        vibShapeBox.addItemList (choice->choices, 1);
    modeAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "latency", modeBox);
    vibShapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "vibshape", vibShapeBox);

    for (auto* box : { &keyBox, &scaleBox, &modeBox, &vibShapeBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, colPanel);
        box->setColour (juce::ComboBox::textColourId, colText);
        box->setColour (juce::ComboBox::outlineColourId, colTrack);
        box->setColour (juce::ComboBox::arrowColourId, colAccent);
        addAndMakeVisible (*box);
    }

    for (auto* lab : { &modeLabel, &vibLabel })
    {
        lab->setJustificationType (juce::Justification::centred);
        lab->setColour (juce::Label::textColourId, colMuted);
        lab->setFont (makeFont (11.0f, true));
        addAndMakeVisible (*lab);
    }
    modeLabel.setText ("LATENCIA", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    vibLabel.setText ("VIBRATO CREATIVO", juce::dontSendNotification);

    styleToggle (conformButton,  "CONFORM TO SCALE");
    styleToggle (preserveButton, "FORMANT PRESERVE");
    addAndMakeVisible (conformButton);
    addAndMakeVisible (preserveButton);
    conformAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "conform", conformButton);
    preserveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "fpreserve", preserveButton);

    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteButtons[(size_t) i];
        const bool black = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
        styleToggle (b, XexProcessor::noteName (i));
        b.setColour (juce::TextButton::buttonColourId, black ? colKeyB : colKeyW);
        addAndMakeVisible (b);
        noteAttachments[(size_t) i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.apvts, "note" + juce::String (i), b);
    }

    setSize (900, 730);
    startTimerHz (30);
}

void XexEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, colAccent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, colTrack);
    k.slider.setColour (juce::Slider::thumbColourId, colText);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colText);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, colMuted);
    k.label.setFont (makeFont (10.0f, true));

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

// ---------------------------------------------------------------------------
// Visualizador en tiempo real: ultimos ~6 s de entrada, objetivo y salida
// ---------------------------------------------------------------------------
void XexEditor::paintGraph (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (colPanel);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);

    constexpr int N = 600;
    static_assert (N <= XexProcessor::kVisFrames, "historial insuficiente");

    std::array<float, N> det {}, out {}, tgt {};
    const uint32_t w = proc.visWrite.load (std::memory_order_acquire);
    float lo = 1e9f, hi = -1e9f;
    for (int t = 0; t < N; ++t)
    {
        const size_t slot = (size_t) ((w - (uint32_t) N + (uint32_t) t) & (uint32_t) (XexProcessor::kVisFrames - 1));
        det[(size_t) t] = proc.visDet[slot].load (std::memory_order_relaxed);
        out[(size_t) t] = proc.visOut[slot].load (std::memory_order_relaxed);
        tgt[(size_t) t] = proc.visTgt[slot].load (std::memory_order_relaxed);
        for (float v : { det[(size_t) t], out[(size_t) t], tgt[(size_t) t] })
            if (! std::isnan (v)) { lo = std::min (lo, v); hi = std::max (hi, v); }
    }
    if (hi >= lo)
    {
        viewCenter += (0.5f * (lo + hi) - viewCenter) * 0.12f;
        viewSpan   += (std::max (12.0f, hi - lo + 4.0f) - viewSpan) * 0.12f;
    }

    const auto inner = area.reduced (8, 8).toFloat();
    const float left = inner.getX() + 38.0f, right = inner.getRight();
    const float top = inner.getY(), bottom = inner.getBottom();
    const float vLo = viewCenter - viewSpan * 0.5f;
    auto yOf = [&] (float m) { return bottom - (m - vLo) / viewSpan * (bottom - top); };

    bool allowed[12];
    for (int pc = 0; pc < 12; ++pc)
        allowed[pc] = proc.apvts.getRawParameterValue ("note" + juce::String (pc))->load() > 0.5f;

    // rejilla: una linea por semitono; las notas activas se marcan y se nombran
    for (int k = (int) std::floor (vLo); k <= (int) std::ceil (vLo + viewSpan); ++k)
    {
        const float y = yOf ((float) k);
        if (y < top || y > bottom) continue;
        const bool on = allowed[((k % 12) + 12) % 12];
        g.setColour (on ? colAccent.withAlpha (0.30f) : colTrack.withAlpha (0.35f));
        g.fillRect (left, y - 0.5f, right - left, 1.0f);
        if (on)
        {
            g.setColour (colMuted);
            g.setFont (makeFont (10.0f));
            g.drawText (midiName (k), (int) inner.getX(), (int) y - 6, 34, 12, juce::Justification::centredRight);
        }
    }

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area.reduced (2));

    auto drawCurve = [&] (const std::array<float, N>& v, juce::Colour c, float thickness)
    {
        juce::Path path;
        bool pen = false;
        for (int t = 0; t < N; ++t)
        {
            const float val = v[(size_t) t];
            if (std::isnan (val)) { pen = false; continue; }
            const float x = left + (right - left) * (float) t / (float) (N - 1);
            const float y = yOf (val);
            if (! pen) { path.startNewSubPath (x, y); pen = true; }
            else path.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawCurve (tgt, colTgt.withAlpha (0.85f), 3.5f);
    drawCurve (det, colIn, 1.5f);
    drawCurve (out, colOut, 2.2f);

    // leyenda
    g.setFont (makeFont (10.0f, true));
    int lx = (int) right - 190;
    for (auto [c, name] : { std::pair<juce::Colour, const char*> { colIn, "entrada" },
                            std::pair<juce::Colour, const char*> { colTgt, "objetivo" },
                            std::pair<juce::Colour, const char*> { colOut, "salida" } })
    {
        g.setColour (c);
        g.fillEllipse ((float) lx, top + 3.0f, 7.0f, 7.0f);
        g.setColour (colMuted);
        g.drawText (name, lx + 10, (int) top, 52, 14, juce::Justification::centredLeft);
        lx += 62;
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
    g.drawText ("afinador vocal  -  fase 2", 24, 46, 300, 16, juce::Justification::centredLeft);
    g.setColour (colAccent);
    g.fillRect (24, 66, 40, 2);

    paintGraph (g, graphArea);

    g.setColour (colPanel);
    g.fillRoundedRectangle (panel1.toFloat(), 10.0f);
    g.fillRoundedRectangle (panel2.toFloat(), 10.0f);

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
        g.setFont (makeFont (28.0f, true));
        g.drawText (midiName (detNote), readoutArea.getX() + 18, readoutArea.getY() + 6, 90, 36, juce::Justification::centredLeft);

        g.setColour (colMuted);
        g.setFont (makeFont (12.0f));
        g.drawText (juce::String (detCents, 0) + " cents", readoutArea.getX() + 18, readoutArea.getY() + 42, 100, 16, juce::Justification::centredLeft);

        if (shownTarget >= 0)
        {
            g.setColour (colMuted);
            g.drawText ("OBJETIVO", readoutArea.getX() + 130, readoutArea.getY() + 10, 80, 14, juce::Justification::centredLeft);
            g.setColour (colTgt);
            g.setFont (makeFont (22.0f, true));
            g.drawText (midiName (shownTarget), readoutArea.getX() + 130, readoutArea.getY() + 26, 80, 28, juce::Justification::centredLeft);

            // barra de desviacion (+-100 cents respecto a la nota objetivo)
            const float bx0 = (float) readoutArea.getX() + 240.0f, bx1 = (float) readoutArea.getRight() - 24.0f;
            const float cy  = (float) readoutArea.getCentreY() - 4.0f;
            auto xOf = [&] (float cents) { return bx0 + (bx1 - bx0) * (juce::jlimit (-100.0f, 100.0f, cents) + 100.0f) / 200.0f; };

            g.setColour (colTrack);
            g.fillRoundedRectangle (bx0, cy - 2.0f, bx1 - bx0, 4.0f, 2.0f);
            for (int c = -100; c <= 100; c += 25)
                g.fillRect (xOf ((float) c) - 0.5f, cy - (c == 0 ? 11.0f : 6.0f), 1.0f, c == 0 ? 22.0f : 12.0f);

            g.setColour (colIn);
            g.fillEllipse (xOf ((shownDet - (float) shownTarget) * 100.0f) - 5.0f, cy - 5.0f, 10.0f, 10.0f);
            g.setColour (colOut);
            g.fillEllipse (xOf ((shownOut - (float) shownTarget) * 100.0f) - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        }
    }

    // marcas bajo el teclado: punto blanco = nota detectada, azul = objetivo
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
        dot (shownTarget, colTgt, 5.0f);
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

    area.removeFromTop (10);
    graphArea = area.removeFromTop (190);

    area.removeFromTop (10);
    panel1 = area.removeFromTop (118);
    area.removeFromTop (8);
    panel2 = area.removeFromTop (118);

    auto layoutRow = [] (juce::Rectangle<int> panel, std::vector<Knob*> knobs, int firstCell)
    {
        auto inner = panel.reduced (8, 6);
        const int cellW = inner.getWidth() / 8;
        inner.removeFromLeft (cellW * firstCell);
        for (auto* k : knobs)
        {
            auto cell = inner.removeFromLeft (cellW);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
    };
    layoutRow (panel1, { &retune, &flex, &humanize, &natVibrato, &mix, &transpose, &detune, &formant }, 0);
    layoutRow (panel2, { &vibRate, &vibDelay, &vibOnset, &vibVar, &vibDepth, &vibTrem }, 1);

    // fila 2: selector de forma (primera celda) y conmutadores (ultima celda)
    {
        auto inner = panel2.reduced (8, 6);
        const int cellW = inner.getWidth() / 8;
        auto first = inner.removeFromLeft (cellW);
        vibLabel.setBounds (first.removeFromTop (16));
        vibShapeBox.setBounds (first.reduced (6, 0).removeFromTop (28).translated (0, 20));

        auto last = inner.removeFromRight (cellW);
        conformButton.setBounds  (last.removeFromTop (44).reduced (2, 6));
        preserveButton.setBounds (last.removeFromTop (44).reduced (2, 6));
    }

    area.removeFromTop (10);
    readoutArea = area.removeFromTop (64);

    area.removeFromTop (8);
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

    shownVoiced = v; shownDet = d; shownOut = o; shownTarget = t;
    repaint (graphArea.getUnion (readoutArea).getUnion (keysArea));
}
