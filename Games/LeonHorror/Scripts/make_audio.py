"""Synthesize the LeonHorror sound set (clinic survival horror) from nothing but maths.

Everything is procedural: oscillators, coloured noise, modal strikes, formant
voices and convolution with synthetic room impulse responses. No samples.

Run (the system Python has no numpy, Blender's bundled Python does; bpy is not used):

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
        --python Games/LeonHorror/Scripts/make_audio.py -- [options]

Options (after the "--"):
    --only A,B,...   generate only these sounds; a name matches its numbered
                     variants too (SFX_Step_Vinyl -> _01.._06), globs allowed
    --check          do not generate, only measure the files on disk
    --list           print the sound table and exit
    --readme         rewrite Content/Audio/README.md from the table and the files

Writes Games/LeonHorror/Content/Audio/<Name>.wav, 44.1 kHz 16-bit PCM:
    SFX_*  mono one-shots         (peak about -3 dBFS)
    AMB_*  mono seamless loops    (positional, the engine spatialises them)
    MUS_*  stereo seamless beds   (peak about -9 dBFS)
    STG_*  stereo one-shot stingers
and, on a full run, README.md with one row per file.

Every run ends with a measurement pass over what was written (duration, peak,
RMS, DC, spectral centroid, loop seam) and exits non-zero if a file is silent,
clipped, offset, over its duration budget, or a loop does not close.
The output is deterministic: each sound owns a generator seeded from its name.
"""

from __future__ import annotations

import fnmatch
import os
import sys
import wave
import zlib

import numpy as np

SR = 44100
SEED = 1987
TWO_PI = 2.0 * np.pi
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Audio"))
TOTAL_BUDGET_MB = 26.0


# =============================================================================
# Toolkit: basics and envelopes
# =============================================================================

def secs(t):
    return max(1, int(round(t * SR)))


def tgrid(n):
    return np.arange(n) / SR


def db(x):
    return 10.0 ** (x / 20.0)


def to_db(v):
    return 20.0 * np.log10(max(float(v), 1e-12))


def nrm(x):
    return x / (np.abs(x).max() + 1e-12)


def env(n, pts):
    """Piecewise-linear envelope through (seconds, value) points."""
    ts, vs = zip(*pts)
    return np.interp(tgrid(n), ts, vs)


def P(n, v):
    """A parameter may be a number, an array or a list of (seconds, value) points."""
    if isinstance(v, np.ndarray):
        return v
    if isinstance(v, (int, float)):
        return np.full(n, float(v))
    return env(n, v)


def decay(n, t60):
    return np.exp(-6.908 * tgrid(n) / t60)


def bell(n, peak=0.5, sharp=2.0):
    """A single hump, zero at both ends, with its top at `peak` (0..1) of the length."""
    x = np.linspace(0.0, 1.0, n) ** (np.log(0.5) / np.log(peak))
    return np.sin(np.pi * x) ** sharp


def fade(x, fin, fout):
    x = x.copy()
    a, b = min(secs(fin), len(x) // 2), min(secs(fout), len(x) // 2)
    wi = 0.5 - 0.5 * np.cos(np.pi * np.arange(a) / a)
    wo = 0.5 + 0.5 * np.cos(np.pi * np.arange(b) / b)
    if x.ndim == 2:
        wi, wo = wi[:, None], wo[:, None]
    x[:a] *= wi
    x[-b:] *= wo
    return x


def drive(x, g):
    """Soft saturation; keeps unity gain for small signals out of the comparison."""
    return np.tanh(g * x) / np.tanh(g)


def pan(x, p):
    """Equal-power pan of a mono signal, p in [-1, 1]."""
    a = (p + 1.0) * np.pi / 4.0
    return np.stack([np.cos(a) * x, np.sin(a) * x], axis=1)


def put(out, x, t, g=1.0, p=0.0):
    """Mix x into out at t seconds; what does not fit is dropped."""
    if out.ndim == 2 and x.ndim == 1:
        x = pan(x, p)
    i = max(0, int(round(t * SR)))
    m = min(len(x), len(out) - i)
    if m > 0:
        out[i:i + m] += g * x[:m]


def put_wrap(out, x, t, g=1.0, p=0.0):
    """Mix x into a loop: whatever runs past the end comes back in at the start."""
    if out.ndim == 2 and x.ndim == 1:
        x = pan(x, p)
    idx = (int(round(t * SR)) + np.arange(len(x))) % len(out)
    np.add.at(out, idx, g * x)


def qf(f, n):
    """Nearest frequency with a whole number of cycles in n samples (so it loops)."""
    return max(1.0, round(f * n / SR)) * SR / n


# =============================================================================
# Toolkit: filters. A pure-Python sample loop is far too slow at 44.1 kHz, so
# static filters multiply the spectrum by the exact biquad response (causal, the
# ringing is real) and sweeps go through a short-time Fourier filter.
# =============================================================================

def _apply(x, hfn, tail=0.0):
    n = x.shape[0] + (secs(tail) if tail else 0)
    h = hfn(np.fft.rfftfreq(n, 1.0 / SR))
    if x.ndim == 2:
        h = h[:, None]
    return np.fft.irfft(np.fft.rfft(x, n, axis=0) * h, n, axis=0)


def _biquad(f, kind, f0, q=0.7071, gain=0.0):
    """Frequency response of an RBJ cookbook biquad."""
    w0 = TWO_PI * min(f0, 0.49 * SR) / SR
    cw, alpha, a_lin = np.cos(w0), np.sin(w0) / (2.0 * q), 10.0 ** (gain / 40.0)
    if kind == "lp":
        b, a = ((1 - cw) / 2, 1 - cw, (1 - cw) / 2), (1 + alpha, -2 * cw, 1 - alpha)
    elif kind == "hp":
        b, a = ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2), (1 + alpha, -2 * cw, 1 - alpha)
    elif kind == "bp":
        b, a = (alpha, 0.0, -alpha), (1 + alpha, -2 * cw, 1 - alpha)
    elif kind == "peak":
        b, a = (1 + alpha * a_lin, -2 * cw, 1 - alpha * a_lin), (1 + alpha / a_lin, -2 * cw, 1 - alpha / a_lin)
    else:
        raise ValueError(kind)
    z = np.exp(-1j * TWO_PI * f / SR)
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def filt(x, stages, tail=0.0):
    """Cascade of biquads, each (kind, f0, q[, gain_db]). Without a tail it is circular,
    which is exactly what a loop wants and harmless for a sound that ends in silence."""
    def h(f):
        r = np.ones(len(f), complex)
        for s in stages:
            r = r * _biquad(f, *s)
        return r
    return _apply(x, h, tail)


_BUTTER = {2: (0.7071,), 4: (0.5412, 1.3066)}


def lp(x, fc, order=2, tail=0.0):
    return filt(x, [("lp", fc, q) for q in _BUTTER[order]], tail)


def hp(x, fc, order=2, tail=0.0):
    return filt(x, [("hp", fc, q) for q in _BUTTER[order]], tail)


def band(x, lo, hi, order=2, tail=0.0):
    return filt(x, [("hp", lo, q) for q in _BUTTER[order]] + [("lp", hi, q) for q in _BUTTER[order]], tail)


def resonate(x, freqs, q=20.0, amps=None, tail=0.0):
    """Parallel bank of resonators: the body an excitation rings through."""
    qs = np.broadcast_to(np.asarray(q, float), (len(freqs),))
    amps = np.ones(len(freqs)) if amps is None else amps

    def h(f):
        r = np.zeros(len(f), complex)
        for f0, qq, a in zip(freqs, qs, amps):
            r = r + a * _biquad(f, "bp", f0, qq)
        return r
    return _apply(x, h, tail)


def stft_filter(x, mag_fn, block=1024):
    """Time-varying filter: mag_fn(t[:, None], f[None, :]) gives a gain per frame and bin.
    Hann analysis and synthesis windows at 75 % overlap add up to a constant 1.5."""
    hop, n = block // 4, len(x)
    xp = np.concatenate([np.zeros(block), x, np.zeros(2 * block)])
    nfr = (len(xp) - block) // hop + 1
    nfr -= nfr % 4
    win = 0.5 - 0.5 * np.cos(TWO_PI * np.arange(block) / block)
    idx = np.arange(block)[None, :] + hop * np.arange(nfr)[:, None]
    spec = np.fft.rfft(xp[idx] * win, axis=1)
    tc = ((np.arange(nfr) * hop + block / 2 - block) / SR)[:, None]
    f = np.fft.rfftfreq(block, 1.0 / SR)[None, :]
    y = np.fft.irfft(spec * mag_fn(tc, f), block, axis=1) * win
    out = np.zeros(len(xp))
    for ph in range(4):  # frames ph, ph+4, ... sit end to end, so each phase is one reshape
        seg = y[ph::4].reshape(-1)
        out[ph * hop:ph * hop + len(seg)] += seg
    return out[block:block + n] / 1.5


def sweep_bp(x, times, centers, q):
    """Band-pass whose centre follows (times, centers); q may follow it too."""
    qa = np.broadcast_to(np.asarray(q, float), np.shape(centers))

    def mag(tc, f):
        fc = np.interp(tc[:, 0], times, centers)[:, None]
        qq = np.interp(tc[:, 0], times, qa)[:, None]
        ff = np.maximum(f, 1.0)
        return 1.0 / np.sqrt(1.0 + (qq * (ff / fc - fc / ff)) ** 2)
    return stft_filter(x, mag)


def conv(x, k):
    n = len(x) + len(k) - 1
    m = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(k, m), m)[:n]


def conv_circ(x, k):
    """Circular convolution: a tail that leaves the end of a loop re-enters at its start."""
    n = len(x)
    kf = np.zeros(n)
    np.add.at(kf, np.arange(len(k)) % n, k)
    return np.fft.irfft(np.fft.rfft(x) * np.fft.rfft(kf), n)


# =============================================================================
# Toolkit: sources
# =============================================================================

def noise(rng, n, slope=0.0, lo=None, hi=None, order=4):
    """Gaussian noise with a spectral slope in dB/octave (0 white, -3 pink, -6 brown)
    and optional band edges. Built in the frequency domain, so it is periodic in n."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    f[0] = f[1] if n > 1 else 1.0
    mag = (f / 1000.0) ** (slope / 6.0206)
    if lo:
        mag = mag / np.sqrt(1.0 + (lo / f) ** (2 * order))
    if hi:
        mag = mag / np.sqrt(1.0 + (f / hi) ** (2 * order))
    mag[0] = 0.0
    x = np.fft.irfft(spec * mag, n)
    return x / (x.std() + 1e-12)


def lfo_noise(rng, n, rate):
    """Smooth random wander around zero (unit deviation), periodic in n."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / SR)
    x = np.fft.irfft(spec * np.exp(-(f / rate) ** 2), n)
    return x / (x.std() + 1e-12)


def wander(rng, n, rate):
    """Smooth random wander squashed into 0..1."""
    return np.clip(0.5 + 0.35 * lfo_noise(rng, n, rate), 0.0, 1.0)


def additive(phase, amps, fmax):
    out = np.zeros(len(phase))
    for k, a in enumerate(amps, 1):
        if k * fmax > 0.45 * SR:  # stay under Nyquist instead of aliasing
            break
        out += a * np.sin(k * phase)
    return out


def tone(f, n, amps=(1.0,), ph=0.0):
    """Harmonic oscillator; f is a number or a per-sample array (for glides)."""
    if np.ndim(f) == 0:
        return additive(TWO_PI * f * tgrid(n) + ph, amps, f)
    return additive(TWO_PI * np.cumsum(f) / SR + ph, amps, float(np.max(f)))


def saw(k, roll=1.0):
    return [1.0 / i ** roll for i in range(1, k + 1)]


def modal(n, freqs, t60s, amps=None):
    """Sum of decaying partials: the ring of a struck object."""
    f = np.asarray(freqs, float)[:, None]
    d = np.broadcast_to(np.asarray(t60s, float), (len(freqs),))[:, None]
    a = (np.ones(len(freqs)) if amps is None else np.asarray(amps, float))[:, None]
    t = tgrid(n)[None, :]
    return (a * np.exp(-6.908 * t / d) * np.sin(TWO_PI * f * t)).sum(0)


def burst(rng, dur, t60, lo, hi, attack=0.0005, slope=0.0):
    """Band-limited noise burst with a fast decay: the contact noise of an impact."""
    n = secs(dur)
    return nrm(noise(rng, n, slope, lo, hi) * decay(n, t60) * np.minimum(1.0, tgrid(n) / attack))


def strike(rng, dur, freqs, t60s, amps=None, nz=0.5, nz_band=(500, 6000), nz_t60=0.02, detune=0.03):
    """An impact: modal ring plus contact noise. The modes are detuned a little at
    random so that numbered variants of a sound are not copies."""
    n = secs(dur)
    f = np.asarray(freqs, float) * (1.0 + rng.uniform(-detune, detune, len(freqs)))
    y = nrm(modal(n, f, t60s, amps))
    if nz:
        b = burst(rng, min(dur, nz_t60 * 3.0), nz_t60, *nz_band)
        y[:len(b)] += nz * b[:len(y)]
    return nrm(y)


def grains(rng, dur, density, lo, hi, g_t60=0.004, shape=None):
    """Crackle: random sparse impulses (per second: density), each a tiny noise grain."""
    n = secs(dur)
    d = density * (P(n, shape) if shape is not None else 1.0)
    imp = (rng.random(n) < d / SR) * rng.uniform(0.2, 1.0, n) * rng.choice([-1.0, 1.0], n)
    return nrm(conv(imp, burst(rng, max(2.5 * g_t60, 0.002), g_t60, lo, hi))[:n])


def pulses(rng, n, rate, irr=0.3, amp_var=0.3):
    """Irregular impulse train (rate in Hz, number or points): stick-slip, vocal fry."""
    r = np.maximum(P(n, rate), 0.5)
    out = np.zeros(n)
    pos = rng.uniform(0.0, SR / r[0])
    while pos < n - 1:
        i = int(pos)
        out[i] += max(0.1, 1.0 + amp_var * rng.standard_normal())
        pos += SR / r[i] * np.exp(irr * rng.standard_normal())
    return out


def creak(rng, dur, rate, res, q=22.0, shape=None, irr=0.12):
    """Stick-slip friction: an irregular pulse train ringing the resonances of the part."""
    n = secs(dur)
    p = pulses(rng, n, rate, irr)
    if shape is not None:
        p = p * P(n, shape)
    amps = [1.0 / np.sqrt(k) for k in range(1, len(res) + 1)]
    return nrm(resonate(p, res, q, amps, tail=0.12))


# =============================================================================
# Toolkit: voice. A glottal pulse train plus aspiration noise through moving
# formants; size and menace come from the pitch and formant ranges.
# =============================================================================

def glottal(rng, f0, jitter=0.02, shimmer=0.06, rough=0.0, oq=0.65):
    """Derivative of a Rosenberg glottal flow. `rough` weakens alternate cycles
    (a subharmonic, the sound of a strained throat) and drops a few at random."""
    n = len(f0)
    j = 1.0 + jitter * lfo_noise(rng, n, 40.0) if jitter else 1.0
    phase = np.cumsum(f0 * j) / SR
    cyc = np.floor(phase).astype(int)
    ph = phase - cyc
    ncyc = int(cyc[-1]) + 2
    amp = 1.0 + shimmer * rng.standard_normal(ncyc)
    if rough:
        amp = amp * (1.0 - rough * (np.arange(ncyc) % 2) * rng.uniform(0.5, 1.0, ncyc))
        amp = amp * np.where(rng.random(ncyc) < 0.12 * rough, 0.25, 1.0)
    tp, tn = 0.66 * oq, 0.34 * oq
    flow = np.where(ph < tp, 0.5 * (1.0 - np.cos(np.pi * ph / tp)),
                    np.where(ph < tp + tn, np.cos(0.5 * np.pi * (ph - tp) / tn), 0.0))
    return nrm(np.diff(flow * amp[cyc], prepend=0.0))


def tract(x, forms, times=None, bws=(80, 100, 140, 200, 250), amps=(1.0, 0.7, 0.4, 0.25, 0.15), dark=4500.0):
    """Moving formant filter. forms is one row of formant frequencies per keyframe."""
    forms = np.atleast_2d(np.asarray(forms, float))
    if times is None:
        times = np.linspace(0.0, len(x) / SR, forms.shape[0])

    def mag(tc, f):
        m = 0.0
        for j in range(forms.shape[1]):
            fj = np.interp(tc[:, 0], times, forms[:, j])[:, None]
            m = m + amps[j] / np.sqrt(1.0 + ((f - fj) / (0.5 * bws[j])) ** 2)
        return m / np.sqrt(1.0 + (f / dark) ** 2)
    return stft_filter(x, mag)


def formant_static(x, forms, bws=(110, 140, 190, 260), amps=(1.0, 0.8, 0.5, 0.3), tail=0.05):
    """Fixed formants with causal ringing, for clicks where a pre-echo would show."""
    return resonate(x, forms, [f / b for f, b in zip(forms, bws)], amps[:len(forms)], tail)


def voice(rng, dur, f0, forms, times=None, bws=(80, 100, 140, 200, 250), amps=(1.0, 0.7, 0.4, 0.25, 0.15),
          voiced=1.0, breath=0.2, jitter=0.02, shimmer=0.06, rough=0.0, oq=0.65, dark=4500.0):
    n = secs(dur)
    src = glottal(rng, P(n, f0), jitter, shimmer, rough, oq) * P(n, voiced)
    src = src + 0.25 * noise(rng, n, -3.0, 300, 9000) * P(n, breath)
    return tract(src, forms, times, bws, amps, dark)


def mask(x):
    """A rubber respirator: band-limited, boxy, with one short internal reflection."""
    y = filt(x, [("hp", 170, 0.7071), ("lp", 1500, 0.5412), ("lp", 1500, 1.3066),
                 ("peak", 430, 2.5, 7.0), ("peak", 980, 4.0, 4.0)], tail=0.05)
    d = secs(0.0013)
    y[d:] += 0.4 * y[:-d].copy()
    return y


# =============================================================================
# Toolkit: rooms. Sparse early reflections plus band-split noise whose high bands
# die sooner (tile and plaster absorb little, air and gaps eat the top).
# =============================================================================

ROOMS = {
    #            rt60  lowpass  predelay  early-reflection window (s)
    "small":    (0.45, 5000.0, 0.003, 0.025),
    "wetroom":  (0.75, 5500.0, 0.004, 0.035),
    "corridor": (1.00, 4200.0, 0.008, 0.060),
    "tiled":    (1.10, 6500.0, 0.006, 0.050),
    "hall":     (3.20, 3500.0, 0.020, 0.090),
}
_IR_CACHE = {}


def room_ir(name, ch=0):
    key = (name, ch)
    if key in _IR_CACHE:
        return _IR_CACHE[key]
    rt, cut, pre, er_win = ROOMS[name]
    rng = np.random.default_rng([SEED, zlib.crc32(name.encode()), ch])
    n = secs(rt * 1.35 + 0.1)
    t = tgrid(n)
    late = np.zeros(n)
    for lo, hi, k in ((None, 250, 1.15), (250, 1000, 1.0), (1000, 4000, 0.7), (4000, None, 0.35)):
        late += noise(rng, n, 0.0, lo or 20.0, hi, order=2) * np.exp(-6.908 * t / (rt * k))
    late *= 1.0 - np.exp(-t / (0.2 * er_win))  # the diffuse field builds up, it does not start at full level
    ir = 0.35 * late / np.sqrt((late ** 2).sum())
    for _ in range(12):
        d = rng.uniform(0.15, 1.0) * er_win
        ir[secs(d)] += rng.choice([-1.0, 1.0]) * 0.12 * (0.15 * er_win / d) ** 0.7
    ir = np.concatenate([np.zeros(secs(pre)), lp(ir, cut, 2)])
    ir /= np.sqrt((ir ** 2).sum())  # unit energy: `wet` then means the same for every room
    _IR_CACHE[key] = ir
    return ir


def reverb(x, room="corridor", wet=0.2, tail=None, stereo=False):
    """Dry plus convolved wet. A mono input stays mono unless stereo is asked for."""
    tail_n = secs(ROOMS[room][0] * 0.9 if tail is None else tail) if tail != 0 else 0
    n = len(x) + tail_n
    chans = 2 if (stereo or x.ndim == 2) else 1
    out = np.zeros((n, chans))
    for c in range(chans):
        xc = x[:, c] if x.ndim == 2 else (x * (0.7071 if chans == 2 else 1.0))
        w = conv(xc, room_ir(room, c if chans == 2 else 0))[:n]
        out[:len(xc), c] += xc
        out[:len(w), c] += wet * w
    if tail_n > 1:  # a tail cut shorter than the room rings is eased out, not gated
        m = int(0.6 * tail_n)
        out[n - m:] *= (0.5 + 0.5 * np.cos(np.pi * np.arange(m) / m))[:, None]
    return out[:, 0] if chans == 1 else out


def reverb_loop(x, room="corridor", wet=0.2, dry=1.0):
    """Reverb for loops: the convolution is circular, so the loop stays seamless."""
    if x.ndim == 1:
        return dry * x + wet * conv_circ(x, room_ir(room, 0))
    return np.stack([dry * x[:, c] + wet * conv_circ(x[:, c], room_ir(room, c)) for c in (0, 1)], axis=1)


# =============================================================================
# Toolkit: loops, finishing, files
# =============================================================================

def crossfade_loop(x, n_loop):
    """Fold the overhang past n_loop back into the head with an equal-power crossfade,
    so the last sample is followed by what really came after it."""
    xf = len(x) - n_loop
    a = np.arange(xf) / xf
    y = x[:n_loop].copy()
    y[:xf] = x[:xf] * np.sin(0.5 * np.pi * a) + x[n_loop:] * np.cos(0.5 * np.pi * a)
    return y


def finish_loop(x, peak_db):
    x = x - x.mean(axis=0)
    return x * db(peak_db) / np.abs(x).max()


def finish_oneshot(x, peak_db, max_dur):
    """Remove DC, trim silence, fade the ends so nothing clicks, normalise."""
    x = hp(x, 22.0, 2, tail=0.1)
    mono = np.abs(x).max(axis=1) if x.ndim == 2 else np.abs(x)
    idx = np.nonzero(mono > mono.max() * db(-60.0))[0]
    x = x[max(0, idx[0] - secs(0.001)):min(len(x), idx[-1] + secs(0.02))]
    x = x[:secs(max_dur) - 2]
    x = fade(x, 0.0007, min(0.04, 0.25 * len(x) / SR))
    return x * db(peak_db) / np.abs(x).max()


def write_wav(path, x):
    pcm = np.round(np.clip(x, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2 if x.ndim == 2 else 1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    return np.frombuffer(raw, "<i2").astype(float).reshape(-1, ch), ch, sw, sr


# =============================================================================
# The sound table
# =============================================================================

SOUNDS = []
KINDS = {  # kind: (channels, loops, default peak dBFS)
    "oneshot": (1, False, -3.0),
    "loop": (1, True, -9.0),
    "bed": (2, True, -9.0),
    "stinger": (2, False, -3.0),
}


def sound(stem, kind="oneshot", variants=0, max_dur=1.0, desc="", vol=0.8, radius=None, peak=None, centroid=None):
    """Register a sound. The function gets (rng, variant_index) and returns samples.
    max_dur is the budget in seconds; centroid an optional sanity range in Hz."""
    def deco(fn):
        names = [stem] if not variants else ["%s_%02d" % (stem, i + 1) for i in range(variants)]
        for i, name in enumerate(names):
            SOUNDS.append(dict(name=name, stem=stem, fn=fn, index=i, kind=kind, max_dur=max_dur, desc=desc,
                               vol=vol, radius=radius, peak=KINDS[kind][2] if peak is None else peak,
                               centroid=centroid))
        return fn
    return deco


# ----------------------------------------------------------------------------
# Footsteps
# ----------------------------------------------------------------------------

def _step(rng, surface):
    out = np.zeros(secs(0.33))
    pitch, lvl = rng.uniform(0.9, 1.12), rng.uniform(0.8, 1.0)
    toe_t = rng.uniform(0.085, 0.13)
    if surface == "vinyl":  # dull: the sheet damps the slab under it
        heel = strike(rng, 0.2, [78 * pitch, 131 * pitch, 240 * pitch], [0.09, 0.06, 0.04], [1, 0.5, 0.25],
                      nz=0.9, nz_band=(60, 700), nz_t60=0.05)
        tick = 0.22 * burst(rng, 0.03, 0.012, 900, 3200)
        toe = 0.45 * strike(rng, 0.15, [110 * pitch, 190 * pitch], [0.06, 0.04], [1, 0.4],
                            nz=1.0, nz_band=(150, 1200), nz_t60=0.04)
        ns = secs(0.16)
        scuff = 0.035 * noise(rng, ns, -3.0, 1500, 6000) * bell(ns, 0.3)
    else:  # ceramic tile: hard contact, the tile itself rings
        heel = strike(rng, 0.2, [95 * pitch, 170 * pitch, 320 * pitch], [0.06, 0.045, 0.03], [1, 0.5, 0.3],
                      nz=0.7, nz_band=(120, 1500), nz_t60=0.03)
        tick = 0.55 * strike(rng, 0.09, [1900 * pitch, 3150 * pitch, 4700 * pitch, 6900 * pitch],
                             [0.05, 0.04, 0.03, 0.02], [1, 0.7, 0.5, 0.3], nz=0.8, nz_band=(2000, 9000), nz_t60=0.008)
        toe = 0.4 * strike(rng, 0.12, [140 * pitch, 2400 * pitch, 3900 * pitch], [0.05, 0.03, 0.02], [1, 0.35, 0.2],
                           nz=0.9, nz_band=(300, 5000), nz_t60=0.02)
        ns = secs(0.14)
        scuff = 0.04 * noise(rng, ns, -2.0, 2500, 9000) * bell(ns, 0.3)
    put(out, heel, 0.0, lvl)
    put(out, tick, 0.001, lvl)
    put(out, toe, toe_t, lvl * rng.uniform(0.8, 1.0))
    put(out, scuff, toe_t + 0.01, rng.uniform(0.6, 1.0))
    return reverb(out, "small", 0.06 if surface == "vinyl" else 0.1, tail=0)


@sound("SFX_Step_Vinyl", variants=6, max_dur=0.36, vol=0.5, radius=9,
       desc="Player footstep, hard shoe on vinyl floor (pick a variant at random)")
def sfx_step_vinyl(rng, i):
    return _step(rng, "vinyl")


@sound("SFX_Step_Tile", variants=6, max_dur=0.36, vol=0.55, radius=11,
       desc="Player footstep on ceramic tile: brighter, with ring")
def sfx_step_tile(rng, i):
    return _step(rng, "tile")


@sound("SFX_EnemyStep", variants=4, max_dur=0.7, vol=0.7, radius=14,
       desc="Enemy bare foot: wet slap and drag")
def sfx_enemy_step(rng, i):
    out = np.zeros(secs(0.64))
    slap = burst(rng, 0.12, 0.07, 250, 2600, attack=0.002)
    thump = strike(rng, 0.2, [62, 104], [0.12, 0.08], [1, 0.5], nz=0.5, nz_band=(40, 300), nz_t60=0.08)
    wet = grains(rng, 0.09, 900, 1800, 7000, 0.006) * decay(secs(0.09), 0.08)
    nd = secs(rng.uniform(0.3, 0.4))
    drag = noise(rng, nd, -3.0, 350, 2400) * bell(nd, 0.35, 1.5) * (0.3 + 0.7 * wander(rng, nd, 28.0) ** 2)
    t_slap, t_drag = (0.0, 0.14) if i % 2 == 0 else (0.3, 0.0)  # drag after the slap, or into it
    put(out, slap, t_slap, 0.9)
    put(out, thump, t_slap, 0.8)
    put(out, wet, t_slap + 0.012, 0.3)
    put(out, drag, t_drag, 0.16)
    return reverb(out, "small", 0.08, tail=0)


# ----------------------------------------------------------------------------
# Doors
# ----------------------------------------------------------------------------

def _handle_click(rng, g=1.0):
    return g * strike(rng, 0.08, [1750, 3100, 4700, 7200], [0.04, 0.035, 0.03, 0.02], [1, 0.8, 0.5, 0.3],
                      nz=0.6, nz_band=(1500, 9000), nz_t60=0.01)


def _latch(rng):
    return strike(rng, 0.15, [410, 930, 1520, 2650], [0.09, 0.07, 0.05, 0.03], [1, 0.8, 0.5, 0.3],
                  nz=0.7, nz_band=(200, 3000), nz_t60=0.025)


def _door_body(rng, dur=0.3, t60=0.15):
    return strike(rng, dur, [58, 92, 163], [t60, 0.8 * t60, 0.5 * t60], [0.7, 1, 0.5],
                  nz=0.35, nz_band=(40, 500), nz_t60=0.06)


@sound("SFX_Door_Open", max_dur=1.7, vol=0.8, radius=16,
       desc="Wooden door opens: handle click, latch, hinge creak")
def sfx_door_open(rng, i):
    out = np.zeros(secs(1.25))
    cr = creak(rng, 0.85, [(0, 36), (0.25, 62), (0.55, 47), (0.85, 28)], [640, 1180, 1930, 2740, 3600], 22.0,
               shape=[(0, 0), (0.06, 1), (0.3, 0.6), (0.5, 0.9), (0.85, 0)])
    put(out, _handle_click(rng), 0.0, 0.55)
    put(out, _latch(rng), 0.11, 0.9)
    put(out, _door_body(rng, 0.2, 0.1), 0.115, 0.45)
    put(out, cr, 0.3, 0.5)
    return reverb(out, "corridor", 0.2, tail=0.4)


@sound("SFX_Door_Close", max_dur=1.9, vol=0.85, radius=20,
       desc="Door swings shut: air, latch slam, corridor tail")
def sfx_door_close(rng, i):
    out = np.zeros(secs(0.9))
    nw = secs(0.36)
    whoosh = lp(noise(rng, nw, -4.0, 80, 1500), 700) * env(nw, [(0, 0), (0.3, 0.8), (0.36, 1.0)]) ** 2
    rattle = grains(rng, 0.12, 180, 600, 3500, 0.008, shape=[(0, 1), (0.12, 0)])
    put(out, fade(whoosh, 0.05, 0.004), 0.0, 0.12)
    put(out, _door_body(rng, 0.5, 0.3), 0.36, 0.8)
    put(out, burst(rng, 0.1, 0.05, 150, 2500), 0.36, 0.8)
    put(out, _latch(rng), 0.372, 0.75)
    put(out, rattle, 0.39, 0.15)
    return reverb(out, "corridor", 0.45, tail=0.95)


@sound("SFX_Door_Locked", max_dur=1.2, vol=0.8, radius=14,
       desc="Locked door: the handle rattles twice against the bolt")
def sfx_door_locked(rng, i):
    out = np.zeros(secs(0.75))
    for t0, g in ((0.0, 1.0), (0.33, 0.85)):
        put(out, _handle_click(rng), t0, 0.6 * g)                    # handle down
        put(out, _latch(rng), t0 + 0.035, 0.8 * g)                    # bolt against the strike plate
        put(out, _door_body(rng, 0.2, 0.09), t0 + 0.04, 0.45 * g)     # the leaf thuds in its frame
        put(out, _handle_click(rng), t0 + rng.uniform(0.11, 0.14), 0.4 * g)  # handle springs back
    return reverb(out, "corridor", 0.15, tail=0.35)


@sound("SFX_Door_Unlock", max_dur=1.7, vol=0.8, radius=14,
       desc="Key goes in, turns, the bolt withdraws")
def sfx_door_unlock(rng, i):
    out = np.zeros(secs(1.15))
    for k in range(6):  # the key bumps over the pins on its way in
        pin = strike(rng, 0.03, [4200, 6100, 8300], [0.012, 0.01, 0.008], nz=0.5, nz_band=(3000, 10000), nz_t60=0.004)
        put(out, pin, 0.03 + 0.04 * k + rng.uniform(-0.008, 0.008), rng.uniform(0.15, 0.3))
    ns = secs(0.26)
    put(out, noise(rng, ns, 0.0, 3000, 9000) * bell(ns, 0.4), 0.02, 0.02)
    nt = secs(0.16)
    turn = noise(rng, nt, 0.0, 1500, 4500) * bell(nt, 0.6) * (0.4 + 0.6 * wander(rng, nt, 60.0))
    put(out, turn, 0.46, 0.05)
    put(out, _handle_click(rng), 0.6, 0.35)
    bolt = strike(rng, 0.25, [250, 520, 1100, 2100, 3300], [0.14, 0.1, 0.07, 0.05, 0.03], [1, 0.9, 0.6, 0.35, 0.2],
                  nz=0.7, nz_band=(200, 4000), nz_t60=0.02)
    put(out, bolt, 0.74, 1.0)
    put(out, _door_body(rng, 0.2, 0.08), 0.745, 0.3)
    return reverb(out, "corridor", 0.15, tail=0.45)


@sound("SFX_Door_Swing", max_dur=1.7, vol=0.75, radius=14,
       desc="Double swing ward door: two soft flaps on rubber edges")
def sfx_door_swing(rng, i):
    out = np.zeros(secs(1.25))
    for t0, g in ((0.0, 1.0), (0.46, 0.6), (0.82, 0.25)):  # each return swing is weaker
        nw = secs(0.3)
        air = lp(noise(rng, nw, -4.0, 60, 1200), 450) * bell(nw, 0.55, 2.0)
        flap = strike(rng, 0.3, [52, 77, 118], [0.2, 0.14, 0.1], [1, 0.8, 0.4], nz=0.9, nz_band=(60, 450), nz_t60=0.07)
        ns = secs(0.07)
        rub = noise(rng, ns, -3.0, 300, 1800) * bell(ns, 0.3)  # the rubber lips sliding past each other
        put(out, air, t0, 0.3 * g)
        put(out, flap, t0 + 0.17, 0.9 * g)
        put(out, rub, t0 + 0.15, 0.1 * g)
    sq = creak(rng, 0.3, [(0, 90), (0.3, 60)], [1400, 2300], 30.0, shape=[(0, 0), (0.1, 1), (0.3, 0)])
    put(out, sq, 0.02, 0.05)
    return reverb(out, "corridor", 0.25, tail=0.4)


@sound("SFX_Door_Metal", max_dur=3.0, vol=0.9, radius=24,
       desc="Heavy security door: steel groan, then the clunk")
def sfx_door_metal(rng, i):
    out = np.zeros(secs(2.1))
    groan = creak(rng, 1.3, [(0, 34), (0.5, 58), (0.9, 44), (1.3, 26)], [118, 222, 471, 833, 1350, 2100], 35.0,
                  shape=[(0, 0), (0.1, 1), (0.6, 0.7), (1.0, 1.0), (1.3, 0)], irr=0.18)
    nr = secs(1.35)
    rumble = noise(rng, nr, -6.0, 30, 160) * bell(nr, 0.5, 1.0)
    clunk = strike(rng, 0.7, [48, 121, 208, 383, 642, 1150, 1870], [0.6, 0.5, 0.4, 0.3, 0.22, 0.15, 0.1],
                   [0.6, 0.9, 1, 0.8, 0.6, 0.4, 0.25], nz=0.7, nz_band=(80, 3000), nz_t60=0.04)
    put(out, _handle_click(rng), 0.0, 0.3)
    put(out, groan, 0.08, 0.7)
    put(out, rumble, 0.05, 0.12)
    put(out, clunk, 1.42, 1.0)
    put(out, grains(rng, 0.2, 120, 500, 3000, 0.01, shape=[(0, 1), (0.2, 0)]), 1.45, 0.12)
    return reverb(out, "corridor", 0.4, tail=0.85)


# ----------------------------------------------------------------------------
# Interaction
# ----------------------------------------------------------------------------

@sound("SFX_Pickup_Item", max_dur=0.6, vol=0.6, desc="Generic pickup: soft cloth and plastic handling")
def sfx_pickup_item(rng, i):
    out = np.zeros(secs(0.5))
    for t0, d, g, lo, hi in ((0.0, 0.2, 0.7, 700, 4500), (0.14, 0.26, 1.0, 900, 6000)):
        ns = secs(d)
        put(out, noise(rng, ns, -3.0, lo, hi) * bell(ns, 0.35, 1.5) * (0.5 + 0.5 * wander(rng, ns, 35.0)), t0, 0.2 * g)
    put(out, grains(rng, 0.3, 160, 2500, 9000, 0.003, shape=[(0, 0.2), (0.12, 1), (0.3, 0)]), 0.1, 0.35)
    put(out, strike(rng, 0.1, [210, 340], [0.06, 0.04], nz=0.6, nz_band=(100, 900), nz_t60=0.03), 0.29, 0.35)
    return out


@sound("SFX_Pickup_Key", max_dur=1.0, vol=0.65, desc="Key pickup: small metal jingle")
def sfx_pickup_key(rng, i):
    out = np.zeros(secs(0.85))
    t0 = 0.0
    for k in range(8):
        base = rng.uniform(2600, 4200)
        ring = strike(rng, 0.5, [base, base * 1.47, base * 2.09, base * 2.83, base * 3.61],
                      rng.uniform(0.12, 0.4, 5), [1, 0.8, 0.6, 0.4, 0.25], nz=0.3, nz_band=(3000, 12000), nz_t60=0.004)
        put(out, ring, t0, rng.uniform(0.4, 1.0) * (0.9 ** k))
        t0 += rng.uniform(0.02, 0.07)
    return reverb(out, "small", 0.08, tail=0)


@sound("SFX_Pickup_Ammo", max_dur=0.8, vol=0.65, desc="Ammo pickup: cartridges shifting in a cardboard box")
def sfx_pickup_ammo(rng, i):
    out = np.zeros(secs(0.6))
    put(out, strike(rng, 0.15, [180, 290, 470], [0.07, 0.05, 0.04], nz=1.0, nz_band=(120, 1400), nz_t60=0.035), 0.0, 0.8)
    for k in range(11):
        base = rng.uniform(2200, 3800)  # brass on brass, damped by the box
        tink = strike(rng, 0.1, [base, base * 1.62, base * 2.4], rng.uniform(0.02, 0.07, 3), [1, 0.6, 0.35],
                      nz=0.5, nz_band=(1500, 8000), nz_t60=0.006)
        put(out, tink, 0.02 + abs(rng.normal(0.0, 0.13)), rng.uniform(0.15, 0.55))
    put(out, strike(rng, 0.12, [160, 260], [0.06, 0.04], nz=1.0, nz_band=(100, 1000), nz_t60=0.03), 0.33, 0.4)
    return lp(out, 9000)


@sound("SFX_Paper", max_dur=1.1, vol=0.6, desc="Reading a note: paper rustle")
def sfx_paper(rng, i):
    out = np.zeros(secs(0.95))
    for t0, d, g in ((0.0, 0.4, 0.8), (0.33, 0.6, 1.0)):
        ns = secs(d)
        sheet = noise(rng, ns, 1.5, 1200, 10000) * bell(ns, 0.4, 1.5) * (0.15 + 0.85 * wander(rng, ns, 22.0) ** 3)
        put(out, sheet, t0, 0.3 * g)
        put(out, grains(rng, d, 450, 2000, 11000, 0.0025, shape=[(0, 0), (0.4 * d, 1), (d, 0)]), t0, 0.5 * g)
    put(out, strike(rng, 0.08, [320, 610], [0.04, 0.03], nz=1.0, nz_band=(300, 3000), nz_t60=0.015), 0.36, 0.25)
    return out


@sound("SFX_Save", max_dur=1.9, vol=0.75, desc="Saving: an old typewriter clack and a soft tone")
def sfx_save(rng, i):
    out = np.zeros(secs(1.5))
    for t0, g in ((0.0, 0.8), (0.14, 1.0)):
        key = strike(rng, 0.12, [900, 1900, 3300, 5200], [0.07, 0.05, 0.04, 0.03], [1, 0.9, 0.6, 0.4],
                     nz=0.9, nz_band=(800, 8000), nz_t60=0.012)
        put(out, key, t0, 0.8 * g)
        put(out, strike(rng, 0.12, [160, 270], [0.07, 0.05], nz=0.6, nz_band=(80, 700), nz_t60=0.03), t0 + 0.004, 0.6 * g)
    nt = secs(1.2)
    chime = sum(a * tone(f, nt, (1.0, 0.25, 0.08)) for f, a in ((220.0, 1.0), (330.0, 0.6), (440.5, 0.4)))
    chime = chime * decay(nt, 1.3) * (1.0 - np.exp(-tgrid(nt) / 0.02))
    put(out, chime, 0.2, 0.16)
    return reverb(out, "corridor", 0.25, tail=0.35)


@sound("SFX_Medkit", max_dur=2.1, vol=0.7, desc="Using a medkit: plastic case, velcro, an exhale")
def sfx_medkit(rng, i):
    out = np.zeros(secs(1.95))
    for t0, g in ((0.0, 0.9), (0.1, 1.0)):  # the two case latches
        put(out, strike(rng, 0.08, [1100, 2400, 3900], [0.035, 0.03, 0.02], nz=0.8, nz_band=(700, 6000), nz_t60=0.01), t0, 0.6 * g)
    put(out, strike(rng, 0.2, [140, 230, 390], [0.09, 0.07, 0.05], nz=0.8, nz_band=(100, 1200), nz_t60=0.04), 0.24, 0.5)
    velcro = grains(rng, 0.42, 2600, 1500, 9000, 0.0018, shape=[(0, 0.2), (0.1, 1), (0.34, 0.8), (0.42, 0)])
    nv = len(velcro)
    put(out, velcro * env(nv, [(0, 0.3), (0.12, 1), (0.36, 0.9), (0.42, 0)]), 0.42, 0.45)
    nb = secs(0.8)
    breath = tract(noise(rng, nb, -3.0, 250, 8000), [[560, 1450, 2500], [460, 1250, 2400]],
                   bws=(260, 320, 420), amps=(1, 0.6, 0.35)) * bell(nb, 0.22, 1.2)
    put(out, nrm(breath), 1.05, 0.33)
    return out


def _switch(rng, pitch, second_gain):
    out = np.zeros(secs(0.14))
    press = strike(rng, 0.06, [2400 * pitch, 4100 * pitch, 6500 * pitch], [0.02, 0.015, 0.012], [1, 0.8, 0.5],
                   nz=0.7, nz_band=(1200, 10000), nz_t60=0.004)
    tube = strike(rng, 0.08, [880 * pitch, 1630 * pitch], [0.04, 0.03], nz=0)  # the aluminium body answers
    put(out, press, 0.0, 1.0)
    put(out, tube, 0.0005, 0.3)
    put(out, strike(rng, 0.05, [2900 * pitch, 5200 * pitch], [0.015, 0.01], nz=0.6,
                    nz_band=(1500, 9000), nz_t60=0.004), 0.048, second_gain)  # spring return
    return out


@sound("SFX_Flashlight_On", max_dur=0.2, vol=0.6, desc="Flashlight switch on: press and spring return")
def sfx_flashlight_on(rng, i):
    return _switch(rng, 1.0, 0.55)


@sound("SFX_Flashlight_Off", max_dur=0.2, vol=0.6, desc="Flashlight switch off: lower, softer return")
def sfx_flashlight_off(rng, i):
    return _switch(rng, 0.8, 0.3)


@sound("SFX_Fuse_Insert", max_dur=1.0, vol=0.8, radius=10,
       desc="Ceramic fuse pushed home into its clips, contact spark")
def sfx_fuse_insert(rng, i):
    out = np.zeros(secs(0.75))
    ns = secs(0.16)
    scrape = noise(rng, ns, 0.0, 2000, 7000) * env(ns, [(0, 0), (0.05, 0.6), (0.15, 1), (0.16, 0)]) * (0.4 + 0.6 * wander(rng, ns, 70.0))
    seat = strike(rng, 0.15, [350, 1300, 2900, 4700], [0.07, 0.05, 0.04, 0.03], [1, 0.8, 0.6, 0.3],
                  nz=0.8, nz_band=(300, 6000), nz_t60=0.012)
    na = secs(0.2)
    crackle = grains(rng, 0.2, 700, 2000, 12000, 0.0015, shape=[(0, 1), (0.05, 0.6), (0.2, 0)])
    arc = tone(100.0, na, saw(40, 0.8)) * noise(rng, na, 0.0, 200, 6000) * decay(na, 0.12)  # mains-rate buzz in the arc
    put(out, scrape, 0.0, 0.08)
    put(out, seat, 0.16, 0.9)
    put(out, crackle, 0.2, 0.5)
    put(out, nrm(arc), 0.2, 0.3)
    return reverb(out, "small", 0.15, tail=0.25)


def _mains_hum(rng, n, harmonics=14, f1=100.0, roll=1.1):
    """Ballast hum: the 100 Hz magnetostriction series over a little 50 Hz."""
    t = tgrid(n)
    x = 0.15 * np.sin(TWO_PI * qf(50.0, n) * t)
    for k in range(1, harmonics + 1):
        x += rng.uniform(0.6, 1.0) / k ** roll * np.sin(TWO_PI * qf(f1 * k, n) * t + rng.uniform(0, TWO_PI))
    return x


def _tube_buzz(rng, n):
    """The discharge re-striking every half cycle: hiss gated at 100 Hz."""
    return noise(rng, n, 0.0, 2000, 7000) * np.abs(np.sin(TWO_PI * qf(50.0, n) * tgrid(n))) ** 6


def _tink(rng):
    return strike(rng, 0.2, [3100, 4300, 5900, 7400], [0.15, 0.1, 0.07, 0.04], [1, 0.7, 0.4, 0.2],
                  nz=0.5, nz_band=(2500, 10000), nz_t60=0.004, detune=0.08)


@sound("SFX_Power_On", max_dur=3.6, vol=0.9, radius=30,
       desc="Power restored: breaker thrown, relays, tubes striking into a rising hum")
def sfx_power_on(rng, i):
    n = secs(3.2)
    out = np.zeros(n)
    breaker = strike(rng, 0.4, [180, 420, 950, 2200, 3600], [0.25, 0.18, 0.1, 0.06, 0.04], [1, 0.9, 0.7, 0.4, 0.2],
                     nz=0.9, nz_band=(150, 5000), nz_t60=0.03)
    put(out, breaker, 0.0, 1.0)
    put(out, _door_body(rng, 0.3, 0.15), 0.003, 0.5)
    for t0 in (0.27, 0.41, 0.5, 0.74):
        relay = strike(rng, 0.06, [1300, 2700, 4400], [0.03, 0.02, 0.015], nz=0.7, nz_band=(1000, 7000), nz_t60=0.006)
        put(out, relay, t0 + rng.uniform(-0.02, 0.02), rng.uniform(0.25, 0.45))
    gate = np.zeros(n)  # tubes catch in fits before they hold
    for t0, d in ((0.72, 0.05), (0.93, 0.08), (1.1, 0.04), (1.32, 0.16), (1.62, 0.1)):
        gate[secs(t0):secs(t0 + d)] = rng.uniform(0.5, 1.0)
        put(out, _tink(rng), t0, rng.uniform(0.12, 0.25))
    gate[secs(1.85):] = 1.0
    gate = np.clip(lp(gate, 150.0), 0.0, 1.2)
    rise = env(n, [(0, 0), (0.7, 0.25), (1.85, 0.6), (2.6, 1.0), (3.0, 1.0), (3.2, 0.0)])
    hum = _mains_hum(rng, n) * 0.2 + _tube_buzz(rng, n) * 0.035
    out += hum * gate * rise
    return reverb(out, "corridor", 0.25, tail=0.35)


# ----------------------------------------------------------------------------
# Weapons
# ----------------------------------------------------------------------------

@sound("SFX_Pistol_Fire", max_dur=1.5, vol=1.0, radius=45, centroid=(500, 5000),
       desc="9 mm pistol indoors: crack, body, tiled room tail")
def sfx_pistol_fire(rng, i):
    n = secs(0.36)
    t = tgrid(n)
    dry = np.zeros(n)
    crack = burst(rng, 0.09, 0.04, 600, 14000, attack=0.0001)
    body = np.sin(TWO_PI * np.cumsum(85.0 + 260.0 * np.exp(-t / 0.025)) / SR) * decay(n, 0.16)
    boom = burst(rng, 0.3, 0.12, 140, 2200)
    put(dry, crack, 0.0, 1.0)
    dry += 0.3 * body
    put(dry, boom, 0.0, 0.4)
    dry = drive(dry, 2.5)  # the muzzle blast overloads the ear, flatten the top of it
    for t0, g in ((0.018, 0.16), (0.07, 0.12)):  # slide back, slide home
        put(dry, strike(rng, 0.08, [2300, 3900, 5600], [0.03, 0.025, 0.02], nz=0.5, nz_band=(2000, 8000), nz_t60=0.01), t0, g)
    return reverb(dry, "tiled", 0.85, tail=1.05)


def _mag_clack(rng):
    return strike(rng, 0.15, [300, 800, 1700, 3400, 5100], [0.07, 0.06, 0.05, 0.03, 0.02], [1, 0.9, 0.7, 0.45, 0.25],
                  nz=0.8, nz_band=(400, 7000), nz_t60=0.012)


@sound("SFX_Pistol_Reload", max_dur=1.9, vol=0.8, radius=10,
       desc="Pistol reload: magazine out, magazine in, slide racked")
def sfx_pistol_reload(rng, i):
    out = np.zeros(secs(1.7))

    def scrape(d, lo, hi, peak):
        ns = secs(d)
        return noise(rng, ns, 0.0, lo, hi) * bell(ns, peak, 1.5) * (0.5 + 0.5 * wander(rng, ns, 80.0))
    put(out, _handle_click(rng), 0.0, 0.5)             # magazine release
    put(out, scrape(0.2, 1000, 3500, 0.3), 0.03, 0.06)  # magazine slides out
    put(out, scrape(0.14, 1200, 4000, 0.7), 0.62, 0.07)  # fresh one goes in
    put(out, _mag_clack(rng), 0.76, 0.9)
    put(out, scrape(0.09, 1500, 6000, 0.6), 1.12, 0.1)  # slide pulled back
    put(out, _handle_click(rng), 1.2, 0.5)
    home = strike(rng, 0.25, [520, 1250, 2100, 3700, 5900], [0.08, 0.08, 0.12, 0.1, 0.05], [1, 0.9, 0.8, 0.6, 0.3],
                  nz=0.9, nz_band=(500, 9000), nz_t60=0.012)
    put(out, home, 1.34, 1.0)                           # and let go: the spring rings
    return reverb(out, "small", 0.1, tail=0.15)


@sound("SFX_Pistol_Empty", max_dur=0.25, vol=0.7, radius=6, desc="Pistol dry fire: the hammer falls on nothing")
def sfx_pistol_empty(rng, i):
    out = np.zeros(secs(0.18))
    put(out, strike(rng, 0.1, [1500, 3200, 5500, 7800], [0.035, 0.03, 0.02, 0.015], [1, 0.8, 0.5, 0.3],
                    nz=0.7, nz_band=(1500, 9000), nz_t60=0.005), 0.0, 1.0)
    put(out, strike(rng, 0.1, [240, 610], [0.04, 0.03], nz=0.4, nz_band=(150, 1200), nz_t60=0.015), 0.001, 0.4)
    put(out, strike(rng, 0.05, [3900, 6400], [0.012, 0.01], nz=0.5, nz_band=(2500, 9000), nz_t60=0.003), 0.055, 0.2)
    return out


@sound("SFX_Pipe_Swing", max_dur=0.5, vol=0.7, radius=8, desc="Pipe swing: whoosh")
def sfx_pipe_swing(rng, i):
    n = secs(0.42)
    ts = np.linspace(0, 0.42, 22)
    arc = np.sin(np.pi * ts / 0.42) ** 1.5  # the tip is fastest mid-swing: pitch and level follow it
    air = sweep_bp(noise(rng, n, -2.0, 120, 9000), ts, 350.0 + 1500.0 * arc, 2.2)
    whistle = sweep_bp(noise(rng, n, 0.0, 400, 5000), ts, 700.0 + 600.0 * arc, 14.0)  # the open end of the pipe
    e = np.interp(tgrid(n), ts, arc) ** 1.3
    return (nrm(air) + 0.35 * nrm(whistle)) * e


@sound("SFX_Pipe_Hit_Flesh", max_dur=0.6, vol=0.9, radius=14, desc="Pipe hits a body: dull wet impact")
def sfx_pipe_hit_flesh(rng, i):
    n = secs(0.45)
    t = tgrid(n)
    out = 0.9 * np.sin(TWO_PI * np.cumsum(48.0 + 60.0 * np.exp(-t / 0.035)) / SR) * decay(n, 0.34)
    put(out, burst(rng, 0.15, 0.09, 80, 700, attack=0.001), 0.0, 0.9)
    put(out, burst(rng, 0.05, 0.03, 500, 3000), 0.0, 0.3)
    put(out, grains(rng, 0.12, 800, 900, 4500, 0.007, shape=[(0, 1), (0.12, 0)]), 0.008, 0.28)
    put(out, modal(secs(0.1), [520, 1380], [0.05, 0.04], [1, 0.5]), 0.0, 0.08)  # the pipe, choked by the hand and the hit
    return drive(out, 1.6)


@sound("SFX_Pipe_Hit_Wall", max_dur=1.5, vol=0.9, radius=22, desc="Pipe hits plaster: metal ring and crumbs")
def sfx_pipe_hit_wall(rng, i):
    out = np.zeros(secs(0.95))
    f1 = 312.0  # free bar partials: 1, 2.76, 5.40, 8.93, 13.34
    ring = strike(rng, 0.9, [f1, f1 * 2.76, f1 * 5.40, f1 * 8.93, f1 * 13.34], [0.8, 0.65, 0.45, 0.3, 0.2],
                  [0.7, 1, 0.8, 0.5, 0.3], nz=0.6, nz_band=(800, 9000), nz_t60=0.008, detune=0.01)
    put(out, ring, 0.0, 0.8)
    put(out, burst(rng, 0.12, 0.06, 150, 3000), 0.0, 0.8)   # plaster giving way
    put(out, strike(rng, 0.2, [110, 190], [0.1, 0.07], nz=0), 0.0, 0.6)
    put(out, grains(rng, 0.5, 90, 1500, 8000, 0.004, shape=[(0, 1), (0.15, 0.5), (0.5, 0)]), 0.05, 0.14)  # crumbs
    return reverb(out, "corridor", 0.3, tail=0.5)


# ----------------------------------------------------------------------------
# Player voice
# ----------------------------------------------------------------------------

@sound("SFX_Player_Hurt", variants=3, max_dur=0.6, vol=0.85, radius=12,
       desc="Player hurt: short breathy grunt")
def sfx_player_hurt(rng, i):
    dur = (0.34, 0.4, 0.3)[i]
    f_hi, f_lo = ((152, 104), (136, 96), (165, 118))[i]
    vow = ([[640, 1190, 2450, 3400], [480, 1020, 2400, 3300]],   # "uh"
           [[720, 1150, 2550, 3450], [520, 980, 2350, 3300]],    # "ah" closing
           [[560, 1350, 2500, 3400], [430, 1100, 2350, 3300]])[i]  # "eh", clipped
    v = voice(rng, dur, [(0, f_hi * 0.92), (0.04, f_hi), (dur, f_lo)], vow,
              bws=(110, 130, 180, 240), amps=(1, 0.55, 0.3, 0.15),
              voiced=[(0, 0), (0.012, 1), (0.35 * dur, 0.8), (0.75 * dur, 0.15), (dur, 0)],
              breath=[(0, 0.9), (0.03, 0.5), (0.5 * dur, 0.8), (dur, 0)],
              jitter=0.03, shimmer=0.12, rough=0.2, oq=0.6, dark=3200)
    return reverb(nrm(v), "small", 0.06, tail=0.12)


@sound("SFX_Player_Death", max_dur=2.4, vol=0.9, radius=14, desc="Player death: a falling groan that runs out of air")
def sfx_player_death(rng, i):
    dur = 1.9
    v = voice(rng, dur, [(0, 148), (0.1, 138), (0.8, 104), (1.5, 78), (dur, 66)],
              [[700, 1180, 2500, 3400], [600, 1080, 2450, 3350], [470, 980, 2350, 3300], [400, 900, 2300, 3250]],
              bws=(110, 130, 180, 240), amps=(1, 0.55, 0.3, 0.15),
              voiced=[(0, 0), (0.03, 1), (0.6, 0.8), (1.2, 0.3), (1.55, 0)],
              breath=[(0, 0.6), (0.1, 0.35), (1.0, 0.7), (1.5, 0.8), (dur, 0)],
              jitter=0.04, shimmer=0.15, rough=0.3, oq=0.6, dark=3000)
    v = v * (1.0 + 0.12 * lfo_noise(rng, len(v), 7.0))  # the breath shakes
    return reverb(nrm(v), "corridor", 0.15, tail=0.4)


@sound("SFX_Heartbeat", kind="loop", max_dur=3.5, vol=0.7, peak=-5.0, centroid=(20, 400),
       desc="Low health heartbeat, 70 bpm, non-positional loop (4 beats)")
def sfx_heartbeat(rng, i):
    n = secs(4 * 60.0 / 70.0)
    out = np.zeros(n)
    for b in range(4):
        t0 = b * n / 4.0 / SR
        for dt, f_hi, f_lo, t60, g in ((0.02, 88.0, 50.0, 0.2, 1.0), (0.31, 104.0, 58.0, 0.15, 0.62)):  # lub, dub
            m = secs(0.3)
            t = tgrid(m)
            thump = np.sin(TWO_PI * np.cumsum(f_lo + (f_hi - f_lo) * np.exp(-t / 0.04)) / SR) * decay(m, t60)
            thump = thump * (1.0 - np.exp(-t / 0.004))
            thump[:secs(0.1)] += 0.35 * burst(rng, 0.1, 0.05, 40, 260, attack=0.004)
            put_wrap(out, thump, t0 + dt, g * rng.uniform(0.9, 1.0))
    return lp(out, 300.0, 4)


@sound("SFX_Breath_Tired", kind="loop", max_dur=4.9, vol=0.6, peak=-6.0,
       desc="Out of stamina: fast open-mouth breathing, non-positional loop (3 breaths)")
def sfx_breath_tired(rng, i):
    n = secs(4.8)
    out = np.zeros(n)
    for c in range(3):
        t0 = 0.05 + c * 1.6 + rng.uniform(-0.03, 0.03)
        ni = secs(rng.uniform(0.5, 0.58))
        inh = tract(noise(rng, ni, -2.0, 400, 9000), [[760, 1750, 2750], [840, 1850, 2850]],
                    bws=(300, 380, 480), amps=(0.8, 1, 0.6), dark=6000) * bell(ni, 0.6, 1.3)
        ne = secs(rng.uniform(0.66, 0.76))
        src = noise(rng, ne, -3.0, 250, 8000) + 0.5 * glottal(rng, np.full(ne, rng.uniform(105, 120)), 0.04, 0.2) * env(ne, [(0, 0.6), (0.25, 0), (1, 0)])
        exh = tract(src, [[620, 1350, 2500], [480, 1200, 2400]], bws=(260, 320, 420), amps=(1, 0.6, 0.35), dark=4000) * bell(ne, 0.22, 1.2)
        g = rng.uniform(0.85, 1.0)
        put(out, nrm(inh), t0, 0.55 * g)
        put(out, nrm(exh), t0 + ni / SR + 0.09, 1.0 * g)
    return out


# ----------------------------------------------------------------------------
# Enemy voices. Patient: thin, tense, high formants. Orderly: big, low, masked.
# ----------------------------------------------------------------------------

P_BW, P_AMP = (90, 120, 170, 240), (1.0, 0.9, 0.6, 0.35)


def _throat_clicks(rng, dur, rate, shape, forms=(850, 1750, 2900, 4100)):
    n = secs(dur)
    p = pulses(rng, n, rate, irr=0.45, amp_var=0.4) * P(n, shape)
    return nrm(formant_static(p, forms))


@sound("SFX_Patient_Idle", variants=3, max_dur=2.0, vol=0.7, radius=14,
       desc="Patient idle: wheeze and throat clicks")
def sfx_patient_idle(rng, i):
    dur = (1.3, 1.1, 1.4)[i]
    n = secs(dur)
    out = np.zeros(n)
    ts = np.linspace(0, dur, 5)
    drift = rng.uniform(-150, 150, (5, 1))
    wheeze = tract(noise(rng, n, -3.0, 500, 9000), np.array([[1500, 2600, 3600]] * 5) + drift, ts,
                   bws=(70, 100, 160), amps=(1, 0.8, 0.4), dark=6000)
    whistle = sweep_bp(noise(rng, n, 0.0, 1000, 6000), ts, 2200.0 + rng.uniform(-400, 400, 5), 55.0)  # a narrowed airway sings
    air = nrm(wheeze) + 0.6 * nrm(whistle)
    if i == 0:    # wheeze in, then a rattle of clicks
        out += air * env(n, [(0, 0), (0.25, 1), (0.6, 0.5), (0.75, 0), (dur, 0)])
        put(out, _throat_clicks(rng, 0.5, [(0, 11), (0.3, 24), (0.5, 8)], [(0, 0), (0.05, 1), (0.4, 1), (0.5, 0)]), 0.72, 0.8)
    elif i == 1:  # clicks first, a short thin exhale after
        put(out, _throat_clicks(rng, 0.45, [(0, 7), (0.2, 19), (0.45, 13)], [(0, 1), (0.4, 1), (0.45, 0)]), 0.0, 0.8)
        out += air * env(n, [(0, 0), (0.42, 0), (0.6, 1), (dur, 0)])
    else:         # a wheeze that stutters, clicks under it
        gate = np.clip(lp((pulses(rng, n, 9.0, 0.5) > 0).astype(float), 30.0) * 60.0, 0.15, 1.0)
        out += air * bell(n, 0.4, 1.2) * gate
        put(out, _throat_clicks(rng, dur, [(0, 5), (dur, 12)], [(0, 0.5), (dur * 0.8, 1), (dur, 0)]), 0.0, 0.45)
    return reverb(nrm(out), "corridor", 0.14, tail=0.5)


def _rasp(rng, n, rate, shape, lo=2200, hi=9000):
    """Noise chopped at an irregular fry rate: the hiss that rides on a torn voice."""
    chop = np.abs(lp(pulses(rng, n, rate, 0.3), 180.0))
    return nrm(noise(rng, n, 0.0, lo, hi) * (0.25 + nrm(chop)) * P(n, shape))


@sound("SFX_Patient_Alert", max_dur=1.8, vol=0.85, radius=22, desc="Patient notices the player: rising rasp")
def sfx_patient_alert(rng, i):
    dur = 1.2
    v = voice(rng, dur, [(0, 64), (0.45, 118), (0.85, 390), (dur, 580)],
              [[700, 1600, 2800, 3900], [850, 1800, 2950, 3950], [1050, 2050, 3150, 4050]],
              bws=P_BW, amps=P_AMP, voiced=[(0, 0), (0.06, 0.5), (1.08, 1), (dur, 0)],
              breath=[(0, 0.3), (dur, 1.0)], jitter=0.07, shimmer=0.25, rough=0.7, oq=0.42, dark=6500)
    n = len(v)
    out = drive(nrm(v), 3.0) + 0.3 * _rasp(rng, n, [(0, 30), (dur, 95)], [(0, 0), (0.3, 0.3), (1.08, 1), (dur, 0)])
    return reverb(nrm(out), "corridor", 0.2, tail=0.55)


@sound("SFX_Patient_Attack", variants=2, max_dur=1.4, vol=0.95, radius=24, desc="Patient attacks: shriek-rasp")
def sfx_patient_attack(rng, i):
    dur = (0.75, 0.82)[i]
    top = (735.0, 660.0)[i]
    if i == 0:
        f0 = [(0, 380), (0.07, top), (0.45, top * 0.87), (dur, 410)]
        voiced = [(0, 0), (0.015, 1), (0.55, 0.9), (dur, 0)]
    else:  # a bark, a catch, then the shriek
        f0 = [(0, 420), (0.1, 520), (0.17, 300), (0.26, top), (0.6, top * 0.9), (dur, 380)]
        voiced = [(0, 0), (0.012, 1), (0.13, 0.9), (0.18, 0.1), (0.24, 1), (0.62, 0.9), (dur, 0)]
    v = voice(rng, dur, f0, [[900, 1900, 3000, 4000], [1150, 2200, 3300, 4200], [950, 1900, 3000, 4000]],
              bws=P_BW, amps=P_AMP, voiced=voiced, breath=0.6, jitter=0.08, shimmer=0.25, rough=0.55, oq=0.5, dark=7000)
    n = len(v)
    out = drive(nrm(v), 4.0) + 0.35 * _rasp(rng, n, 70.0, voiced)
    return reverb(nrm(out), "corridor", 0.2, tail=0.5)


@sound("SFX_Patient_Hurt", max_dur=1.0, vol=0.85, radius=20, desc="Patient hit: a torn yelp")
def sfx_patient_hurt(rng, i):
    dur = 0.45
    voiced = [(0, 0), (0.01, 1), (0.25, 0.7), (dur, 0)]
    v = voice(rng, dur, [(0, 820), (0.1, 610), (dur, 300)], [[1100, 2150, 3250, 4150], [850, 1750, 2900, 3950]],
              bws=P_BW, amps=P_AMP, voiced=voiced, breath=0.7, jitter=0.09, shimmer=0.3, rough=0.6, oq=0.5, dark=7000)
    out = drive(nrm(v), 3.5) + 0.3 * _rasp(rng, len(v), 60.0, voiced)
    return reverb(nrm(out), "corridor", 0.18, tail=0.4)


@sound("SFX_Patient_Death", max_dur=2.5, vol=0.9, radius=22, desc="Patient dies: the rasp falls apart into clicks and air")
def sfx_patient_death(rng, i):
    dur = 1.9
    n = secs(dur)
    v = voice(rng, dur, [(0, 560), (0.3, 420), (0.9, 200), (1.3, 90), (dur, 45)],
              [[1100, 2100, 3200, 4100], [950, 1900, 3000, 4000], [800, 1700, 2850, 3900], [700, 1550, 2750, 3850]],
              bws=P_BW, amps=P_AMP, voiced=[(0, 0), (0.015, 1), (0.8, 0.7), (1.5, 0)],
              breath=[(0, 0.5), (1.0, 0.8), (1.6, 0.9), (dur, 0)], jitter=0.1, shimmer=0.3, rough=0.7, oq=0.45, dark=6000)
    out = drive(nrm(v), 3.0)[:n]
    out += 0.25 * _rasp(rng, n, [(0, 80), (dur, 20)], [(0, 0), (0.02, 1), (1.2, 0.4), (dur, 0)])
    put(out, _throat_clicks(rng, 0.8, [(0, 15), (0.8, 5)], [(0, 0), (0.1, 1), (0.6, 0.8), (0.8, 0)]), 1.05, 0.5)
    return reverb(nrm(out), "corridor", 0.2, tail=0.5)


O_BW, O_AMP = (100, 130, 190, 260), (1.0, 0.7, 0.35, 0.2)


def _valve_flutter(rng, n, rate=24.0, depth=0.5):
    return 1.0 - depth * (0.5 + 0.5 * np.sin(TWO_PI * rate * tgrid(n) + 2.0 * lfo_noise(rng, n, 6.0)))


@sound("SFX_Orderly_Idle", variants=3, max_dur=2.9, vol=0.75, radius=16,
       desc="Orderly idle: low laboured breathing through a mask")
def sfx_orderly_idle(rng, i):
    t_in, t_ex = ((0.75, 1.0), (0.95, 0.9), (0.65, 1.15))[i]
    out = np.zeros(secs(t_in + t_ex + 0.25))
    ni, ne = secs(t_in), secs(t_ex)
    inh = tract(noise(rng, ni, -3.0, 150, 6000), [[420, 950, 2000], [470, 1000, 2050]],
                bws=(170, 220, 320), amps=(1, 0.7, 0.3), dark=2500) * bell(ni, 0.55, 1.2)
    if i == 1:  # phlegm on the way in
        inh = inh * (0.5 + 0.5 * np.clip(lp(pulses(rng, ni, 30.0, 0.4), 120.0) * 80.0, 0, 1))
    src = noise(rng, ne, -3.0, 120, 5000)
    if i != 1:  # a low voiced rumble under the exhale
        src = src + 1.6 * glottal(rng, P(ne, [(0, 54), (t_ex, 44)]), 0.05, 0.2, rough=0.5, oq=0.5) * bell(ne, 0.3, 1.0)
    exh = tract(src, [[340, 800, 1800], [300, 740, 1750]], bws=(150, 200, 300), amps=(1, 0.6, 0.25), dark=2200)
    exh = exh * bell(ne, 0.2, 1.1) * _valve_flutter(rng, ne)
    valve = strike(rng, 0.05, [620, 1180], [0.03, 0.02], nz=0.5, nz_band=(400, 2000), nz_t60=0.008)
    put(out, nrm(inh), 0.0, 0.6)
    put(out, valve, t_in + 0.06, 0.25)  # the exhale valve pops open
    put(out, nrm(exh), t_in + 0.08, 1.0)
    return reverb(mask(out), "corridor", 0.12, tail=0.4)


@sound("SFX_Orderly_Alert", max_dur=2.2, vol=0.9, radius=24, desc="Orderly notices the player: low growl")
def sfx_orderly_alert(rng, i):
    dur = 1.5
    v = voice(rng, dur, [(0, 58), (0.5, 72), (1.1, 80), (dur, 62)],
              [[420, 850, 2000, 2800], [520, 950, 2100, 2800], [400, 800, 1900, 2700]],
              bws=O_BW, amps=O_AMP, voiced=[(0, 0), (0.15, 0.8), (1.2, 1), (dur, 0)],
              breath=[(0, 0.4), (1.2, 0.5), (dur, 0.2)], jitter=0.05, shimmer=0.2, rough=0.6, oq=0.5, dark=2600)
    v = drive(nrm(v) * _valve_flutter(rng, len(v), 23.0, 0.35), 2.5)
    return reverb(mask(v), "corridor", 0.18, tail=0.55)


@sound("SFX_Orderly_Attack", variants=2, max_dur=1.4, vol=0.95, radius=24, desc="Orderly attacks: grunt with effort")
def sfx_orderly_attack(rng, i):
    if i == 0:
        dur = 0.7
        f0 = [(0, 82), (0.07, 118), (0.35, 100), (dur, 70)]
        voiced = [(0, 0), (0.02, 1), (0.4, 0.8), (dur, 0)]
        breath = [(0, 0.9), (0.1, 0.3), (0.5, 0.5), (dur, 0)]
    else:  # wind-up, then the blow
        dur = 0.8
        f0 = [(0, 74), (0.16, 84), (0.24, 70), (0.32, 128), (0.55, 104), (dur, 68)]
        voiced = [(0, 0), (0.03, 0.6), (0.17, 0.5), (0.24, 0.1), (0.3, 1), (0.55, 0.8), (dur, 0)]
        breath = [(0, 0.5), (0.2, 0.7), (0.3, 0.3), (0.6, 0.5), (dur, 0)]
    v = voice(rng, dur, f0, [[520, 1000, 2100, 2900], [600, 1050, 2200, 2900], [420, 850, 1900, 2700]],
              bws=O_BW, amps=O_AMP, voiced=voiced, breath=breath, jitter=0.05, shimmer=0.2, rough=0.45, oq=0.55, dark=2800)
    return reverb(mask(drive(nrm(v), 3.0)), "corridor", 0.18, tail=0.5)


@sound("SFX_Orderly_Hurt", max_dur=1.1, vol=0.9, radius=20, desc="Orderly hit: an abrupt low grunt")
def sfx_orderly_hurt(rng, i):
    dur = 0.55
    v = voice(rng, dur, [(0, 125), (0.08, 105), (dur, 72)], [[560, 1000, 2100, 2900], [400, 820, 1900, 2700]],
              bws=O_BW, amps=O_AMP, voiced=[(0, 0), (0.012, 1), (0.25, 0.6), (dur, 0)],
              breath=[(0, 1.0), (0.06, 0.4), (0.4, 0.6), (dur, 0)], jitter=0.05, shimmer=0.2, rough=0.4, oq=0.55, dark=2800)
    return reverb(mask(drive(nrm(v), 2.5)), "corridor", 0.15, tail=0.4)


@sound("SFX_Orderly_Death", max_dur=3.0, vol=0.95, radius=24, desc="Orderly dies: a groan that sinks into a long masked exhale")
def sfx_orderly_death(rng, i):
    dur = 2.3
    n = secs(dur)
    v = voice(rng, dur, [(0, 92), (0.4, 80), (1.2, 55), (1.9, 34), (dur, 30)],
              [[560, 1000, 2100, 2900], [480, 900, 2000, 2800], [380, 800, 1850, 2700], [320, 740, 1750, 2650]],
              bws=O_BW, amps=O_AMP, voiced=[(0, 0), (0.03, 1), (1.0, 0.7), (1.75, 0)],
              breath=[(0, 0.4), (1.0, 0.6), (1.5, 1.0), (dur, 0)], jitter=0.06, shimmer=0.25, rough=0.6, oq=0.5, dark=2600)
    flutter = _valve_flutter(rng, len(v), 22.0, 0.4) * (1.0 + 0.1 * lfo_noise(rng, len(v), 5.0))
    out = drive(nrm(v) * flutter, 2.5)
    return reverb(mask(out)[:n + secs(0.05)], "corridor", 0.18, tail=0.5)


# ----------------------------------------------------------------------------
# Positional ambience loops. Where every layer is periodic in the loop length
# (FFT-built noise, whole numbers of cycles, wrapped events, circular reverb)
# the loop closes by construction; the swept ones are crossfaded instead.
# ----------------------------------------------------------------------------

@sound("AMB_Fluorescent_Hum", kind="loop", max_dur=5.1, vol=0.35, radius=6, centroid=(80, 900),
       desc="Fluorescent tube: mains hum with harmonics and a faint buzz")
def amb_fluorescent_hum(rng, i):
    n = secs(5.0)
    t = tgrid(n)
    beat = 1.0 + 0.06 * np.sin(TWO_PI * 0.4 * t) + 0.04 * np.sin(TWO_PI * 1.2 * t + 1.0)  # 2 and 6 cycles per loop
    return _mains_hum(rng, n) * beat + 0.05 * _tube_buzz(rng, n)


@sound("AMB_Fluorescent_Flicker", kind="loop", max_dur=8.1, vol=0.4, radius=7, peak=-7.0, centroid=(80, 1800),
       desc="Failing tube: hum with irregular ticks and dropouts (pair with a flickering light)")
def amb_fluorescent_flicker(rng, i):
    n = secs(8.0)
    gate = np.ones(n)
    ticks = np.zeros(n)
    t0 = rng.uniform(0.3, 0.8)
    while t0 < 7.6:
        for _ in range(rng.choice([1, 1, 2, 4])):  # single dips and stutters
            d = rng.uniform(0.03, 0.22)
            idx = (secs(t0) + np.arange(secs(d))) % n
            gate[idx] = rng.uniform(0.05, 0.35)
            put_wrap(ticks, _tink(rng), t0, rng.uniform(0.15, 0.3))         # goes out
            put_wrap(ticks, _tink(rng), t0 + d, rng.uniform(0.4, 1.0))      # re-strikes
            t0 += d + rng.uniform(0.03, 0.09)
        t0 += rng.uniform(0.5, 1.7)
    gate = lp(gate, 200.0)
    hum = _mains_hum(rng, n) + 0.07 * _tube_buzz(rng, n)
    return hum * gate + 0.9 * ticks


@sound("AMB_Ventilation", kind="loop", max_dur=6.1, vol=0.45, radius=10, centroid=(90, 1500),
       desc="Air duct: rumble, hiss and a faint fan tone")
def amb_ventilation(rng, i):
    n = secs(6.0)
    t = tgrid(n)
    x = noise(rng, n, -5.0, 38, 260) + 0.28 * noise(rng, n, -4.0, 250, 3500)
    x = filt(x, [("peak", 88, 7.0, 8.0), ("peak", 176, 8.0, 6.0), ("peak", 265, 8.0, 4.0)])  # the duct's own modes
    fan = sum(a * np.sin(TWO_PI * qf(f, n) * t + rng.uniform(0, TWO_PI)) for f, a in ((118.0, 1.0), (236.0, 0.5), (354.0, 0.25)))
    x = x + 0.08 * fan * (1.0 + 0.3 * lfo_noise(rng, n, 3.0))
    return x * (1.0 + 0.15 * lfo_noise(rng, n, 0.5))


def _drop(rng):
    n = secs(0.3)
    t = tgrid(n)
    out = np.zeros(n)
    f0 = rng.uniform(700, 1400)
    for dt, fm, g in ((0.0, 1.0, 1.0), (rng.uniform(0.04, 0.075), rng.uniform(1.3, 1.9), 0.35)):
        tau = rng.uniform(0.012, 0.03)
        f = f0 * fm * (1.0 + 0.9 * (1.0 - np.exp(-t / 0.02)))  # the trapped bubble shrinks: pitch climbs
        put(out, np.sin(TWO_PI * np.cumsum(f) / SR) * np.exp(-t / tau) * (1.0 - np.exp(-t / 0.0008)), dt, g)
    put(out, burst(rng, 0.006, 0.003, 2500, 9000), 0.0, 0.3)
    put(out, modal(secs(0.08), [rng.uniform(170, 260)], [0.06]), 0.0, 0.3)
    return out


@sound("AMB_Drip", kind="loop", max_dur=12.1, vol=0.5, radius=8, peak=-6.0,
       desc="Water dripping into a puddle, small tiled room")
def amb_drip(rng, i):
    n = secs(12.0)
    out = np.zeros(n)
    gaps = rng.uniform(0.5, 1.9, 10)
    gaps[rng.integers(0, 10)] *= 0.3  # one quick double drop
    times = np.cumsum(gaps) * 12.0 / gaps.sum()
    for t0 in times:
        put_wrap(out, _drop(rng), t0, rng.uniform(0.5, 1.0))
    return reverb_loop(out, "wetroom", 0.9)


@sound("AMB_Fridge", kind="loop", max_dur=6.1, vol=0.4, radius=6, centroid=(90, 700),
       desc="Fridge compressor hum (morgue, pharmacy)")
def amb_fridge(rng, i):
    n = secs(6.0)
    t = tgrid(n)
    x = np.zeros(n)
    for f, a in ((50.0, 0.6), (48.5, 0.3), (100.0, 1.0), (97.0, 0.3), (150.0, 0.4), (200.0, 0.35), (250.0, 0.12), (300.0, 0.2), (400.0, 0.08)):
        x += a * np.sin(TWO_PI * qf(f, n) * t + rng.uniform(0, TWO_PI))  # 48.5 against 50, 97 against 100: the slow motor-slip wobble
    rattle = noise(rng, n, -3.0, 200, 900) * (0.5 + 0.5 * np.sin(TWO_PI * 25.0 * t)) ** 3
    x += 0.1 * rattle + 0.3 * noise(rng, n, -6.0, 30, 120) + 0.006 * noise(rng, n, 0.0, 3000, 8000)
    return filt(x, [("peak", 240, 5.0, 5.0)]) * (1.0 + 0.05 * lfo_noise(rng, n, 0.7))


@sound("AMB_Generator", kind="loop", max_dur=6.1, vol=0.6, radius=14, peak=-6.0, centroid=(60, 1500),
       desc="Idling generator in the utility room")
def amb_generator(rng, i):
    n = secs(6.0)
    cycles = 132  # 22 firings a second
    imp = np.zeros(n)
    rat = np.zeros(n)
    for c in range(cycles):
        pos = (c + rng.normal(0.0, 0.012)) * n / cycles
        imp[int(pos) % n] += (1.0, 0.8)[c % 2] * rng.uniform(0.85, 1.15)  # two cylinders, never quite equal
        rat[int(pos + 0.5 * n / cycles) % n] += rng.uniform(0.3, 1.0)
    thump = resonate(imp, [44, 88, 175, 340], [9, 9, 8, 6], [1, 0.9, 0.5, 0.25])
    k = decay(secs(0.03), 0.03)
    exhaust = noise(rng, n, -3.0, 200, 2200) * conv_circ(imp, k)
    rattle = resonate(rat, [1300, 2100, 3400], 15.0, [1, 0.7, 0.4])
    t = tgrid(n)
    whine = np.sin(TWO_PI * qf(300.0, n) * t) + 0.5 * np.sin(TWO_PI * qf(600.0, n) * t)
    return (nrm(thump) + 0.3 * nrm(exhaust) + 0.05 * nrm(rattle) + 0.02 * whine
            + 0.12 * noise(rng, n, -6.0, 30, 150))


@sound("AMB_Wind_Window", kind="loop", max_dur=8.1, vol=0.5, radius=9,
       desc="Wind through a cracked window: gusts and a thin whistle")
def amb_wind_window(rng, i):
    n_loop, n = secs(8.0), secs(10.0)
    ts = np.arange(0.0, 10.0, 0.02)
    gust = np.interp(ts, tgrid(n), wander(rng, n, 0.45))
    g2 = np.interp(ts, tgrid(n), wander(rng, n, 0.8))
    body = sweep_bp(noise(rng, n, -3.0, 80, 6000), ts, 250.0 * 2.0 ** (1.8 * gust), 2.5)
    whistle = sweep_bp(noise(rng, n, 0.0, 500, 5000), ts, 1150.0 * (1.0 + 0.25 * g2), 40.0)
    ge = np.interp(tgrid(n), ts, gust)
    x = nrm(body) * (0.3 + 0.7 * ge) + 0.3 * nrm(whistle) * ge ** 2 + 0.25 * noise(rng, n, -6.0, 30, 150) * (0.5 + ge)
    return crossfade_loop(x, n_loop)


@sound("AMB_Pipes", kind="loop", max_dur=10.1, vol=0.55, radius=12, peak=-6.0,
       desc="Plumbing in the walls: occasional knocks and a groan")
def amb_pipes(rng, i):
    n = secs(10.0)
    out = 0.012 * noise(rng, n, -8.0, 30, 140)  # water somewhere far along the run
    for t0 in (1.2, 3.9, 4.27, 7.6, 7.84, 8.31):
        knock = strike(rng, 0.7, [138, 305, 590, 960, 1420], [0.5, 0.35, 0.25, 0.15, 0.1], [1, 0.7, 0.45, 0.25, 0.12],
                       nz=0.4, nz_band=(80, 900), nz_t60=0.04, detune=0.05)
        put_wrap(out, lp(knock, 1100.0), t0 + rng.uniform(-0.05, 0.05), rng.uniform(0.45, 1.0))
    groan = creak(rng, 1.6, [(0, 27), (0.8, 44), (1.6, 23)], [172, 263, 418, 610], 30.0,
                  shape=[(0, 0), (0.3, 1), (1.1, 0.8), (1.6, 0)], irr=0.2)
    put_wrap(out, lp(groan, 800.0, 4), 5.3, 0.3)
    return reverb_loop(out, "corridor", 0.6)


@sound("AMB_Static_Radio", kind="loop", max_dur=6.1, vol=0.45, radius=7,
       desc="Radio static with faint ghostly carrier tones")
def amb_static_radio(rng, i):
    n_loop, n = secs(6.0), secs(7.2)
    t = tgrid(n)
    x = 0.5 * noise(rng, n, 0.0, 400, 4500) * (1.0 + 0.25 * lfo_noise(rng, n, 3.0))
    x += 0.5 * grains(rng, 7.2, 25, 1500, 6000, 0.002)
    for fc, dev in ((640.0, 12.0), (1215.0, 60.0), (1890.0, 25.0)):  # stations that never quite come in
        f = fc + dev * lfo_noise(rng, n, 0.6)
        x += 0.16 * np.sin(TWO_PI * np.cumsum(f) / SR) * wander(rng, n, 0.5) ** 2
    het = 900.0 + 1600.0 * wander(rng, n, 0.25)  # heterodyne whistle sliding across the dial
    x += 0.08 * np.sin(TWO_PI * np.cumsum(het) / SR) * wander(rng, n, 0.7) ** 3
    k = 28
    vowels = np.array([[650, 1100, 2500], [400, 2000, 2700], [450, 900, 2400], [550, 1700, 2500]])
    murmur = tract(glottal(rng, 105.0 + 12.0 * lfo_noise(rng, n, 2.0), 0.03, 0.1) + 0.2 * noise(rng, n, -3.0, 300, 5000),
                   vowels[rng.integers(0, 4, k)], np.linspace(0, 7.2, k), bws=(90, 110, 160), amps=(1, 0.6, 0.3))
    x += 0.5 * nrm(murmur) * wander(rng, n, 3.0) ** 2 * wander(rng, n, 0.4)  # something almost like speech
    x = drive(band(x, 300.0, 3800.0), 1.5)
    return crossfade_loop(x, n_loop)


# ----------------------------------------------------------------------------
# Beds (stereo, seamless). Every partial has a whole number of cycles per loop.
# ----------------------------------------------------------------------------

def _swell(rng, n, lo, hi, max_cycles=4):
    """Slow sine LFO with a whole number of cycles in the loop."""
    c = rng.integers(1, max_cycles + 1)
    return lo + (hi - lo) * (0.5 + 0.5 * np.sin(TWO_PI * c * np.arange(n) / n + rng.uniform(0, TWO_PI)))


def _bowed_metal(rng, dur, base, ratios=(1.0, 1.59, 2.31, 3.14, 4.2, 5.43)):
    n = secs(dur)
    t = tgrid(n)
    f = base * np.asarray(ratios) * (1.0 + rng.uniform(-0.01, 0.01, len(ratios)))
    x = modal(n, f, rng.uniform(0.5, 1.0, len(f)) * dur, 1.0 / np.arange(1, len(f) + 1) ** 0.7)
    return nrm(x * (1.0 - np.exp(-t / rng.uniform(0.3, 0.8))))  # slow onset: bowed, not struck


@sound("MUS_Clinic_Ambience", kind="bed", max_dur=32.1, vol=0.5,
       desc="Main ambience: dark evolving drones, distant metallic resonances, no melody")
def mus_clinic_ambience(rng, i):
    n = secs(32.0)
    t = tgrid(n)
    out = np.zeros((n, 2))
    # A1 against Bb1 a semitone up and D1 a fifth below: the roots beat slowly, never settle
    for ch in (0, 1):
        d = np.zeros(n)
        for root, level in ((55.0, 1.0), (58.27, 0.45), (82.41, 0.45), (36.71, 0.4)):
            for k in range(1, 9):
                f = qf(root * k * (1.0 + rng.uniform(-0.004, 0.004)), n)
                d += level / k ** 1.15 * _swell(rng, n, 0.15, 1.0) * np.sin(TWO_PI * f * t + rng.uniform(0, TWO_PI))
        out[:, ch] += 0.5 * d
        out[:, ch] += 0.12 * noise(rng, n, -3.0, 150, 900) * _swell(rng, n, 0.2, 1.0, 3)  # air
        out[:, ch] += 0.2 * noise(rng, n, -9.0, 25, 70) * _swell(rng, n, 0.4, 1.0, 2)     # sub rumble
        whine = np.sin(TWO_PI * qf(1244.5, n) * t + rng.uniform(0, TWO_PI)) + np.sin(TWO_PI * qf(1251.0, n) * t)
        out[:, ch] += 0.006 * whine * _swell(rng, n, 0.0, 1.0, 2) ** 2
    far = np.zeros((n, 2))
    for base in (207.0, 311.0, 466.0, 277.0, 370.0, 622.0):
        put_wrap(far, _bowed_metal(rng, rng.uniform(5.0, 8.0), base), rng.uniform(0.0, 32.0),
                 rng.uniform(0.5, 1.0), rng.uniform(-0.8, 0.8))
    out += 0.22 * reverb_loop(far, "hall", 2.0, dry=0.25)  # nearly all wet: heard from rooms away
    return lp(out, 6000.0)


def _shepard(n, f_base, octaves=6):
    """Endlessly rising glide: each partial climbs one octave over the loop and hands
    over, in frequency and in phase, to the one above, so the loop has no seam."""
    u = np.arange(n) / n
    t_loop = n / SR
    out, phi = np.zeros(n), 0.0
    for k in range(octaves):
        cyc = f_base * 2.0 ** k * t_loop / np.log(2.0)
        w = (0.5 - 0.5 * np.cos(TWO_PI * (k + u) / octaves)) ** 1.5
        out += w * np.sin(TWO_PI * cyc * (2.0 ** u - 1.0) + phi)
        phi += TWO_PI * cyc
    return out


@sound("MUS_Chase", kind="bed", max_dur=16.1, vol=0.6,
       desc="Chase: low throb at 150 bpm under endlessly rising dissonant layers")
def mus_chase(rng, i):
    n = secs(16.0)
    t = tgrid(n)
    beat = 0.4
    low = np.zeros(n)
    wet = np.zeros((n, 2))
    m = secs(0.38)
    tm = tgrid(m)
    kick = np.sin(TWO_PI * np.cumsum(42.0 + 40.0 * np.exp(-tm / 0.03)) / SR) * decay(m, 0.3)
    for b in range(40):
        put_wrap(low, kick, b * beat, 1.0)
        put_wrap(low, kick, b * beat + 0.2, 0.45 if b % 4 != 3 else 0.8)  # the bar leans into its last off-beat
    # bass: D2, with every fourth bar a semitone up
    mb = secs(0.2)
    for e in range(80):
        f = 73.42 if (e // 8) % 4 != 3 else 77.78
        note = lp(tone(f, mb, saw(12)), 320.0) * decay(mb, 0.22) * (1.0 - np.exp(-tgrid(mb) / 0.003))
        put_wrap(low, note, e * 0.2, 0.5 * (1.0 if e % 2 == 0 else 0.7))
    # rising cluster: root, minor second and tritone, with a nervous tremolo
    trem = 0.75 + 0.25 * np.sin(TWO_PI * 8.0 * t)
    rise = np.stack([_shepard(n, 55.0) + 0.7 * _shepard(n, 77.78), _shepard(n, 58.27) + 0.7 * _shepard(n, 77.78 * 1.003)], axis=1)
    wet += 0.17 * rise * trem[:, None]
    for s in range(160):  # sixteenth-note ticking
        tick = burst(rng, 0.04, 0.02, 5000, 12000)
        put_wrap(wet, tick, s * 0.1, 0.14 * (1.0, 0.3, 0.55, 0.3)[s % 4], 0.5 if s % 2 else -0.5)
    for bar, base in ((0, 1730.0), (5, 1320.0)):  # scraped metal on bars 1 and 6
        stab = _bowed_metal(rng, 2.5, base, (1.0, 1.06, 1.41, 2.09, 2.83))
        put_wrap(wet, stab, bar * 1.6, 0.16, rng.uniform(-0.5, 0.5))
    out = reverb_loop(wet, "corridor", 0.35)
    out += (0.75 * drive(low, 1.5))[:, None]
    return out


@sound("MUS_Safe_Room", kind="bed", max_dur=20.1, vol=0.5,
       desc="Save room: calm, warm low pad")
def mus_safe_room(rng, i):
    n = secs(20.0)
    t = tgrid(n)
    pad = np.zeros((n, 2))
    # open D chord with the ninth on top: D2 A2 D3 F#3 A3 E4
    for f0, level in ((73.42, 1.0), (110.0, 0.8), (146.83, 0.7), (185.0, 0.45), (220.0, 0.4), (329.63, 0.18)):
        for ch in (0, 1):
            v = np.zeros(n)
            for det in (-0.0025, 0.0, 0.003):
                for k, a in ((1, 1.0), (2, 0.3), (3, 0.12), (4, 0.05)):
                    v += a * np.sin(TWO_PI * qf(f0 * k * (1.0 + det + rng.uniform(-0.0008, 0.0008)), n) * t + rng.uniform(0, TWO_PI))
            pad[:, ch] += level * v * _swell(rng, n, 0.45, 1.0, 3)
    pad = lp(pad, 1400.0)
    for ch in (0, 1):
        pad[:, ch] += 0.25 * noise(rng, n, -6.0, 60, 500) * _swell(rng, n, 0.5, 1.0, 2)  # warmth, like tape
    glints = np.zeros((n, 2))
    for f, t0 in ((440.0, 3.0), (587.33, 9.5), (739.99, 15.0)):
        m = secs(4.0)
        g = tone(f, m, (1.0, 0.15)) * decay(m, 3.5) * (1.0 - np.exp(-tgrid(m) / 0.06))
        put_wrap(glints, g, t0, 0.5, rng.uniform(-0.5, 0.5))
    return reverb_loop(pad, "hall", 0.5) + reverb_loop(glints, "hall", 1.5, dry=0.5)


# ----------------------------------------------------------------------------
# Stingers (stereo one-shots)
# ----------------------------------------------------------------------------

def _strings(rng, n, freqs, harm=12, roll=1.0, detune=0.004):
    """A section's worth of slightly detuned sawtooth voices; freqs may be glide arrays."""
    out = np.zeros(n)
    for f in freqs:
        for _ in range(2):
            out += tone(f * (1.0 + rng.uniform(-detune, detune)), n, saw(harm, roll), ph=rng.uniform(0, TWO_PI))
    return out / (2 * len(freqs))


@sound("STG_Enemy_Spotted", kind="stinger", max_dur=3.5, vol=0.8,
       desc="Enemy spotted: reverse swell into a sharp dissonant hit")
def stg_enemy_spotted(rng, i):
    th = 0.65
    cluster = [110.0, 116.54, 233.08, 311.13, 466.16, 493.88, 932.33, 987.77]
    # the swell is a reverberated hit played backwards, so it sucks into the real one
    ns = secs(0.5)
    pre = (_strings(rng, ns, cluster[2:6]) + 0.1 * noise(rng, ns, -3.0, 500, 4000)) * decay(ns, 0.4)
    pre = reverb(fade(pre, 0.002, 0.01), "hall", 1.2, tail=0.5, stereo=True)[:secs(th)][::-1]
    nh = secs(1.5)
    hit = drive(3.0 * _strings(rng, nh, cluster) * (1.0 + 0.2 * lfo_noise(rng, nh, 9.0)), 2.0)
    hit = hit * decay(nh, 1.3) * (1.0 - np.exp(-tgrid(nh) / 0.003))
    tb = tgrid(secs(1.0))
    boom = np.sin(TWO_PI * np.cumsum(38.0 + 30.0 * np.exp(-tb / 0.05)) / SR) * decay(len(tb), 0.9)
    scrape = strike(rng, 0.9, [1730, 2410, 3350, 4620], [0.8, 0.6, 0.45, 0.3], nz=0)
    dry = np.zeros(secs(1.5))
    put(dry, hit, 0.0, 0.9)
    put(dry, boom, 0.0, 0.55)
    put(dry, burst(rng, 0.08, 0.05, 800, 8000), 0.0, 0.6)
    put(dry, scrape, 0.0, 0.14)
    hit_wet = reverb(dry, "hall", 0.6, tail=1.2, stereo=True)
    out = np.zeros((secs(th) + len(hit_wet), 2))
    put(out, fade(pre, 0.25, 0.004), 0.0, 0.5)
    put(out, hit_wet, th)
    return out


@sound("STG_Death", kind="stinger", max_dur=4.0, vol=0.8,
       desc="Player died: a low hit and a cluster sinking an octave")
def stg_death(rng, i):
    n = secs(3.2)
    t = tgrid(n)
    fall = 2.0 ** (-t / 3.2)  # one octave down over the length
    sink = _strings(rng, n, [f * fall for f in (220.0, 233.08, 277.18, 329.63)], harm=10, roll=1.2)
    sink = tract(sink, [[420, 820, 2500], [330, 700, 2300]], bws=(140, 170, 260), amps=(1, 0.7, 0.2), dark=1800)  # a dark "oo" colour
    sink = nrm(sink) * (1.0 - np.exp(-t / 0.03)) * decay(n, 3.4)
    boom = np.sin(TWO_PI * np.cumsum(41.0 + 40.0 * np.exp(-t / 0.08)) / SR) * decay(n, 1.8)
    dry = 0.8 * sink + 0.6 * boom
    put(dry, burst(rng, 0.25, 0.15, 60, 1200), 0.0, 0.5)
    put(dry, _bowed_metal(rng, 2.5, 147.0), 0.05, 0.12)
    return reverb(dry, "hall", 0.55, tail=0.8, stereo=True)


@sound("STG_Completed", kind="stinger", max_dur=4.6, vol=0.8,
       desc="Demo completed: a suspended chord resolves; relief")
def stg_completed(rng, i):
    n = secs(3.6)
    t = tgrid(n)
    amps = (1.0, 0.35, 0.15, 0.08, 0.04)

    def pad(freqs):
        return sum(g * sum(tone(f * (1.0 + d), n, amps, ph=rng.uniform(0, TWO_PI)) for d in (-0.003, 0.0, 0.0035))
                   for f, g in freqs)
    base = pad([(73.42, 1.0), (110.0, 0.8), (146.83, 0.7), (220.0, 0.5), (293.66, 0.35), (440.0, 0.15)])
    sus = pad([(196.0, 0.5), (392.0, 0.2)])     # the fourth, G, hangs ...
    res = pad([(185.0, 0.5), (369.99, 0.22)])   # ... and falls to the major third, F#
    x = np.interp(t, [0.0, 0.95, 1.35, 3.6], [0.0, 0.0, 1.0, 1.0])
    chord = base + sus * np.cos(0.5 * np.pi * x) + res * np.sin(0.5 * np.pi * x)
    chord = lp(chord, 2200.0) * env(n, [(0, 0), (0.45, 1.0), (1.9, 0.9), (3.6, 0.0)]) ** 1.5
    dry = np.zeros((n, 2))
    dry += (0.12 * chord)[:, None] * 0.7071
    for f, t0, p in ((880.0, 1.3, -0.4), (1174.66, 1.55, 0.4), (1479.98, 1.8, 0.0)):  # A5, D6, F#6: the triad again, as light
        m = secs(1.8)
        put(dry, tone(f, m, (1.0, 0.2)) * decay(m, 1.8) * (1.0 - np.exp(-tgrid(m) / 0.01)), t0, 0.06, p)
    return reverb(dry, "hall", 0.5, tail=0.9)


# =============================================================================
# Render, measure, document
# =============================================================================

def render(entry):
    rng = np.random.default_rng([SEED, zlib.crc32(entry["name"].encode())])
    x = np.asarray(entry["fn"](rng, entry["index"]), float)
    channels, loops, _ = KINDS[entry["kind"]]
    if (x.ndim == 2) != (channels == 2):
        raise RuntimeError("%s: wrong channel count" % entry["name"])
    x = finish_loop(x, entry["peak"]) if loops else finish_oneshot(x, entry["peak"], entry["max_dur"])
    path = os.path.join(OUT_DIR, entry["name"] + ".wav")
    write_wav(path, x)
    return path


def seam(x):
    """Loop closure, measured on the 16-bit samples: the step from the last sample to
    the first, and the change of slope there, each against the largest value the same
    quantity takes in the 2048 samples either side. A ratio near or under 1 means the
    seam is indistinguishable from the rest of the signal."""
    worst = (0.0, 0.0, 0.0)
    ok = True
    for c in range(x.shape[1]):
        v = x[:, c]
        w = min(2048, len(v) // 4)
        ext = np.concatenate([v[-w:], v[:w]])
        d1, d2 = np.diff(ext), np.diff(ext, 2)
        jump, ref1 = abs(d1[w - 1]), np.abs(np.delete(d1, w - 1)).max()
        curv, ref2 = max(abs(d2[w - 2]), abs(d2[w - 1])), np.abs(np.delete(d2, [w - 2, w - 1])).max()
        ok = ok and jump <= 1.5 * ref1 + 2.0 and curv <= 1.5 * ref2 + 4.0
        worst = max(worst, (jump, jump / max(ref1, 1.0), curv / max(ref2, 1.0)))
    return ok, worst


def check(entries):
    """Measure the files on disk and return the list of problems found."""
    problems, total = [], 0
    print("%-28s %2s %7s %7s %7s %8s %8s %-18s %8s" % ("file", "ch", "dur s", "peak dB", "rms dB", "dc", "centr Hz", "seam jmp/r1/r2", "kB"))
    for e in entries:
        path = os.path.join(OUT_DIR, e["name"] + ".wav")
        bad = []
        if not os.path.isfile(path):
            problems.append("%s: missing" % e["name"])
            continue
        x, ch, sw, sr = read_wav(path)
        size = os.path.getsize(path)
        total += size
        channels, loops, _ = KINDS[e["kind"]]
        f = x / 32768.0
        dur = len(f) / sr
        peak, rms, dc = to_db(np.abs(f).max()), to_db(np.sqrt((f ** 2).mean())), float(np.abs(f.mean(axis=0)).max())
        mono = f.mean(axis=1)
        mag = np.abs(np.fft.rfft(mono)) ** 2  # by power: where the energy sits, not where the noise floor is wide
        centroid = float((mag * np.fft.rfftfreq(len(mono), 1.0 / sr)).sum() / (mag.sum() + 1e-12))
        if (ch, sw, sr) != (channels, 2, SR):
            bad.append("format %d ch / %d bit / %d Hz" % (ch, 8 * sw, sr))
        if rms < -45.0 or peak < -20.0:
            bad.append("silent")
        if np.abs(x).max() >= 32767 or peak > -0.5:
            bad.append("clipped")
        if abs(peak - e["peak"]) > 0.5:
            bad.append("peak off target %.1f" % e["peak"])
        if dc > 0.002:
            bad.append("DC offset")
        if dur > e["max_dur"]:
            bad.append("longer than %.2f s" % e["max_dur"])
        if e["centroid"] and not (e["centroid"][0] <= centroid <= e["centroid"][1]):
            bad.append("centroid outside %s" % (e["centroid"],))
        seam_txt = "-"
        if loops:
            ok, (jump, r1, r2) = seam(x)
            seam_txt = "%d lsb %.2f %.2f" % (jump, r1, r2)
            if not ok:
                bad.append("loop does not close")
        else:
            edge = max(np.abs(x[0]).max(), np.abs(x[-1]).max())
            if edge > 64:
                bad.append("starts or ends off zero (%d lsb)" % edge)
        print("%-28s %2d %7.2f %7.1f %7.1f %8.5f %8.0f %-18s %8.1f %s" % (
            e["name"], ch, dur, peak, rms, dc, centroid, seam_txt, size / 1024.0, ("  <-- " + "; ".join(bad)) if bad else ""))
        problems += ["%s: %s" % (e["name"], b) for b in bad]
    print("checked %d files, %.2f MB" % (len(entries), total / 1048576.0))
    if len(entries) == len(SOUNDS) and total > TOTAL_BUDGET_MB * 1048576:
        problems.append("set is %.1f MB, over the %.0f MB budget" % (total / 1048576.0, TOTAL_BUDGET_MB))
    return problems


LEGACY = (  # in the folder before this script existed; not generated, not touched
    ("SFX_DamageTaken", "Earlier prototype: player damage"),
    ("SFX_EmptyClick", "Earlier prototype: empty weapon click"),
    ("SFX_Footstep", "Earlier prototype: footstep"),
    ("SFX_HitConfirm", "Earlier prototype: hit confirmation"),
    ("SFX_RifleFire", "Earlier prototype: rifle shot"),
    ("SFX_RifleReload", "Earlier prototype: rifle reload"),
    ("SFX_UIClick", "Earlier prototype: UI click"),
)


def write_readme():
    rows = []
    for e in SOUNDS:
        path = os.path.join(OUT_DIR, e["name"] + ".wav")
        channels, loops, _ = KINDS[e["kind"]]
        with wave.open(path, "rb") as w:
            dur = w.getnframes() / w.getframerate()
        rows.append("| `%s` | %s | %s | %s | %.2f | %.2f | %s |" % (
            e["name"], e["desc"], "stereo" if channels == 2 else "mono", "loop" if loops else "one-shot", dur,
            e["vol"], ("%g" % e["radius"]) if e["radius"] else "- (2D)"))
    for name, desc in LEGACY:
        path = os.path.join(OUT_DIR, name + ".wav")
        if os.path.isfile(path):
            with wave.open(path, "rb") as w:
                rows.append("| `%s` | %s (not generated by the script) | %s | one-shot | %.2f | - | - |" % (
                    name, desc, "stereo" if w.getnchannels() == 2 else "mono", w.getnframes() / w.getframerate()))
    text = """# LeonHorror audio

Every `SFX_*`, `AMB_*`, `MUS_*` and `STG_*` file listed as generated below is synthesized by
`Games/LeonHorror/Scripts/make_audio.py` (no samples). Do not edit the WAVs by hand: change the
script and run it again; it is deterministic. This file is rewritten by the script as well
(`--readme`, or any full run).

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python Games/LeonHorror/Scripts/make_audio.py -- [--only Name,...] [--check]

All files are 44.1 kHz, 16-bit PCM. The engine resolves them by path without extension, for
example `/Game/Audio/SFX_Door_Open`.

- **mono one-shot**: positional effect. Peak about -3 dBFS.
- **mono loop**: positional emitter, loops seamlessly. Peak about -9 dBFS (a few sparse ones higher).
- **stereo loop**: non-positional bed, loops seamlessly. Peak about -9 dBFS.
- **stereo one-shot**: non-positional stinger.
- Numbered variants (`_01`, `_02`, ...) are takes of the same event: pick one at random, never the
  same twice in a row, and vary pitch by a few percent.
- *Volume* is a suggested playback gain (0..1) as a starting mix. *Radius* is the suggested audible
  radius in metres for positional playback; "- (2D)" means play it non-positionally.

The set was verified by measurement only (duration, peak, RMS, DC, spectral centroid, loop
closure). Nobody had listened to it when it was generated, so treat volumes and radii as first
guesses and expect to retune some sounds by ear.

| File | Purpose | Channels | Type | Duration (s) | Volume | Radius (m) |
|---|---|---|---|---|---|---|
""" + "\n".join(rows) + "\n"
    with open(os.path.join(OUT_DIR, "README.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("wrote README.md (%d rows)" % len(rows))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ([] if "bpy" in sys.modules else sys.argv[1:])
    only, flags = None, set()
    it = iter(argv)
    for a in it:
        if a == "--only":
            only = [s for s in next(it).split(",") if s]
        elif a in ("--check", "--list", "--readme"):
            flags.add(a)
        else:
            raise SystemExit("unknown argument: " + a)
    entries = SOUNDS
    if only:
        entries = [e for e in SOUNDS if any(fnmatch.fnmatch(e["name"], p) or p in (e["name"], e["stem"]) for p in only)]
        missing = [p for p in only if not any(fnmatch.fnmatch(e["name"], p) or p in (e["name"], e["stem"]) for e in SOUNDS)]
        if missing:
            raise SystemExit("no such sound: " + ", ".join(missing))
    if "--list" in flags:
        for e in entries:
            print("%-28s %-8s %s" % (e["name"], e["kind"], e["desc"]))
        return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    if "--check" not in flags and not ("--readme" in flags and not only):
        for e in entries:
            render(e)
            print("wrote " + e["name"] + ".wav")
            sys.stdout.flush()
    problems = check(entries)
    if "--readme" in flags or (not only and "--check" not in flags):
        write_readme()
    if problems:
        print("FAILED:")
        for p in problems:
            print("  " + p)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    try:
        code = main()
    except Exception:
        import traceback
        traceback.print_exc()
        code = 2
    sys.stdout.flush()
    if code:
        sys.exit(code)  # Blender reports a script's exit code only when the script exits itself
