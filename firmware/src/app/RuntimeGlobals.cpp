#include "RuntimeGlobals.h"
#include "../hardware/Display.h"

Buttons buttons;
Touch touch;
Motion motion;
HomeGesture homeGesture;
ProfileManager profiles;
MouseProfile mouseProfile;
TextProfile textProfile;
SettingsProfile settingsProfile;
SpeakerProfile speakerProfile;
ESPCubeHID hid;
ESPCubeSpeech speech;
ESPCubeSpeechLink speechLink;
ESPCubeSpeakerControl speakerControl;
ESPCubeSpeakerPlayback speakerPlayback;
ESPCubeSpeakerVolume speakerVolume;

bool lastDisplayPaired = false;
bool lastDisplayStationary = false;
bool firstDisplayDraw = true;
uint32_t lastDisplayUpdate = 0;
