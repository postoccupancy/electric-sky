/*
 * WAVELET GENERATIVE SKETCH
 * ─────────────────────────────────────────────────────────────────────────────
 * Reads the JSON exported from the wavelet analysis page and builds a
 * polyrhythmic generative piece. Each significant frequency band becomes
 * a voice. Period ratios define the polyrhythmic grid. Euclidean gate
 * patterns come from blob activity density. PLV values control how tightly
 * each voice locks to the master clock.
 *
 * Aesthetic: Eno / Reich / Dub / Detroit Techno
 *
 * QUICK START
 *   1. s.boot  (Cmd+B) — wait for "server ready" in post window
 *   2. Evaluate each block top to bottom with Cmd+Return
 *   3. Cmd+.  to stop everything
 */


// ─── BLOCK 1: CONFIGURATION ──────────────────────────────────────────────────
(
~jsonPath = "/Users/adrianmacdonald/Documents/projects/developer/residentfrequency/electric-sky/wavelet_2025-11-11_2026-06-16.json";
~signal   = "temperature_f";  // "temperature_f" or "dewpoint_f"

~bpm      = 400;    // master tempo
~dayBeats = 32;    // beats per 24h period — lower = faster/denser, higher = slower/ambient
~rootFreq = 55.0;  // A2 — all pitches are harmonics of this
~echoAmt  = 0.22;  // dub ghost level (0 = off)
)


// ─── BLOCK 2: LOAD DATA ───────────────────────────────────────────────────────
(
var raw  = File.readAllString(~jsonPath);
var json = raw.parseYAML;

~sig   = json["signals"][~signal];
~bands = ~sig["bands"];
~plvV  = ~sig["plv_matrix"]["values"];
~plvL  = ~sig["plv_matrix"]["labels"];
~meta  = json["meta"];

"Loaded: % — % bands  (%h total)".format(~signal, ~bands.size, ~meta["total_hours"]).postln;
~bands.do { |b|
    var k = b["euclidean"]["k"].asInteger;
    var n = b["euclidean"]["n"].asInteger;
    "  %  %h  density=%  E(%,%)".format(
        b["label"],
        b["period_hours"].asFloat.round(0.1),
        (k / n.max(1)).round(0.01),
        k, n
    ).postln
};
)


// ─── BLOCK 3: SYNTH DEFS ──────────────────────────────────────────────────────
// Requires s.boot. The four -> lines printed are normal (.add return values).
(
// Sub-bass: sine stack, long release — the dub bass character
SynthDef(\wvBass, { |out=0, freq=55, amp=0.5, atk=0.02, rel=1.6, pan=0|
    var env = EnvGen.kr(Env.perc(atk, rel, amp, -4), doneAction: Done.freeSelf);
    var sig = SinOsc.ar(freq)
            + SinOsc.ar(freq * 2, mul: 0.20)
            + SinOsc.ar(freq * 3, mul: 0.07);
    Out.ar(out, Pan2.ar(LPF.ar(sig, freq * 4) * env, pan))
}).add;

// Tone: filtered pulse, Moog-style sweep on each hit
SynthDef(\wvTone, { |out=0, freq=110, amp=0.28, atk=0.008, rel=0.75, pan=0|
    var env = EnvGen.kr(Env.perc(atk, rel, amp, -3), doneAction: Done.freeSelf);
    var sig = Pulse.ar(freq, 0.35, 0.65) + LFTri.ar(freq * 0.998, mul: 0.3);
    var flt = RLPF.ar(sig, (freq * 4 * env + freq).clip(freq, 12000), 0.25);
    Out.ar(out, Pan2.ar(flt * env, pan))
}).add;

// Pad: slow-attack cluster of detuned sines — Eno tape-loop texture
SynthDef(\wvPad, { |out=0, freq=55, amp=0.16, atk=0.5, sus=1.0, rel=2.5, pan=0|
    var env = EnvGen.kr(Env([0,1,0.65,0], [atk, sus, rel], \sin), doneAction: Done.freeSelf);
    var sig = Mix.fill(6, { SinOsc.ar(freq * Rand(0.985, 1.015), Rand(0, 2pi), 1/6) });
    Out.ar(out, Pan2.ar(sig * env * amp, pan))
}).add;

// Click: band-pass noise burst — hi-hat / transient character
SynthDef(\wvClick, { |out=0, freq=1000, amp=0.22, rel=0.09, pan=0|
    var env = EnvGen.kr(Env.perc(0.001, rel, amp, -8), doneAction: Done.freeSelf);
    var sig = BPF.ar(WhiteNoise.ar, freq, 0.1) + SinOsc.ar(freq * 0.5, mul: 0.25);
    Out.ar(out, Pan2.ar(sig * env, pan))
}).add;
)


// ─── BLOCK 4: PLAY ────────────────────────────────────────────────────────────
// Re-evaluate this block alone to restart. Change ~bpm / ~rootFreq in Block 1 first.
(
var euclid, plvFor, clockLabel, voiceFor, patterns;

TempoClock.default.tempo = ~bpm / 60.0;

euclid = { |k, n|
    var bucket = 0;
    Array.fill(n.max(1), {
        bucket = bucket + k;
        if (bucket >= n) { bucket = bucket - n; 1 } { 0 }
    })
};

plvFor = { |la, lb|
    var ia = ~plvL.indexOf(la), ib = ~plvL.indexOf(lb);
    if (ia.notNil and: { ib.notNil }) {
        var v = ~plvV[ia][ib];
        if (v.isNumber) { v.asFloat } { 0.0 }
    } { 0.0 }
};

clockLabel = (~bands.detect { |b| b["label"] == "~24h" } ?? ~bands[0])["label"];

voiceFor = { |period|
    case
    { period >= 120 } { [\wvPad,   ~rootFreq,                    1.0.rand2] }
    { period >= 36  } { [\wvPad,   ~rootFreq * 2,                1.0.rand2] }
    { period >= 20  } { [\wvBass,  ~rootFreq * 2,                1.0.rand2] }
    { period >= 9   } { [\wvTone,  [3,4,6].choose * ~rootFreq,   1.0.rand2] }
    { true          } { [\wvClick, [8,12,16].choose * ~rootFreq, 1.0.rand2] }
};

patterns = ~bands.collect { |band|
    var label   = band["label"];
    var period  = band["period_hours"].asFloat;
    var loopDur = (~dayBeats * (period / 24.0)).round.max(1);

    var k       = band["euclidean"]["k"].asInteger;
    var n       = band["euclidean"]["n"].asInteger;
    var kGrid   = ((k / n.max(1)) * 16).round.asInteger.clip(1, 16);
    var gate    = euclid.(kGrid, 16);
    var stepDur = loopDur / 16;

    // PLV with 24h clock → timing jitter. Low PLV = loose dub feel. High PLV = tight grid.
    var plv     = plvFor.(label, clockLabel);
    var jitter  = plv.linlin(0.0, 1.0, 0.15, 0.0);

    var baseAmp = (band["amplitude_envelope"].collect(_.asFloat).mean * 0.55 + 0.1).clip(0.05, 0.65);
    var voice   = voiceFor.(period);
    var instr   = voice[0], freq = voice[1], pan = voice[2];

    var durs = Pseq(gate.collect { |g|
        if (g == 1)
            { Pwhite(stepDur * (1 - jitter), stepDur * (1 + jitter), 1) }
            { Rest(stepDur) }
    }, inf);

    // Dub ghost: same gate rotated 1 step, opposite pan, quieter
    var echoGate = [gate.last] ++ gate.copyRange(0, 14);
    var echoDurs = Pseq(echoGate.collect { |g|
        if (g == 1) { stepDur } { Rest(stepDur) }
    }, inf);

    Ppar([
        Pbind(
            \instrument, instr,
            \freq,  freq,
            \amp,   Pwhite(baseAmp * 0.82, baseAmp * 1.18, inf) * Pseq(gate, inf),
            \dur,   durs,
            \pan,   pan,
        ),
        Pbind(
            \instrument, instr,
            \freq,  freq * 0.998,
            \amp,   baseAmp * ~echoAmt * Pseq(echoGate, inf),
            \dur,   echoDurs,
            \pan,   pan.neg,
        ),
    ], inf)
};

~score = Ppar(patterns, inf).play(TempoClock.default);
"Playing % bands at % BPM — Cmd+. to stop".format(~bands.size, ~bpm).postln;
)


/*
 * LIVE TWEAKS (put cursor on a line, Cmd+Return)
 *
 * Change tempo:      TempoClock.default.tempo = 110/60
 * Stop:              ~score.stop
 * Restart:           Re-evaluate Block 4
 * Other signal:      Change ~signal in Block 1, re-evaluate Blocks 1+2+4
 *
 * PARAMETERS TO EXPLORE FIRST
 *   ~dayBeats = 4    fast / rhythmically dense
 *   ~dayBeats = 256   very slow / ambient
 *   ~echoAmt  = 0     remove dub ghost layer
 *   ~echoAmt  = 0.6   heavy echo
 *   ~rootFreq = 41.2  E1 — darker root
 *   ~rootFreq = 65.4  C2 — brighter root
 */
