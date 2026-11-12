#include <assert.h>
#include <stdio.h>
#include "../src/common/lilith_rebound.h"
int main(void) {
 assert(ll_rebound_candidate(-2,4,2,1000,980,0));
 assert(ll_rebound_candidate(-1,2,2,1300,1290,0));
 assert(!ll_rebound_candidate(3,2.8f,2,1000,980,0));
 assert(!ll_rebound_candidate(3,3.1f,2,1000,980,0));
 assert(!ll_rebound_candidate(-2,-1,2,1000,980,0));
 assert(!ll_rebound_candidate(-2,4,2,1000,0,0));
 assert(!ll_rebound_candidate(-2,4,2,1000,600,0));
 assert(!ll_rebound_candidate(-2,4,2,1000,980,950));
 assert(ll_rebound_candidate(-2,4,2,1000,980,400));
 assert(!ll_rebound_candidate(-2,4,0,1000,980,0));
 assert(ll_rebound_candidate(-2,4,2,10,0xfffffff0u,0));
 puts("PASS: window kick; repeated kick; ballistic flight unchanged; hand throw grace; expired/no contact; height validation; timer wrap");
}
