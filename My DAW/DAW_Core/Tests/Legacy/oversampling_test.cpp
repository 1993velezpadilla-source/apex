#include <cstdio>
#include <cmath>
#include <cstring>

static int gp=0, gf=0;
static FILE* flog=0;
static void ck(int c,const char* n,const char* d="") {
    if(c){fprintf(flog,"  PASS: %s %s\n",n,d);gp++;}
    else{fprintf(flog,"  FAIL: %s %s\n",n,d);gf++;}
}

struct OS {
    int F=2; float pL=0,pR=0; float buf[4096];
    void reset(){pL=pR=0;}
    void up(const float* L,const float* R,int n){
        float aL=pL,aR=pR;
        for(int i=0;i<n;i++){
            float cL=L[i],cR=R[i];
            for(int k=0;k<F;k++){
                float t=(float)k/(float)F;
                buf[i*F+k]=aL+(cL-aL)*t;
                buf[n*F+i*F+k]=aR+(cR-aR)*t;
            }
            aL=cL;aR=cR;
        }
        pL=aL;pR=aR;
    }
    void dn(float* L,float* R,int n){
        for(int i=0;i<n;i++){
            float sL=0,sR=0;
            for(int k=0;k<F;k++){sL+=buf[i*F+k];sR+=buf[n*F+i*F+k];}
            L[i]=sL/(float)F;R[i]=sR/(float)F;
        }
    }
};

int main(int argc, char* argv[]){
    const char* outPath = (argc > 1) ? argv[1] : "oversampling_results.txt";
    flog=fopen(outPath,"w");
    if(!flog) return 1;
    const int N=256,F=2;
    OS os; os.F=F;
    float sig[256],rL[256],rR[256],tL[256],tR[256];
    float me; char d[64];

    fprintf(flog,"=== Oversampling Block-Invariance Test (F=%d N=%d) ===\n\n",F,N);

    // 1. Silence
    memset(sig,0,sizeof(sig));
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();os.up(sig,sig,N/2);os.dn(tL,tR,N/2);os.up(sig+N/2,sig+N/2,N/2);os.dn(tL+N/2,tR+N/2,N/2);
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-10f,"silence",d);

    // 2. DC
    for(int i=0;i<N;i++)sig[i]=0.5f;
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();os.up(sig,sig,N/2);os.dn(tL,tR,N/2);os.up(sig+N/2,sig+N/2,N/2);os.dn(tL+N/2,tR+N/2,N/2);
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-10f,"DC",d);

    // 3. 1kHz sine, 64-sample blocks
    for(int i=0;i<N;i++)sig[i]=sinf(6.28318f*1000.0f*i/48000.0f);
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=64){int c=(p+64<=N)?64:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"1kHz 64-blk",d);

    // 4. 1kHz sine, irregular blocks {17,63,5,128,41}
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    int bs[]={17,63,5,128,41};int bi=0,p=0;
    while(p<N){int c=bs[bi%5];if(p+c>N)c=N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);p+=c;bi++;}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"1kHz irregular",d);

    // 5. Impulse, 32-sample blocks
    memset(sig,0,sizeof(sig));sig[0]=1.0f;
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=32){int c=(p+32<=N)?32:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"impulse 32-blk",d);

    // 6. Zero-cross, 32-sample blocks
    for(int i=0;i<N;i++)sig[i]=(i%2==0)?1.0f:-1.0f;
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=32){int c=(p+32<=N)?32:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"zero-cross 32-blk",d);

    // 7. Amplitude step, 64-sample blocks
    for(int i=0;i<N;i++)sig[i]=(i<64)?1.0f:((i<128)?-0.5f:0.8f);
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=64){int c=(p+64<=N)?64:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"amp-step 64-blk",d);

    // 8. 20kHz near-Nyquist, 128-sample blocks
    for(int i=0;i<N;i++)sig[i]=sinf(6.28318f*20000.0f*i/48000.0f);
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=128){int c=(p+128<=N)?128:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-5f,"20kHz 128-blk",d);

    // 9. Ramp, 64-sample blocks
    for(int i=0;i<N;i++)sig[i]=(float)i/(float)N;
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();
    for(int p=0;p<N;p+=64){int c=(p+64<=N)?64:N-p;os.up(sig+p,sig+p,c);os.dn(tL+p,tR+p,c);}
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"ramp 64-blk",d);

    // 10. Two halves WITHOUT reset should match whole (continuity preserved)
    for(int i=0;i<N;i++)sig[i]=sinf(6.28318f*1000.0f*i/48000.0f);
    os.reset();os.up(sig,sig,N);os.dn(rL,rR,N);
    os.reset();os.up(sig,sig,N/2);os.dn(tL,tR,N/2);
    os.up(sig+N/2,sig+N/2,N/2);os.dn(tL+N/2,tR+N/2,N/2);
    me=0;for(int i=0;i<N;i++)me=fmaxf(me,fabsf(rL[i]-tL[i]));
    sprintf(d,"(maxErr=%.2e)",me);ck(me<1e-6f,"two-halves-no-reset==whole",d);

    fprintf(flog,"\n=== Results: %d passed, %d failed ===\n",gp,gf);
    fclose(flog);
    return gf>0?1:0;
}
