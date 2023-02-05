/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/



#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleEQAudioProcessor::SimpleEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

SimpleEQAudioProcessor::~SimpleEQAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleEQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleEQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleEQAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..


    juce::dsp::ProcessSpec spec;

    //number of samples processed per block of data
    spec.maximumBlockSize = samplesPerBlock;

    //mono channel
    spec.numChannels = 1;

    //sample rate
    spec.sampleRate = sampleRate;

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    updateFilters();

    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);

    //oscilator for testing the accuracy of the frequency spectrum analyzer
    osc.initialise([](float x) {return std::sin(x); });

    spec.numChannels = getTotalNumOutputChannels();
    osc.prepare(spec);
    osc.setFrequency(500);

}

void SimpleEQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpleEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{

    //left channel = 0
    //right channel = 1



    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateFilters();
    
    juce::dsp::AudioBlock<float> block(buffer);
    
    //generates audio from oscillator for testing
    /*buffer.clear();

    juce::dsp::ProcessContextReplacing<float> stereoContext(block);
    osc.process(stereoContext);*/

    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);

    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);


}

//==============================================================================
bool SimpleEQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleEQAudioProcessor::createEditor()
{
    return new SimpleEQAudioProcessorEditor (*this);
    //return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{

    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);

}

void SimpleEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid()) {

        apvts.replaceState(tree);
        updateFilters();

    }
}


//Purpose: Helper function that returns the parameter settings
ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts) {

    ChainSettings settings;

    //low cut settings
    settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());

    //high cut settings
    settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());

    //peak settings
    settings.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    settings.peakGainInDecibels = apvts.getRawParameterValue("Peak Gain")->load();
    settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    
    //bypass settings
    settings.lowCutBypassed = apvts.getRawParameterValue("LowCut Bypassed")->load() > 0.5f;
    settings.peakBypassed = apvts.getRawParameterValue("Peak Bypassed")->load() > 0.5f;
    settings.highCutBypassed = apvts.getRawParameterValue("HighCut Bypassed")->load() > 0.5f;



    return settings;

}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate) {

    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
        chainSettings.peakFreq,
        chainSettings.peakQuality,
        juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));

}

void SimpleEQAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings) {

    //produces static coefficients for the peak filter
    //auto peakCoefficients = juce::dsp::IIR::Coefficients<float>::
    //    //peak frequency        //peak quality            //peak gain must be converted from gain to decibels using juce::Decibels::decibelsToGain function
    //    makePeakFilter(getSampleRate(), chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));


  //  *leftChain.get<ChainPositions::Peak>().coefficients = *peakCoefficients;
  //  *rightChain.get<ChainPositions::Peak>().coefficients = *peakCoefficients;

    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());

    leftChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);
    rightChain.setBypassed<ChainPositions::Peak>(chainSettings.peakBypassed);



    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

}

void /*SimpleEQAudioProcessor::*/updateCoefficients(Coefficients& old, const Coefficients& replacements) {

    *old = *replacements;

}

void SimpleEQAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings) {


    auto cutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());

    ////LEFT CHAIN
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();

    updateCutFilter(leftLowCut, cutCoefficients, chainSettings.lowCutSlope);

    leftChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);

    //RIGHT CHAIN
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();

    updateCutFilter(rightLowCut, cutCoefficients, chainSettings.lowCutSlope);

    rightChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);

}

void SimpleEQAudioProcessor::updateHighCutFilters(const ChainSettings& chainSettings) {

    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());

    //LEFT CHAIN
    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();

    updateCutFilter(leftHighCut, highCutCoefficients, chainSettings.highCutSlope);

    leftChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);

    //RIGHT CHAIN
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    updateCutFilter(rightHighCut, highCutCoefficients, chainSettings.highCutSlope);

    rightChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);

}

void SimpleEQAudioProcessor::updateFilters() {

    auto chainSettings = getChainSettings(apvts);

    updatePeakFilter(chainSettings);

    updateLowCutFilters(chainSettings);
    updateHighCutFilters(chainSettings);
}



//responsible for the low cut filter
//Purpose: To return the layout with the lowcut, highcut, and mid freq
juce::AudioProcessorValueTreeState::ParameterLayout
SimpleEQAudioProcessor::createParameterLayout() {

    
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray stringArray;


    //Low Cut Frequency
    //juce::NormalisableRange<datatype>(lowvalue, highvalue, intervalchangevalue, skewfactor (<1 lowerend skew focus, >1 higherend skew focus, 1 no skew))
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut Freq", "LowCut Freq", juce::NormalisableRange<float>(
        20.f, 20000.f, 1., 0.40f), 20.f));


    //High Cut Frequency
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut Freq", "HighCut Freq", juce::NormalisableRange<float>(
        20.f, 20000.f, 1., 0.40f), 15000.f));


    //Mid Frequency
    //juce::NormalisableRange<datatype>(lowvalue, highvalue, intervalchangevalue, skewfactor (<1 lowerend skew focus, >1 higherend skew focus, 1 no skew))
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Freq", "Peak Freq", juce::NormalisableRange<float>(
        20.f, 20000.f, 1., 0.40f), 2000.f));


    //Mid Band Gain
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Gain", "Peak Gain", juce::NormalisableRange<float>(
        -24.f, 24.f, 0.5f, 1.f), 0.f));

    //Q
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak Quality", "Peak Quality", juce::NormalisableRange<float>(
        0.1f, 6.f, 0.05f, 1.f), 1.f));


    //Creates the choices for lowcut and highcut slopes
    for (int i = 0; i < 4; ++i) {

        juce::String str;
        str << (12 + i * 12);
        str << "dB/Oct";
        stringArray.add(str);

    }

    layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut Slope", "LowCut Slope", stringArray, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut Slope", "HighCut Slope", stringArray, 0));


    //Parameters for bypass
    layout.add(std::make_unique<juce::AudioParameterBool>("LowCut Bypassed", "LowCut Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Peak Bypassed", "Peak Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("HighCut Bypassed", "HighCut Bypassed", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Analyzer Enabled", "Analyzer Enabled", true));



    return layout;

}




//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleEQAudioProcessor();
}
