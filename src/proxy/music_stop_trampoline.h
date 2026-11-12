#pragma once
/* A guarded, method-specific trampoline preserves ALL original internal-audio cleanup.
   This is not a general instruction decoder: fail closed if the verified prologue changes. */
static void *ll_native_stop_trampoline(unsigned char *p){
    static const unsigned char head[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x3d};
    if(!p || __builtin_memcmp(p,head,sizeof(head)) || p[12]!=0 || p[13]!=0x75 || p[14]!=0x2b)return NULL;
    unsigned char *t=VirtualAlloc(NULL,128,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!t)return NULL;
    __builtin_memcpy(t,p,6);unsigned n=6;t[n++]=0x50;t[n++]=0x48;t[n++]=0xb8;
    int32_t disp;__builtin_memcpy(&disp,p+8,4);uintptr_t flag=(uintptr_t)(p+13+disp);__builtin_memcpy(t+n,&flag,8);n+=8;
    t[n++]=0x80;t[n++]=0x38;t[n++]=0;t[n++]=0x58; // cmp [flag],0; restore rax without changing flags
    t[n++]=0x74;t[n++]=14; // original JNE -> absolute taken branch, otherwise skip it
    uintptr_t target=(uintptr_t)(p+15+(int8_t)p[14]);t[n++]=0xff;t[n++]=0x25;__builtin_memset(t+n,0,4);n+=4;__builtin_memcpy(t+n,&target,8);n+=8;
    target=(uintptr_t)(p+15);t[n++]=0xff;t[n++]=0x25;__builtin_memset(t+n,0,4);n+=4;__builtin_memcpy(t+n,&target,8);n+=8;
    DWORD old;if(!VirtualProtect(t,128,PAGE_EXECUTE_READ,&old)){VirtualFree(t,0,MEM_RELEASE);return NULL;}FlushInstructionCache(GetCurrentProcess(),t,n);return t;
}
