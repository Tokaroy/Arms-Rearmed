// Sampling profiler (Windows; a diagnostic). LARECOMP_SAMPLE=<from s>:<to s> (seconds after start): every ~1 ms a
// host thread suspends each other thread of the process and notes where it is (RIP); at <to> build/samples.txt has,
// per thread, how its samples split between modules, then the most seen addresses (module + offset) of the threads
// that ran outside ntdll / win32u (where waiting threads sit) the most. Recompiled game code is armsgame.dll's
// f_<guest address> functions: `nm -n build/armsgame.dll` names an offset. Unlike LARECOMP_FNPROF (function
// entries), this shows where the time goes - e.g. what the game's loading at start spends its seconds on.
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"

#define NBUCKET (1 << 16)
#define NTHR 256
#define NMOD 64
static struct { DWORD64 pc; DWORD tid; unsigned n; } g_b[NBUCKET];
static struct { DWORD tid; HANDLE h; unsigned n, mod[NMOD]; } g_t[NTHR];
static int g_nt;
static struct { DWORD64 base, end; char name[64]; } g_mod[NMOD];
static int g_nmod;

static void load_modules(void) {
    HMODULE mods[512];
    DWORD need = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    g_nmod = 0;
    for (int m = 0; m < (int)(need / sizeof mods[0]) && m < 512 && g_nmod < NMOD - 1; m++) {
        MODULEINFO mi;
        if (!GetModuleInformation(GetCurrentProcess(), mods[m], &mi, sizeof mi)) continue;
        g_mod[g_nmod].base = (DWORD64)mi.lpBaseOfDll;
        g_mod[g_nmod].end = g_mod[g_nmod].base + mi.SizeOfImage;
        GetModuleBaseNameA(GetCurrentProcess(), mods[m], g_mod[g_nmod].name, sizeof g_mod[g_nmod].name);
        g_nmod++;
    }
    strcpy(g_mod[g_nmod].name, "?");  // (the last slot: no module)
}
static int mod_of(DWORD64 pc) {
    for (int m = 0; m < g_nmod; m++) if (pc >= g_mod[m].base && pc < g_mod[m].end) return m;
    return g_nmod;
}
static bool waiting_mod(int m) { return !_stricmp(g_mod[m].name, "ntdll.dll") || !_stricmp(g_mod[m].name, "win32u.dll"); }

static void note(int k, DWORD64 pc) {
    int m = mod_of(pc);
    g_t[k].n++;
    g_t[k].mod[m]++;
    if (waiting_mod(m)) return;
    unsigned h = (unsigned)((pc >> 2) * 2654435761u + g_t[k].tid) & (NBUCKET - 1);
    for (int i = 0; i < NBUCKET; i++, h = (h + 1) & (NBUCKET - 1)) {
        if (g_b[h].pc == pc && g_b[h].tid == g_t[k].tid) { g_b[h].n++; break; }
        if (!g_b[h].pc) { g_b[h].pc = pc; g_b[h].tid = g_t[k].tid; g_b[h].n = 1; break; }
    }
}

static void refresh_threads(DWORD me, DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = {.dwSize = sizeof te};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        int k = 0;
        while (k < g_nt && g_t[k].tid != te.th32ThreadID) k++;
        if (k < g_nt || g_nt >= NTHR) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        g_t[g_nt].tid = te.th32ThreadID;
        g_t[g_nt].h = th;
        g_nt++;
    }
    CloseHandle(snap);
}

static int cmp_desc(const void *a, const void *b) {
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return x < y ? 1 : x > y ? -1 : 0;
}

static DWORD WINAPI sampler(LPVOID arg) {
    double from = ((double *)arg)[0], to = ((double *)arg)[1];
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    ULONGLONG t0 = GetTickCount64();
    timeBeginPeriod(1);
    while (GetTickCount64() - t0 < (ULONGLONG)(from * 1000)) Sleep(10);
    load_modules();
    unsigned rounds = 0;
    while (GetTickCount64() - t0 < (ULONGLONG)(to * 1000)) {
        if (rounds % 200 == 0) refresh_threads(me, pid);
        for (int k = 0; k < g_nt; k++) {
            if (SuspendThread(g_t[k].h) == (DWORD)-1) continue;
            CONTEXT ctx = {.ContextFlags = CONTEXT_CONTROL};
            if (GetThreadContext(g_t[k].h, &ctx)) note(k, ctx.Rip);
            ResumeThread(g_t[k].h);
        }
        rounds++;
        Sleep(1);
    }
    FILE *f = fopen("build/samples.txt", "w");
    if (f) {
        fprintf(f, "# %u rounds from %.0f to %.0f s\n# threads with samples outside ntdll / win32u:\n", rounds, from, to);
        for (int k = 0; k < g_nt; k++) {
            unsigned busy = 0;
            for (int m = 0; m <= g_nmod; m++) if (!waiting_mod(m)) busy += g_t[k].mod[m];
            if (busy * 50 < g_t[k].n) continue;  // (under 2%: idle)
            fprintf(f, "# thread %lu: %u samples, %u busy:", (unsigned long)g_t[k].tid, g_t[k].n, busy);
            for (int m = 0; m <= g_nmod; m++) if (g_t[k].mod[m]) fprintf(f, " %s %u", g_mod[m].name, g_t[k].mod[m]);
            fprintf(f, "\n");
        }
        static unsigned rows[NBUCKET][4];  // n, index
        int nr = 0;
        for (int i = 0; i < NBUCKET; i++) if (g_b[i].pc) rows[nr][0] = g_b[i].n, rows[nr][1] = (unsigned)i, nr++;
        qsort(rows, nr, sizeof rows[0], cmp_desc);
        for (int i = 0; i < nr && i < 600; i++) {
            DWORD64 pc = g_b[rows[i][1]].pc;
            int m = mod_of(pc);
            fprintf(f, "%6u thread %lu %s+%llx\n", rows[i][0], (unsigned long)g_b[rows[i][1]].tid, g_mod[m].name,
                    (unsigned long long)(pc - (m < g_nmod ? g_mod[m].base : 0)));
        }
        fclose(f);
    }
    LOG("[sample] %u rounds: build/samples.txt\n", rounds);
    return 0;
}

void sampler_start(void) {
    const char *e = getenv("LARECOMP_SAMPLE");
    if (!e) return;
    static double range[2];
    if (sscanf(e, "%lf:%lf", &range[0], &range[1]) != 2 || range[1] <= range[0]) return;
    CloseHandle(CreateThread(NULL, 0, sampler, range, 0, NULL));
}
#else
void sampler_start(void) {}
#endif
