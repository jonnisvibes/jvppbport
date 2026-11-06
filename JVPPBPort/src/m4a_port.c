// JVPPBPort: C versions of the sound engine's assembly half (src/m4a_1.s).
// pret's src/m4a.c (the engine's C half) is compiled as-is around these.
//
// Written from m4a_1.s, routine by routine, to give the same results:
//   SoundMain / SoundMainRAM  the mixer: envelopes, resampling, reverb
//   MPlayMain                 the sequencer: one tick of a music player
//   ply_*                     song command handlers
//   TrackStop, RealClearChain, ChnVolSet, ...  the helpers they share
// The assembly packs four 8-bit output samples into one register and steals
// pointer bits for loop counters; here every output sample is simply added
// into its byte, which gives the same bytes.
//
// What differs from the GBA:
//   - The pointer fields are 8 bytes, so code that copies several fields as
//     one word copies them one by one here.
//   - Song command streams hold GOTO/PATT targets in 4 bytes. On PC those
//     are offsets from the 4 bytes themselves rather than addresses, since a
//     PC address doesn't fit (see JVPPBPort/tools/data2c.py). A keysplit
//     voice's table address is an 8-byte pointer (ToneData.keySplitTable).
//   - No timing budget: the GBA mixer gives up on channels when it runs out of
//     scanlines (SoundInfo.maxLines). A PC never runs out.
//   - The finished samples go to JVPPBPort/src/audio.c instead of DMA.
//
// This file uses the game's structs, so it follows the GBA layout rule.
#include "jvppbport_layout.h"
#include "gba/m4a_internal.h"
#include "jvppbport.h"
#include <stdio.h>

extern const u8 gClockTable[];
extern const s8 gDeltaEncodingTable[];
extern void *const gMPlayJumpTableTemplate[];
extern s8 gDpcmSampleBuffer[];

#define CHN_ON_MASK 0xC7    // any of: start, stop (release), echo, envelope phase
#define CHN_START   0x80
#define CHN_STOP    0x40
#define CHN_LOOP    0x10
#define CHN_DPCM_INIT 0x20  // compressed/reversed sample set up (sub_82DF49C)
#define CHN_IEC     0x04    // pseudo-echo
#define CHN_ENV_MASK 0x03   // 3 attack, 2 decay, 1 sustain, 0 release

#define TYPE_FIX     0x08   // play at the sample's own rate (no resampling)
#define TYPE_REVERSE 0x10
#define TYPE_COMPRESSED 0x20

#define CHAIN(p) ((struct SoundChannel *)(uintptr_t)(p))
#define ADDR(p) ((uintptr_t)(p))

_Static_assert(offsetof(struct SoundChannel, track) == offsetof(struct CgbChannel, tp), "channel lists");
_Static_assert(offsetof(struct SoundChannel, pp) == offsetof(struct CgbChannel, pp), "channel lists");
_Static_assert(offsetof(struct SoundChannel, np) == offsetof(struct CgbChannel, np), "channel lists");
_Static_assert(offsetof(struct SoundChannel, wav) == offsetof(struct CgbChannel, wp), "channel wave");
_Static_assert(offsetof(struct SoundChannel, freq) == offsetof(struct CgbChannel, fr), "channel frequency");
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) <= sizeof(struct MusicPlayerInfo), "Clear64byte size");

typedef void (*TrackFunc)(struct MusicPlayerInfo *, struct MusicPlayerTrack *);

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * multiplicand) >> 32);
}

static u8 ReadCmdByte(struct MusicPlayerTrack *track)
{
    return *track->cmdPtr++;
}

// A GOTO/PATT target: stored as an offset from where it's stored.
static u8 *ReadAddress(const u8 *p)
{
    s32 offset = (s32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24));

    return (u8 *)p + offset;
}

// ---------------------------------------------------------------------------
// Mixer

// One output sample into one byte of the (8-bit) PCM buffer, as the assembly's
// "add r6, r1, r6, ror 8": adds bits 8-15 of volume * sample, wrapping.
static inline void MixSample(s8 *right, s8 *left, s32 sample, u32 er, u32 el)
{
    *right = (s8)(*right + ((s32)(er * sample) >> 8));
    *left = (s8)(*left + ((s32)(el * sample) >> 8));
}

// sub_522D0: sample `index` of a compressed (4-bit delta) sample. Blocks of 64
// samples are decoded into gDpcmSampleBuffer, which all channels share (the
// per-channel "already decoded" tag doesn't notice when another channel
// decoded a different block into it in between; that's how the GBA does it).
static s32 DpcmSample(struct SoundChannel *chan, u32 index)
{
    u32 block = index >> 6;
    u32 tag;

    memcpy(&tag, &chan->xpi, 4);
    if (block != tag)
    {
        const u8 *src = (const u8 *)chan->wav->data + block * 0x21;
        s8 *out = gDpcmSampleBuffer;
        u8 value = *src++;
        u8 byte = *src++;
        int i;

        memcpy(&chan->xpi, &block, 4);
        *out++ = value;
        value += gDeltaEncodingTable[byte & 0xF];
        *out++ = value;
        for (i = 0; i < 31; i++)
        {
            byte = *src++;
            value += gDeltaEncodingTable[byte >> 4];
            *out++ = value;
            value += gDeltaEncodingTable[byte & 0xF];
            *out++ = value;
        }
    }
    return gDpcmSampleBuffer[index & 0x3F];
}

// sub_82DF49C: reversed and/or compressed samples. `cp` is a pointer for plain
// samples and a sample index for compressed ones.
static void MixSpecial(struct SoundInfo *si, struct SoundChannel *chan, s8 *out, s32 count,
                       u32 er, u32 el, u32 loopLen)
{
    struct WaveData *wav = chan->wav;
    s32 ct = chan->ct;
    uintptr_t cp = chan->cp;
    u32 fw = chan->fw;
    u32 step;
    s32 r0, r1, adv;
    s32 i;

    if (!(chan->status & CHN_DPCM_INIT))
    {
        chan->status |= CHN_DPCM_INIT;
        if (chan->type & TYPE_REVERSE)
            cp = ADDR(wav->data) + wav->size - (cp - ADDR(wav->data));
        if (wav->type != 0)
            cp -= ADDR(wav->data);
        chan->cp = cp;
    }

    step = (chan->type & TYPE_FIX) ? 0x800000 : si->divFreq * chan->freq;

    if (wav->type != 0)
    {
        u32 xpi = 0xFF000000;

        memcpy(&chan->xpi, &xpi, 4);
        if (!(chan->type & TYPE_REVERSE))
        {
            r0 = DpcmSample(chan, cp);
            cp++;
            r1 = DpcmSample(chan, cp) - r0;
            for (i = 0; i < count; i++)
            {
                MixSample(&out[i], &out[i + PCM_DMA_BUF_SIZE], r0 + ((s32)(fw * r1) >> 23), er, el);
                fw += step;
                adv = fw >> 23;
                if (adv == 0)
                    continue;
                fw &= ~0x3F800000;
                ct -= adv;
                if (ct <= 0)
                {
                    if (loopLen == 0)
                        goto stop;
                    cp = wav->loopStart;
                    adv = -ct;
                    for (;;)
                    {
                        ct += loopLen;
                        if (ct > 0)
                            break;
                        adv -= loopLen;
                    }
                    cp += adv;
                    r0 = DpcmSample(chan, cp);
                }
                else if (--adv == 0)
                {
                    r0 += r1;
                }
                else
                {
                    cp += adv;
                    r0 = DpcmSample(chan, cp);
                }
                cp++;
                r1 = DpcmSample(chan, cp) - r0;
            }
            cp--;
        }
        else
        {
            cp--;
            r0 = DpcmSample(chan, cp);
            cp--;
            r1 = DpcmSample(chan, cp) - r0;
            for (i = 0; i < count; i++)
            {
                MixSample(&out[i], &out[i + PCM_DMA_BUF_SIZE], r0 + ((s32)(fw * r1) >> 23), er, el);
                fw += step;
                adv = fw >> 23;
                if (adv == 0)
                    continue;
                fw &= ~0x3F800000;
                ct -= adv;
                if (ct <= 0)
                    goto stop;
                if (--adv == 0)
                {
                    r0 += r1;
                }
                else
                {
                    cp -= adv;
                    r0 = DpcmSample(chan, cp);
                }
                cp--;
                r1 = DpcmSample(chan, cp) - r0;
            }
            cp += 2;
        }
    }
    else if (chan->type & TYPE_REVERSE)
    {
        const s8 *p = (const s8 *)(uintptr_t)cp;

        r0 = *--p;
        r1 = p[-1] - r0;
        for (i = 0; i < count; i++)
        {
            MixSample(&out[i], &out[i + PCM_DMA_BUF_SIZE], r0 + ((s32)(fw * r1) >> 23), er, el);
            fw += step;
            adv = fw >> 23;
            if (adv == 0)
                continue;
            fw &= ~0x3F800000;
            ct -= adv;
            if (ct <= 0)
                goto stop;
            p -= adv;
            r0 = *p;
            r1 = p[-1] - r0;
        }
        cp = ADDR(p + 1);
    }
    // (A compressed voice whose sample isn't compressed plays nothing, as on
    // the GBA.)

    chan->fw = fw;
    chan->ct = ct;
    chan->cp = cp;
    return;

stop:
    chan->status = 0;
}

// SoundMainRAM (MixFrame here): reverb, then mix every active DirectSound channel into the
// next frame's part of the PCM buffer.
static void MixFrame(struct SoundInfo *si, s8 *buf, u8 dmaCounter)
{
    s32 spv = si->pcmSamplesPerVBlank;
    struct SoundChannel *chan;
    s32 i, n;

    if (si->reverb)
    {
        s8 *prev = (dmaCounter == 2) ? si->pcmBuffer : buf + spv;

        for (i = 0; i < spv; i++)
        {
            s32 sum = buf[i + PCM_DMA_BUF_SIZE] + buf[i] + prev[i + PCM_DMA_BUF_SIZE] + prev[i];
            s32 v = (sum * si->reverb) >> 9;

            if (v & 0x80)
                v++;
            buf[i + PCM_DMA_BUF_SIZE] = buf[i] = (s8)v;
        }
    }
    else
    {
        memset(buf, 0, spv);
        memset(buf + PCM_DMA_BUF_SIZE, 0, spv);
    }

    for (n = si->maxChans, chan = si->chans; n > 0; n--, chan++)
    {
        struct WaveData *wav = chan->wav;
        u8 status = chan->status;
        u32 env, vol, er, el;
        uintptr_t loopStart = 0;
        u32 loopLen = 0;

        if (!(status & CHN_ON_MASK))
            continue;

        if (status & CHN_START)
        {
            if (status & CHN_STOP)
            {
                chan->status = 0;
                continue;
            }
            status = 3;
            chan->status = status;
            chan->cp = ADDR(wav->data) + chan->ct;
            chan->ct = wav->size - chan->ct;
            env = 0;
            chan->ev = 0;
            chan->fw = 0;
            if (wav->status & 0xC000)
            {
                status |= CHN_LOOP;
                chan->status = status;
            }
            goto attack;
        }

        env = chan->ev;
        if (status & CHN_IEC)
        {
            u8 echoLength = chan->echoLength--;

            if (echoLength <= 1)
            {
                chan->status = 0;
                continue;
            }
            goto store;
        }
        if (status & CHN_STOP)
        {
            env = (env * chan->release) >> 8;
            if (env > chan->echoVolume)
                goto store;
        echo:
            env = chan->echoVolume;
            if (env == 0)
            {
                chan->status = 0;
                continue;
            }
            status |= CHN_IEC;
            chan->status = status;
            goto store;
        }
        if ((status & CHN_ENV_MASK) == 2)
        {
            env = (env * chan->decay) >> 8;
            if (env > chan->sustain)
                goto store;
            env = chan->sustain;
            if (env == 0)
                goto echo;
            status--;
            chan->status = status;
            goto store;
        }
        if ((status & CHN_ENV_MASK) != 3)
            goto store;
    attack:
        env += chan->attack;
        if (env >= 0xFF)
        {
            env = 0xFF;
            status--;
            chan->status = status;
        }
    store:
        chan->ev = env;
        vol = (env * (si->masterVolume + 1)) >> 4;
        er = chan->er = (chan->rightVolume * vol) >> 8;
        el = chan->el = (chan->leftVolume * vol) >> 8;
        if (status & CHN_LOOP)
        {
            loopStart = ADDR(wav->data) + wav->loopStart;
            loopLen = wav->size - wav->loopStart;
        }

        if (chan->type & (TYPE_REVERSE | TYPE_COMPRESSED))
        {
            MixSpecial(si, chan, buf, spv, er, el, loopLen);
        }
        else if (chan->type & TYPE_FIX)
        {
            const s8 *p = (const s8 *)(uintptr_t)chan->cp;
            s32 ct = chan->ct;

            for (i = 0; i < spv; i++)
            {
                MixSample(&buf[i], &buf[i + PCM_DMA_BUF_SIZE], *p++, er, el);
                if (--ct == 0)
                {
                    if (loopLen == 0)
                    {
                        chan->status = 0;
                        break;
                    }
                    p = (const s8 *)(uintptr_t)loopStart;
                    ct = loopLen;
                }
            }
            if (i == spv)
            {
                chan->ct = ct;
                chan->cp = ADDR(p);
            }
        }
        else
        {
            const s8 *p = (const s8 *)(uintptr_t)chan->cp;
            s32 ct = chan->ct;
            u32 fw = chan->fw;
            u32 step = si->divFreq * chan->freq;
            s32 r0 = p[0];
            s32 r1 = p[1] - r0;
            s32 adv;

            p++;
            for (i = 0; i < spv; i++)
            {
                MixSample(&buf[i], &buf[i + PCM_DMA_BUF_SIZE], r0 + ((s32)(fw * r1) >> 23), er, el);
                fw += step;
                adv = fw >> 23;
                if (adv == 0)
                    continue;
                fw &= ~0x3F800000;
                ct -= adv;
                if (ct <= 0)
                {
                    if (loopLen == 0)
                    {
                        chan->status = 0;
                        break;
                    }
                    p = (const s8 *)(uintptr_t)loopStart;
                    adv = -ct;
                    for (;;)
                    {
                        ct += loopLen;
                        if (ct > 0)
                            break;
                        adv -= loopLen;
                    }
                    p += adv;
                    r0 = *p;
                }
                else if (--adv == 0)
                {
                    r0 += r1;
                }
                else
                {
                    p += adv;
                    r0 = *p;
                }
                p++;
                r1 = *p - r0;
            }
            if (i == spv)
            {
                chan->fw = fw;
                chan->ct = ct;
                chan->cp = ADDR(p - 1);
            }
        }
    }
}

void SoundMain(void)
{
    struct SoundInfo *si = SOUND_INFO_PTR;
    s8 *buf;
    u8 dmaCounter;

    if (si->ident != ID_NUMBER)
        return;
    si->ident++;

    if (si->func)
        ((void (*)(void *))(uintptr_t)si->func)((void *)(uintptr_t)si->intp);
    si->CgbSound();

    buf = si->pcmBuffer;
    dmaCounter = si->pcmDmaCounter;
    if (dmaCounter > 1)
        buf += (si->pcmDmaPeriod - (dmaCounter - 1)) * si->pcmSamplesPerVBlank;
    MixFrame(si, buf, dmaCounter);

    si->ident = ID_NUMBER;
}

// m4aSoundVSync: the GBA restarts the sound DMA from the start of the PCM
// buffer every pcmDmaPeriod frames. audio.c plays the buffer the same way.
void m4aSoundVSync(void)
{
    struct SoundInfo *si = SOUND_INFO_PTR;

    u8 counter;

    if (si == NULL || si->ident - ID_NUMBER > 1)
        return;
    counter = si->pcmDmaCounter;
    si->pcmDmaCounter = counter - 1;
    if ((s32)counter - 1 > 0)
        return;
    si->pcmDmaCounter = si->pcmDmaPeriod;
    PortAudioRestartDma();
}

void SoundMainBTM(void *p)
{
    // Clears a track up to its command pointer, or the start of a player
    // (Clear64byte; 64 bytes on the GBA, which is the same two spans there).
    memset(p, 0, offsetof(struct MusicPlayerTrack, cmdPtr));
}

void RealClearChain(void *x)
{
    struct SoundChannel *chan = x;
    struct MusicPlayerTrack *track = chan->track;
    struct SoundChannel *next, *prev;

    if (track == NULL)
        return;
    next = CHAIN(chan->np);
    prev = CHAIN(chan->pp);
    if (prev)
        prev->np = ADDR(next);
    else
        track->chan = next;
    if (next)
        next->pp = ADDR(prev);
    chan->track = NULL;
}

void MPlayJumpTableCopy(void **mplayJumpTable)
{
    int i;

    for (i = 0; i < 36; i++)
        mplayJumpTable[i] = gMPlayJumpTableTemplate[i];
}

// ---------------------------------------------------------------------------
// Sequencer

static void ClearModMInternal(struct MusicPlayerTrack *track)
{
    track->modM = 0;
    track->lfoSpeedC = 0;
    track->flags |= (track->modT == 0) ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG;
}

// ChnVolSetAsm
static void ChnVolSet(struct SoundChannel *chan, struct MusicPlayerTrack *track)
{
    s32 rp = (s8)chan->rp;
    s32 v;

    v = (s32)(track->volMR * ((0x80 + rp) * chan->ve)) >> 14;
    chan->rightVolume = v > 0xFF ? 0xFF : v;
    v = (s32)(track->volML * ((0x7F - rp) * chan->ve)) >> 14;
    chan->leftVolume = v > 0xFF ? 0xFF : v;
}

void MPlayMain(struct MusicPlayerInfo *mi)
{
    struct SoundInfo *si;
    struct MusicPlayerTrack *track;
    struct SoundChannel *chan;
    u32 tempoC;
    s32 i;

    if (mi->ident != ID_NUMBER)
        return;
    mi->ident++;

    if (mi->func)
        ((void (*)(void *))(uintptr_t)mi->func)((void *)(uintptr_t)mi->intp);

    if (mi->status < 0)
        goto done;
    si = SOUND_INFO_PTR;
    FadeOutBody(mi);
    if (mi->status < 0)
        goto done;

    tempoC = mi->tempoC + mi->tempoI;
    mi->tempoC = tempoC;
    while (tempoC >= 150)
    {
        u32 bit = 1, active = 0;

        for (i = mi->trackCount, track = mi->tracks; i > 0; i--, track++, bit <<= 1)
        {
            if (!(track->flags & MPT_FLG_EXIST))
                continue;
            active |= bit;

            for (chan = track->chan; chan; chan = CHAIN(chan->np))
            {
                if (chan->status & CHN_ON_MASK)
                {
                    if (chan->gt && --chan->gt == 0)
                        chan->status |= CHN_STOP;
                }
                else
                {
                    ClearChain(chan);
                }
            }

            if (track->flags & MPT_FLG_START)
            {
                Clear64byte(track);
                track->flags = MPT_FLG_EXIST;
                track->bendRange = 2;
                track->volX = 64;
                track->lfoSpeed = 22;
                track->tone.type = 1;
            }

            while (track->wait == 0)
            {
                u8 cmd = *track->cmdPtr;

                if (cmd < 0x80)
                {
                    cmd = track->runningStatus;
                }
                else
                {
                    track->cmdPtr++;
                    if (cmd >= 0xBD)
                        track->runningStatus = cmd;
                }

                if (cmd >= 0xCF)
                {
                    ((void (*)(u32, struct MusicPlayerInfo *, struct MusicPlayerTrack *))(uintptr_t)si->plynote)(cmd - 0xCF, mi, track);
                }
                else if (cmd > 0xB0)
                {
                    mi->cmd = cmd - 0xB1;
                    ((TrackFunc)((void **)(uintptr_t)si->MPlayJumpTable)[mi->cmd])(mi, track);
                    if (track->flags == 0)
                        goto next_track;
                }
                else
                {
                    track->wait = gClockTable[cmd - 0x80];
                }
            }

            track->wait--;
            if (track->lfoSpeed && track->mod)
            {
                if (track->lfoDelayC)
                {
                    track->lfoDelayC--;
                }
                else
                {
                    // (The sum isn't cut to 8 bits before "0x80 - sum".)
                    u32 sum = track->lfoSpeedC + track->lfoSpeed;
                    s32 wave;

                    track->lfoSpeedC = sum;
                    wave = ((sum - 0x40) & 0x80) ? (s8)sum : 0x80 - (s32)sum;

                    wave = (track->mod * wave) >> 6;
                    if ((u8)(track->modM ^ wave) != 0)
                    {
                        track->modM = wave;
                        track->flags |= (track->modT == 0) ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG;
                    }
                }
            }
        next_track:;
        }

        mi->clock++;
        if (active == 0)
        {
            mi->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mi->status = active;
        tempoC = (u32)mi->tempoC - 150;
        mi->tempoC = tempoC;
    }

    for (i = mi->trackCount, track = mi->tracks; i > 0; i--, track++)
    {
        if (!(track->flags & MPT_FLG_EXIST) || !(track->flags & 0xF))
            continue;
        TrkVolPitSet(mi, track);
        for (chan = track->chan; chan; chan = CHAIN(chan->np))
        {
            u32 cgbType;

            if (!(chan->status & CHN_ON_MASK))
            {
                ClearChain(chan);
                continue;
            }
            cgbType = chan->type & 7;
            if (track->flags & MPT_FLG_VOLCHG)
            {
                ChnVolSet(chan, track);
                if (cgbType)
                    ((struct CgbChannel *)chan)->mo |= 1;
            }
            if (track->flags & MPT_FLG_PITCHG)
            {
                s32 key = chan->ky + (s8)track->keyM;

                if (key < 0)
                    key = 0;
                if (cgbType)
                {
                    ((struct CgbChannel *)chan)->fr = si->MidiKeyToCgbFreq(cgbType, key, track->pitM);
                    ((struct CgbChannel *)chan)->mo |= 2;
                }
                else
                {
                    chan->freq = MidiKeyToFreq(chan->wav, key, track->pitM);
                }
            }
        }
        track->flags &= 0xF0;
    }

done:
    mi->ident = ID_NUMBER;
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    if (!(track->flags & MPT_FLG_EXIST))
        return;
    for (chan = track->chan; chan; chan = CHAIN(chan->np))
    {
        if (chan->status)
        {
            if (chan->type & 7)
                SOUND_INFO_PTR->CgbOscOff(chan->type & 7);
            chan->status = 0;
        }
        chan->track = NULL;
    }
    track->chan = NULL;
}

void ply_note(u32 noteIndex, struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    struct SoundInfo *si = SOUND_INFO_PTR;
    const struct ToneData *tone;
    struct SoundChannel *chan;
    u8 *cp;
    u32 key, priority, cgbType;
    s32 rhythmPan = 0;
    s32 k;

    track->gateTime = gClockTable[noteIndex];
    cp = track->cmdPtr;
    if (*cp < 0x80)
    {
        track->key = *cp++;
        if (*cp < 0x80)
        {
            track->velocity = *cp++;
            if (*cp < 0x80)
                track->gateTime += *cp++;
        }
        track->cmdPtr = cp;
    }

    if (track->tone.type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
    {
        u32 index = track->key;

        if (track->tone.type & TONEDATA_TYPE_SPL)
            index = track->tone.keySplitTable[index];
        tone = &((const struct ToneData *)track->tone.wav)[index];
        if (tone->type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
            return;
        key = track->key;
        if (track->tone.type & TONEDATA_TYPE_RHY)
        {
            if (tone->pan_sweep & 0x80)
                rhythmPan = (tone->pan_sweep - 0xC0) * 2;
            key = tone->key;
        }
    }
    else
    {
        tone = &track->tone;
        key = track->key;
    }

    priority = mi->priority + track->priority;
    if (priority > 0xFF)
        priority = 0xFF;

    cgbType = tone->type & 7;
    if (cgbType)
    {
        u8 status;

        if (si->cgbChans == NULL)
            return;
        chan = (struct SoundChannel *)&si->cgbChans[cgbType - 1];
        status = chan->status;
        if ((status & CHN_ON_MASK) && !(status & CHN_STOP))
        {
            if (chan->pr > priority)
                return;
            if (chan->pr == priority && chan->track < track)
                return;
        }
    }
    else
    {
        u32 bestPriority = priority;
        struct MusicPlayerTrack *bestTrack = track;
        int foundStopping = 0;
        struct SoundChannel *c;
        s32 n;

        chan = NULL;
        for (n = si->maxChans, c = si->chans; n > 0; n--, c++)
        {
            u8 status = c->status;

            if (!(status & CHN_ON_MASK))
            {
                chan = c;
                break;
            }
            if (status & CHN_STOP)
            {
                if (!foundStopping)
                {
                    foundStopping = 1;
                    bestPriority = c->pr;
                    bestTrack = c->track;
                    chan = c;
                    continue;
                }
            }
            else if (foundStopping)
            {
                continue;
            }
            if (c->pr < bestPriority)
            {
                bestPriority = c->pr;
                bestTrack = c->track;
                chan = c;
            }
            else if (c->pr == bestPriority && c->track >= bestTrack)
            {
                if (c->track > bestTrack)
                    bestTrack = c->track;
                chan = c;
            }
        }
        if (chan == NULL)
            return;
    }

    ClearChain(chan);
    chan->pp = 0;
    chan->np = ADDR(track->chan);
    if (track->chan)
        track->chan->pp = ADDR(chan);
    track->chan = chan;
    chan->track = track;

    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay)
        ClearModMInternal(track);
    TrkVolPitSet(mi, track);

    chan->gt = track->gateTime;
    chan->mk = track->key;
    chan->ve = track->velocity;
    chan->pr = priority;
    chan->ky = key;
    chan->rp = rhythmPan;
    chan->type = tone->type;
    chan->wav = tone->wav;
    chan->attack = tone->attack;
    chan->decay = tone->decay;
    chan->sustain = tone->sustain;
    chan->release = tone->release;
    chan->echoVolume = track->echoVolume;
    chan->echoLength = track->echoLength;
    ChnVolSet(chan, track);

    k = (s32)key + (s8)track->keyM;
    if (k < 0)
        k = 0;
    if (cgbType)
    {
        struct CgbChannel *cgb = (struct CgbChannel *)chan;
        u8 sweep = tone->pan_sweep;

        cgb->le = tone->length;
        if ((sweep & 0x80) || !(sweep & 0x70))
            sweep = 8;
        cgb->sw = sweep;
        chan->freq = si->MidiKeyToCgbFreq(cgbType, k, track->pitM);
    }
    else
    {
        chan->ct = track->unk_3C;
        chan->freq = MidiKeyToFreq(chan->wav, k, track->pitM);
    }
    chan->status = CHN_START;
    track->flags &= 0xF0;
}

void ply_endtie(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;
    u8 key;

    if (*track->cmdPtr < 0x80)
        key = track->key = ReadCmdByte(track);
    else
        key = track->key;

    for (chan = track->chan; chan; chan = CHAIN(chan->np))
    {
        if ((chan->status & 0x83) && !(chan->status & CHN_STOP) && chan->mk == key)
        {
            chan->status |= CHN_STOP;
            return;
        }
    }
}

void ply_fine(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    for (chan = track->chan; chan; chan = CHAIN(chan->np))
    {
        if (chan->status & CHN_ON_MASK)
            chan->status |= CHN_STOP;
        RealClearChain(chan);
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->cmdPtr = ReadAddress(track->cmdPtr);
}

void ply_patt(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    if (track->patternLevel >= 3)
    {
        ply_fine(mi, track);
        return;
    }
    track->patternStack[track->patternLevel++] = track->cmdPtr + 4;
    ply_goto(mi, track);
}

void ply_pend(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    if (track->patternLevel != 0)
        track->cmdPtr = track->patternStack[--track->patternLevel];
}

void ply_rept(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    u8 *cp = track->cmdPtr;

    if (*cp == 0)
    {
        track->cmdPtr = cp + 1;
        ply_goto(mi, track);
        return;
    }
    track->repN++;
    if (track->repN < ReadCmdByte(track))
    {
        ply_goto(mi, track);
        return;
    }
    track->repN = 0;
    track->cmdPtr = cp + 5;
}

void ply_prio(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->priority = ReadCmdByte(track);
}

void ply_tempo(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    mi->tempoD = ReadCmdByte(track) * 2;
    mi->tempoI = (mi->tempoD * mi->tempoU) >> 8;
}

void ply_keysh(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->keyShift = ReadCmdByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_voice(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    const struct ToneData *tone = &mi->tone[ReadCmdByte(track)];

    track->tone = *tone;
}

void ply_vol(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->vol = ReadCmdByte(track);
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_pan(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->pan = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_bend(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->bend = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_bendr(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->bendRange = ReadCmdByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_lfodl(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->lfoDelay = ReadCmdByte(track);
}

void ply_modt(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    u8 modT = ReadCmdByte(track);

    if (track->modT != modT)
    {
        track->modT = modT;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->tune = ReadCmdByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_port(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    u8 reg = ReadCmdByte(track);
    u8 value = ReadCmdByte(track);

    *(vu8 *)(REG_ADDR_SOUND1CNT_L + reg) = value;
}

void ply_lfos(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->lfoSpeed = ReadCmdByte(track);
    if (track->lfoSpeed == 0)
        ClearModMInternal(track);
}

void ply_mod(struct MusicPlayerInfo *mi, struct MusicPlayerTrack *track)
{
    track->mod = ReadCmdByte(track);
    if (track->mod == 0)
        ClearModMInternal(track);
}

// Test aid for JVPPBPORT_PCMDUMP (see audio.c): the DirectSound channels'
// non-pointer fields in their GBA layout (12 x 64 bytes, pointers left 0), so
// they can be compared with a GBA emulator's memory.
void PortDumpSoundChannels(void *fileHandle)
{
    struct SoundInfo *si = SOUND_INFO_PTR;
    u8 out[MAX_DIRECTSOUND_CHANNELS][64];
    int i;

    memset(out, 0, sizeof(out));
    for (i = 0; si && i < MAX_DIRECTSOUND_CHANNELS; i++)
    {
        struct SoundChannel *c = &si->chans[i];

        memcpy(out[i], c, 0x18);  // the u8 fields
        memcpy(out[i] + 0x18, &c->ct, 4);
        memcpy(out[i] + 0x1C, &c->fw, 4);
        memcpy(out[i] + 0x20, &c->freq, 4);
    }
    fwrite(out, sizeof(out), 1, fileHandle);
}
