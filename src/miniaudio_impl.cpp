// miniaudio is compiled in its own translation unit with everything we don't
// need switched off. Backends (ALSA / PulseAudio / JACK) are dlopen()ed at
// runtime, so there are no extra link-time dependencies. On PipeWire systems
// the PulseAudio backend talks to pipewire-pulse.
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_JACK
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
