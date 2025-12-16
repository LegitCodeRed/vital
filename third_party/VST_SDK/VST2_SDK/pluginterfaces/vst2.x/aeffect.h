// Minimal VST2 SDK stub header for JUCE compatibility
// This is a minimal version for building VST3 plugins only

#ifndef __aeffect__
#define __aeffect__

#if defined(_WIN32) || defined(__CYGWIN__)
  #define VST_EXPORT __declspec(dllexport)
#else
  #define VST_EXPORT __attribute__((visibility("default")))
#endif

typedef long VstInt32;
typedef long long VstInt64;
typedef VstInt64 VstIntPtr;

#define CCONST(a, b, c, d) \
  ((((VstInt32)a) << 24) | (((VstInt32)b) << 16) | (((VstInt32)c) << 8) | (((VstInt32)d) << 0))

#define kEffectMagic CCONST('V', 's', 't', 'P')

const VstInt32 kVstVersion = 2400;

struct ERect {
  short top;
  short left;
  short bottom;
  short right;
};

struct VstEvent {
  VstInt32 type;
  VstInt32 byteSize;
  VstInt32 deltaFrames;
  VstInt32 flags;
  char data[16];
};

struct VstEvents {
  VstInt32 numEvents;
  VstIntPtr reserved;
  VstEvent* events[2];
};

struct VstMidiEvent {
  VstInt32 type;
  VstInt32 byteSize;
  VstInt32 deltaFrames;
  VstInt32 flags;
  VstInt32 noteLength;
  VstInt32 noteOffset;
  char midiData[4];
  char detune;
  char noteOffVelocity;
  char reserved1;
  char reserved2;
};

struct AEffect;

typedef VstIntPtr (*audioMasterCallback)(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (*AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
typedef void (*AEffectProcessProc)(AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void (*AEffectProcessDoubleProc)(AEffect* effect, double** inputs, double** outputs, VstInt32 sampleFrames);
typedef void (*AEffectSetParameterProc)(AEffect* effect, VstInt32 index, float parameter);
typedef float (*AEffectGetParameterProc)(AEffect* effect, VstInt32 index);

struct AEffect {
  VstInt32 magic;
  AEffectDispatcherProc dispatcher;
  AEffectProcessProc process;
  AEffectSetParameterProc setParameter;
  AEffectGetParameterProc getParameter;
  VstInt32 numPrograms;
  VstInt32 numParams;
  VstInt32 numInputs;
  VstInt32 numOutputs;
  VstInt32 flags;
  VstIntPtr resvd1;
  VstIntPtr resvd2;
  VstInt32 initialDelay;
  VstInt32 realQualities;
  VstInt32 offQualities;
  float ioRatio;
  void* object;
  void* user;
  VstInt32 uniqueID;
  VstInt32 version;
  AEffectProcessProc processReplacing;
  AEffectProcessDoubleProc processDoubleReplacing;
  char future[56];
};

enum {
  effFlagsHasEditor = 1 << 0,
  effFlagsCanReplacing = 1 << 4,
  effFlagsProgramChunks = 1 << 5,
  effFlagsIsSynth = 1 << 8,
  effFlagsNoSoundInStop = 1 << 9,
  effFlagsCanDoubleReplacing = 1 << 12
};

enum {
  kPlugCategEffect = 1,
  kPlugCategSynth = 2,
  kPlugCategAnalysis = 3,
  kPlugCategMastering = 4,
  kPlugCategSpacializer = 5,
  kPlugCategRoomFx = 6,
  kPlugCategSurroundFx = 7,
  kPlugCategRestoration = 8,
  kPlugCategOfflineProcess = 9,
  kPlugCategShell = 10,
  kPlugCategGenerator = 11,
  kPlugCategMaxCount = 12
};

enum AEffectOpcodes {
  effOpen = 0,
  effClose,
  effSetProgram,
  effGetProgram,
  effSetProgramName,
  effGetProgramName,
  effGetParamLabel,
  effGetParamDisplay,
  effGetParamName,
  effGetVu,
  effSetSampleRate,
  effSetBlockSize,
  effMainsChanged,
  effEditGetRect,
  effEditOpen,
  effEditClose,
  effEditDraw,
  effEditMouse,
  effEditKey,
  effEditIdle,
  effEditTop,
  effEditSleep,
  effIdentify,
  effGetChunk,
  effSetChunk,
  effProcessEvents,
  effCanBeAutomated,
  effString2Parameter,
  effGetNumProgramCategories,
  effGetProgramNameIndexed,
  effCopyProgram,
  effConnectInput,
  effConnectOutput,
  effGetInputProperties,
  effGetOutputProperties,
  effGetPlugCategory,
  effGetCurrentPosition,
  effGetDestinationBuffer,
  effOfflineNotify,
  effOfflinePrepare,
  effOfflineRun,
  effProcessVarIo,
  effSetSpeakerArrangement,
  effSetBlockSizeAndSampleRate,
  effSetBypass,
  effGetEffectName,
  effGetErrorText,
  effGetVendorString,
  effGetProductString,
  effGetVendorVersion,
  effVendorSpecific,
  effCanDo,
  effGetTailSize,
  effIdle,
  effGetIcon,
  effSetViewPosition,
  effGetParameterProperties,
  effKeysRequired,
  effGetVstVersion,
  effEditKeyDown,
  effEditKeyUp,
  effSetEditKnobMode,
  effGetMidiProgramName,
  effGetCurrentMidiProgram,
  effGetMidiProgramCategory,
  effHasMidiProgramsChanged,
  effGetMidiKeyName,
  effBeginSetProgram,
  effEndSetProgram,
  effGetSpeakerArrangement,
  effShellGetNextPlugin,
  effStartProcess,
  effStopProcess,
  effSetTotalSampleToProcess,
  effSetPanLaw,
  effBeginLoadBank,
  effBeginLoadProgram,
  effSetProcessPrecision,
  effGetNumMidiInputChannels,
  effGetNumMidiOutputChannels
};

enum AudioMasterOpcodes {
  audioMasterAutomate = 0,
  audioMasterVersion,
  audioMasterCurrentId,
  audioMasterIdle,
  audioMasterPinConnected,
  audioMasterWantMidi = 6,
  audioMasterGetTime,
  audioMasterProcessEvents,
  audioMasterSetTime,
  audioMasterTempoAt,
  audioMasterGetNumAutomatableParameters,
  audioMasterGetParameterQuantization,
  audioMasterIOChanged,
  audioMasterNeedIdle,
  audioMasterSizeWindow,
  audioMasterGetSampleRate,
  audioMasterGetBlockSize,
  audioMasterGetInputLatency,
  audioMasterGetOutputLatency,
  audioMasterGetPreviousPlug,
  audioMasterGetNextPlug,
  audioMasterWillReplaceOrAccumulate,
  audioMasterGetCurrentProcessLevel,
  audioMasterGetAutomationState,
  audioMasterOfflineStart,
  audioMasterOfflineRead,
  audioMasterOfflineWrite,
  audioMasterOfflineGetCurrentPass,
  audioMasterOfflineGetCurrentMetaPass,
  audioMasterSetOutputSampleRate,
  audioMasterGetOutputSpeakerArrangement,
  audioMasterGetVendorString,
  audioMasterGetProductString,
  audioMasterGetVendorVersion,
  audioMasterVendorSpecific,
  audioMasterSetIcon,
  audioMasterCanDo,
  audioMasterGetLanguage,
  audioMasterOpenWindow,
  audioMasterCloseWindow,
  audioMasterGetDirectory,
  audioMasterUpdateDisplay,
  audioMasterBeginEdit,
  audioMasterEndEdit,
  audioMasterOpenFileSelector,
  audioMasterCloseFileSelector,
  audioMasterEditFile,
  audioMasterGetChunkFile,
  audioMasterGetInputSpeakerArrangement
};

#endif
