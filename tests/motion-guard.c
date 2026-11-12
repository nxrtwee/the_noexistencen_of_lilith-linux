#include <assert.h>
#include <stdio.h>
#include "../src/common/lilith_motion.h"
int main(void) {
 ll_motion_guard g={0};ll_window w[2]={{1,100,400,700,400,0,0},{2,900,350,500,400,0,0}},out[32];
 assert(ll_motion_update(&g,w,2,0,out)==2);
 w[0].x+=5;assert(ll_motion_update(&g,w,2,33,out)==2);
 w[0].y-=100;assert(ll_motion_update(&g,w,2,66,out)==1 && out[0].id==2 && g.events==1);
 assert(ll_motion_update(&g,w,2,99,out)==1);
 assert(ll_motion_update(&g,w,2,315,out)==1);
 assert(ll_motion_update(&g,w,2,316,out)==2);
 for(unsigned i=1;i<30;i++){w[0].x+=10;assert(ll_motion_update(&g,w,2,316+33*i,out)==2);}
 w[0].w+=120;assert(ll_motion_update(&g,w,2,1306,out)==1);
 assert(ll_motion_update(&g,w,2,1556,out)==2);
 assert(ll_motion_update(&g,w,0,1600,out)==0);
 assert(ll_motion_update(&g,w,2,1700,out)==2);
 ll_motion_guard wrap={0};w[0].x=100;assert(ll_motion_update(&wrap,w,1,0xffffffc0u,out)==1);
 w[0].x=250;assert(ll_motion_update(&wrap,w,1,0xfffffff0u,out)==0);
 assert(ll_motion_update(&wrap,w,1,233u,out)==0);
 assert(ll_motion_update(&wrap,w,1,234u,out)==1);
 puts("PASS: slow tracking; abrupt move isolation; cooldown; resize; independent window; remove/restore; timer wrap");
}
