// Minimal VST2 SDK stub header for JUCE compatibility
// This is a minimal version for building VST3 plugins only

#ifndef __vstfxstore__
#define __vstfxstore__

#include "aeffect.h"

#if defined(_WIN32)
  #pragma pack(push, 8)
#else
  #pragma pack(push, 4)
#endif

struct fxProgram {
  VstInt32 chunkMagic;
  VstInt32 byteSize;
  VstInt32 fxMagic;
  VstInt32 version;
  VstInt32 fxID;
  VstInt32 fxVersion;
  VstInt32 numParams;
  char prgName[28];
  union {
    float params[1];
    struct {
      VstInt32 size;
      char chunk[1];
    } data;
  } content;
};

struct fxBank {
  VstInt32 chunkMagic;
  VstInt32 byteSize;
  VstInt32 fxMagic;
  VstInt32 version;
  VstInt32 fxID;
  VstInt32 fxVersion;
  VstInt32 numPrograms;
  char future[128];
  union {
    fxProgram programs[1];
    struct {
      VstInt32 size;
      char chunk[1];
    } data;
  } content;
};

#define cMagic CCONST('C', 'c', 'n', 'K')
#define fMagic CCONST('F', 'x', 'C', 'k')
#define bankMagic CCONST('F', 'x', 'B', 'k')
#define chunkPresetMagic CCONST('F', 'P', 'C', 'h')
#define chunkBankMagic CCONST('F', 'B', 'C', 'h')

#pragma pack(pop)

#endif
