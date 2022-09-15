/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AmpSim2022AudioProcessor::AmpSim2022AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       )
, mTreeState(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    mTreeState.addParameterListener(inputID, this);
    mTreeState.addParameterListener(speakerToggleID, this);
}

AmpSim2022AudioProcessor::~AmpSim2022AudioProcessor()
{
    mTreeState.removeParameterListener(inputID, this);
    mTreeState.removeParameterListener(speakerToggleID, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout AmpSim2022AudioProcessor::createParameterLayout()
{
    std::vector <std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(inputID, inputName, -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(outputID, outputName, -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(speakerToggleID, speakerToggleName, true));
    
    return { params.begin(), params.end() };
    
}

void AmpSim2022AudioProcessor::parameterChanged(const juce::String &parameterID, float newValue)
{
    updateParams();
}

void AmpSim2022AudioProcessor::updateParams()
{
    mAmpDisModule.setDrive(mTreeState.getRawParameterValue(inputID)->load());
}

//==============================================================================
const juce::String AmpSim2022AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AmpSim2022AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AmpSim2022AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AmpSim2022AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AmpSim2022AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AmpSim2022AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AmpSim2022AudioProcessor::getCurrentProgram()
{
    return 0;
}

void AmpSim2022AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String AmpSim2022AudioProcessor::getProgramName (int index)
{
    return {};
}

void AmpSim2022AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void AmpSim2022AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mSpec.maximumBlockSize = samplesPerBlock;
    mSpec.sampleRate = sampleRate;
    mSpec.numChannels = getTotalNumOutputChannels();
    
    mSpeakerModule.prepare(mSpec);
    mSpeakerModule.loadImpulseResponse(BinaryData::GuitarHack_Edge_Straight_10_wav, BinaryData::GuitarHack_Edge_Straight_10_wavSize, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0);
    
    mSpeakerCompensate.prepare(mSpec);
    mSpeakerCompensate.setRampDurationSeconds(0.02);
    mSpeakerCompensate.setGainDecibels(6.0);
    
    mAmpDisModule.prepare(mSpec);
    mAmpDisModule.setClipperType(viator_dsp::Distortion<float>::ClipType::kTube);
    
    mPreHPFilter.prepare(mSpec);
    mPreHPFilter.setStereoType(viator_dsp::SVFilter<float>::StereoId::kStereo);
    mPreHPFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kType, viator_dsp::SVFilter<float>::FilterType::kHighPass);
    mPreHPFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kQType, viator_dsp::SVFilter<float>::QType::kParametric);
    mPreHPFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kCutoff, 150.0);
    
    mPreMidFilter.prepare(mSpec);
    mPreMidFilter.setStereoType(viator_dsp::SVFilter<float>::StereoId::kStereo);
    mPreMidFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kType, viator_dsp::SVFilter<float>::FilterType::kBandShelf);
    mPreMidFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kQType, viator_dsp::SVFilter<float>::QType::kParametric);
    mPreMidFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kCutoff, 1000.0);
    mPreMidFilter.setParameter(viator_dsp::SVFilter<float>::ParameterId::kGain, 15.0);
    
    updateParams();
}

void AmpSim2022AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AmpSim2022AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void AmpSim2022AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    juce::dsp::AudioBlock<float> block {buffer};
    
    mPreHPFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    mPreMidFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    mAmpDisModule.process(juce::dsp::ProcessContextReplacing<float>(block));
    
    if (mTreeState.getRawParameterValue(speakerToggleID)->load())
    {
        mSpeakerModule.process(juce::dsp::ProcessContextReplacing<float>(block));
    }
    
    mSpeakerCompensate.process(juce::dsp::ProcessContextReplacing<float>(block));
}

//==============================================================================
bool AmpSim2022AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AmpSim2022AudioProcessor::createEditor()
{
    //return new AmpSim2022AudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void AmpSim2022AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void AmpSim2022AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpSim2022AudioProcessor();
}
