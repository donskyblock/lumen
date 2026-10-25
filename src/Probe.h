#pragma once
#include "Track.h"
#include <optional>

// Reads tags + stream properties with FFmpeg (no separate tag library needed).
std::optional<Track> probeFile(const QString& path);
bool isAudioFile(const QString& suffixLower);
