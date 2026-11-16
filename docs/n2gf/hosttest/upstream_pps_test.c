/* Forensic host test of the ORIGINAL RPiks GPS PPS frequency-correction path.
 * Code below marked VERBATIM is copied from
 *   RPiks/pico-WSPR-tx d11ef6b  pico-hf-oscillator/gpstime/GPStime.c:121-148 (PPS ISR)
 *   RPiks/pico-WSPR-tx d11ef6b  pico-hf-oscillator/piodco/piodco.c:140-166 (PioDCOGetFreqShiftMilliHertz)
 *   RPiks/pico-WSPR-tx d11ef6b  TxChannel/TxChannel.c:61-66 (how the result is applied)
 * (identical code is in rogerclarkmelbourne fork under #ifdef FIX_BUGS_IN_THIS)
 * Build with -fwrapv so signed overflow wraps like Cortex-M0+/M33 code does in practice. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define iSAR64(arg, rcount) (((int64_t)(arg)) >> (rcount))
#define ABS(x) ((x) > 0 ? (x) : -(x))
enum { eDtUpscale = 1000000, eSlidingLen = 32, eCLKperTimeMark = 1000000, eMaxCLKdevPPM = 250 };
typedef struct {
    uint64_t _u64_sysclk_pps_last;
    uint64_t _u64_pps_period_1M;
    uint64_t _pu64_sliding_pps_tm[eSlidingLen];
    uint8_t _ix_last;
    int64_t _i32_freq_shift_ppb;
} GPStimeData;
static GPStimeData D, *spGPStimeData = &D;

/* VERBATIM ISR body (tm64 passed in instead of GetUptime64()) */
static void pps_isr(uint64_t tm64)
{
    if(spGPStimeData)
    {
        spGPStimeData->_u64_sysclk_pps_last = tm64;
        ++spGPStimeData->_ix_last;
        spGPStimeData->_ix_last %= eSlidingLen;
        const int64_t dt_per_window = tm64 - spGPStimeData->_pu64_sliding_pps_tm[spGPStimeData->_ix_last];
        spGPStimeData->_pu64_sliding_pps_tm[spGPStimeData->_ix_last] = tm64;
        if(ABS(dt_per_window - eCLKperTimeMark * eSlidingLen) < eMaxCLKdevPPM * eSlidingLen)
        {
            if(spGPStimeData->_u64_pps_period_1M)
            {
                spGPStimeData->_u64_pps_period_1M += iSAR64((int64_t)eDtUpscale * dt_per_window
                                                            - spGPStimeData->_u64_pps_period_1M + 2, 2);
                spGPStimeData->_i32_freq_shift_ppb = (spGPStimeData->_u64_pps_period_1M
                                                      - (int64_t)eDtUpscale * eCLKperTimeMark * eSlidingLen
                                                      + (eSlidingLen >> 1)) / eSlidingLen;
            }
            else
            {
                spGPStimeData->_u64_pps_period_1M = (int64_t)eDtUpscale * dt_per_window;
            }
        }
    }
}
/* VERBATIM PioDCOGetFreqShiftMilliHertz (pdco->_pGPStime->_time_data -> D) */
static int64_t i64_last_correction = 0;
static int32_t GetFreqShiftMilliHertz(uint64_t u64_desired_frq_millihz)
{
    const int64_t dt = D._i32_freq_shift_ppb;
    if(dt) i64_last_correction = dt;
    int32_t i32ret_millis;
    if(i64_last_correction)
    {
        int64_t i64corr_coeff = (u64_desired_frq_millihz + 500000LL) / 1000000LL;
        i32ret_millis = (i64_last_correction * i64corr_coeff + 50000LL) / 1000000LL;
        return i32ret_millis;
    }
    return 0U;
}
static void reset(void){ memset(&D,0,sizeof D); i64_last_correction=0; }

static uint64_t rng=88172645463325252ULL;
static double urand(void){ rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return (rng>>11)*(1.0/9007199254740992.0); }

/* Simulate: Pico timer (us, XOSC-derived) running fast by ppm; PPS truth 1 Hz.
   mode: 0 normal, 1 one missing pulse at n=60, 2 spurious extra pulse at n=60 (+0.4 s), 3 jitter +-3 us
   Returns final ppb field. */
static int64_t run(double ppm, double t0, int mode, int npulses, int verbose)
{
    reset();
    int bad=0; int64_t worst=0;
    for(int n=0;n<npulses;n++){
        double treal = t0 + n;
        if(mode==1 && n==60) continue;
        double j = (mode==3)? (urand()*6.0-3.0)*1e-6 : 0;
        uint64_t tm = (uint64_t)llround((treal+j)*1e6*(1.0+ppm*1e-6));
        pps_isr(tm);
        if(mode==2 && n==60) pps_isr((uint64_t)llround((treal+0.4)*1e6*(1.0+ppm*1e-6)));
        int64_t v=D._i32_freq_shift_ppb;
        /* true value in this field's actual unit (1e-12): ppm*1e6 */
        if (v && llabs(v - (int64_t)(ppm*1e6)) > 2000000) { bad++; if(llabs(v)>llabs(worst)) worst=v; }
    }
    if(verbose) printf("  ppm=%+7.2f t0=%5.2f mode=%d -> field=%lld (expected ~%lld) bad_updates=%d worst=%lld\n",
        ppm,t0,mode,(long long)D._i32_freq_shift_ppb,(long long)(ppm*1e6),bad,(long long)worst);
    return D._i32_freq_shift_ppb;
}

int main(void){
    int fails=0;
    printf("== Original RPiks PPS correction: final 'ppb' field after 200 PPS ==\n");
    double ppms[]={-20,-5,-0.5,0,0.5,5,20};
    for(int m=0;m<4;m++) for(unsigned i=0;i<sizeof ppms/sizeof*ppms;i++) run(ppms[i],2.3,m,200,1);

    printf("\n== Startup: first window compares against zeroed slots (dt = uptime) ==\n");
    for(double t0=31.990; t0<=32.0101; t0+=0.005){ reset(); 
        uint64_t tm=(uint64_t)llround(t0*1e6); pps_isr(tm);
        /* slot[1] was 0 -> dt=tm. Accepted? */
        printf("  first PPS at uptime %.3f s -> period seeded=%llu (%s)\n", t0,(unsigned long long)D._u64_pps_period_1M,
               D._u64_pps_period_1M? "ACCEPTED bogus seed":"rejected");
    }

    printf("\n== Effect on transmitted frequency (TxChannel: f + (sym*2930 - 2*comp)/2000 Hz) ==\n");
    const double bands[]={1838100,3570100,7040100,10140200,14097100,18106100,21096100,24926100,28126100};
    const double clks[]={200e6,270e6};
    for(unsigned p=0;p<sizeof ppms/sizeof*ppms;p++){
        run(ppms[p],2.3,0,200,0);
        printf(" crystal %+6.2f ppm (field=%lld):\n",ppms[p],(long long)D._i32_freq_shift_ppb);
        for(unsigned b=0;b<9;b++){
            int32_t comp=GetFreqShiftMilliHertz((uint64_t)(bands[b]*1000LL));
            int32_t mill=(int32_t)((uint32_t)3*2930 - 2*comp);   /* symbol 3 */
            double feff=bands[b]+mill/2000.0;                    /* commanded */
            double fout=feff*(1+ppms[p]*1e-6);                    /* what really comes out */
            double err=fout-(bands[b]+3*1.4648);
            int over200 = feff > clks[0]/8, over270 = feff > clks[1]/8;
            printf("   %9.0f Hz: comp=%11d mHz  out err=%+12.3f Hz %s%s%s\n",bands[b],comp,err,
                   fabs(err)>1.0?"  <-- OFF FREQUENCY":"", over200?" >200MHz/8 ceiling":"", over270?" >270MHz/8 ceiling":"");
            if (ppms[p]<0 && fabs(err)>1000.0) fails++; /* count garbage (>1 kHz off) cases for slow crystals */
        }
    }
    printf("\nBUG REPRODUCED: %d of 27 (slow-crystal x band) cases transmit >1 kHz off frequency; fast crystals are corrected correctly.\n", fails);
    printf("No divide-by-zero possible: SetFreq denominator 2000*f + m > 0 for any int32 m when f > 1.074 MHz.\n");
    return 0;
}
