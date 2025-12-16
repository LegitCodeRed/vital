// Minimal VST2 SDK stub header for JUCE compatibility
// This is a minimal version for building VST3 plugins only

#ifndef __aeffectx__
#define __aeffectx__

#include "aeffect.h"

struct VstTimeInfo {
  double samplePos;
  double sampleRate;
  double nanoSeconds;
  double ppqPos;
  double tempo;
  double barStartPos;
  double cycleStartPos;
  double cycleEndPos;
  VstInt32 timeSigNumerator;
  VstInt32 timeSigDenominator;
  VstInt32 smpteOffset;
  VstInt32 smpteFrameRate;
  VstInt32 samplesToNextClock;
  VstInt32 flags;
};

enum VstTimeInfoFlags {
  kVstTransportChanged = 1,
  kVstTransportPlaying = 1 << 1,
  kVstTransportCycleActive = 1 << 2,
  kVstTransportRecording = 1 << 3,
  kVstAutomationWriting = 1 << 6,
  kVstAutomationReading = 1 << 7,
  kVstNanosValid = 1 << 8,
  kVstPpqPosValid = 1 << 9,
  kVstTempoValid = 1 << 10,
  kVstBarsValid = 1 << 11,
  kVstCyclePosValid = 1 << 12,
  kVstTimeSigValid = 1 << 13,
  kVstSmpteValid = 1 << 14,
  kVstClockValid = 1 << 15
};

enum VstEventTypes {
  kVstMidiType = 1,
  kVstAudioType,
  kVstVideoType,
  kVstParameterType,
  kVstTriggerType,
  kVstSysExType
};

struct VstParameterProperties {
  float stepFloat;
  float smallStepFloat;
  float largeStepFloat;
  char label[64];
  VstInt32 flags;
  VstInt32 minInteger;
  VstInt32 maxInteger;
  VstInt32 stepInteger;
  VstInt32 largeStepInteger;
  char shortLabel[8];
  VstInt32 displayIndex;
  VstInt32 category;
  VstInt32 numParametersInCategory;
  VstInt32 reserved;
  char categoryLabel[24];
  char future[16];
};

struct VstPinProperties {
  char label[64];
  VstInt32 flags;
  VstInt32 arrangementType;
  char shortLabel[8];
  char future[48];
};

struct VstMidiKeyName {
  char keyName[64];
  VstInt32 reserved;
};

#endif
