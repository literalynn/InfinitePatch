// InfinitePatch native fixes, loaded by the game as dinput8.dll
// https://github.com/literalynn/InfinitePatch
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <share.h>
#include <vector>

static HMODULE g_self, g_real;
static wchar_t g_ini[MAX_PATH];
static bool g_fixPolling = true, g_fixSprint = true;

#ifdef IP_LOG
static void Log(const char* fmt, ...) {
    static FILE* f;
    if (!f) {
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_ini);
        wcscpy_s(wcsrchr(path, L'.'), 5, L".log");
        f = _wfsopen(path, L"w", _SH_DENYNO);
        if (!f) return;
    }
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fflush(f);
}
#else
#define Log(...) (void)0
#endif

static void LoadSettings() {
    // the dll sits in <game>\Binaries\Win32, InfinitePatch.ini in <game>
    GetModuleFileNameW(g_self, g_ini, MAX_PATH);
    for (int i = 0; i < 3; i++) *wcsrchr(g_ini, L'\\') = 0;
    wcscat_s(g_ini, L"\\InfinitePatch.ini");
    g_fixPolling = GetPrivateProfileIntW(L"Fixes", L"FixHighPollingRate", 1, g_ini) != 0;
    g_fixSprint = GetPrivateProfileIntW(L"Fixes", L"FixSprintSensitivity", 1, g_ini) != 0;
}

static FARPROC Real(const char* name) {
    if (!g_real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\dinput8.dll");
        g_real = LoadLibraryW(path);
    }
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

// --- mouse reader -----------------------------------------------------------
// The game reads the mouse through DirectInput, which handles every mouse report on the game's
// main thread. With a 1000-8000 Hz mouse that work (made worse by overlays hooking it) stalls
// the game for up to 200 ms per frame while the camera turns. Instead, the mouse is read with
// Raw Input on a separate thread, and the game gets the total movement in a handful of events.
static struct {
    CRITICAL_SECTION lock;
    std::vector<DIDEVICEOBJECTDATA> queue;  // buffered events, movement merged per axis
    LONG state[3];                           // movement since the last GetDeviceState
    BYTE buttons[8];
    DWORD buttonCount = 4, bufferSize = 0, sequence = 0;
    bool overflow = false;
    HANDLE notify = nullptr;
    HWND volatile window = nullptr;          // game window while the mouse is acquired
    LONG absX = 0, absY = 0; bool absValid = false;

    void Push(DWORD ofs, DWORD data, DWORD time) {
        if (ofs <= DIMOFS_Z) {
            for (auto it = queue.rbegin(); it != queue.rend() && it->dwOfs <= DIMOFS_Z; ++it) {
                if (it->dwOfs != ofs) continue;
                it->dwData = (DWORD)((LONG)it->dwData + (LONG)data);
                it->dwTimeStamp = time;
                return;
            }
        }
        if (bufferSize && queue.size() >= bufferSize) { queue.erase(queue.begin()); overflow = true; }
        queue.push_back({ ofs, data, time, ++sequence, 0 });
    }

    void Add(const RAWMOUSE& m) {
        HWND w = window;
        if (!w || GetForegroundWindow() != w) return;
        LONG dx = m.lLastX, dy = m.lLastY;
        if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {  // remote desktop / streaming tools send positions
            bool virt = m.usFlags & MOUSE_VIRTUAL_DESKTOP;
            LONG x = MulDiv(m.lLastX, GetSystemMetrics(virt ? SM_CXVIRTUALSCREEN : SM_CXSCREEN), 65535);
            LONG y = MulDiv(m.lLastY, GetSystemMetrics(virt ? SM_CYVIRTUALSCREEN : SM_CYSCREEN), 65535);
            dx = absValid ? x - absX : 0; dy = absValid ? y - absY : 0;
            absX = x; absY = y; absValid = true;
        }
        static const USHORT down[5] = { RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN };
        static const USHORT up[5] = { RI_MOUSE_BUTTON_1_UP, RI_MOUSE_BUTTON_2_UP, RI_MOUSE_BUTTON_3_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP };
        DWORD t = GetTickCount();
        EnterCriticalSection(&lock);
        if (dx) { Push(DIMOFS_X, dx, t); state[0] += dx; }
        if (dy) { Push(DIMOFS_Y, dy, t); state[1] += dy; }
        if (m.usButtonFlags & RI_MOUSE_WHEEL) { LONG z = (SHORT)m.usButtonData; Push(DIMOFS_Z, z, t); state[2] += z; }
        for (DWORD i = 0; i < 5 && i < buttonCount; i++) {
            if (m.usButtonFlags & down[i]) { buttons[i] = 0x80; Push(DIMOFS_BUTTON0 + i, 0x80, t); }
            if (m.usButtonFlags & up[i]) { buttons[i] = 0; Push(DIMOFS_BUTTON0 + i, 0, t); }
        }
        LeaveCriticalSection(&lock);
        if (notify) SetEvent(notify);
    }

    void Reset() {
        EnterCriticalSection(&lock);
        queue.clear(); memset(state, 0, sizeof(state)); memset(buttons, 0, sizeof(buttons));
        overflow = false; absValid = false;
        LeaveCriticalSection(&lock);
    }
} g_mouse;

static LRESULT CALLBACK RawWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        RAWINPUT ri; UINT size = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE)
            g_mouse.Add(ri.data.mouse);
    }
    return DefWindowProcW(h, msg, w, l);
}

static DWORD WINAPI RawThread(LPVOID) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = RawWndProc; wc.hInstance = g_self; wc.lpszClassName = L"InfinitePatchMouse";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_self, nullptr);
    RAWINPUTDEVICE rid = { 1, 2, RIDEV_INPUTSINK, h };  // generic desktop / mouse
    if (!h || !RegisterRawInputDevices(&rid, 1, sizeof(rid))) { Log("raw input setup failed %lu", GetLastError()); return 1; }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    return 0;
}

// --- message pump -----------------------------------------------------------
// Windows sends the game a WM_MOUSEMOVE every time the cursor moved since its last look at the
// queue. A fast 1000-8000 Hz mouse moves it between every single message, so the game's message
// loop is handed hundreds of mouse moves per frame and frames take 50-200 ms. As in Godot's fix
// (godotengine.org/article/fixing-high-polling-rate-mice-on-windows), mouse moves go out at most
// once per pass of the loop: the next one stays queued, where Windows only keeps the latest.
#ifdef IP_LOG
static LONG g_movesSent, g_movesHeld;
#define COUNT(x) InterlockedIncrement(&x)
#else
#define COUNT(x) (void)0
#endif

typedef BOOL(WINAPI* PeekMessageFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageFn g_PeekMessageA, g_PeekMessageW;

static BOOL Peek(PeekMessageFn peek, LPMSG m, HWND h, UINT min, UINT max, UINT flags) {
    static bool moveSent, ncMoveSent;
    if (min || max || !(flags & PM_REMOVE)) return peek(m, h, min, max, flags);
    if (!moveSent && !ncMoveSent) {
        if (!peek(m, h, 0, 0, flags)) return FALSE;
        moveSent = m->message == WM_MOUSEMOVE;
        ncMoveSent = m->message == WM_NCMOUSEMOVE;
        if (moveSent || ncMoveSent) COUNT(g_movesSent);
        return TRUE;
    }
    // everything but the mouse moves already sent during this pass
    UINT skip[2], n = 0;
    if (ncMoveSent) skip[n++] = WM_NCMOUSEMOVE;
    if (moveSent) skip[n++] = WM_MOUSEMOVE;
    UINT from = 0;
    for (UINT i = 0; i <= n; i++) {
        UINT to = i < n ? skip[i] - 1 : UINT_MAX;
        if (peek(m, h, from, to, flags)) {
            if (m->message == WM_MOUSEMOVE) moveSent = true;
            if (m->message == WM_NCMOUSEMOVE) ncMoveSent = true;
            return TRUE;
        }
        if (i < n) from = skip[i] + 1;
    }
#ifdef IP_LOG
    MSG probe;
    if (peek(&probe, h, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_NOREMOVE)) COUNT(g_movesHeld);
#endif
    moveSent = ncMoveSent = false;  // queue drained: the game moves on to its frame
    return FALSE;
}
static BOOL WINAPI Hook_PeekMessageA(LPMSG m, HWND h, UINT min, UINT max, UINT f) { return Peek(g_PeekMessageA, m, h, min, max, f); }
static BOOL WINAPI Hook_PeekMessageW(LPMSG m, HWND h, UINT min, UINT max, UINT f) { return Peek(g_PeekMessageW, m, h, min, max, f); }

// Replaces one of the game exe's imports, returns the original function
static void* PatchImport(const char* dll, const char* func, void* hook) {
    auto base = (BYTE*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    void* target = GetProcAddress(GetModuleHandleA(dll), func);
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); dir.VirtualAddress && imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), dll)) continue;
        auto iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        auto names = imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk) : nullptr;
        for (int i = 0; iat[i].u1.Function; i++) {
            bool match = names && !IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)
                ? !strcmp(((IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData))->Name, func)
                : (void*)iat[i].u1.Function == target;
            if (!match) continue;
            void* orig = (void*)iat[i].u1.Function;
            DWORD old;
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            iat[i].u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), old, &old);
            return orig;
        }
    }
    Log("import %s!%s not found", dll, func);
    return nullptr;
}

// --- sprint sensitivity -----------------------------------------------------
// While sprinting the game sets XPlayerInput.MouseYawScale to 0.5 and MousePitchScale to 0.7, so
// the camera suddenly turns at half speed. While a sprint is on, mouse movement is multiplied by
// the inverse of those scales, so the camera turns exactly as when walking. Offsets are for the
// Steam build of 2022-05-11, checked before patching. GetFOVAngle's address comes from
// github.com/itsloopyo/bioshock-infinite-headtracking (MIT).
static BYTE* volatile g_controller;  // the local XPlayerController, seen once per frame

// APlayerController::GetFOVAngle re-implemented as is, plus remembering `this`
static float __fastcall Hook_GetFOVAngle(BYTE* pc) {
    g_controller = pc;
    BYTE* camera = *(BYTE**)(pc + 0x240);
    return camera ? *(float*)(camera + 0x214) : *(float*)(pc + 0x284);
}

static void HookFovAngle() {
    auto base = (BYTE*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    BYTE* fn = base + 0x1D73A0;
    static const BYTE original[] = { 0x8B, 0x81, 0x40, 0x02, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x07, 0xD9, 0x80, 0x14, 0x02,
                                     0x00, 0x00, 0xC3, 0xD9, 0x81, 0x84, 0x02, 0x00, 0x00, 0xC3 };
    if (nt->FileHeader.TimeDateStamp != 0x627BE455 || nt->OptionalHeader.SizeOfImage != 0x0124F000 || memcmp(fn, original, sizeof(original))) {
        Log("unknown game build, sprint sensitivity fix disabled");
        return;
    }
    DWORD old;
    VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old);
    fn[0] = 0xE9;  // jmp Hook_GetFOVAngle
    *(INT32*)(fn + 1) = (INT32)((BYTE*)Hook_GetFOVAngle - (fn + 5));
    VirtualProtect(fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 5);
}

// the multipliers that undo the sprint's mouse scales, false when not sprinting
static bool SprintScale(float* sx, float* sy) {
    BYTE* pc = g_controller;
    if (!pc) return false;
    __try {  // the controller can be gone during a level change
        if (*(float*)(pc + 0x73C) <= *(float*)(pc + 0x748)) return false;  // LastTimeStartedSprint <= LastTimeStoppedSprint
        BYTE* input = *(BYTE**)(pc + 0x340);                                // PlayerInput
        if (!input) return false;
        float yaw = *(float*)(input + 0x294), pitch = *(float*)(input + 0x298);  // MouseYawScale, MousePitchScale
        *sx = yaw >= 0.1f && yaw < 1 ? 1 / yaw : 1;
        *sy = pitch >= 0.1f && pitch < 1 ? 1 / pitch : 1;
        return *sx != 1 || *sy != 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void ScaleMovement(DWORD cb, LPDIDEVICEOBJECTDATA out, DWORD n, float sx, float sy) {
    static float rest[2];  // keeps the fractions so slow movements are not rounded away
    for (DWORD i = 0; i < n; i++) {
        auto e = (LPDIDEVICEOBJECTDATA)((BYTE*)out + i * cb);
        if (e->dwOfs != DIMOFS_X && e->dwOfs != DIMOFS_Y) continue;
        bool y = e->dwOfs == DIMOFS_Y;
        float v = (LONG)e->dwData * (y ? sy : sx) + rest[y];
        LONG whole = (LONG)v;
        rest[y] = v - whole;
        e->dwData = (DWORD)whole;
    }
}

static void HookMessagePump() {
    if (auto f = PatchImport("user32.dll", "PeekMessageA", (void*)Hook_PeekMessageA)) g_PeekMessageA = (PeekMessageFn)f;
    if (auto f = PatchImport("user32.dll", "PeekMessageW", (void*)Hook_PeekMessageW)) g_PeekMessageW = (PeekMessageFn)f;
}

// --- DirectInput wrappers ---------------------------------------------------
// The A and W interfaces share the binary layout of every method intercepted here, so one
// wrapper serves both: the other methods only forward their arguments untouched.
struct MouseWrap : IDirectInputDevice8W {
    IDirectInputDevice8W* real;
    LONG refs = 1;
    bool raw = false, acquired = false, foreground = true;
    HWND window = nullptr;
    DWORD dataSize = 0;
#ifdef IP_LOG
    DWORD calls = 0, frames = 0, events = 0, lastLog = 0;
    double maxMs = 0;
    float maxSprintX = 1;
#endif

    explicit MouseWrap(IDirectInputDevice8W* r) : real(r) {}

    // DIERR_INPUTLOST when the game window lost focus, like DirectInput does for foreground devices
    HRESULT CheckAcquired() {
        if (!acquired) return DIERR_NOTACQUIRED;
        if (foreground && GetForegroundWindow() != window) { Unacquire(); return DIERR_INPUTLOST; }
        return DI_OK;
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* out) override {
        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8W || riid == IID_IDirectInputDevice8A) {
            *out = this; AddRef(); return S_OK;
        }
        return real->QueryInterface(riid, out);
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs); }
    STDMETHOD_(ULONG, Release)() override {
        LONG n = InterlockedDecrement(&refs);
        if (!n) { if (acquired) g_mouse.window = nullptr; real->Release(); delete this; }
        return n;
    }

    // intercepted
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT df) override {
        HRESULT hr = real->SetDataFormat(df);
        if (FAILED(hr)) return hr;
        // the standard mouse formats (what the game uses); anything else stays on DirectInput
        raw = g_fixPolling && (df->dwDataSize == sizeof(DIMOUSESTATE) || df->dwDataSize == sizeof(DIMOUSESTATE2));
        if (raw) {
            dataSize = df->dwDataSize;
            g_mouse.buttonCount = dataSize - DIMOFS_BUTTON0;
            static bool started;
            if (!started) { InitializeCriticalSection(&g_mouse.lock); CloseHandle(CreateThread(nullptr, 0, RawThread, nullptr, 0, nullptr)); started = true; }
        }
        Log("SetDataFormat size=%lu raw=%d", df->dwDataSize, raw);
        return hr;
    }
    STDMETHOD(SetProperty)(REFGUID prop, LPCDIPROPHEADER ph) override {
        if (&prop == &DIPROP_BUFFERSIZE && ph) g_mouse.bufferSize = ((LPCDIPROPDWORD)ph)->dwData;
        return real->SetProperty(prop, ph);
    }
    STDMETHOD(SetCooperativeLevel)(HWND w, DWORD flags) override {
        window = GetAncestor(w, GA_ROOT);
        foreground = !(flags & DISCL_BACKGROUND);
        return real->SetCooperativeLevel(w, flags);
    }
    STDMETHOD(Acquire)() override {
        if (!raw) return real->Acquire();
        if (foreground && GetForegroundWindow() != window) return DIERR_OTHERAPPHASPRIO;
        if (acquired) return S_FALSE;
        g_mouse.Reset();
        g_mouse.window = window;
        acquired = true;
        return DI_OK;
    }
    STDMETHOD(Unacquire)() override {
        if (!raw) return real->Unacquire();
        if (!acquired) return DI_NOEFFECT;
        acquired = false;
        g_mouse.window = nullptr;
        return DI_OK;
    }
    STDMETHOD(Poll)() override {
        if (!raw) return real->Poll();
        HRESULT hr = CheckAcquired();
        return FAILED(hr) ? hr : DI_NOEFFECT;
    }
    STDMETHOD(SetEventNotification)(HANDLE e) override {
        if (!raw) return real->SetEventNotification(e);
        g_mouse.notify = e;
        return DI_OK;
    }
    STDMETHOD(GetDeviceState)(DWORD cb, LPVOID data) override {
        if (!raw) return real->GetDeviceState(cb, data);
        if (cb != dataSize || !data) return DIERR_INVALIDPARAM;
        HRESULT hr = CheckAcquired();
        if (FAILED(hr)) return hr;
        auto s = (DIMOUSESTATE2*)data;
        EnterCriticalSection(&g_mouse.lock);
        s->lX = g_mouse.state[0]; s->lY = g_mouse.state[1]; s->lZ = g_mouse.state[2];
        memcpy(s->rgbButtons, g_mouse.buttons, g_mouse.buttonCount);
        memset(g_mouse.state, 0, sizeof(g_mouse.state));
        LeaveCriticalSection(&g_mouse.lock);
        return DI_OK;
    }
    STDMETHOD(GetDeviceData)(DWORD cb, LPDIDEVICEOBJECTDATA out, LPDWORD inout, DWORD flags) override {
        HRESULT hr = raw ? ReadEvents(cb, out, inout, flags) : real->GetDeviceData(cb, out, inout, flags);
        float sx = 1, sy = 1;
        if (g_fixSprint && SUCCEEDED(hr) && out && inout && *inout && !(flags & DIGDD_PEEK) && SprintScale(&sx, &sy))
            ScaleMovement(cb, out, *inout, sx, sy);
#ifdef IP_LOG
        if (sx > maxSprintX) maxSprintX = sx;
        DWORD got = SUCCEEDED(hr) && inout ? *inout : 0;
        calls++; events += got;
        if (!got) {  // the game reads until the buffer is empty, once per frame
            static LARGE_INTEGER freq, last; LARGE_INTEGER now;
            if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&now);
            double ms = last.QuadPart ? (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart : 0;
            if (ms > maxMs) maxMs = ms;
            last = now; frames++;
        }
        if (GetTickCount() - lastLog > 1000) {
            Log("t=%lu fps=%lu maxms=%.1f raw=%d events=%lu moves_sent=%ld moves_held=%ld sprint_x=%.2f", GetTickCount() / 1000, frames, maxMs, raw, events,
                InterlockedExchange(&g_movesSent, 0), InterlockedExchange(&g_movesHeld, 0), maxSprintX);
            lastLog = GetTickCount(); calls = frames = events = 0; maxMs = 0; maxSprintX = 1;
        }
#endif
        return hr;
    }
    HRESULT ReadEvents(DWORD cb, LPDIDEVICEOBJECTDATA out, LPDWORD inout, DWORD flags) {
        if (!inout || (out && cb < sizeof(DIDEVICEOBJECTDATA_DX3))) return DIERR_INVALIDPARAM;
        HRESULT hr = CheckAcquired();
        if (FAILED(hr)) return hr;
        EnterCriticalSection(&g_mouse.lock);
        DWORD n = std::min<DWORD>(*inout, (DWORD)g_mouse.queue.size());
        if (out)
            for (DWORD i = 0; i < n; i++) memcpy((BYTE*)out + i * cb, &g_mouse.queue[i], std::min<DWORD>(cb, sizeof(DIDEVICEOBJECTDATA)));
        if (!(flags & DIGDD_PEEK)) g_mouse.queue.erase(g_mouse.queue.begin(), g_mouse.queue.begin() + n);
        *inout = n;
        hr = g_mouse.overflow ? DI_BUFFEROVERFLOW : DI_OK;
        g_mouse.overflow = false;
        LeaveCriticalSection(&g_mouse.lock);
        return hr;
    }

    // forwarded
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS a) override { return real->GetCapabilities(a); }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKW a, LPVOID b, DWORD c) override { return real->EnumObjects(a, b, c); }
    STDMETHOD(GetProperty)(REFGUID a, LPDIPROPHEADER b) override { return real->GetProperty(a, b); }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEW a, DWORD b, DWORD c) override { return real->GetObjectInfo(a, b, c); }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEW a) override { return real->GetDeviceInfo(a); }
    STDMETHOD(RunControlPanel)(HWND a, DWORD b) override { return real->RunControlPanel(a, b); }
    STDMETHOD(Initialize)(HINSTANCE a, DWORD b, REFGUID c) override { return real->Initialize(a, b, c); }
    STDMETHOD(CreateEffect)(REFGUID a, LPCDIEFFECT b, LPDIRECTINPUTEFFECT* c, LPUNKNOWN d) override { return real->CreateEffect(a, b, c, d); }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKW a, LPVOID b, DWORD c) override { return real->EnumEffects(a, b, c); }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOW a, REFGUID b) override { return real->GetEffectInfo(a, b); }
    STDMETHOD(GetForceFeedbackState)(LPDWORD a) override { return real->GetForceFeedbackState(a); }
    STDMETHOD(SendForceFeedbackCommand)(DWORD a) override { return real->SendForceFeedbackCommand(a); }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK a, LPVOID b, DWORD c) override { return real->EnumCreatedEffectObjects(a, b, c); }
    STDMETHOD(Escape)(LPDIEFFESCAPE a) override { return real->Escape(a); }
    STDMETHOD(SendDeviceData)(DWORD a, LPCDIDEVICEOBJECTDATA b, LPDWORD c, DWORD d) override { return real->SendDeviceData(a, b, c, d); }
    STDMETHOD(EnumEffectsInFile)(LPCWSTR a, LPDIENUMEFFECTSINFILECALLBACK b, LPVOID c, DWORD d) override { return real->EnumEffectsInFile(a, b, c, d); }
    STDMETHOD(WriteEffectToFile)(LPCWSTR a, DWORD b, LPDIFILEEFFECT c, DWORD d) override { return real->WriteEffectToFile(a, b, c, d); }
    STDMETHOD(BuildActionMap)(LPDIACTIONFORMATW a, LPCWSTR b, DWORD c) override { return real->BuildActionMap(a, b, c); }
    STDMETHOD(SetActionMap)(LPDIACTIONFORMATW a, LPCWSTR b, DWORD c) override { return real->SetActionMap(a, b, c); }
    STDMETHOD(GetImageInfo)(LPDIDEVICEIMAGEINFOHEADERW a) override { return real->GetImageInfo(a); }
};

// only there to hand out the mouse wrapper
struct DInputWrap : IDirectInput8W {
    IDirectInput8W* real;
    LONG refs = 1;
    explicit DInputWrap(IDirectInput8W* r) : real(r) {}

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* out) override {
        if (riid == IID_IUnknown || riid == IID_IDirectInput8W || riid == IID_IDirectInput8A) {
            *out = this; AddRef(); return S_OK;
        }
        return real->QueryInterface(riid, out);
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs); }
    STDMETHOD_(ULONG, Release)() override {
        LONG n = InterlockedDecrement(&refs);
        if (!n) { real->Release(); delete this; }
        return n;
    }
    STDMETHOD(CreateDevice)(REFGUID guid, LPDIRECTINPUTDEVICE8W* out, LPUNKNOWN outer) override {
        HRESULT hr = real->CreateDevice(guid, out, outer);
        if (SUCCEEDED(hr) && guid == GUID_SysMouse) *out = new MouseWrap(*out);
        return hr;
    }
    STDMETHOD(EnumDevices)(DWORD a, LPDIENUMDEVICESCALLBACKW b, LPVOID c, DWORD d) override { return real->EnumDevices(a, b, c, d); }
    STDMETHOD(GetDeviceStatus)(REFGUID a) override { return real->GetDeviceStatus(a); }
    STDMETHOD(RunControlPanel)(HWND a, DWORD b) override { return real->RunControlPanel(a, b); }
    STDMETHOD(Initialize)(HINSTANCE a, DWORD b) override { return real->Initialize(a, b); }
    STDMETHOD(FindDevice)(REFGUID a, LPCWSTR b, LPGUID c) override { return real->FindDevice(a, b, c); }
    STDMETHOD(EnumDevicesBySemantics)(LPCWSTR a, LPDIACTIONFORMATW b, LPDIENUMDEVICESBYSEMANTICSCBW c, LPVOID d, DWORD e) override { return real->EnumDevicesBySemantics(a, b, c, d, e); }
    STDMETHOD(ConfigureDevices)(LPDICONFIGUREDEVICESCALLBACK a, LPDICONFIGUREDEVICESPARAMSW b, DWORD c, LPVOID d) override { return real->ConfigureDevices(a, b, c, d); }
};

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    auto create = (decltype(&Proxy_DirectInput8Create))Real("DirectInput8Create");
    if (!create) return DIERR_GENERIC;
    static bool hooked;
    if (!hooked) {
        if (g_fixPolling) HookMessagePump();
        if (g_fixSprint) HookFovAngle();
        hooked = true;
    }
    HRESULT hr = create(inst, ver, riid, out, outer);
    if (SUCCEEDED(hr) && !outer && (riid == IID_IDirectInput8W || riid == IID_IDirectInput8A)) *out = new DInputWrap((IDirectInput8W*)*out);
    return hr;
}
extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow() {
    auto f = (HRESULT(WINAPI*)())Real("DllCanUnloadNow"); return f ? f() : S_FALSE;
}
extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID a, REFIID b, LPVOID* c) {
    auto f = (HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*))Real("DllGetClassObject"); return f ? f(a, b, c) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI Proxy_DllRegisterServer() {
    auto f = (HRESULT(WINAPI*)())Real("DllRegisterServer"); return f ? f() : E_FAIL;
}
extern "C" HRESULT WINAPI Proxy_DllUnregisterServer() {
    auto f = (HRESULT(WINAPI*)())Real("DllUnregisterServer"); return f ? f() : E_FAIL;
}
extern "C" LPCDIDATAFORMAT WINAPI Proxy_GetdfDIJoystick() {
    auto f = (LPCDIDATAFORMAT(WINAPI*)())Real("GetdfDIJoystick"); return f ? f() : nullptr;
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = self;
        DisableThreadLibraryCalls(self);
        LoadSettings();
    }
    return TRUE;
}
