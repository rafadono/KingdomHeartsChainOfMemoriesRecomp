# Prompt Phase 4: Audio DSP Suite & HD Orchestrated Soundtrack

## Prompt Objective
Implement a high-fidelity 65,536 Hz audio DSP pipeline and an HD orchestrated soundtrack replacement engine with independent BGM and SFX volume controls.

---

## Instructions to Agent / LLM

```text
Act as an audio DSP engineer and sound designer. Implement a modern audio processing suite and an HD music modding engine for the recompiled GBA game.

### Mandatory Rules
1. Submodule gbarecomp/ must remain strictly untouched.
2. Intercept audio cleanly at the push buffer / audio callback level without modifying upstream core code.
3. No emojis in code or documentation.

### Step-by-Step Execution Plan

1. MP2K Shadow Voice Mixer:
   - Configure game.toml with [audio] shadow = true.
   - Run the shadow voice mixer at 65,536 Hz with 32-bit floating-point precision, eliminating 8-bit DAC PWM quantization noise while maintaining 100% voice envelope compatibility.

2. Audio DSP Pipeline:
   - Create src/audio_dsp.h and src/audio_dsp.cpp.
   - Implement an interleaved stereo float processor with:
     - Biquad low-pass anti-aliasing filter to remove ultrasonic PWM switching hiss.
     - 3-band parametric equalizer with presets: Flat (Authentic), Warm Retro, Crisp Modern, Bass Boost.
     - Mid-Side stereo widener adjustable from 0% (mono) to 200% (expanded stereo).
     - Soft-knee peak limiter to eliminate digital clipping during multi-voice battle climaxes.

3. HD Orchestrated Music Replacement Engine:
   - Create src/hd_audio_player.h and src/hd_audio_player.cpp.
   - Create external track mapping configuration: config/music_tracks.ini.
   - Intercept in-game BGM trigger events (matching song IDs to mapped audio assets).
   - Implement SDL_AudioCVT dynamic resampling to convert external WAV/OGG/FLAC files to native engine 65,536 Hz 16-bit stereo.
   - Implement seamless loop playback without clicks or silence gaps.

4. Independent BGM vs. SFX Volume Mixer:
   - Isolate native GBA sound effects from orchestrated background music.
   - Apply independent volume multipliers:
     - BGM Volume: 0% to 150% (scales HD soundtrack stream).
     - SFX Volume: 0% to 150% (scales native game audio and battle effects).
   - Mix both streams into the final SDL audio queue before hardware delivery.
```
