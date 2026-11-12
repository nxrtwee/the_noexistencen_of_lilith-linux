#pragma once
// URL percent escapes are UTF-8, not ANSI. Wine PathCreateFromUrlW alone corrupts non-ASCII escapes.
typedef HRESULT (WINAPI *ll_url_unescape_fn)(const WCHAR*,WCHAR*,DWORD*,DWORD);
static int ll_file_uri_path(const WCHAR *uri,WCHAR *output,DWORD cap,ll_url_unescape_fn unescape) {
    WCHAR decoded[LL_DESKTOP_PATH];DWORD n=LL_DESKTOP_PATH;
    if(!unescape || unescape(uri,decoded,&n,0x00040000u)<0)return 0; // URL_UNESCAPE_AS_UTF8
    WCHAR *p=decoded;
    if(!((p[0]=='f'||p[0]=='F') && (p[1]=='i'||p[1]=='I') && (p[2]=='l'||p[2]=='L') && (p[3]=='e'||p[3]=='E') && p[4]==':'))return 0;
    p+=5;
    if(p[0]=='/' && p[1]=='/') {
        p+=2;
        if(p[0] && p[0]!='/' && !(p[1]==':')) {
            const WCHAR localhost[]=L"localhost";int i=0;
            while(i<9 && p[i] && ((p[i]|32)==localhost[i]))i++;
            if(i!=9 || p[i]!='/')return 0;p+=9;
        }
    }
    if(p[0]=='/' && p[1] && p[2]==':')p++;
    if(!(p[0]=='/' || (p[0] && p[1]==':')))return 0;
    int len=lstrlenW(p);if((DWORD)len>=cap)return 0;
    for(int i=0;i<=len;i++)output[i]=p[i];return 1;
}
