/* Host tests for the N2GF safe GPS-PPS calibration (repo/pico-hf-oscillator/gpstime/ppscal.c). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "ppscal.h"

static int fails = 0;
#define CHECK(c, ...) do { if(!(c)) { ++fails; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while(0)

static uint64_t rs = 0x9E3779B97F4A7C15ULL;
static double ur(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return (rs>>11)*(1.0/9007199254740992.0); }

/* Firmware ISR front end (GPStime.c): edges < 900 ms after the last accepted one are ignored. */
typedef struct { PPSCal c; uint64_t last; int have; } Sys;
static void edge(Sys *s, uint64_t t){ if(s->have && t - s->last < 900000) return; s->have=1; s->last=t; PPSCalOnEdge(&s->c,t); }
static uint64_t loc(double treal, double ppm){ return (uint64_t)llround(treal*1e6*(1.0+ppm*1e-6)); }

static void t_normal(void)
{
    const double ppms[] = {-60,-20,-5,-0.3,0,0.3,5,20,60};
    for(unsigned i=0;i<sizeof ppms/sizeof*ppms;i++) for(int k=0;k<3;k++){
        Sys s={0}; PPSCalReset(&s.c);
        double t0 = 0.5 + ur()*40.0;    /* includes the upstream bad case t0 ~ 32 s */
        if(k==2) t0 = 32.000;
        int32_t p=0; int st;
        for(int n=0;n<200;n++){
            double j=(ur()*6-3)*1e-6;    /* +-3 us PPS + ISR latency jitter */
            edge(&s, loc(t0+n+j, ppms[i]));
            st = PPSCalGet(&s.c, loc(t0+n,ppms[i]), &p);
            if(n<32) CHECK(st==PPSCAL_NONE, "valid before a full window (n=%d)", n);
            if(n==32) CHECK(st==PPSCAL_LOCKED, "not locked after 33 pulses");
            if(n>=32) CHECK(abs(p) <= PPSCAL_MAX_PPB, "clamp");
        }
        double err = p - ppms[i]*1000;
        CHECK(fabs(err) < 120, "ppm=%g t0=%.3f: ppb=%d err=%.0f", ppms[i], t0, p, err);
        if(k==0) printf("  normal  %+6.1f ppm (t0=%6.3f): ppb=%7d  err=%+5.0f ppb\n", ppms[i], t0, p, err);
    }
}

static void t_missing_spurious_loss(void)
{
    Sys s={0}; PPSCalReset(&s.c); const double ppm=-12.5; int32_t p=0, p_before=0; int st;
    double t=3.7;
    for(int n=0;n<1200;n++,t+=1.0){
        if(n%47==0 && n) continue;                           /* missing pulse */
        if(n%61==0) edge(&s, loc(t-0.55,ppm));               /* spurious inside the 900 ms lockout */
        if(n%89==0) { edge(&s, loc(t-0.05,ppm)); }           /* spurious that wins: real edge then filtered */
        if(n%131==0) for(int b=0;b<500;b++) edge(&s, loc(t+0.1+ur()*0.8,ppm)); /* RF burst on PPS line */
        edge(&s, loc(t+(ur()*4-2)*1e-6,ppm));
        st=PPSCalGet(&s.c, loc(t,ppm), &p);
        if(st!=PPSCAL_NONE) CHECK(fabs(p-ppm*1000) < 300, "n=%d disturbed estimate %d", n, p);
    }
    printf("  disturbed -12.5 ppm: ppb=%d  ivl-rej=%u win-rej=%u outliers=%u relock=%u\n", p,
           s.c.n_ivl_reject, s.c.n_win_reject, s.c.n_outlier, s.c.n_relock);
    CHECK(PPSCalGet(&s.c, loc(t,ppm), &p)!=PPSCAL_NONE, "lost lock in disturbed run");
    p_before=p;
    /* PPS lost for 30 minutes (e.g. GPS desensed while transmitting): hold last good value */
    st=PPSCalGet(&s.c, loc(t+1800,ppm), &p);
    CHECK(st==PPSCAL_HELD && p==p_before, "hold after PPS loss st=%d p=%d", st, p);
    /* PPS returns: re-measures after a full window, value continues */
    t+=1800; for(int n=0;n<40;n++,t+=1.0) edge(&s, loc(t,ppm));
    st=PPSCalGet(&s.c, loc(t,ppm), &p);
    CHECK(st==PPSCAL_LOCKED && fabs(p-ppm*1000)<120, "after PPS return st=%d p=%d", st, p);
    printf("  PPS lost 30 min: held %d ppb; after return: locked %d ppb\n", p_before, p);
}

static void t_out_of_range(void)
{
    const double ppms[]={-180, -120, 120, 300};
    for(unsigned i=0;i<4;i++){
        Sys s={0}; PPSCalReset(&s.c); int32_t p=12345;
        for(int n=0;n<200;n++) edge(&s, loc(5+n, ppms[i]));
        int st=PPSCalGet(&s.c, loc(205,ppms[i]), &p);
        CHECK(st==PPSCAL_NONE, "accepted absurd %g ppm (p=%d)", ppms[i], p);
        printf("  %+5.0f ppm reference: %s (falls back to CALPPM)\n", ppms[i], st==PPSCAL_NONE?"rejected":"ACCEPTED");
    }
}

static void t_step(void)
{   /* crystal warms up: -8 ppm -> -3 ppm step (beyond the 2 ppm outlier gate) */
    Sys s={0}; PPSCalReset(&s.c); int32_t p=0; double t=1.2; uint64_t tl=0; int n;
    for(n=0;n<120;n++,t+=1.0){ tl+= (uint64_t)llround(1e6*(1-8e-6)); edge(&s,tl); }
    PPSCalGet(&s.c,tl,&p); CHECK(abs(p+8000)<120,"pre-step %d",p);
    int settle=-1;
    for(n=0;n<300;n++){ tl+= (uint64_t)llround(1e6*(1-3e-6)); edge(&s,tl); PPSCalGet(&s.c,tl,&p);
        if(settle<0 && abs(p+3000)<150) settle=n; }
    CHECK(settle>0 && settle<120, "step settle %d", settle);
    printf("  step -8 -> -3 ppm: within 150 ppb after %d s (relocks=%u)\n", settle, s.c.n_relock);
}

static void t_big_uptime_and_math(void)
{
    Sys s={0}; PPSCalReset(&s.c); int32_t p=0;
    uint64_t base = 1ULL<<45;   /* ~1.1 years of uptime */
    for(int n=0;n<100;n++) edge(&s, base + (uint64_t)llround(n*1e6*(1+7.25e-6)));
    PPSCalGet(&s.c, base+100000000ULL, &p);
    CHECK(abs(p-7250)<60, "large uptime %d", p);
    /* end-to-end frequency: command f - shift, real clock fast by e => out = cmd*(1+e) */
    const double bands[]={1838100,7040100,14097100,21096100,24926100,28126100};
    double worst=0;
    for(int e=-100;e<=100;e+=5) for(int b=0;b<6;b++){
        int32_t sh = PPSCalShiftMilliHz((uint64_t)(bands[b]*1000), e*1000);
        double cmd = bands[b] - sh/1000.0, out = cmd*(1+e*1e-6), err=out-bands[b];
        if(fabs(err)>fabs(worst)) worst=err;
    }
    printf("  end-to-end residual over +-100 ppm, 160-10 m: worst %.3f Hz (rounding only)\n", worst);
    CHECK(fabs(worst) < 0.002, "residual %f", worst);
    CHECK(PPSCalShiftMilliHz(28126100000ULL, 2000000000) == PPSCalShiftMilliHz(28126100000ULL, PPSCAL_MAX_PPB), "clamp");
    CHECK(PPSCalShiftMilliHz(28126100000ULL, 20000) == 562511, "units: 20 ppm @ 28.1261 MHz = 562.511 Hz, got %d",
          PPSCalShiftMilliHz(28126100000ULL, 20000));
}

static void t_fuzz(void)
{   /* random edge streams: must never publish outside +-100 ppm or crash (run under UBSan/ASan) */
    for(int r=0;r<200;r++){
        Sys s={0}; PPSCalReset(&s.c); uint64_t t=(uint64_t)(ur()*1e8);
        for(int n=0;n<5000;n++){
            double u=ur();
            t += u<0.7 ? (uint64_t)(1000000 + (int)(ur()*400-200)) : (uint64_t)(ur()*3e6);
            edge(&s,t); int32_t p; if(PPSCalGet(&s.c,t,&p)) CHECK(abs(p)<=PPSCAL_MAX_PPB,"fuzz %d",p);
        }
    }
    printf("  fuzz: 200 x 5000 random edges, published value always within +-100 ppm\n");
}

int main(void)
{
    t_normal(); t_missing_spurious_loss(); t_out_of_range(); t_step(); t_big_uptime_and_math(); t_fuzz();
    printf(fails ? "ppscal_test: %d FAILURES\n" : "ppscal_test: PASS\n", fails);
    return fails != 0;
}
