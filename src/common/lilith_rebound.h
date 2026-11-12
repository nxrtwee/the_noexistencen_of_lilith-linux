#pragma once
#include <stdint.h>
/* A positive velocity jump near a recently contacted moving support, never a hand throw. */
static int ll_rebound_candidate(float previous,float current,float height,uint32_t now,uint32_t contact,uint32_t hand) {
    if(!(height>0.01f && height<1000) || !(current>height*0.1f && current<10000))return 0;
    if(!(current-previous>height*0.12f))return 0;
    if(!contact || now-contact>350)return 0;
    if(hand && now-hand<500)return 0;
    return 1;
}
