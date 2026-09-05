#include "rs_vm_latex.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static void replace_all(char* s, size_t cap, const char* a, const char* b) {
    char tmp[4096];
    size_t al = strlen(a), bl = strlen(b);
    char* w = tmp;
    const char* r = s;
    while (*r) {
        if (al && strncmp(r, a, al) == 0) { memcpy(w, b, bl); w += bl; r += al; }
        else *w++ = *r++;
        if ((size_t)(w - tmp) >= sizeof(tmp) - 8) break;
    }
    *w = 0; strncpy(s, tmp, cap - 1); s[cap - 1] = 0;
}

static int frac_once(char* s) {
    char* p = strstr(s, "\\frac");
    if (!p) return 0;
    char* q = p + 5;
    while (*q && isspace((unsigned char)*q)) q++;
    if (*q != '{') return 0;
    int depth = 1; char* a0 = q + 1; char* a1 = a0;
    for (; *a1 && depth; a1++) { if (*a1=='{') depth++; else if (*a1=='}') depth--; }
    if (depth) return 0;
    char* aend = a1 - 1;
    while (*a1 && isspace((unsigned char)*a1)) a1++;
    if (*a1 != '{') return 0;
    depth = 1; char* b0 = a1 + 1; char* b1 = b0;
    for (; *b1 && depth; b1++) { if (*b1=='{') depth++; else if (*b1=='}') depth--; }
    if (depth) return 0;
    char num[512], den[512], tmp[4096];
    size_t nl = (size_t)(aend - a0), dl = (size_t)((b1 - 1) - b0);
    if (nl >= sizeof num || dl >= sizeof den) return 0;
    memcpy(num, a0, nl); num[nl]=0; memcpy(den, b0, dl); den[dl]=0;
    snprintf(tmp, sizeof tmp, "%.*s((%s)/(%s))%s", (int)(p-s), s, num, den, b1);
    strncpy(s, tmp, 4095); s[4095]=0; return 1;
}

static void wrap_trig(char* s) {
    char tmp[4096]; char* w = tmp; const char* r = s;
    while (*r) {
        const char* name = NULL; int nlen = 0;
        if (!strncmp(r,"sin",3) && !isalnum((unsigned char)r[3]) && r[3]!='(' && (r==s || !isalnum((unsigned char)r[-1]))) { name="sin"; nlen=3; }
        else if (!strncmp(r,"cos",3) && !isalnum((unsigned char)r[3]) && r[3]!='(' && (r==s || !isalnum((unsigned char)r[-1]))) { name="cos"; nlen=3; }
        else if (!strncmp(r,"tan",3) && !isalnum((unsigned char)r[3]) && r[3]!='(' && (r==s || !isalnum((unsigned char)r[-1]))) { name="tan"; nlen=3; }
        if (name) {
            r += nlen; while (*r && isspace((unsigned char)*r)) r++;
            memcpy(w, name, 3); w += 3; *w++ = '(';
            if (*r == '{') {
                r++; int d=1;
                while (*r && d) {
                    if (*r=='{') d++; else if (*r=='}') { d--; if (!d) { r++; break; } }
                    if (d) *w++ = *r++;
                }
            } else if (*r == '(') {
                int d=0; do { if (*r=='(') d++; else if (*r==')') d--; *w++=*r++; } while (*r && d>0);
                continue;
            } else {
                while (*r && (isalnum((unsigned char)*r) || *r=='_')) *w++ = *r++;
            }
            *w++ = ')'; continue;
        }
        *w++ = *r++;
        if ((size_t)(w-tmp) >= sizeof(tmp)-4) break;
    }
    *w=0; strncpy(s,tmp,4095); s[4095]=0;
}

int rsvm_latex_to_vulcan(const char* latex, char* out, size_t cap) {
    if (!latex || !out || cap < 16) return -1;
    char buf[4096]; strncpy(buf, latex, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    replace_all(buf,sizeof buf,"$$"," "); replace_all(buf,sizeof buf,"$"," ");
    replace_all(buf,sizeof buf,"\\["," "); replace_all(buf,sizeof buf,"\\]"," ");
    replace_all(buf,sizeof buf,"\\left",""); replace_all(buf,sizeof buf,"\\right","");
    replace_all(buf,sizeof buf,"\\cdot","*"); replace_all(buf,sizeof buf,"\\times","*");
    replace_all(buf,sizeof buf,"\\div","/"); replace_all(buf,sizeof buf,"\\pi","pi");
    replace_all(buf,sizeof buf,"\\theta","theta"); replace_all(buf,sizeof buf,"\\alpha","alpha");
    replace_all(buf,sizeof buf,"\\beta","beta"); replace_all(buf,sizeof buf,"\\omega","omega");
    replace_all(buf,sizeof buf,"\\\\","\n");
    int guard=0; while (frac_once(buf) && guard++<32) {}
    replace_all(buf,sizeof buf,"\\sin"," sin");
    replace_all(buf,sizeof buf,"\\cos"," cos");
    replace_all(buf,sizeof buf,"\\tan"," tan");
    wrap_trig(buf);
    replace_all(buf,sizeof buf,"{","("); replace_all(buf,sizeof buf,"}",")");
    out[0]=0; char work[4096]; strncpy(work,buf,sizeof work-1);
    char* save=NULL; char last[1024]="";
    for (char* line=strtok_r(work,"\n;",&save); line; line=strtok_r(NULL,"\n;",&save)) {
        while (*line && isspace((unsigned char)*line)) line++;
        char* e=line+strlen(line); while (e>line && isspace((unsigned char)e[-1])) *--e=0;
        if (!*line) continue;
        if (last[0]) { strcat(out,last); strcat(out,";\n"); }
        strncpy(last,line,sizeof last-1);
    }
    if (last[0]) {
        if (strchr(last,'=')) { strcat(out,last); strcat(out,";\n"); }
        else { strcat(out,"return "); strcat(out,last); strcat(out,";\n"); }
    }
    if (!out[0]) strncpy(out,"return 0;\n", cap-1);
    return 0;
}
