#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

static int gp=0,gf=0;
static FILE* flog=0;
static void ck(int c,const char* n,const char* d=""){
    if(c){fprintf(flog,"  PASS: %s %s\n",n,d);gp++;}
    else{fprintf(flog,"  FAIL: %s %s\n",n,d);gf++;}
}

struct Point { long long time; float value; };

// Linear reference (old code)
static float linearEval(const Point* pts, int n, long long samplePos, float def){
    if(n==0) return def;
    if(samplePos<=pts[0].time) return pts[0].value;
    if(samplePos>=pts[n-1].time) return pts[n-1].value;
    for(int i=1;i<n;i++){
        if(samplePos<=pts[i].time){
            long long span=pts[i].time-pts[i-1].time;
            if(span<=0) return pts[i].value;
            float t=(float)(samplePos-pts[i-1].time)/(float)span;
            return pts[i-1].value+t*(pts[i].value-pts[i-1].value);
        }
    }
    return def;
}

// Binary search (new code)
static float binaryEval(const Point* pts, int n, long long samplePos, float def){
    if(n==0) return def;
    if(samplePos<=pts[0].time) return pts[0].value;
    if(samplePos>=pts[n-1].time) return pts[n-1].value;
    auto it=std::upper_bound(pts,pts+n,samplePos,
        [](long long pos,const Point& pt){return pos<pt.time;});
    if(it==pts||it==pts+n) return def;
    const Point& p1=*it;
    const Point& p0=*(it-1);
    long long span=p1.time-p0.time;
    if(span<=0) return p1.value;
    float t=(float)(samplePos-p0.time)/(float)span;
    return p0.value+t*(p1.value-p0.value);
}

int main(int argc, char* argv[]){
    const char* outPath = (argc > 1) ? argv[1] : "automation_results.txt";
    flog=fopen(outPath,"w");
    if(!flog) return 1;

    fprintf(flog,"=== Automation Binary-Search Equivalence Test ===\n\n");

    // Test 1: zero points
    ck(linearEval(0,0,100,42.0f)==binaryEval(0,0,100,42.0f),"zero points","default=42");

    // Test 2: one point
    Point p1[]={ {100, 0.5f} };
    ck(linearEval(p1,1,50,0.0f)==binaryEval(p1,1,50,0.0f),"one point before","both return 0.5");
    ck(linearEval(p1,1,200,0.0f)==binaryEval(p1,1,200,0.0f),"one point after","both return 0.5");

    // Test 3: two points (linear segment)
    Point p2[]={ {0, 0.0f}, {1000, 1.0f} };
    float lv=linearEval(p2,2,500,0.0f);
    float bv=binaryEval(p2,2,500,0.0f);
    char d[64]; sprintf(d,"(lin=%.6f bin=%.6f)",lv,bv);
    ck(fabsf(lv-bv)<1e-7f,"two points midpoint",d);

    // Test 4: exact point hits
    lv=linearEval(p2,2,0,0.0f); bv=binaryEval(p2,2,0,0.0f);
    ck(fabsf(lv-bv)<1e-7f,"exact hit first","both=0.0");
    lv=linearEval(p2,2,1000,0.0f); bv=binaryEval(p2,2,1000,0.0f);
    ck(fabsf(lv-bv)<1e-7f,"exact hit last","both=1.0");

    // Test 5: before first / after last
    lv=linearEval(p2,2,-100,0.0f); bv=binaryEval(p2,2,-100,0.0f);
    ck(fabsf(lv-bv)<1e-7f,"before first","both=0.0");
    lv=linearEval(p2,2,2000,0.0f); bv=binaryEval(p2,2,2000,0.0f);
    ck(fabsf(lv-bv)<1e-7f,"after last","both=1.0");

    // Test 6: dense points (100 points, linear ramp)
    const int DENSE=100;
    Point dense[DENSE];
    for(int i=0;i<DENSE;i++) dense[i]={i*10, (float)i/(float)(DENSE-1)};
    float maxErr=0;
    for(long long s=0;s<1000;s+=3){
        lv=linearEval(dense,DENSE,s,0.0f);
        bv=binaryEval(dense,DENSE,s,0.0f);
        maxErr=fmaxf(maxErr,fabsf(lv-bv));
    }
    sprintf(d,"(maxErr=%.2e)",maxErr);
    ck(maxErr<1e-6f,"dense linear ramp",d);

    // Test 7: step function (flat segments)
    Point step[]={ {0,0.0f},{100,0.0f},{100,1.0f},{200,1.0f},{200,0.5f},{300,0.5f} };
    lv=linearEval(step,6,99,0.0f); bv=binaryEval(step,6,99,0.0f);
    ck(fabsf(lv-bv)<1e-7f,"step before jump",d);
    lv=linearEval(step,6,100,0.0f); bv=binaryEval(step,6,100,0.0f);
    sprintf(d,"(lin=%.6f bin=%.6f)",lv,bv);
    ck(fabsf(lv-bv)<1e-7f,"step at jump",d);
    lv=linearEval(step,6,150,0.0f); bv=binaryEval(step,6,150,0.0f);
    sprintf(d,"(lin=%.6f bin=%.6f)",lv,bv);
    ck(fabsf(lv-bv)<1e-7f,"step mid segment",d);

    // Test 8: large timeline positions
    Point large[]={ {0, 0.0f}, {1000000000LL, 1.0f} };
    lv=linearEval(large,2,500000000LL,0.0f); bv=binaryEval(large,2,500000000LL,0.0f);
    sprintf(d,"(lin=%.6f bin=%.6f)",lv,bv);
    ck(fabsf(lv-bv)<1e-6f,"large timeline",d);

    // Test 9: negative values
    Point neg[]={ {0, -1.0f}, {100, 0.0f}, {200, 1.0f} };
    lv=linearEval(neg,3,50,0.0f); bv=binaryEval(neg,3,50,0.0f);
    sprintf(d,"(lin=%.6f bin=%.6f)",lv,bv);
    ck(fabsf(lv-bv)<1e-7f,"negative values",d);

    // Test 10: random seed consistency (fixed pattern)
    Point randPts[20];
    for(int i=0;i<20;i++) randPts[i]={i*50, (float)((i*7+3)%11)/10.0f};
    maxErr=0;
    for(long long s=0;s<1000;s++){
        lv=linearEval(randPts,20,s,0.0f);
        bv=binaryEval(randPts,20,s,0.0f);
        maxErr=fmaxf(maxErr,fabsf(lv-bv));
    }
    sprintf(d,"(maxErr=%.2e)",maxErr);
    ck(maxErr<1e-6f,"random pattern exhaustive",d);

    fprintf(flog,"\n=== Results: %d passed, %d failed ===\n",gp,gf);
    fclose(flog);
    return gf>0?1:0;
}
