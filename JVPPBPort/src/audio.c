// JVPPBPort: the GBA's sound hardware, played through SDL.
//
// The GBA makes sound two ways, and the game uses both:
//   - DirectSound: two 8-bit sample streams (FIFO A and B), fed by DMA from a
//     buffer that the m4a mixer fills (m4a_port.c). Here we read that buffer
//     at the same rate the GBA's timer 0 would, holding each sample until the
//     next one (as the hardware does).
//   - The four Game Boy channels ("PSG": two square waves, a wave channel, and
//     noise). The m4a engine plays some instruments and sound effects on them
//     by writing their registers. Here they are emulated from those registers.
// Both are mixed the way the GBA mixes them (levels as in the mGBA emulator),
// clipped to the GBA's 10-bit output, passed through a DC-blocking filter like
// the GBA's output capacitor, and sent to SDL at 48 kHz.
//
// Everything is counted in GBA CPU cycles (16,777,216 per second; 280,896 per
// frame). Once per frame, PortAudioFrame() produces that frame's sound.
//
// Register writes can't be watched as they happen, so the Game Boy channels
// read their registers once per frame (the m4a engine also only writes them
// once per frame, in m4aSoundMain). A write of 1 to bit 7 of NRx4 ("restart")
// is seen as that bit being set, and is cleared after being handled, as the
// hardware never lets the game read it back. Simplification: a channel's
// length counter is reloaded from its register on every restart. (This game's
// instruments never enable length counters.)
//
//   JVPPBPORT_MUTE=1          no sound device
//   JVPPBPORT_WAV=file.wav    also write everything played to a WAV file
//   JVPPBPORT_AUDIO_TRACE=1   print how much sound is waiting in SDL's queue, once a second
//   JVPPBPORT_PCMDUMP=file    each frame, write the frame number (4 bytes) and
//                             the m4a mixer's whole PCM buffer (3168 bytes), for
//                             comparing the mixer with a GBA emulator's; then
//                             the channels (see PortDumpSoundChannels)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "global.h"
#include "jvppbport.h"

#define CYCLES_PER_SECOND 16777216
#define CYCLES_PER_FRAME  280896
#define OUTPUT_RATE 48000
#define SEQUENCER_CYCLES (CYCLES_PER_SECOND / 512)  // length, sweep and envelope clock
#define QUEUE_TARGET_FRAMES 4

#define IO8(off) (gPortIo[off])
#define IO16(off) (*(u16 *)&gPortIo[off])
#define IO32(off) (*(u32 *)&gPortIo[off])

struct PsgChannel
{
    bool8 on;
    u8 volume;
    u8 envDir, envPeriod, envTimer;
    bool8 lengthOn;
    u16 length;
    u32 period;   // cycles per step
    s32 timer;    // cycles until the next step
    u32 pos;      // duty step (square) or sample (wave)
    // channel 1 sweep
    u8 sweepPeriod, sweepTimer, sweepShift, sweepDown;
    u16 sweepFreq;
    // channel 3 wave
    u8 wave[32];
    // channel 4 noise
    u16 lfsr;
    u8 noiseBit;
};

static const u8 sDuty[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
};

static SDL_AudioDeviceID sDevice;
static bool8 sQueueAudio;
static FILE *sWav;
static u32 sWavBytes;
static FILE *sPcmDump;
static u32 sFrames;

static struct PsgChannel sPsg[4];
static u32 sSequencerTimer = SEQUENCER_CYCLES;
static u32 sSequencerStep;
static u32 sDmaCycles;           // cycles since the sound DMA last restarted
static double sOutputPhase;      // fraction of an output sample carried between frames
static float sDcLeft, sDcRight, sDcInLeft, sDcInRight;

// ---------------------------------------------------------------------------
// Game Boy channels

static u16 SquareFreq(int ch)
{
    int base = ch == 0 ? REG_OFFSET_NR13 : REG_OFFSET_NR23;
    return IO8(base) | ((IO8(base + 1) & 7) << 8);
}

static void SetSquarePeriod(struct PsgChannel *c, u16 freq)
{
    c->period = (2048 - freq) * 16;
}

static void Restart(int ch)
{
    static const u8 noiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    struct PsgChannel *c = &sPsg[ch];
    u8 env;

    switch (ch)
    {
    case 0:
    case 1:
        env = IO8(ch == 0 ? REG_OFFSET_NR12 : REG_OFFSET_NR22);
        c->length = 64 - (IO8(ch == 0 ? REG_OFFSET_NR11 : REG_OFFSET_NR21) & 0x3F);
        c->lengthOn = (IO8(ch == 0 ? REG_OFFSET_NR14 : REG_OFFSET_NR24) & 0x40) != 0;
        SetSquarePeriod(c, SquareFreq(ch));  // (the duty step keeps going)
        break;
    case 2:
        env = 0;
        c->length = 256 - IO8(REG_OFFSET_NR31);
        c->lengthOn = (IO8(REG_OFFSET_NR34) & 0x40) != 0;
        {
            u16 freq = IO8(REG_OFFSET_NR33) | ((IO8(REG_OFFSET_NR34) & 7) << 8);
            u8 wave[32];
            int i;

            c->period = (2048 - freq) * 8;
            for (i = 0; i < 16; i++)
            {
                wave[i * 2] = IO8(REG_OFFSET_WAVE_RAM0 + i) >> 4;
                wave[i * 2 + 1] = IO8(REG_OFFSET_WAVE_RAM0 + i) & 0xF;
            }
            // The game switches to freshly written wave RAM for a new wave
            // (playing it from the start); otherwise playback carries on.
            if (memcmp(wave, c->wave, sizeof(wave)) != 0)
            {
                memcpy(c->wave, wave, sizeof(wave));
                c->pos = 0;
                c->timer = c->period;
            }
        }
        break;
    default:
        env = IO8(REG_OFFSET_NR42);
        c->length = 64 - (IO8(REG_OFFSET_NR41) & 0x3F);
        c->lengthOn = (IO8(REG_OFFSET_NR44) & 0x40) != 0;
        c->period = (noiseDivisors[IO8(REG_OFFSET_NR43) & 7] << (IO8(REG_OFFSET_NR43) >> 4)) * 4;
        c->timer = c->period;
        c->lfsr = (IO8(REG_OFFSET_NR43) & 8) ? 0x7F : 0x7FFF;
        c->noiseBit = 0;
        break;
    }

    c->volume = env >> 4;
    c->envDir = (env >> 3) & 1;
    c->envPeriod = env & 7;
    c->envTimer = c->envPeriod;
    // A channel whose DAC is off (envelope register 0x00-0x07) stays silent.
    c->on = (ch == 2) ? (IO8(REG_OFFSET_NR30) & 0x80) != 0 : (env & 0xF8) != 0;

    if (ch == 0)
    {
        u8 nr10 = IO8(REG_OFFSET_NR10);

        c->sweepFreq = SquareFreq(0);
        c->sweepPeriod = (nr10 >> 4) & 7;
        c->sweepDown = (nr10 >> 3) & 1;
        c->sweepShift = nr10 & 7;
        c->sweepTimer = c->sweepPeriod ? c->sweepPeriod : 8;
    }
}

// Reads the registers the game wrote this frame.
static void ReadPsgRegisters(void)
{
    static const u8 nrx4[4] = {REG_OFFSET_NR14, REG_OFFSET_NR24, REG_OFFSET_NR34, REG_OFFSET_NR44};
    int ch;

    for (ch = 0; ch < 4; ch++)
    {
        if (IO8(nrx4[ch]) & 0x80)
        {
            IO8(nrx4[ch]) &= ~0x80;
            Restart(ch);
        }
    }
    // Frequencies and the noise setting can change without a restart.
    if (!sPsg[0].sweepPeriod || !sPsg[0].sweepShift)
        SetSquarePeriod(&sPsg[0], SquareFreq(0));
    SetSquarePeriod(&sPsg[1], SquareFreq(1));
    sPsg[2].period = (2048 - (IO8(REG_OFFSET_NR33) | ((IO8(REG_OFFSET_NR34) & 7) << 8))) * 8;
    if (!(IO8(REG_OFFSET_NR30) & 0x80))
        sPsg[2].on = FALSE;
}

static void ClockSequencer(void)
{
    int ch;

    sSequencerStep = (sSequencerStep + 1) & 7;
    if (!(sSequencerStep & 1))
    {
        // 256 Hz: length counters
        for (ch = 0; ch < 4; ch++)
        {
            struct PsgChannel *c = &sPsg[ch];

            if (c->lengthOn && c->length && --c->length == 0)
                c->on = FALSE;
        }
    }
    if (sSequencerStep == 2 || sSequencerStep == 6)
    {
        // 128 Hz: channel 1 frequency sweep
        struct PsgChannel *c = &sPsg[0];

        if (c->sweepPeriod && --c->sweepTimer == 0)
        {
            c->sweepTimer = c->sweepPeriod;
            if (c->sweepShift)
            {
                s32 delta = c->sweepFreq >> c->sweepShift;
                s32 freq = c->sweepDown ? c->sweepFreq - delta : c->sweepFreq + delta;

                if (freq > 2047)
                {
                    c->on = FALSE;
                }
                else if (freq >= 0)
                {
                    c->sweepFreq = freq;
                    IO8(REG_OFFSET_NR13) = freq;
                    IO8(REG_OFFSET_NR14) = (IO8(REG_OFFSET_NR14) & ~7) | (freq >> 8);
                    SetSquarePeriod(c, freq);
                }
            }
        }
    }
    if (sSequencerStep == 7)
    {
        // 64 Hz: volume envelopes
        for (ch = 0; ch < 4; ch++)
        {
            struct PsgChannel *c = &sPsg[ch];

            if (ch == 2 || c->envPeriod == 0 || --c->envTimer)
                continue;
            c->envTimer = c->envPeriod;
            if (c->envDir && c->volume < 15)
                c->volume++;
            else if (!c->envDir && c->volume > 0)
                c->volume--;
        }
    }
}

static u32 ChannelLevel(int ch)
{
    struct PsgChannel *c = &sPsg[ch];

    if (!c->on)
        return 0;
    switch (ch)
    {
    case 0:
        return sDuty[IO8(REG_OFFSET_NR11) >> 6][c->pos] * c->volume;
    case 1:
        return sDuty[IO8(REG_OFFSET_NR21) >> 6][c->pos] * c->volume;
    case 2:
    {
        u8 nr32 = IO8(REG_OFFSET_NR32);
        u32 sample = c->wave[c->pos];

        if (nr32 & 0x80)
            return (sample * 3) >> 2;  // forced 75%
        switch ((nr32 >> 5) & 3)
        {
        case 0: return 0;
        case 1: return sample;
        case 2: return sample >> 1;
        default: return sample >> 2;
        }
    }
    default:
        return c->noiseBit * c->volume;
    }
}

static void StepChannel(int ch)
{
    struct PsgChannel *c = &sPsg[ch];

    switch (ch)
    {
    case 0:
    case 1:
        c->pos = (c->pos + 1) & 7;
        break;
    case 2:
        c->pos = (c->pos + 1) & 31;
        break;
    default:
        // The same generator as mGBA (so noise effects can be compared).
        c->noiseBit = c->lfsr & 1;
        c->lfsr >>= 1;
        if (c->noiseBit)
            c->lfsr ^= (IO8(REG_OFFSET_NR43) & 8) ? 0x60 : 0x6000;
        break;
    }
}

// Runs the Game Boy channels for `cycles`, adding each channel's average level
// over that time (0-15) to out[].
static void RunPsg(u32 cycles, float out[4])
{
    u32 area[4] = {0, 0, 0, 0};
    u32 left = cycles;

    while (left)
    {
        u32 span = left < sSequencerTimer ? left : sSequencerTimer;
        int ch;

        for (ch = 0; ch < 4; ch++)
        {
            struct PsgChannel *c = &sPsg[ch];
            u32 t = span;

            if (!c->on)
                continue;
            while (t)
            {
                u32 d = (u32)c->timer < t ? (u32)c->timer : t;

                area[ch] += ChannelLevel(ch) * d;
                c->timer -= d;
                t -= d;
                if (c->timer <= 0)
                {
                    StepChannel(ch);
                    c->timer += c->period;
                }
            }
        }
        left -= span;
        sSequencerTimer -= span;
        if (sSequencerTimer == 0)
        {
            sSequencerTimer = SEQUENCER_CYCLES;
            ClockSequencer();
        }
    }
    for (int ch = 0; ch < 4; ch++)
        out[ch] = (float)area[ch] / cycles;
}

// ---------------------------------------------------------------------------
// DirectSound

void PortAudioRestartDma(void)
{
    sDmaCycles = 0;
}

// Where the FIFO sample DMA channels read from. The game sets this up once
// (SoundInit); the GBA's 32-bit address registers can't hold a PC address.
static const s8 *sDmaSource[3];

void PortAudioSetDmaSource(int dma, const s8 *src)
{
    sDmaSource[dma] = src;
}

// The FIFO sample DMA channel `dma` (1 or 2) feeds at this moment.
static s32 DirectSoundSample(int dma, u32 samplePos)
{
    u32 cntOffset = dma == 1 ? REG_OFFSET_DMA1CNT_H : REG_OFFSET_DMA2CNT_H;

    if (!(IO16(cntOffset) & DMA_ENABLE) || !sDmaSource[dma] || samplePos >= 1584)  // PCM_DMA_BUF_SIZE
        return 0;
    return sDmaSource[dma][samplePos];
}

// ---------------------------------------------------------------------------
// Output

static void WriteWavHeader(void)
{
    u32 header[11] = {
        0x46464952, 36 + sWavBytes, 0x45564157, 0x20746D66, 16,
        0x00020001, OUTPUT_RATE, OUTPUT_RATE * 4, 0x00100004, 0x61746164, sWavBytes,
    };

    fseek(sWav, 0, SEEK_SET);
    fwrite(header, sizeof(header), 1, sWav);
    fseek(sWav, 0, SEEK_END);
    fflush(sWav);
}

static void CloseWav(void)
{
    if (sWav)
        WriteWavHeader();
}

void PortAudioInit(bool8 fast)
{
    const char *wav = getenv("JVPPBPORT_WAV");

    if (wav)
    {
        sWav = fopen(wav, "wb");
        if (sWav)
        {
            WriteWavHeader();
            atexit(CloseWav);
        }
    }
    if (getenv("JVPPBPORT_PCMDUMP"))
        sPcmDump = fopen(getenv("JVPPBPORT_PCMDUMP"), "wb");
    if (getenv("JVPPBPORT_MUTE") || SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return;
    {
        SDL_AudioSpec want = {0}, have;

        want.freq = OUTPUT_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        sDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (sDevice == 0)
        {
            fprintf(stderr, "JVPPBPort: no sound (%s)\n", SDL_GetError());
            return;
        }
        SDL_PauseAudioDevice(sDevice, 0);
        // Fast-forward makes frames faster than real time; don't pile them up.
        sQueueAudio = !fast;
    }
}

static s16 FinishSample(float v, float *dcIn, float *dcOut)
{
    // The GBA adds a bias of 0x200 and clips to 0-0x3FF (10 bits).
    if (v > 0x1FF)
        v = 0x1FF;
    if (v < -0x200)
        v = -0x200;
    // DC blocker (about 15 Hz), standing in for the output capacitor. Same
    // strength as mGBA's (its blip_buf output filter). The Game Boy channels
    // only output positive levels, so this matters for how they sound.
    *dcOut = v - *dcIn + (1 - 1.0f / 512) * *dcOut;
    *dcIn = v;
    v = *dcOut * 64;
    if (v > 32767)
        v = 32767;
    if (v < -32768)
        v = -32768;
    return (s16)v;
}

// Produces one frame of sound. Called once per frame, before the VBlank
// interrupt (which may restart the sound DMA).
void PortAudioFrame(void)
{
    static s16 out[OUTPUT_RATE / 50 * 2];
    double perFrame = (double)OUTPUT_RATE * CYCLES_PER_FRAME / CYCLES_PER_SECOND;
    u16 timerReload = IO16(REG_OFFSET_TM0CNT_L);
    u32 cyclesPerSample = 0x10000 - timerReload;
    u16 cntH = IO16(REG_OFFSET_SOUNDCNT_H);
    u8 nr50 = IO8(REG_OFFSET_NR50), nr51 = IO8(REG_OFFSET_NR51);
    bool8 masterOn = (IO8(REG_OFFSET_SOUNDCNT_X) & 0x80) != 0;
    int psgShift = 4 - (cntH & 3);
    int n, i;
    u32 cycle = 0;

    sFrames++;
    if (sPcmDump)
    {
        static const u8 zeros[1584 * 2];

        fwrite(&sFrames, 4, 1, sPcmDump);
        fwrite(sDmaSource[1] ? (const void *)sDmaSource[1] : zeros, 1, sizeof(zeros), sPcmDump);
        PortDumpSoundChannels(sPcmDump);
    }
    ReadPsgRegisters();

    // Keep about QUEUE_TARGET_FRAMES frames waiting in SDL's queue by playing
    // up to 0.5% faster or slower (the frame timer and the sound card's clock
    // drift apart slowly).
    if (sQueueAudio)
    {
        double queued = SDL_GetQueuedAudioSize(sDevice) / 4.0 / perFrame;
        double adjust = (QUEUE_TARGET_FRAMES - queued) / QUEUE_TARGET_FRAMES;

        if (sFrames % 60 == 0 && getenv("JVPPBPORT_AUDIO_TRACE"))
            fprintf(stderr, "audio: frame %u, %.1f frames queued\n", sFrames, queued);

        if (adjust > 1)
            adjust = 1;
        if (adjust < -1)
            adjust = -1;
        perFrame *= 1 + 0.005 * adjust;
    }
    sOutputPhase += perFrame;
    n = (int)sOutputPhase;
    sOutputPhase -= n;

    for (i = 0; i < n; i++)
    {
        u32 next = (u32)((u64)CYCLES_PER_FRAME * (i + 1) / n);
        float psg[4];
        float left = 0, right = 0;
        int ch;

        RunPsg(next - cycle, psg);
        if (masterOn)
        {
            for (ch = 0; ch < 4; ch++)
            {
                if (nr51 & (0x10 << ch))
                    left += psg[ch];
                if (nr51 & (1 << ch))
                    right += psg[ch];
            }
            left = left * 8 * (1 + ((nr50 >> 4) & 7)) / (1 << psgShift);
            right = right * 8 * (1 + (nr50 & 7)) / (1 << psgShift);

            if (cyclesPerSample && (IO16(REG_OFFSET_TM0CNT_H) & TIMER_ENABLE))
            {
                // The sample playing in the middle of this output sample.
                u32 pos = (sDmaCycles + (cycle + next) / 2) / cyclesPerSample;
                s32 a = (DirectSoundSample(1, pos) * 4) >> !(cntH & 0x0004);
                s32 b = (DirectSoundSample(2, pos) * 4) >> !(cntH & 0x0008);

                if (cntH & 0x0100) right += a;
                if (cntH & 0x0200) left += a;
                if (cntH & 0x1000) right += b;
                if (cntH & 0x2000) left += b;
            }
        }
        out[i * 2] = FinishSample(left, &sDcInLeft, &sDcLeft);
        out[i * 2 + 1] = FinishSample(right, &sDcInRight, &sDcRight);
        cycle = next;
    }
    sDmaCycles += CYCLES_PER_FRAME;

    if (sQueueAudio)
    {
        Uint32 queued = SDL_GetQueuedAudioSize(sDevice);

        // Far behind (e.g. the window was dragged): drop instead of lagging.
        if (queued > 4 * perFrame * QUEUE_TARGET_FRAMES * 3)
            SDL_ClearQueuedAudio(sDevice);
        // Nearly run dry (at the start, or after a slow frame): pad with
        // silence back up to the target, rather than crackle.
        if (queued < 4 * perFrame)
        {
            static const s16 silence[OUTPUT_RATE / 10 * 2];
            Uint32 pad = (Uint32)(perFrame * (QUEUE_TARGET_FRAMES - 1)) * 4 - queued;

            SDL_QueueAudio(sDevice, silence, pad < sizeof(silence) ? pad : sizeof(silence));
        }
        SDL_QueueAudio(sDevice, out, n * 4);
    }
    if (sWav)
    {
        fwrite(out, 4, n, sWav);
        sWavBytes += n * 4;
    }
}
