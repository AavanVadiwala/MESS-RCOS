#include "sgp30.h"
#include <math.h>

uint8_t sgp30_crc(const uint8_t *data, size_t length) {
    uint8_t crc=0xff;
    for (size_t i=0; i<length; ++i) {
        crc ^= data[i];
        for (unsigned b=0; b<8; ++b)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1)^0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}
static int command(sgp30_t *s, uint16_t cmd, const uint16_t *args,
                   size_t nargs, uint16_t *out, size_t nout, unsigned wait_ms) {
    if (!s || !s->write || !s->read || !s->sleep_ms || nargs>2 || nout>3 ||
        (nargs && !args) || (nout && !out)) return SGP_ARGUMENT;
    uint8_t tx[8]={(uint8_t)(cmd>>8),(uint8_t)cmd}, rx[9];
    for (size_t i=0; i<nargs; ++i) {
        tx[2+i*3]=(uint8_t)(args[i]>>8); tx[3+i*3]=(uint8_t)args[i];
        tx[4+i*3]=sgp30_crc(tx+2+i*3,2);
    }
    if (s->write(s->context,tx,2+3*nargs)) return SGP_IO;
    s->sleep_ms(wait_ms);
    if (!nout) return SGP_OK;
    if (s->read(s->context,rx,3*nout)) return SGP_IO;
    /* Validate ALL words before exposing any output to the caller. */
    for (size_t i=0; i<nout; ++i)
        if (sgp30_crc(rx+3*i,2)!=rx[3*i+2]) return SGP_CRC;
    for (size_t i=0; i<nout; ++i) out[i]=(uint16_t)((rx[3*i]<<8)|rx[3*i+1]);
    return SGP_OK;
}
int sgp30_init(sgp30_t *s) { return command(s,0x2003,0,0,0,0,10); }
int sgp30_identify(sgp30_t *s, uint64_t *serial, uint16_t *features) {
    if (!serial || !features) return SGP_ARGUMENT;
    uint16_t words[3]; int e=command(s,0x3682,0,0,words,3,1);
    if(e) return e;
    *serial=((uint64_t)words[0]<<32)|((uint64_t)words[1]<<16)|words[2];
    return command(s,0x202f,0,0,features,1,10);
}
int sgp30_selftest(sgp30_t *s) {
    uint16_t v=0; int e=command(s,0x2032,0,0,&v,1,220);
    return e ? e : (v==0xd400 ? SGP_OK : SGP_SELFTEST);
}
static int pair(sgp30_t *s,uint16_t cmd,unsigned ms,uint16_t *a,uint16_t *b) {
    if(!a || !b) return SGP_ARGUMENT;
    uint16_t words[2]; int e=command(s,cmd,0,0,words,2,ms);
    if(!e) { *a=words[0]; *b=words[1]; } return e;
}
int sgp30_measure(sgp30_t *s,uint16_t *eco2,uint16_t *tvoc) {
    return pair(s,0x2008,12,eco2,tvoc);
}
int sgp30_get_baseline(sgp30_t *s,uint16_t *eco2,uint16_t *tvoc) {
    return pair(s,0x2015,10,eco2,tvoc);
}
int sgp30_set_baseline(sgp30_t *s,uint16_t eco2,uint16_t tvoc) {
    /* GET returns CO2eq,TVOC; SET requires the reverse order. */
    const uint16_t args[2]={tvoc,eco2};
    return command(s,0x201e,args,2,0,0,10);
}
int sgp30_set_humidity(sgp30_t *s,float g) {
    if(!isfinite(g) || g<0 || g>65535.0f/256.0f) return SGP_ARGUMENT;
    uint16_t fixed=(uint16_t)(g*256.0f+0.5f);
    return command(s,0x2061,&fixed,1,0,0,10);
}
