// Audio processing: tempo / pitch / speed via SoundTouch, run on Winamp's
// decode thread from ModifySamples().
//
// Winamp lets a DSP return between n/2 and 2n samples per call, so output
// from SoundTouch goes through a small FIFO with a reserve. A gentle feedback
// term keeps the reserve level steady, so tempo changes play without gaps.
//
// The plug-in also records which source sample each output sample came
// from, so the UI can work out what part of the song you are actually
// hearing even when audio is stretched (needed for accurate A-B looping).
#include "musicpractice.h"
#include "SoundTouch.h"
#include <vector>
#include <cmath>
#include <climits>
#include <algorithm>

namespace pl {

std::atomic<int>  g_tempo10{0};
std::atomic<int>  g_pitch10{0};
std::atomic<int>  g_speed10{0};
std::atomic<bool> g_clearRequest{false};
std::atomic<bool> g_enabled{true};

namespace {

soundtouch::SoundTouch* st = nullptr;
int  curRate = 0, curNch = 0;
int  appliedTempo = INT_MIN, appliedPitch = INT_MIN, appliedSpeed = INT_MIN;
double ioRatio = 1.0;          // source duration / output duration
bool active = false;           // SoundTouch currently in the signal path
bool primed = false;           // FIFO has reached its reserve level

std::vector<float> inBuf;
std::vector<float> fifo;       // interleaved
size_t fifoHead = 0;           // read offset in floats

// Position tracking (shared with the UI thread).
CRITICAL_SECTION cs;
bool csReady = false;
struct Checkpoint { int64_t outPos; double srcPos; };
const int kRing = 8192;
Checkpoint ring[kRing];
int ringNext = 0, ringCount = 0;
int64_t totalIn = 0, totalOut = 0;
int trackRate = 44100;

size_t FifoFrames() { return curNch ? (fifo.size() - fifoHead) / curNch : 0; }

void FifoConsume(size_t frames) {
    fifoHead += frames * curNch;
    if (fifoHead == fifo.size()) { fifo.clear(); fifoHead = 0; }
    else if (fifoHead > 1 << 16 && fifoHead * 2 > fifo.size()) {
        fifo.erase(fifo.begin(), fifo.begin() + fifoHead);
        fifoHead = 0;
    }
}

void ResetPipeline() {
    if (st) st->clear();
    fifo.clear();
    fifoHead = 0;
    primed = false;
}

void Track(int nIn, int nOut, double srcLagFrames, int srate) {
    if (!csReady) return;
    EnterCriticalSection(&cs);
    totalIn += nIn;
    totalOut += nOut;
    trackRate = srate;
    ring[ringNext] = { totalOut, (double)totalIn - srcLagFrames };
    ringNext = (ringNext + 1) % kRing;
    if (ringCount < kRing) ringCount++;
    LeaveCriticalSection(&cs);
}

inline float ReadSample(const unsigned char* p, int bps) {
    switch (bps) {
    case 16: return *(const int16_t*)p * (1.0f / 32768.0f);
    case 24: { int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); return (v >> 8) * (1.0f / 8388608.0f); }
    default: return (float)(*(const int32_t*)p * (1.0 / 2147483648.0));
    }
}

inline void WriteSample(unsigned char* p, int bps, float f) {
    if (f > 1.0f) f = 1.0f; else if (f < -1.0f) f = -1.0f;
    switch (bps) {
    case 16: *(int16_t*)p = (int16_t)lrintf(f * 32767.0f); break;
    case 24: { int32_t v = (int32_t)lrintf(f * 8388607.0f); p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); break; }
    default: *(int32_t*)p = (int32_t)llrint(f * 2147483647.0); break;
    }
}

} // namespace

void DspInit() {
    if (!csReady) { InitializeCriticalSection(&cs); csReady = true; }
    if (!st) st = new soundtouch::SoundTouch();
    curRate = curNch = 0;
    active = false;
    ResetPipeline();
}

void DspQuit() {
    delete st;
    st = nullptr;
    fifo.clear(); fifo.shrink_to_fit();
    inBuf.clear(); inBuf.shrink_to_fit();
}

int DspProcess(short* samples, int n, int bps, int nch, int srate) {
    if (!st || n <= 0 || nch < 1 || nch > 8 || srate < 1000 || (bps != 16 && bps != 24 && bps != 32)) {
        Track(n, n, 0, srate > 0 ? srate : 44100);
        return n;
    }

    if (srate != curRate || nch != curNch) {
        curRate = srate; curNch = nch;
        st->setSampleRate(srate);
        st->setChannels(nch);
        appliedTempo = appliedPitch = appliedSpeed = INT_MIN;
        ResetPipeline();
    }
    if (g_clearRequest.exchange(false)) ResetPipeline();

    const int t = g_tempo10.load(), p = g_pitch10.load(), s = g_speed10.load();
    if (!g_enabled.load() || (t == 0 && p == 0 && s == 0)) {
        // Bypassed or neutral: pass audio straight through, zero added latency.
        if (active) { ResetPipeline(); active = false; }
        Track(n, n, 0, srate);
        return n;
    }
    active = true;

    if (t != appliedTempo || s != appliedSpeed) {
        double tf = 1.0 + t / 1000.0, sf = 1.0 + s / 1000.0;
        // Winamp only allows 0.5x..2x output per call; keep the overall
        // playback rate inside that window.
        if (tf * sf < 0.5) tf = 0.5 / sf;
        if (tf * sf > 2.0) tf = 2.0 / sf;
        st->setTempo(tf);
        st->setRate(sf);
        ioRatio = tf * sf;
        appliedTempo = t; appliedSpeed = s;
    }
    if (p != appliedPitch) { st->setPitchSemiTones(p / 10.0); appliedPitch = p; }

    // Feed the decoded block.
    const int bytes = bps / 8;
    unsigned char* raw = (unsigned char*)samples;
    inBuf.resize((size_t)n * nch);
    for (size_t i = 0; i < inBuf.size(); i++) inBuf[i] = ReadSample(raw + i * bytes, bps);
    st->putSamples(inBuf.data(), (unsigned)n);

    // Collect whatever SoundTouch produced.
    unsigned ready = st->numSamples();
    if (ready) {
        size_t old = fifo.size();
        fifo.resize(old + (size_t)ready * nch);
        unsigned got = st->receiveSamples(&fifo[old], ready);
        fifo.resize(old + (size_t)got * nch);
    }

    // Decide how much to hand back to Winamp.
    const size_t reserve = (size_t)std::max(2048, srate / 8);   // ~125 ms
    const int minOut = (n + 1) / 2, maxOut = n * 2;
    size_t have = FifoFrames();
    if (!primed && have >= reserve) primed = true;

    int emit = minOut;
    if (primed) {
        double want = n / ioRatio + ((double)have - (double)reserve) * 0.02;
        emit = (int)std::lround(want);
        emit = std::min(std::max(emit, minOut), maxOut);
    }
    int fromFifo = primed ? (int)std::min<size_t>((size_t)emit, have) : 0;
    if (primed && fromFifo < emit) primed = false;  // underrun: rebuild reserve

    const float* src = fifo.data() + fifoHead;
    size_t outVals = (size_t)fromFifo * nch;
    for (size_t i = 0; i < outVals; i++) WriteSample(raw + i * bytes, bps, src[i]);
    memset(raw + outVals * bytes, 0, ((size_t)emit * nch - outVals) * bytes);
    FifoConsume((size_t)fromFifo);

    // Source-sample lag of the last sample we just emitted.
    double lag = (double)st->numUnprocessedSamples()
               + ((double)st->numSamples() + (double)FifoFrames()) * ioRatio;
    Track(n, emit, lag, srate);
    return emit;
}

bool DspSourceLagMs(int outputBufferedMs, double* lagMs) {
    if (!csReady) return false;
    EnterCriticalSection(&cs);
    if (ringCount == 0) { LeaveCriticalSection(&cs); return false; }
    const double rate = trackRate;
    const int64_t heardOut = totalOut - (int64_t)(outputBufferedMs * rate / 1000.0);

    // Walk back from the newest checkpoint to find the one covering heardOut.
    int newest = (ringNext - 1 + kRing) % kRing;
    int idx = newest;
    double src = ring[idx].srcPos;
    bool found = false;
    for (int k = 0; k < ringCount; k++) {
        const Checkpoint& c = ring[idx];
        if (c.outPos <= heardOut) {
            src = c.srcPos;
            if (idx != newest) {
                const Checkpoint& nx = ring[(idx + 1) % kRing];
                if (nx.outPos > c.outPos)
                    src = c.srcPos + (nx.srcPos - c.srcPos) * double(heardOut - c.outPos) / double(nx.outPos - c.outPos);
            }
            found = true;
            break;
        }
        src = c.srcPos;   // oldest seen so far
        idx = (idx - 1 + kRing) % kRing;
    }
    (void)found;
    *lagMs = ((double)totalIn - src) * 1000.0 / rate;
    LeaveCriticalSection(&cs);
    return true;
}

} // namespace pl
