#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
static _Atomic unsigned state[3],cur,done;
static uint32_t pixels[3][256];
static void *writer(void *unused) {
 for(unsigned frame=1;frame<=200000;frame++) {
  int index=-1;unsigned current=atomic_load_explicit(&cur,memory_order_acquire);
  for(unsigned n=1;n<=3;n++) {
   unsigned k=(current+n)%3;if(k==current)continue;unsigned expected=0;
   if(atomic_compare_exchange_strong_explicit(&state[k],&expected,1,memory_order_acquire,memory_order_relaxed)){index=k;break;}
  }
  if(index<0){frame--;sched_yield();continue;}
  for(unsigned p=0;p<256;p++)pixels[index][p]=frame;
  atomic_store_explicit(&state[index],0,memory_order_release);
  atomic_store_explicit(&cur,index,memory_order_release);
  if(!(frame%32))sched_yield();
 }
 atomic_store(&done,1);return 0;
}
int main(void) {
 pthread_t thread;assert(!pthread_create(&thread,0,writer,0));
 int held=-1;unsigned reads=0;
 while(!atomic_load(&done)) {
  unsigned index=atomic_load_explicit(&cur,memory_order_acquire),expected=0;
  if(!atomic_compare_exchange_strong_explicit(&state[index],&expected,2,memory_order_acquire,memory_order_relaxed)){sched_yield();continue;}
  int old=held;held=index;if(old>=0)atomic_store_explicit(&state[old],0,memory_order_release);
  uint32_t value=pixels[index][0];sched_yield();
  for(unsigned p=0;p<256;p++)assert(pixels[index][p]==value);
  reads++;
 }
 if(held>=0)atomic_store(&state[held],0);
 pthread_join(thread,0);assert(reads>0);printf("PASS: 200000 publications, %u reads, no torn frames\n",reads);
}
