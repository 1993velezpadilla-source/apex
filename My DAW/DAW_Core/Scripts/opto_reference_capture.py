#!/usr/bin/env python3
"""APEX Opto 2A independent, deterministic LA-2A reference capture harness.

Generate a mono 24-bit PCM reference stimulus, load it unchanged in the
licensed UAD LA-2A Gray and APEX Opto 2A, render both at 48 kHz, 24-bit
PCM WAV and compare the results with sample-accurate segment boundaries.

NEVER bundles, copies or invokes Universal Audio code. This tool is
offline research ONLY; reports errors, not a fake "1:1 certified" result.
Python 3 standard library, no third-party dependencies.
"""
import argparse
import csv
import json
import math
import pathlib
import struct
import sys
import wave

SR=48000
FULL=8388607
def rms_db(sum_squares, count):
    if not count or sum_squares<=0:
        return -240.0
    return 10.0*math.log10(sum_squares/count)

def make_segments(sr):
    # 5 ms anti-click attack/release (except memory probe transition)
    # and a full-on program burst followed by a quiet recovery probe.
    steps=[]
    def add(name,duration,db,freq,kind="sine"):
        steps.append(dict(id=name,duration_sec=duration,dbfs=db,
                          hz=freq,kind=kind,frames=int(round(sr*duration))))
    add("baseline_silence",0.5,-240,0,"silence")
    for level in (-36,-24,-18,-12,-6):
        add("1khz_{}_dbfs".format(level),1.0,level,1000)
        add("rest_after_{}_dbfs".format(level),0.4,-240,0,"silence")
    for frequency in (50,80,250,3000,8000,14000):
        add("frequency_{}_hz".format(frequency),1.0,-18,frequency)
        add("frequency_rest_{}_hz".format(frequency),0.3,-240,0,"silence")
    add("t4_preexposure",12.0,-12,1000)
    add("t4_recovery_probe",16.0,-40,1000,"probe")
    add("intermodulation_250hz_8khz",3.0,-18,250,"two_tone")
    add("end_silence",1.0,-240,0,"silence")
    total=0
    for step in steps:
        step["start_sample"]=total
        total+=step["frames"]
        step["end_sample"]=total
        step["start_sec"]=round(step["start_sample"]/sr,9)
        step["end_sec"]=round(total/sr,9)
    return steps,total

def generate(destination):
    destination=pathlib.Path(destination)
    destination.mkdir(parents=True,exist_ok=True)
    wav_path=destination/"apex_opto2a_reference_input_48k_pcm24.wav"
    meta_path=destination/"apex_opto2a_reference_manifest.json"
    segments,total=make_segments(SR)
    with wave.open(str(wav_path),"wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(3)
        writer.setframerate(SR)
        writer.setcomptype("NONE","not compressed")
        for step in segments:
            frequency=step["hz"]
            amplitude=10**(step["dbfs"]/20) if step["kind"]!="silence" else 0.0
            out=bytearray()
            for i in range(step["frames"]):
                global_index=step["start_sample"]+i
                elapsed=i/SR
                if step["kind"]=="silence":
                    v=0.0
                elif step["kind"]=="two_tone":
                    # Total signal bounded to the target peak level.
                    v=amplitude*0.5*(math.sin(2*math.pi*250*global_index/SR)
                                     +math.sin(2*math.pi*8000*global_index/SR))
                else:
                    v=amplitude*math.sin(2*math.pi*frequency*global_index/SR)
                if step["kind"] not in ("probe","silence"):
                    fade=min(1.0,elapsed/.005,
                             max(0.0,(step["frames"]-i-1)/(.005*SR)))
                    v*=max(0.0,fade)
                v=max(-1,min(1,v))
                sample=int(round(v*FULL))
                out.extend(struct.pack("<i",sample)[:3])
                if len(out)>=3*4096:
                    writer.writeframesraw(out)
                    out.clear()
            if out:
                writer.writeframesraw(out)
    manifest=dict(format="APEX-OPTO-GOLDEN-INPUT-V1",sample_rate=SR,
                  channels=1,bits=24,frames=total,
                  intended_comparison="UAD LA-2A Gray primary, same settings and gain calibration; Silver/LA-2 optional contrast only",
                  capture_notes=[
                      "Export identical wave from APEX and installed licensed UAD LA-2A Gray.",
                      "No normalize, no dither differences, no oversampling or conversions between runs.",
                      "Set identical host SR, input trim and output gain; print exact plugin version and Peak Reduction, Gain, Compress/Limit, Emphasis.",
                      "Align transport and compensate latency/sample shift BEFORE running comparison.",
                      "Capture an additional dry source path to validate source and routing.",
                      "UA software code and audio models are proprietary; no internal UA implementation accessed."
                  ],segments=segments)
    meta_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(str(wav_path))
    print(str(meta_path))
    return 0

def read_pcm(path):
    with wave.open(str(path),"rb") as wav:
        if wav.getnchannels()!=1 or wav.getsampwidth() not in (2,3) or wav.getcomptype()!="NONE":
            raise ValueError("Render as MONO PCM16/24 WAV for this independent harness")
        sr=wav.getframerate()
        bits=wav.getsampwidth()
        frames=wav.getnframes()
        payload=wav.readframes(frames)
    samples=[]
    if bits==2:
        samples=[x/32768.0 for x in struct.unpack("<"+"h"*frames,payload)]
    else:
        for i in range(0,len(payload),3):
            b=payload[i:i+3]
            sample=int.from_bytes(b,"little",signed=False)
            if sample>=8388608:sample-=16777216
            samples.append(sample/8388608.0)
    return sr,samples

def compare(manifest_path,uad_path,apex_path,destination):
    manifest=json.loads(pathlib.Path(manifest_path).read_text(encoding="utf-8"))
    if manifest.get("format")!="APEX-OPTO-GOLDEN-INPUT-V1":
        raise ValueError("Unknown fixture version")
    ur,reference=read_pcm(uad_path)
    ar,candidate=read_pcm(apex_path)
    count=manifest["frames"]
    if ur!=manifest["sample_rate"] or ar!=ur or len(reference)!=count or len(candidate)!=count:
        raise ValueError("Sample rate/length mismatch: align and export lossless exactly, without trimming or added tail")
    destination=pathlib.Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    data=[]
    for step in manifest["segments"]:
        start,end=step["start_sample"],step["end_sample"]
        # Exclude 20 ms on each side of any transition to avoid click
        # / boundary artifacts polluting steady-state error metrics.
        pad=min(int(0.020*ur),max(0,(end-start)//4))
        a=reference[start+pad:end-pad]
        b=candidate[start+pad:end-pad]
        n=len(a)
        sum_a=sum(x*x for x in a)
        sum_b=sum(x*x for x in b)
        diff=sum((x-y)**2 for x,y in zip(a,b))
        dot=sum(x*y for x,y in zip(a,b))
        correlation=dot/math.sqrt(sum_a*sum_b) if sum_a*sum_b>1e-20 else 0.0
        row=dict(section=step["id"],seconds=step["duration_sec"],
                 reference_rms_dbfs=round(rms_db(sum_a,n),5),
                 apex_rms_dbfs=round(rms_db(sum_b,n),5),
                 delta_level_db=round(rms_db(sum_b,n)-rms_db(sum_a,n),5),
                 residual_rms_dbfs=round(rms_db(diff,n),5),
                 correlation=round(correlation,6))
        data.append(row)
    with destination.open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,fieldnames=list(data[0].keys()))
        writer.writeheader()
        writer.writerows(data)
    print("Measured differences, NOT a fidelity certification:",destination)
    for item in data:
        print("{:30s} level_delta={:+8.3f}dB residual={:8.3f}dBFS".format(
              item["section"],item["delta_level_db"],item["residual_rms_dbfs"]))
    return 0

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    command=parser.add_subparsers(dest="command",required=True)
    gen=command.add_parser("generate",help="create independent 48k PCM24 source WAV + manifest")
    gen.add_argument("--output",required=True)
    c=command.add_parser("compare",help="compare licensed UAD Gray and APEX renders")
    c.add_argument("--manifest",required=True)
    c.add_argument("--uad",required=True)
    c.add_argument("--apex",required=True)
    c.add_argument("--csv",required=True)
    args=parser.parse_args(argv)
    if args.command=="generate":return generate(args.output)
    return compare(args.manifest,args.uad,args.apex,args.csv)
if __name__=="__main__":
    try:sys.exit(main())
    except (ValueError,OSError) as exc:
        print("ERROR:",exc,file=sys.stderr);sys.exit(2)
