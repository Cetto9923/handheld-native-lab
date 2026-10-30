#include "../../src/runtime/hle/audio_transport.h"
#include <stdio.h>
#include <stdlib.h>

static int errors;
#define CHECK(v) do {if(!(v)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#v);errors++;}}while(0)
int main(void) {
    int16_t storage[11*6]={0},source[8*6],readback[8*6];
    for(int i=0;i<8*6;i++)source[i]=(int16_t)(i-23);
    ffrs_audio_fifo q={.pcm=storage,.capacity=11,.channels=6};
    CHECK(ffrs_audio_fifo_push(&q,source,8));
    CHECK(!ffrs_audio_fifo_push(&q,source,4));
    CHECK(ffrs_audio_fifo_peek(&q,readback,8)==8 && !memcmp(source,readback,sizeof source));
    CHECK(ffrs_audio_fifo_consume(&q,5));
    CHECK(ffrs_audio_fifo_push(&q,source,8));
    CHECK(!ffrs_audio_fifo_consume(&q,12));
    CHECK(ffrs_audio_fifo_peek(&q,readback,3)==3 && !memcmp(readback,source+5*6,3*6*2));
    CHECK(ffrs_audio_fifo_consume(&q,3));
    memset(source,0,sizeof source); /* guest reuse must not erase the queue */
    CHECK(ffrs_audio_fifo_peek(&q,readback,8)==8);
    for(int i=0;i<8*6;i++)CHECK(readback[i]==i-23);
    CHECK(ffrs_audio_fifo_consume(&q,8) && ffrs_audio_fifo_size(&q)==0);
    CHECK(ffrs_audio_fifo_push(&q,source,8));
    CHECK(ffrs_audio_fifo_peek(&q,readback,8)==8 && !memcmp(source,readback,sizeof source));
    ffrs_audio_sample_clock c={0};uint64_t now=1000000000ull;
    CHECK(ffrs_audio_clock_reserve(&c,now,240,48000)==now+5000000ull);
    CHECK(c.deadline_ns>now); /* no completion on copy/append */
    for(int i=1;i<200;i++){now=c.deadline_ns;ffrs_audio_clock_reserve(&c,now,240,48000);}
    CHECK(c.deadline_ns==2000000000ull);
    /* Worker scheduling may be late; already enqueued buffers still expire on
     * their original sample timeline. Newly arriving PCM cannot expire there. */
    uint64_t old=c.deadline_ns;
    CHECK(ffrs_audio_clock_reserve(&c,old-10000000ull,240,48000)==old+5000000ull);
    now=c.deadline_ns+80000000ull; /* a new packet arriving after a real gap */
    CHECK(ffrs_audio_clock_reserve(&c,now,240,48000)==now+5000000ull);
    c=(ffrs_audio_sample_clock){0};now=3000000000ull;
    for(int i=0;i<441;i++)ffrs_audio_clock_reserve(&c,i?c.deadline_ns:now,100,44100);
    CHECK(c.deadline_ns==now+1000000000ull && c.remainder==0);
    printf("audio_transport_contract errors=%d ownership/wrap/backpressure/rate/reanchor\n",errors);
    return errors!=0;
}
