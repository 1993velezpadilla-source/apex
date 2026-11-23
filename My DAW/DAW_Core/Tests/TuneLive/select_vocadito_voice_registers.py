#!/usr/bin/env python3
"""Select two independent licensed real-singer takes by annotated F0 register.

Research QA only: median F0 is a pitch register descriptor, NOT singer gender
or a determination that anyone sang badly. The existing #24 test stays pinned.
No third-party imports, network calls, or user-recording access.
"""
import csv
import io
import math
import pathlib
import statistics
import wave
import zipfile


ARCHIVE = "vocadito.zip"
EVIDENCE = "VOCADITO_REGISTER_MATRIX_LICENSE.txt"
AUTHORS = ("Rachel M. Bittner, Katherine Pasalo, Juan Jose Bosch, "
           "Gabriel Meseguer-Brocal, David Rubinstein")


def annotated_f0(data):
    frequencies = []
    for row in csv.reader(io.StringIO(data.decode("utf-8-sig", errors="replace"))):
        if len(row) < 2:
            continue
        try:
            hz = float(row[1])
        except ValueError:
            continue
        if math.isfinite(hz) and 65.0 <= hz <= 1300.0:
            frequencies.append(hz)
    return frequencies


def prepare_wave(raw):
    with wave.open(io.BytesIO(raw), "rb") as source:
        rate, channels = source.getframerate(), source.getnchannels()
        width, frames = source.getsampwidth(), source.getnframes()
        if (source.getcomptype() != "NONE" or rate < 8000 or rate > 96000
                or channels not in (1, 2) or width not in (2, 3, 4)
                or frames < rate * 2):
            return None
        data = source.readframes(min(frames, 15 * rate))
    return rate, channels, width, data


def main():
    with zipfile.ZipFile(ARCHIVE) as archive:
        wavs = sorted(name for name in archive.namelist()
                      if name.startswith("Audio/") and name.lower().endswith(".wav"))
        labels = {pathlib.PurePosixPath(name).stem.removesuffix("_f0"): name
                  for name in archive.namelist()
                  if name.startswith("Annotations/F0/")
                  and name.lower().endswith("_f0.csv")}
        candidates = []
        for name in wavs:
            base = pathlib.PurePosixPath(name).stem
            if base == "vocadito_24":  # Already our fixed strict baseline.
                continue
            annotation = labels.get(base)
            if annotation is None:
                continue
            f0 = annotated_f0(archive.read(annotation))
            if len(f0) < 150:
                continue
            wav_format = prepare_wave(archive.read(name))
            if wav_format is None:
                continue
            candidates.append((statistics.median(f0), base, name, annotation,
                               len(f0), wav_format))
        if len(candidates) < 4:
            raise RuntimeError("Vocadito lacks four valid independent real singing takes")
        candidates.sort(key=lambda x: (x[0], x[1]))
        selected = [("low", candidates[0]), ("high", candidates[-1])]
        if selected[0][1][1] == selected[1][1][1]:
            raise RuntimeError("Low/high singer register selections collided")
        report = [
            "VOCADITO REAL HUMAN VOICE — TWO INDEPENDENT REGISTER PROBES",
            "Creators: " + AUTHORS,
            "Source: https://zenodo.org/records/5578807",
            "License: CC BY 4.0 https://creativecommons.org/licenses/by/4.0/",
            "These are different voice recordings from pinned baseline #24.",
            "Median annotated F0 is a register proxy, not singer sex/gender.",
            "Low/high selection is deterministic for the dataset ZIP checksum.",
            "Pitch-register extremes may contain challenging trackers or octave",
            "errors; the research report is descriptive, not a performance claim.",
            "No private Apollo/Singer recordings are uploaded.",
        ]
        for label, choice in selected:
            median_hz, base, path, annotation, n, params = choice
            rate, channels, width, data = params
            filename = "vocadito_" + label + "_source.wav"
            with wave.open(filename, "wb") as out:
                out.setnchannels(channels)
                out.setsampwidth(width)
                out.setframerate(rate)
                out.writeframes(data)
            report.extend([
                "",
                "profile=" + label,
                "WAV entry=" + path,
                "F0 entry=" + annotation,
                "original voiced F0 frames=" + str(n),
                "annotated median F0 Hz=" + format(median_hz, ".3f"),
                "sample rate=" + str(rate),
                "channels=" + str(channels),
                "seconds=" + format(len(data)/(rate*channels*width), ".3f"),
            ])
            print("VOCADITO_REGISTER_SELECTED profile=" + label
                  + " take=" + base + " median_annotation_f0_hz="
                  + format(median_hz, ".3f")
                  + " samples=" + str(len(data)//(channels*width)))
        pathlib.Path(EVIDENCE).write_text("\n".join(report) + "\n",
                                         encoding="utf-8")
        print("\n".join(report))


if __name__ == "__main__":
    main()
