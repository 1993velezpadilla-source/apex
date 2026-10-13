#!/usr/bin/env python3
"""APEX Opto 2A: independent windowed transfer + recovery diagnostic.

Compares legally captured UAD and APEX PCM render outputs with the *same*
known dry source. Never executes or bundles Universal Audio code.

This tool does NOT certify LA-2A fidelity: it measures time-dependent
effective gain, not the internal T4 voltage or UAD's VU display. Use accurate
manual sample alignment (PDC) before analysing data.
"""
import argparse
import csv
import json
import math
import pathlib
import sys

from opto_reference_capture import read_pcm

EPSILON=1e-24


def dbfs(power, count):
    return 10.0*math.log10(max(EPSILON, power/max(1, count)))


def scan_window(dry, reference, candidate, start, end):
    """Causal-free OFFLINE statistics for one exact window, no window overlap."""
    n=end-start
    a=b=c=0.0
    for i in range(start, end):
        a+=dry[i]*dry[i]
        b+=reference[i]*reference[i]
        c+=candidate[i]*candidate[i]
    drydb=dbfs(a,n)
    refdb=dbfs(b,n)
    apexdb=dbfs(c,n)
    if drydb < -80.0:
        return None  # neither gain nor recovery is observable in silence
    return dict(start_sample=start,end_sample=end,
                dry_rms_dbfs=round(drydb,5),
                uad_rms_dbfs=round(refdb,5),
                apex_rms_dbfs=round(apexdb,5),
                uad_effective_gain_db=round(refdb-drydb,5),
                apex_effective_gain_db=round(apexdb-drydb,5),
                gain_error_db=round(apexdb-refdb,5))


def compute(manifest, dry, uad, apex, window_ms=50.0):
    """Return [(stage, seconds, measurements)], comparing same source timing."""
    sr=manifest["sample_rate"]
    window=max(64,int(round(sr*window_ms*0.001)))
    total=manifest["frames"]
    if min(map(len,(dry,uad,apex)))!=total or max(map(len,(dry,uad,apex)))!=total:
        raise ValueError("All source/reference/candidate arrays must exactly match manifest frames")
    if manifest.get("format")!="APEX-OPTO-GOLDEN-INPUT-V1":
        raise ValueError("Unknown APEX golden manifest revision")
    result=[]
    for stage in manifest["segments"]:
        s,e=stage["start_sample"],stage["end_sample"]
        if s<0 or e>total or s>=e:
            raise ValueError("Invalid segment index/length in manifest")
        rows=[]
        # Window padding: ignore first/last 10ms where fades can dominate.
        pad=min(int(sr*0.010),max(0,(e-s)//6))
        cursor=s+pad
        while cursor+window<=e-pad:
            row=scan_window(dry,uad,apex,cursor,cursor+window)
            if row is not None:
                row["time_sec"]=round((cursor+window/2)/sr,5)
                row["segment"]=stage["id"]
                row["elapsed_sec"]=round((cursor+window/2-s)/sr,5)
                rows.append(row)
            cursor+=window
        result.extend(rows)
    return result


def recovery_report(rows, stage_id="t4_recovery_probe"):
    """Describe recovery relative to final stable window, removing makeup gain.

    Both UAD and APEX may have arbitrary constant output gains. Shape is
    measured as movement of their effective transfer from the last second.
    This is NOT a measured half-release time.
    """
    pool=[r for r in rows if r["segment"]==stage_id]
    if len(pool)<6:
        raise ValueError("Insufficient non-silent 50ms windows for a recovery analysis")
    anchor=max(0.0,pool[-1]["elapsed_sec"]-1.0)
    tail=[r for r in pool if r["elapsed_sec"]>=anchor]
    ref_steady=sum(r["uad_effective_gain_db"] for r in tail)/len(tail)
    apex_steady=sum(r["apex_effective_gain_db"] for r in tail)/len(tail)
    marks=[]
    for sec in (0.05,0.10,0.25,0.50,1.0,2.0,5.0,10.0,15.0):
        row=min(pool,key=lambda r:abs(r["elapsed_sec"]-sec))
        ref_delta=row["uad_effective_gain_db"]-ref_steady
        apex_delta=row["apex_effective_gain_db"]-apex_steady
        marks.append(dict(elapsed_sec=sec,
                          measured_elapsed_sec=row["elapsed_sec"],
                          uad_delta_from_end_db=round(ref_delta,4),
                          apex_delta_from_end_db=round(apex_delta,4),
                          shape_error_db=round(apex_delta-ref_delta,4)))
    return dict(reference_tail_gain_db=round(ref_steady,4),
                apex_tail_gain_db=round(apex_steady,4),
                note="Negative delta means stronger early reduction than late. "
                     "Tail is final 1s, not necessarily full equilibrium.",
                checkpoints=marks)


def analyse(args):
    manifest=json.loads(pathlib.Path(args.manifest).read_text(encoding="utf-8"))
    if manifest.get("format")!="APEX-OPTO-GOLDEN-INPUT-V1":
        raise ValueError("Unsupported reference fixture manifest")
    sampled=[read_pcm(p) for p in (args.dry,args.uad,args.apex)]
    if any(sr!=manifest["sample_rate"] for sr,_ in sampled):
        raise ValueError("All three WAVs must use the fixture sample rate")
    rows=compute(manifest,*[samples for _,samples in sampled],window_ms=args.window_ms)
    if not rows:
        raise ValueError("No valid dynamic measurement windows")
    report=recovery_report(rows)
    out=pathlib.Path(args.output)
    out.mkdir(parents=True,exist_ok=True)
    csv_path=out/"opto_effective_gain_windows.csv"
    with csv_path.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    json_path=out/"opto_recovery_comparison.json"
    json_path.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("Measurement windows:",len(rows),"reports:",csv_path,json_path)
    print("Warning: sample alignment, analog clock and control positions "
          "must be verified before interpreting errors as model fidelity.")
    return 0


def self_test():
    # 2-second synthetic probe with known decreasing attenuation; no UAD
    # proprietary audio. Exercise identity, time shape and sample-index checks.
    sr=1000
    dry=[0.25]*2000
    uad=[v*(0.2+0.65*(1-math.exp(-i/500.0))) for i,v in enumerate(dry)]
    apex=list(uad)
    stage=dict(id="t4_recovery_probe",start_sample=0,end_sample=2000)
    manifest=dict(format="APEX-OPTO-GOLDEN-INPUT-V1",sample_rate=sr,
                  frames=2000,segments=[stage])
    rows=compute(manifest,dry,uad,apex,50.0)
    assert len(rows)>10
    assert all(abs(r["gain_error_db"])<1e-8 for r in rows)
    report=recovery_report(rows)
    assert all(abs(x["shape_error_db"])<1e-8 for x in report["checkpoints"])
    assert report["checkpoints"][0]["uad_delta_from_end_db"] < -1.0
    candidate=[x*0.75 for x in uad]
    rows=compute(manifest,dry,uad,candidate,50.0)
    assert all(abs(r["gain_error_db"]-20*math.log10(.75))<1e-4 for r in rows)
    # Fixed makeup-gain mismatch must not be mistaken for release-shape error.
    shape=recovery_report(rows)
    assert all(abs(x["shape_error_db"])<1e-3 for x in shape["checkpoints"])
    try:
        compute(manifest,dry,uad,apex[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError("Must reject incorrectly aligned length")
    print("APEX reference envelope SELF-TEST GREEN (synthetic signals only)")
    return 0


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest="command",required=True)
    a=sub.add_parser("analyse",help="analyse dry, licensed UAD and experimental APEX exports")
    for name in ("manifest","dry","uad","apex","output"):
        a.add_argument("--"+name,required=True)
    a.add_argument("--window-ms",type=float,default=50.0)
    sub.add_parser("self-test",help="run synthetic no-license-required regression tests")
    args=parser.parse_args(argv)
    if args.command=="self-test":
        return self_test()
    if not math.isfinite(args.window_ms) or not 5.0 <= args.window_ms <= 1000.0:
        raise ValueError("window-ms must be between 5 and 1000")
    return analyse(args)

if __name__=="__main__":
    try:
        sys.exit(main())
    except (ValueError,OSError,KeyError) as error:
        print("ERROR:",error,file=sys.stderr)
        sys.exit(2)
