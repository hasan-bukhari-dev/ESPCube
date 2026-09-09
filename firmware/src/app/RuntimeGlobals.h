#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ESPCubeHID.h>
#include <ESPCubeSpeech.h>
#include <ESPCubeSpeechLink.h>
#include <ESPCubeSpeakerControl.h>
#include <ESPCubeSpeakerPlayback.h>
#include <ESPCubeSpeakerVolume.h>
#include "../input/Buttons.h"
#include "../input/Touch.h"
#include "../input/Motion.h"
#include "HomeGesture.h"
#include "ProfileManager.h"
#include "../profiles/TextProfile.h"
#include "../profiles/MouseProfile.h"
#include "../profiles/SettingsProfile.h"
#include "../profiles/SpeakerProfile.h"

extern Arduino_GFX *gfx;
extern Buttons buttons;
extern Touch touch;
extern Motion motion;
extern HomeGesture homeGesture;
extern ProfileManager profiles;
extern MouseProfile mouseProfile;
extern TextProfile textProfile;
extern SettingsProfile settingsProfile;
extern SpeakerProfile speakerProfile;
extern ESPCubeHID hid;
extern ESPCubeSpeech speech;
extern ESPCubeSpeechLink speechLink;
extern ESPCubeSpeakerControl speakerControl;
extern ESPCubeSpeakerPlayback speakerPlayback;
extern ESPCubeSpeakerVolume speakerVolume;

extern bool lastDisplayPaired;
extern bool lastDisplayStationary;
extern bool firstDisplayDraw;
extern uint32_t lastDisplayUpdate;
