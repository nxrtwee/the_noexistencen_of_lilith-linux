#include <windows.h>
#include <stdint.h>
#include "../src/proxy/music_stop_trampoline.h"
extern int puts(const char*);
void mainCRTStartup(void){
 unsigned char *p=VirtualAlloc(NULL,256,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!p)ExitProcess(1);
 unsigned char code[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x3d,0,0,0,0,0,0x75,0x2b};
 __builtin_memcpy(p,code,sizeof(code));int32_t disp=127-13;__builtin_memcpy(p+8,&disp,4);
 unsigned char tail[]={0xb8,0,0,0,0,0x48,0x83,0xc4,0x20,0x5b,0xc3};
 uint32_t first=0x12345678,second=0x23456789;__builtin_memcpy(p+15,tail,sizeof(tail));__builtin_memcpy(p+16,&first,4);
 __builtin_memcpy(p+58,tail,sizeof(tail));__builtin_memcpy(p+59,&second,4);
 DWORD old;if(!VirtualProtect(p,256,PAGE_EXECUTE_READWRITE,&old))ExitProcess(2);
 uint32_t(*original)(void)=(void*)p;uint32_t(*trampoline)(void)=ll_native_stop_trampoline(p);if(!trampoline)ExitProcess(3);
 p[127]=0;if(original()!=first || trampoline()!=first)ExitProcess(4);
 p[127]=1;if(original()!=second || trampoline()!=second)ExitProcess(5);
 p[0]=0x90;if(ll_native_stop_trampoline(p)!=NULL)ExitProcess(6);
 puts("PASS: exact StopUserMusic trampoline preserves both initialization branches, stack and return value; rejects changed prologue");ExitProcess(0);
}
