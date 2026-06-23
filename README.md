# AI Cubpet Native Demo

`ai-cubpet` is the K1 Cubpet native demo package. The project is organized like
`application/native/omni_agent`, but it keeps only the local pieces needed for a
demo: peripherals, dual-channel audio input, WebRTC AEC, Silero VAD, DOA,
SenseVoice ASR and simple voice-to-motion commands.

Cloud dialog, LLM, TTS, MCP and voiceprint are intentionally not included. The
UI process runs separately and is managed together with the voice process by
`ai_cubpet_daemon`.

## Layout

- `include/`: public headers for the local voice demo skeleton.
- `src/`: reusable voice pipeline, DOA runtime, keyword routing and motor
  actions.
- `examples/peripherals.c`: the original peripheral aggregate demo.
- `examples/main.cpp`: local voice-control demo entry point.
- `cmake/`: third-party dependency helpers copied from `omni_agent`, including
  the WebRTC audio-processing build.

## Targets

- `ai-cubpet`: local voice-control demo.
- `ai_cubpet_daemon`: process manager for `ai-cubpet-ui` and `ai-cubpet`.
- `ai-cubpet-ui`: DDS-driven Qt UI process.
- `ai-cubpet-peripherals`: peripheral CLI demo. This builds from
  `examples/peripherals.c` when `AI_CUBPET_BUILD_PERIPHERALS_DEMO=ON`.

Development-only targets:

- `ai-cubpet-ui-demo`: standalone keyboard UI smoke test.
- `ai-cubpet-dds-test-pub`: DDS media command publisher for UI debugging.

The development-only targets are not built or installed by default. Pass
`AI_CUBPET_BUILD_TEST_TOOLS=ON` to enable them.

## Build

Configure only the peripheral demo:

```sh
cmake -S application/native/ai-cubpet -B build/ai-cubpet \
  -DAI_CUBPET_BUILD_VOICE_DEMO=OFF
cmake --build build/ai-cubpet
```

Configure with the local voice demo:

```sh
cmake -S application/native/ai-cubpet -B build/ai-cubpet \
  -DAI_CUBPET_BUILD_VOICE_DEMO=ON
cmake --build build/ai-cubpet
```

The `k1-ai-cubpet` target enables the audio, DOA, ASR and VAD SDK packages.
WebRTC audio processing is fetched and built through `cmake/webrtc_audio_processing.cmake`
when `AI_CUBPET_USE_AEC=ON`, which is the default for this demo. Pass
`-DAI_CUBPET_USE_AEC=OFF` to build the old capture-only path.

## Demo Flow

The intended local voice-control flow is:

```text
ES8326 dual-channel mic --raw stereo--> DOA angle
ES8326 speech channel + ES7243 reference -> WebRTC AEC/NS -> Silero VAD
    -> SenseVoice ASR -> keyword routing -> Cubpet motor action + DDS GIF update
```

The voice demo listens on the local microphone and dispatches the first matched
keyword to the motor action thread:

```sh
ai-cubpet --aec --input 0 --channels 2 --ref-input 1 \
  --vad-threshold 0.30 --vad-stop-threshold 0.20 --silence-ms 500
```

When DDS is enabled, recognized local commands publish `ToyCommand::CUSTOM_MEDIA`
to `ToyCommand_Msg` with absolute GIF and audio paths under `share/ai-cubpet/gif`
and `share/ai-cubpet/audio` next to the SDK-installed binary. Set
`AI_CUBPET_GIF_DIR` and `AI_CUBPET_AUDIO_DIR` to override those directories. Set
`AI_CUBPET_DDS_DOMAIN_ID` on both publisher and subscriber if the deployment uses
a non-zero DDS domain. Use `--no-ui-dds` to disable UI updates for audio-only
tests.

For product-style use, start both UI and voice through the daemon:

```sh
ai_cubpet_daemon config-init
ai_cubpet_daemon start
ai_cubpet_daemon status
ai_cubpet_daemon logs
ai_cubpet_daemon stop
```

The default config is `~/.config/ai-cubpet/ai_cubpet.json`. It keeps the
product path used on K1: 16 kHz, 4-channel capture, `speech_channel=1`,
WebRTC AGC enabled, DDS enabled, and Qt UI launched as user `initer` on the
Wayland display. Use `ai_cubpet_daemon config-show` to inspect the merged
runtime config.

Run the DDS UI subscriber manually only for debugging:

```sh
ai-cubpet-ui
```

For board-side smoke tests, configure with `AI_CUBPET_BUILD_TEST_TOOLS=ON`,
keep `ai-cubpet-ui` running and publish one media command:

```sh
ai-cubpet-dds-test-pub --gif /tmp/cubpet-gif/02_expect.gif \
  --audio /tmp/cubpet-audio/02_happy.wav
```

`--save-wav` writes the post-AEC 16 kHz mono ASR input. `--save-raw-wav`
writes the raw mic speech channel for A/B checks.

Supported first-pass commands are `抬头`, `点头`, `摇头` and `摇尾巴`.
