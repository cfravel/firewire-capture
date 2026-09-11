#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#ifdef GUI_XP
extern "C" void* __cdecl memset(void* data, int value, unsigned int length) {
    BYTE* bytes = static_cast<BYTE*>(data);
    while (length-- != 0) *bytes++ = static_cast<BYTE>(value);
    return data;
}
#endif

enum ControlId {
    IdDevice = 100,
    IdFormat,
    IdTransport,
    IdTimecode,
    IdRecordingDate,
    IdPreview,
    IdRewind,
    IdStop,
    IdPlay,
    IdRecord,
    IdFastForward,
    IdOutputPath,
    IdBrowse,
    IdProgress,
    IdStatus,
};

struct GuiState {
    HINSTANCE instance;
    HWND window;
    HWND device;
    HWND format;
    HWND transport;
    HWND timecode;
    HWND recordingDate;
    HWND preview;
    HWND outputPath;
    HWND progress;
    HWND status;
    HFONT headingFont;
    HFONT normalFont;
    HBRUSH backgroundBrush;
    HBRUSH panelBrush;
    bool previewEnabled;
};

GuiState g_state = {};

void SetText(HWND control, const wchar_t* text) {
    if (control != 0) SetWindowTextW(control, text);
}

HWND MakeControl(const wchar_t* className,
                 const wchar_t* text,
                 DWORD style,
                 DWORD extendedStyle,
                 int id,
                 int x,
                 int y,
                 int width,
                 int height) {
    HWND control = CreateWindowExW(
        extendedStyle, className, text, style,
        x, y, width, height, g_state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_state.instance, 0);
    if (control != 0) {
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_state.normalFont), TRUE);
    }
    return control;
}

void SetStatus(const wchar_t* text) {
    SetText(g_state.status, text);
}

void HandleTransportKey(wchar_t key) {
    switch (key) {
    case L'r':
    case L'R':
        SetStatus(L"Rewind requested; capture backend is not connected yet.");
        break;
    case L's':
    case L'S':
        SetStatus(L"Stop requested; capture backend is not connected yet.");
        break;
    case L'p':
    case L'P':
        SetStatus(L"Play requested; capture backend is not connected yet.");
        break;
    case L'f':
    case L'F':
        SetStatus(L"Fast-forward requested; capture backend is not connected yet.");
        break;
    default:
        break;
    }
}

void BrowseForOutput() {
    wchar_t path[MAX_PATH] = {};
    GetWindowTextW(g_state.outputPath, path, ARRAYSIZE(path));
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_state.window;
    dialog.lpstrFilter = L"DV and HDV captures\0*.dv;*.m2t\0All files\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = L"dv";
    if (GetSaveFileNameW(&dialog)) {
        SetText(g_state.outputPath, path);
        SetStatus(L"Output path selected; capture backend is not connected yet.");
    }
}

void LayoutControls(int width, int height) {
    const int margin = 18;
    const int labelWidth = 120;
    const int valueX = margin + labelWidth;
    const int valueWidth = width - valueX - margin;
    const int previewTop = 148;
    const int previewHeight = height - 390;

    MoveWindow(g_state.device, valueX, 22, valueWidth, 24, TRUE);
    MoveWindow(g_state.format, valueX, 52, valueWidth / 2 - 6, 24, TRUE);
    MoveWindow(g_state.transport, valueX + valueWidth / 2 + 6, 52,
               valueWidth / 2 - 6, 24, TRUE);
    MoveWindow(g_state.timecode, valueX, 82, valueWidth / 2 - 6, 24, TRUE);
    MoveWindow(g_state.recordingDate, valueX + valueWidth / 2 + 6, 82,
               valueWidth / 2 - 6, 24, TRUE);
    MoveWindow(g_state.preview, margin, 116, 180, 24, TRUE);
    MoveWindow(GetDlgItem(g_state.window, 2000), margin, previewTop,
               width - margin * 2, previewHeight > 80 ? previewHeight : 80, TRUE);

    const int controlsTop = previewTop + (previewHeight > 80 ? previewHeight : 80) + 14;
    const int buttonWidth = (width - margin * 2 - 20) / 5;
    HWND buttons[] = {
        GetDlgItem(g_state.window, IdRewind), GetDlgItem(g_state.window, IdStop),
        GetDlgItem(g_state.window, IdPlay), GetDlgItem(g_state.window, IdRecord),
        GetDlgItem(g_state.window, IdFastForward)};
    for (int index = 0; index < 5; ++index) {
        MoveWindow(buttons[index], margin + index * (buttonWidth + 5),
                   controlsTop, buttonWidth, 30, TRUE);
    }
    const int outputTop = controlsTop + 44;
    MoveWindow(g_state.outputPath, valueX, outputTop, valueWidth - 92, 25, TRUE);
    MoveWindow(GetDlgItem(g_state.window, IdBrowse), width - margin - 82,
               outputTop, 82, 25, TRUE);
    MoveWindow(g_state.progress, margin, outputTop + 42, width - margin * 2, 22, TRUE);
    MoveWindow(g_state.status, margin, outputTop + 72, width - margin * 2, 42, TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_state.window == 0) g_state.window = window;
    switch (message) {
    case WM_CREATE: {
        g_state.headingFont = CreateFontW(
            -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        g_state.normalFont = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        g_state.backgroundBrush = CreateSolidBrush(RGB(31, 40, 52));
        g_state.panelBrush = CreateSolidBrush(RGB(8, 12, 17));

        g_state.device = MakeControl(L"STATIC", L"No FireWire camera detected",
                                      WS_CHILD | WS_VISIBLE, 0, IdDevice,
                                      138, 22, 400, 24);
        g_state.format = MakeControl(L"STATIC", L"Format: unknown",
                                     WS_CHILD | WS_VISIBLE, 0, IdFormat,
                                     138, 52, 200, 24);
        g_state.transport = MakeControl(L"STATIC", L"Transport: stopped",
                                        WS_CHILD | WS_VISIBLE, 0, IdTransport,
                                        350, 52, 200, 24);
        g_state.timecode = MakeControl(L"STATIC", L"Timecode: --:--:--:--",
                                       WS_CHILD | WS_VISIBLE, 0, IdTimecode,
                                       138, 82, 200, 24);
        g_state.recordingDate = MakeControl(L"STATIC", L"Recording date: unavailable",
                                            WS_CHILD | WS_VISIBLE, 0, IdRecordingDate,
                                            350, 82, 250, 24);
        g_state.preview = MakeControl(L"BUTTON", L"Enable video preview",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      0, IdPreview, 18, 116, 180, 24);
        MakeControl(L"STATIC", L"Preview unavailable until capture backend is connected",
                    WS_CHILD | WS_VISIBLE | SS_CENTER | SS_OWNERDRAW,
                    0, 2000, 18, 148, 700, 250);

        MakeControl(L"BUTTON", L"Rewind", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdRewind, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdStop, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdPlay, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Record", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdRecord, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Fast-forward", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdFastForward, 18, 410, 100, 30);
        MakeControl(L"STATIC", L"Output:", WS_CHILD | WS_VISIBLE,
                    0, 2100, 18, 450, 100, 24);
        g_state.outputPath = MakeControl(
            L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            WS_EX_CLIENTEDGE, IdOutputPath, 138, 450, 480, 25);
        MakeControl(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdBrowse, 630, 450, 82, 25);
        g_state.progress = MakeControl(L"STATIC", L"Capture progress: ready",
                                       WS_CHILD | WS_VISIBLE, 0, IdProgress,
                                       18, 492, 700, 22);
        g_state.status = MakeControl(L"STATIC", L"Phase 1 shell: capture backend not connected",
                                     WS_CHILD | WS_VISIBLE, 0, IdStatus,
                                     18, 522, 700, 42);
        SetWindowTextW(g_state.window, L"FireWire Capture");
        return 0;
    }
    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && wParam == L'R') {
            SetStatus(L"Record requested; capture backend is not connected yet.");
        } else {
            HandleTransportKey(static_cast<wchar_t>(wParam));
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdBrowse:
            BrowseForOutput();
            return 0;
        case IdPreview:
            g_state.previewEnabled = IsDlgButtonChecked(window, IdPreview) == BST_CHECKED;
            SetStatus(g_state.previewEnabled
                          ? L"Preview requested; capture backend is not connected yet."
                          : L"Video preview disabled.");
            return 0;
        case IdRewind:
            SetStatus(L"Rewind requested; capture backend is not connected yet.");
            return 0;
        case IdStop:
            SetStatus(L"Stop requested; capture backend is not connected yet.");
            return 0;
        case IdPlay:
            SetStatus(L"Play requested; capture backend is not connected yet.");
            return 0;
        case IdRecord:
            SetStatus(L"Record requested; capture backend is not connected yet.");
            return 0;
        case IdFastForward:
            SetStatus(L"Fast-forward requested; capture backend is not connected yet.");
            return 0;
        default:
            break;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(224, 232, 240));
        return reinterpret_cast<LRESULT>(g_state.backgroundBrush);
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(g_state.backgroundBrush);
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, g_state.backgroundBrush);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (g_state.headingFont != 0) DeleteObject(g_state.headingFont);
        if (g_state.normalFont != 0) DeleteObject(g_state.normalFont);
        if (g_state.backgroundBrush != 0) DeleteObject(g_state.backgroundBrush);
        if (g_state.panelBrush != 0) DeleteObject(g_state.panelBrush);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunGui(HINSTANCE instance, int showCommand) {
    g_state.instance = instance;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSW windowClass = {};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hCursor = LoadCursorW(0, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(31, 40, 52));
    windowClass.lpszClassName = L"FireWireCaptureGui";
    if (!RegisterClassW(&windowClass)) return 1;

    g_state.window = CreateWindowExW(
        0, windowClass.lpszClassName, L"FireWire Capture",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 680,
        0, 0, instance, 0);
    if (g_state.window == 0) return 1;
    ShowWindow(g_state.window, showCommand);
    UpdateWindow(g_state.window);

    MSG message;
    while (GetMessageW(&message, 0, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

#ifdef GUI_XP
extern "C" void WINAPI GuiXpEntry() {
    ExitProcess(static_cast<UINT>(RunGui(GetModuleHandleW(0), SW_SHOWNORMAL)));
}
#else
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    return RunGui(instance, showCommand);
}
#endif
