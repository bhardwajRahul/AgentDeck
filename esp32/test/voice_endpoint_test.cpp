#include "../src/audio/voice_endpoint.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
using E=Audio::VoiceEndpoint;
static void room(E& e,uint16_t rms){for(int i=0;i<128;++i)e.observeNoise(rms);}
int main(){
    E e;room(e,900);
    // Wake word contaminates the latest half-window; room floor survives.
    for(int i=0;i<60;++i)e.observeNoise(5000);
    e.begin(0);assert(e.noise()==900);
    // A loud start cue never counts as speech; steady fan noise times out.
    for(uint32_t t=0;t<180;t+=16)assert(e.update(t,7000,16)==E::Result::Continue);
    for(uint32_t t=192;t<4000;t+=16)assert(e.update(t,950,16)==E::Result::Continue);
    assert(e.update(4000,950,16)==E::Result::NoSpeech);
    e.begin(0);
    // A click after the cue is not a spoken command.
    e.update(200,9000,16);e.update(216,900,16);assert(!e.heard());
    for(uint32_t t=300;t<=700;t+=16)e.update(t,2600,16);
    assert(e.heard() && e.speaking());
    // Brief within-sentence pause must not send the command.
    for(uint32_t t=716;t<1160;t+=16)assert(e.update(t,900,16)==E::Result::Continue);
    for(uint32_t t=1160;t<=1480;t+=16)assert(e.update(t,2200,16)==E::Result::Continue);
    for(uint32_t t=1496;t<2180;t+=16)assert(e.update(t,900,16)==E::Result::Continue);
    assert(e.update(2180,900,16)==E::Result::Complete);
    // A new word just before the endpoint gets its onset confirmation time.
    E boundary;room(boundary,100);boundary.begin(0);
    for(uint32_t t=200;t<=600;t+=16)boundary.update(t,700,16);
    for(uint32_t t=616;t<1290;t+=16)assert(boundary.update(t,100,16)==E::Result::Continue);
    for(uint32_t t=1290;t<=1418;t+=16)assert(boundary.update(t,700,16)==E::Result::Continue);
    assert(boundary.speaking());
    assert(boundary.update(2118,100,16)==E::Result::Complete);
    E late;room(late,100);late.begin(0);
    for(uint32_t t=3990;t<=4070;t+=16)assert(late.update(t,700,16)==E::Result::Continue);
    assert(late.heard());
    // Quiet rooms / soft speech, and unsigned millis wraparound.
    E quiet;room(quiet,40);uint32_t base=UINT32_MAX-300;quiet.begin(base);
    for(uint32_t dt=200;dt<=600;dt+=16)quiet.update(base+dt,350,16);
    assert(quiet.heard());assert(quiet.update(base+1300,45,16)==E::Result::Complete);
    puts("Endpoint: noise, wake contamination, cue, clicks, pauses, soft speech and clock wrap PASS");
}
