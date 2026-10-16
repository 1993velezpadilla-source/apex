#!/usr/bin/env python3
"""Produce reproducible, RMS-level-matched, blinded research clips of a localized real-voice spectral change.

Designed for CI's licensed Vocadito PCM WAV render; uses only the stdlib.
DOES NOT imply singer quality, correct formants, or 1 ms correction.
"""
import argparse
import hashlib
import json
import math
import pathlib
import wave

VARIANTS = {
    "previous_high": "harmonic_live_high_research",
    "ultra": "harmonic_live_ultra_research",
    "ultra_dry_reset": "harmonic_live_ultra_smooth_research",
}


def read_segment(path, start, stop):
    with wave.open(str(path), "rb") as wav:
        rate, channels, width, count = (wav.getframerate(), wav.getnchannels(),
                                        wav.getsampwidth(), wav.getnframes())
        if wav.getcomptype() != "NONE" or width not in (2, 3, 4) or channels not in (1, 2):
            raise ValueError(f"Unsupported input WAV: {path}")
        if not (0 <= start < stop <= count) or rate < 8000 or rate > 96000:
            raise ValueError(f"Invalid excerpt/format: {path}")
        wav.setpos(start)
        raw = wav.readframes(stop - start)
    if len(raw) != (stop - start) * channels * width:
        raise ValueError(f"Truncated PCM: {path}")
    divisor = float(1 << (8 * width - 1))
    samples = [int.from_bytes(raw[i:i+width], "little", signed=True) / divisor
               for i in range(0, len(raw), width)]
    rms = math.sqrt(sum(x*x for x in samples) / len(samples))
    peak = max(abs(x) for x in samples)
    if not all(math.isfinite(x) for x in (rms, peak)) or rms < 1e-5 or peak < 1e-5:
        raise ValueError(f"Silent/invalid focus excerpt: {path}")
    return (rate, channels, samples, rms, peak)


def save_pcm24(path, samples, rate, channels, gain):
    pcm = bytearray()
    for x in samples:
        scaled = x * gain
        if abs(scaled) > 0.72:
            raise ValueError("Unexpected focus WAV peak (level matching error)")
        q = max(-8388608, min(8388607, round(scaled * 8388608)))
        pcm.extend(int(q).to_bytes(3, "little", signed=True))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(3)
        wav.setframerate(rate)
        wav.writeframes(pcm)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", default="vocadito_high")
    parser.add_argument("--out", default="ULTRA_VOCAL_FOCUS_AB")
    args = parser.parse_args()
    prefix = args.prefix
    report = pathlib.Path(f"{prefix}_ULTRA_LOCAL_TRANSIENT_AUDIT.txt")
    data = {}
    for line in report.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            data[key.strip()] = value.strip()
    delta = float(data["worst_13band_delta_db"])
    center = float(data["worst_13band_time_seconds"])
    if not math.isfinite(delta) or not math.isfinite(center):
        raise ValueError("Nonfinite localized real-vocal measurement")
    if delta <= 5.0 or center < 0:
        out = pathlib.Path(args.out)
        out.mkdir(exist_ok=True, parents=True)
        (out / "NO_LOCALIZED_AB_NEEDED.txt").write_text(
            f"No >5 dB voiced 13-band hotspot detected for {prefix} (max={delta:.4f} dB).\n"
            "Automated spectral differences are not proof of natural vocal quality.\n"
        )
        print(f"ULTRA_FOCUS_NO_LOCALIZED_HOTSPOT prefix={prefix} max={delta:.4f}dB")
        return

    wavs = {name: pathlib.Path(f"{prefix}_{stem}.wav") for name, stem in VARIANTS.items()}
    # DRY is an explicitly labeled natural-voice control, not a fourth
    # anonymized tune candidate. It is sample-aligned with research outputs.
    wavs["dry_control"] = pathlib.Path(f"{prefix}_source.wav")
    # 4-hop is a NEW separate research candidate, not part of the pinned
    # A/B/C assignment or its ordering on earlier CI artifacts.
    wavs["ultra_4hop"] = pathlib.Path(f"{prefix}_harmonic_live_ultra_4hop_research.wav")
    wavs["ultra_consensus"] = pathlib.Path(f"{prefix}_harmonic_live_ultra_consensus_research.wav")
    with wave.open(str(next(iter(wavs.values()))), "rb") as reference:
        rate, count = reference.getframerate(), reference.getnframes()
    start = max(0, min(round((center - 0.8) * rate), count - 1))
    end = min(count, round((center + 1.2) * rate))
    if end - start < rate:
        raise ValueError("Not enough audio for >=1 second focus excerpt")
    segments = {key: read_segment(path, start, end) for key, path in wavs.items()}
    shapes = {(sr, ch, len(samples)) for sr, ch, samples, _, _ in segments.values()}
    if len(shapes) != 1 or next(iter(shapes))[0] != rate:
        raise ValueError("AB variants have incompatible sample alignment/format")

    # Match each *excerpt's* full-length RMS. A common target below 0.1FS
    # and each variant's peak-constrained cap keep output unclipped.
    level = min([0.10] + [rms * 0.70 / peak for _, _, _, rms, peak in segments.values()])
    labels = sorted(VARIANTS, key=lambda key: hashlib.sha256(f"{prefix}|{key}|blind_v1".encode()).digest())
    out = pathlib.Path(args.out)
    out.mkdir(exist_ok=True, parents=True)
    manifest = {"dataset": "Vocadito CC BY 4.0 (see VOCADITO_LICENSE_AND_EVIDENCE.txt)",
                "source_prefix": prefix, "hotspot_start_seconds": center,
                "worst_change_db": delta, "excerpt_start_seconds": start / rate,
                "excerpt_end_seconds": end / rate, "rms_target": level,
                "important": "Research, not formant/ABX/1ms certification; compare blind before reading key.",
                "unblind": {}}
    for index, name in enumerate(labels):
        letter = "ABC"[index]
        sr, ch, samples, rms, peak = segments[name]
        gain = level / rms
        filename = f"ULTRA_FOCUS_{letter}.wav"
        save_pcm24(out / filename, samples, sr, ch, gain)
        manifest["unblind"][letter] = {"variant": name, "source_wav": str(wavs[name]),
                                       "gain": gain, "input_rms": rms, "input_peak": peak,
                                       "output_pcm24": filename}
    # Level-match the untouched source to all three research variants without
    # changing their stable blinded A/B/C assignments. Unlike HQ40 it has
    # the same nominal time origin as these non-lookahead research WAVs.
    dry_rate, dry_ch, dry_pcm, dry_rms, dry_peak = segments["dry_control"]
    hop_rate, hop_ch, hop_pcm, hop_rms, hop_peak = segments["ultra_4hop"]
    conf_rate, conf_ch, conf_pcm, conf_rms, conf_peak = segments["ultra_consensus"]
    consensus_file = "ULTRA_CONSENSUS_UNBLINDED_RESEARCH.wav"
    save_pcm24(out / consensus_file, conf_pcm, conf_rate, conf_ch, level / conf_rms)
    manifest["ultra_consensus_research_candidate"] = {
        "file": consensus_file, "gain": level / conf_rms,
        "input_rms": conf_rms, "input_peak": conf_peak,
        "not_blinded": True, "not_a_shipping_mode": True,
    }
    hop_file = "ULTRA_4HOP_UNBLINDED_RESEARCH.wav"
    save_pcm24(out / hop_file, hop_pcm, hop_rate, hop_ch, level / hop_rms)
    manifest["ultra_4hop_research_candidate"] = {
        "file": hop_file, "gain": level / hop_rms,
        "input_rms": hop_rms, "input_peak": hop_peak,
        "not_blinded": True, "not_a_shipping_mode": True,
    }
    dry_filename = "DRY_SINGER_REFERENCE.wav"
    save_pcm24(out / dry_filename, dry_pcm, dry_rate, dry_ch, level / dry_rms)
    manifest["natural_source_reference"] = {
        "file": dry_filename, "gain": level / dry_rms,
        "input_rms": dry_rms, "input_peak": dry_peak,
        "not_blinded": True, "not_pdc_aligned_with_HQ40": True,
    }
    # A local time-domain companion to the existing 2048-point spectral
    # hotspot: report 5ms peaks in PCM difference and adjacent-sample steps.
    # This is a signal diagnostic, NOT evidence that the pitch/glide is right.
    width = max(1, round(rate * .005))
    prior = segments["previous_high"][2]
    ultra = segments["ultra"][2]
    reset = segments["ultra_dry_reset"][2]
    hop = segments["ultra_4hop"][2]
    consensus = segments["ultra_consensus"][2]
    dry = segments["dry_control"][2]
    ch_count = segments["previous_high"][1]
    hot = []
    for offset in range(0, len(prior) - width * ch_count + 1, width * ch_count):
        stop = offset + width * ch_count
        n = stop - offset
        diff_power = sum((ultra[j] - prior[j]) ** 2 for j in range(offset, stop))
        diff_rms = math.sqrt(diff_power / n)
        source_rms = math.sqrt(sum(dry[j] ** 2 for j in range(offset, stop)) / n)
        hop_diff_rms = math.sqrt(sum((hop[j] - ultra[j]) ** 2
                                     for j in range(offset, stop)) / n)
        consensus_high_rms = math.sqrt(sum((consensus[j] - prior[j]) ** 2
                                           for j in range(offset, stop)) / n)
        if diff_rms <= 0.007:
            continue
        def max_step(pcm):
            return max(abs(pcm[j] - pcm[j-ch_count])
                       for j in range(offset+ch_count, stop))
        hot.append({
            "original_time_s": (start + offset // ch_count) / rate,
            "ultra_vs_previous_diff_rms": diff_rms,
            "four_hop_vs_eight_hop_diff_rms": hop_diff_rms,
            "consensus_vs_previous_high_diff_rms": consensus_high_rms,
            "dry_rms": source_rms,
            "previous_adjacent_step_peak": max_step(prior),
            "ultra_adjacent_step_peak": max_step(ultra),
            "four_hop_adjacent_step_peak": max_step(hop),
            "consensus_adjacent_step_peak": max_step(consensus),
            "reset_adjacent_step_peak": max_step(reset),
            "dry_adjacent_step_peak": max_step(dry),
            "note": "Difference is not proof of audible quality or pitch accuracy",
        })
    top = sorted(hot, key=lambda event: -event["ultra_vs_previous_diff_rms"])[:12]
    report = {
        "source": prefix, "rate": rate, "micro_window_ms": 5,
        "micro_windows_with_rms_difference_gt_0_007": len(hot),
        "consensus_vs_prior_high_full_focus_rms": math.sqrt(
            sum((consensus[j] - prior[j]) ** 2
                for j in range(len(consensus))) / len(consensus)),
        "four_hop_vs_eight_hop_full_focus_rms": math.sqrt(
            sum((hop[j] - ultra[j]) ** 2 for j in range(len(hop))) / len(hop)),
        "largest_micro_differences": top,
        "warning": "Do not infer pops/clicks, formant preservation, corrected F0 or 1ms from this table.",
    }
    (out / "MICROTRANSIENT_FORENSICS.json").write_text(json.dumps(report, indent=2) + "\n")
    manifest["microtransient_report"] = "MICROTRANSIENT_FORENSICS.json"
    (out / "UNBLIND_AFTER_LISTENING.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "LISTEN_FIRST.txt").write_text(
        "APEX Tune Live V5 ultra predictor — REAL VOCADITO SINGER LOCAL HOTSPOT\n"
        "Listen A/B/C with matched playback levels, and DO NOT read UNBLIND until rating.\n"
        "DRY_SINGER_REFERENCE.wav is the level-matched ORIGINAL for context only; do not confuse with HQ40.\n"
        "ULTRA_4HOP_UNBLINDED_RESEARCH.wav is a fourth faster-cadence candidate; no objective perceptual approval.\n"
        "ULTRA_CONSENSUS_UNBLINDED_RESEARCH.wav verifies dual-YIN crosscheck, NOT a release candidate.\n"
        "MICROTRANSIENT_FORENSICS.json contains objective 5ms local changes, NOT a listening approval.\n"
        "Pay attention to attack pops, timbre/vowels, glides, pitch jumps and artifacts.\n"
        "Files are equal-length, equal-RMS PCM24 excerpts from sample-synchronous "
        "research variants; level gain differs per file.\n"
        "They are NOT aligned against production HQ40 (which has different latency).\n"
        "No automatic metric substitutes for a blind human audition.\n"
        f"Hotspot at original recording {center:.6f} seconds; {delta:.3f} dB "
        "max localized 13-band change.\n"
        "Dataset source and license: VOCADITO_LICENSE_AND_EVIDENCE.txt "
        "(Vocadito CC BY 4.0).\n"
    )
    print(f"ULTRA_FOCUS_BLIND_AB_READY prefix={prefix} center={center:.6f}s "
          f"delta={delta:.5f}db start={start/rate:.6f}s end={end/rate:.6f}s "
          f"target_rms={level:.6f} variants={len(labels)} NOT_ARTIST_APPROVAL=1")


if __name__ == "__main__":
    main()
