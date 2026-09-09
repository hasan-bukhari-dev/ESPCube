#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, sys

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'firmware' / 'src'

EXPECTED_HASHES = {
'firmware/src/input/Buttons.cpp':'377b0674ec54844665e27a66738e952aa15651acf33bdfed1ba8fd82567cd0f9',
'firmware/src/input/Buttons.h':'e7d0f6283a001067ec22aaeeab85bfae9c89a6a19b17cc50562070790a47931a',
'firmware/src/input/Touch.cpp':'2cea46f2e7f2d5dc5ecb4f54ab1bc4829cee720585315fa1dd4004be86f98f9b',
'firmware/src/input/Touch.h':'084a6358a3ca31c9af899535e783192a013d4a6150e746d81e2e27fdaef1c6dd',
'firmware/src/input/Motion.cpp':'661d98a7bdc248227c9da0f6f3d08f19f34ad4e2b11ae79c34228ac6cbe92115',
'firmware/src/input/Motion.h':'c6697a6205a80d0005e75a5f85ec9584a266ba0923fb74c84eeadddf0e3e03e3',
'firmware/src/app/HomeGesture.cpp':'5d31fbf9928b8bde4ca45c6c729abf83ff03b4f86ff3a1bda6d4468a7309f5a0',
'firmware/src/app/HomeGesture.h':'e61e5091838733a0907c00768dac618b39d5727011b268b22be3c57fcc54a221',
'firmware/src/hardware/BoardConfig.h':'5210063e17cae4cdf8c6302e7f6fc23b49f934584d64f5291a7211e744c441b8',
'firmware/src/main.cpp':'9f7731529472358840e18a4f603e94af2a07f4abe1e0b13ecd818353ff814942',
}
EXPECTED_RENDERER_NORMALIZED='60935023ec4d5506c2cb4043e1946e9c249f0cbc11c6ae6a7ff1a90307b098b4'
EXPECTED_TEXT_NORMALIZED='34ddc5e69a790314fabf208adce7ed0ab8372dde1a46e2c3ef64cd285984291e'
EXPECTED_SPEAKER_NORMALIZED='f845ca845762ff789c88c3a958aad88ac9c35ef803548c37eeab736f0285c949'

checks=[]
def add(name, ok, detail=''):
    checks.append((name, bool(ok), detail))
    print(('PASS' if ok else 'FAIL') + ': ' + name + (f' — {detail}' if detail else ''))

def sha_bytes(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def sha_text(s): return hashlib.sha256(s.strip().encode()).hexdigest()

for rel, expected in EXPECTED_HASHES.items():
    p=ROOT/rel
    add('checkpoint hash '+rel, p.exists() and sha_bytes(p)==expected)

incs=list(SRC.rglob('*.inc'))
add('no .inc bridge fragments remain', not incs, ', '.join(str(x.relative_to(ROOT)) for x in incs))

main=(SRC/'main.cpp').read_text()
add('main delegates only to AppRuntime', 'AppRuntime app;' in main and 'void setup() { app.begin(); }' in main and 'void loop() { app.update(); }' in main)

# UI renderer logic normalized back to the checkpoint representation.
r=(SRC/'ui/ScreenRenderer.cpp').read_text()
r=r[r.index('void ScreenRenderer::centerText'):].strip().replace('ScreenRenderer::','')
for a,b in {
'textProfile.selectedGroup':'selectedGroup','textProfile.shiftOnce':'shiftOnce','textProfile.capsLock':'capsLock','textProfile.t9SecondPage':'t9SecondPage',
'speakerProfile.volumePercent':'speakerVolumePercent','speakerProfile.muted':'speakerVolumeMuted',
'motion.stationary':'stationary','motion.sensitivityMultiplier':'sensitivityMultiplier','motion.gyroEnabled':'gyroEnabled','motion.invertY':'invertY',
'uint8_t size\n':'uint8_t size = 1\n'}.items(): r=r.replace(a,b)
add('renderer logic normalized hash preserved', sha_text(r)==EXPECTED_RENDERER_NORMALIZED)

# Text behavior body normalized to pre-class implementation.
t=(SRC/'profiles/TextProfile.cpp').read_text()
t=t[t.index('void TextProfile::previewPush'):]
idx=t.find('// ============================================================\n// NAVIGATION')
if idx!=-1: t=t[:idx]
t=t.strip().replace('TextProfile::','').replace('renderer.renderCurrentScreen();','renderCurrentScreen();')
add('text behavior normalized hash preserved', sha_text(t)==EXPECTED_TEXT_NORMALIZED)

# Speaker transport body normalized to pre-class implementation.
s=(SRC/'profiles/SpeakerStream.cpp').read_text()
s=s[s.index('void SpeakerStream::closeSpeakerTcpClient'):s.index('// ============================================================\n// EXISTING SPEAKER CONTROL')].strip().replace('SpeakerStream::','')
s=s.replace('void stopPlayback(', 'void stopSpeakerTcpPlayback(').replace('void serviceTcp(', 'void serviceSpeakerTcp(').replace('void serviceControl(', 'void serviceSpeakerControl(').replace('void serviceD2A(', 'void serviceSpeakerTcpD2A(')
for name in ['closeSpeakerTcpClient','stopSpeakerTcpServer','startSpeakerTcpServer','acceptSpeakerTcpClient','playSpeakerTcpChunk','receiveSpeakerTcpAudio','resetSpeakerD2ASession','ensureSpeakerD2ABuffer','closeSpeakerD2AClientOnly','finishSpeakerD2AStream','acceptSpeakerD2AClient','drainSpeakerD2ATcpIntoRing','maybeStartSpeakerD2APlayback','playOneSpeakerD2AChunk']:
    s=re.sub(r'(^|\n)(void|bool) '+name+r'\(', lambda m:m.group(1)+'static '+m.group(2)+' '+name+'(', s)
s=re.sub(r'\bstopPlayback\(', 'stopSpeakerTcpPlayback(', s)
for a,b in {'BufferCapacity':'D2A_BUFFER_CAPACITY','PrebufferBytes':'D2A_PREBUFFER_BYTES','UnderrunGraceMs':'D2A_UNDERRUN_GRACE_MS','TcpPort':'TCP_PORT','PcmFrames':'TCP_PCM_FRAMES','PcmBytes':'TCP_PCM_BYTES'}.items(): s=re.sub(r'\b'+a+r'\b',b,s)
s=s.replace('renderer.renderCurrentScreen();','renderCurrentScreen();').replace('profiles.goHome();','goHome();')
add('speaker transport normalized hash preserved', sha_text(s)==EXPECTED_SPEAKER_NORMALIZED)

sh=(SRC/'profiles/SpeakerStream.h').read_text()
for frag in ['BufferCapacity = 128 * 1024','PrebufferBytes = 12 * 320 * sizeof(int16_t)','UnderrunGraceMs = 14','TcpPort = 47821','PcmFrames = 320']:
    add('speaker contract '+frag, frag in sh)

# One 10 ms chunk per AppRuntime service pass is still explicit.
body=(SRC/'profiles/SpeakerStream.cpp').read_text()
start=body.index('void SpeakerStream::serviceD2A()')
service=body[start:]
# Stop at end of this final function by taking the remainder; call count is enough because this method is last.
add('one-chunk-per-service call preserved', service.count('playOneSpeakerD2AChunk();')==1)

pm=(SRC/'app/ProfileManager.cpp').read_text()
for frag in ['speakerProfile.handleButtons(', 'mouseProfile.handleButtonA(', 'mouseProfile.handleButtonB(', 'mouseProfile.handleButtonC(', 'settingsProfile.handleTouch(', 'textProfile.sendCharacter(', 'textProfile.sendBackspace(', 'textProfile.sendSpace(', 'textProfile.sendEnter(']:
    add('profile dispatch '+frag, frag in pm)

app=(SRC/'app/AppRuntime.cpp').read_text()
order=['speechLink.maintain();','speakerControl.maintain();','speakerStream.serviceControl();','speakerStream.serviceD2A();','profiles.updateButtons();','motion.update(buttons, profiles, hid);','profiles.updateTouch();','renderer.updateDisplay();','delay(1);']
idxs=[app.find(x, app.find('void AppRuntime::update()')) for x in order]
add('AppRuntime update ordering preserved', all(i>=0 for i in idxs) and idxs==sorted(idxs))

# UI constants.
theme=(SRC/'ui/Theme.h').read_text()
for frag in ['RGB565(0,0,0)','RGB565(255,255,255)','RGB565(120,120,120)','RGB565(60,60,60)','RGB565(105,215,145)','RGB565(120,170,240)','RGB565(220,180,80)']:
    add('theme constant '+frag, frag in theme)

# Display construction safety: exported gfx storage/construction lives in Display.cpp, not RuntimeGlobals.cpp.
display_cpp=(SRC/'hardware/Display.cpp').read_text()
runtime_globals=(SRC/'app/RuntimeGlobals.cpp').read_text()
add('display gfx constructed in Display.cpp', 'Arduino_GFX *gfx = new Arduino_ST7789' in display_cpp)
add('no cross-TU Display::graphics global initializer', 'Arduino_GFX *gfx = Display::graphics()' not in runtime_globals)

# Lifecycle completeness without adding asynchronous/event-bus behavior.
profile_h=(SRC/'profiles/Profile.h').read_text()
mouse_h=(SRC/'profiles/MouseProfile.h').read_text()
speaker_h=(SRC/'profiles/SpeakerProfile.h').read_text()
add('MouseProfile implements ProfileLifecycle', 'public ProfileLifecycle' in mouse_h)
add('SpeakerProfile implements ProfileLifecycle', 'public ProfileLifecycle' in speaker_h)
add('unused InputEvent contract removed', not (SRC/'input/InputEvent.h').exists())
add('MouseProfile exit is live', 'mouseProfile.exit();' in pm)
add('SpeakerProfile lifecycle used by BLE control', 'speakerProfile.enter()' in body and 'speakerProfile.exit()' in body)
add('Text dead reset helpers removed', 'resetNavigation' not in (SRC/'profiles/TextProfile.h').read_text() and 'resetSpeechState' not in (SRC/'profiles/TextProfile.h').read_text())

# Optional external oracle comparison for companion/libs.
if len(sys.argv)>1:
    oracle=Path(sys.argv[1]).resolve()
    def treehash(base, rel):
        h=hashlib.sha256()
        root=base/rel
        for p in sorted(x for x in root.rglob('*') if x.is_file()):
            h.update(str(p.relative_to(root)).encode()+b'\0'+p.read_bytes())
        return h.hexdigest()
    for rel in ['companion','firmware/lib']:
        add(rel+' tree byte-identical to supplied oracle', treehash(ROOT,rel)==treehash(oracle,rel))

passed=sum(ok for _,ok,_ in checks)
print(f'RESULT: {passed}/{len(checks)} PASS')
sys.exit(0 if passed==len(checks) else 1)
