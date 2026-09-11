#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dshow.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

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
    IdCapture,
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
IBaseFilter* g_cameraFilter = 0;
IAMExtTransport* g_transport = 0;
IAMTimecodeReader* g_timecodeReader = 0;

void SetText(HWND control, const wchar_t* text);
void SetStatus(const wchar_t* text);

void ReleaseCameraInterfaces() {
    if (g_timecodeReader != 0) {
        g_timecodeReader->Release();
        g_timecodeReader = 0;
    }
    if (g_transport != 0) {
        g_transport->Release();
        g_transport = 0;
    }
    if (g_cameraFilter != 0) {
        g_cameraFilter->Release();
        g_cameraFilter = 0;
    }
}

void FormatTwoDigits(wchar_t* output, int offset, int value) {
    output[offset] = static_cast<wchar_t>(L'0' + (value / 10) % 10);
    output[offset + 1] = static_cast<wchar_t>(L'0' + value % 10);
}

void UpdateLiveStatus();

void SendTransportCommand(long mode, const wchar_t* name) {
    if (g_transport == 0) {
        SetStatus(L"Transport is unavailable for the detected camera.");
        return;
    }
    const HRESULT hr = g_transport->put_Mode(mode);
    if (SUCCEEDED(hr)) {
        SetStatus(name);
        UpdateLiveStatus();
    } else {
        SetStatus(L"Transport command failed.");
    }
}

struct FireWireDeviceInfo {
    bool found;
    bool hdv;
    bool transport;
    wchar_t name[256];
};

void CopyText(wchar_t* destination, int capacity, const wchar_t* source) {
    if (destination == 0 || capacity <= 0) return;
    int index = 0;
    if (source != 0) {
        while (source[index] != L'\0' && index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = L'\0';
}

bool GuiContainsInsensitive(const wchar_t* text, const wchar_t* fragment) {
    if (text == 0 || fragment == 0) return false;
    for (int offset = 0; text[offset] != L'\0'; ++offset) {
        int index = 0;
        while (fragment[index] != L'\0' && text[offset + index] != L'\0') {
            wchar_t left = text[offset + index];
            wchar_t right = fragment[index];
            if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
            if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
            if (left != right) break;
            ++index;
        }
        if (fragment[index] == L'\0') return true;
    }
    return false;
}

bool GuiIsFireWirePath(const wchar_t* path) {
    return path != 0 && (GuiContainsInsensitive(path, L"61883") ||
                         GuiContainsInsensitive(path, L"1394") ||
                         GuiContainsInsensitive(path, L"avc") ||
                         GuiContainsInsensitive(path, L"firewire"));
}

bool EqualText(const wchar_t* left, const wchar_t* right) {
    if (left == 0 || right == 0) return left == right;
    int index = 0;
    while (left[index] != L'\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

void RefreshDeviceStatus() {
    ReleaseCameraInterfaces();
    FireWireDeviceInfo result = {};
    result.hdv = false;
    result.transport = false;
    CopyText(result.name, ARRAYSIZE(result.name), L"No FireWire camera detected");

    HRESULT hr = S_OK;
    ICreateDevEnum* deviceEnumerator = 0;
    if (SUCCEEDED(CoCreateInstance(
            CLSID_SystemDeviceEnum, 0, CLSCTX_INPROC_SERVER,
            IID_ICreateDevEnum, reinterpret_cast<void**>(&deviceEnumerator)))) {
        IEnumMoniker* monikers = 0;
        hr = deviceEnumerator->CreateClassEnumerator(
            CLSID_VideoInputDeviceCategory, &monikers, 0);
        if (SUCCEEDED(hr) && monikers != 0) {
            IMoniker* moniker = 0;
            while (!result.found && monikers->Next(1, &moniker, 0) == S_OK) {
                IPropertyBag* properties = 0;
                VARIANT path;
                VariantInit(&path);
                bool fireWire = false;
                if (SUCCEEDED(moniker->BindToStorage(
                        0, 0, IID_IPropertyBag,
                        reinterpret_cast<void**>(&properties))) &&
                    SUCCEEDED(properties->Read(L"DevicePath", &path, 0)) &&
                    path.vt == VT_BSTR) {
                    fireWire = GuiIsFireWirePath(path.bstrVal);
                }
                VariantClear(&path);
                if (properties != 0) properties->Release();
                if (!fireWire) {
                    moniker->Release();
                    moniker = 0;
                    continue;
                }

                IBaseFilter* filter = 0;
                if (SUCCEEDED(moniker->BindToObject(
                        0, 0, IID_IBaseFilter,
                        reinterpret_cast<void**>(&filter)))) {
                    IEnumPins* pins = 0;
                    if (SUCCEEDED(filter->EnumPins(&pins)) && pins != 0) {
                        IPin* pin = 0;
                        while (!result.found && pins->Next(1, &pin, 0) == S_OK) {
                            PIN_DIRECTION direction = PINDIR_INPUT;
                            PIN_INFO info = {};
                            pin->QueryDirection(&direction);
                            pin->QueryPinInfo(&info);
                            if (info.pFilter != 0) info.pFilter->Release();
                            if (direction == PINDIR_OUTPUT &&
                                EqualText(info.achName, L"MPEG2TS Out")) {
                                result.found = true;
                                result.hdv = true;
                            } else if (direction == PINDIR_OUTPUT &&
                                       EqualText(info.achName, L"DV A/V Out")) {
                                result.found = true;
                                result.hdv = false;
                            }
                            pin->Release();
                            pin = 0;
                        }
                        pins->Release();
                    }
                    if (result.found) {
                        IPropertyBag* deviceProperties = 0;
                        VARIANT friendlyName;
                        VariantInit(&friendlyName);
                        if (SUCCEEDED(moniker->BindToStorage(
                                0, 0, IID_IPropertyBag,
                                reinterpret_cast<void**>(&deviceProperties))) &&
                            SUCCEEDED(deviceProperties->Read(
                                L"FriendlyName", &friendlyName, 0)) &&
                            friendlyName.vt == VT_BSTR) {
                            CopyText(result.name, ARRAYSIZE(result.name),
                                     friendlyName.bstrVal);
                        }
                        VariantClear(&friendlyName);
                        if (deviceProperties != 0) deviceProperties->Release();
                        result.transport = SUCCEEDED(filter->QueryInterface(
                            IID_IAMExtTransport,
                            reinterpret_cast<void**>(&g_transport)));
                        filter->QueryInterface(
                            IID_IAMTimecodeReader,
                            reinterpret_cast<void**>(&g_timecodeReader));
                        g_cameraFilter = filter;
                        filter = 0;
                    }
                    if (filter != 0) filter->Release();
                }
                moniker->Release();
                moniker = 0;
            }
            monikers->Release();
        }
        deviceEnumerator->Release();
    }

    if (!result.found) {
        SetText(g_state.device, L"No FireWire camera detected");
        SetText(g_state.format, L"Format: unknown");
        SetText(g_state.transport, L"Transport: unavailable");
        SetStatus(L"No FireWire camera detected.");
        return;
    }
    wchar_t text[320];
    text[0] = L'\0';
    CopyText(text, ARRAYSIZE(text), L"Device: ");
    int prefixLength = 0;
    while (text[prefixLength] != L'\0') ++prefixLength;
    CopyText(text + prefixLength, ARRAYSIZE(text) - prefixLength, result.name);
    SetText(g_state.device, text);
    SetText(g_state.format, result.hdv ? L"Format: HDV" : L"Format: DV");
    SetText(g_state.transport, result.transport
                ? L"Transport: available"
                : L"Transport: unavailable");
    SetStatus(L"Camera detected; transport controls are active.");
}

void UpdateLiveStatus() {
    if (g_transport != 0) {
        long mode = ED_MODE_STOP;
        if (SUCCEEDED(g_transport->get_Mode(&mode))) {
            const wchar_t* label = L"Transport: unknown";
            if (mode == ED_MODE_STOP) label = L"Transport: stopped";
            else if (mode == ED_MODE_PLAY) label = L"Transport: playing";
            else if (mode == ED_MODE_REW) label = L"Transport: rewinding";
            else if (mode == ED_MODE_FF) label = L"Transport: fast-forwarding";
            else if (mode == ED_MODE_RECORD) label = L"Transport: recording";
            SetText(g_state.transport, label);
        }
    }
    if (g_timecodeReader != 0) {
        TIMECODE_SAMPLE sample = {};
        if (SUCCEEDED(g_timecodeReader->GetTimecode(&sample))) {
            const DWORD value = sample.timecode.dwFrames;
            const int hours = ((value >> 28) & 0x0F) * 10 + ((value >> 24) & 0x0F);
            const int minutes = ((value >> 20) & 0x0F) * 10 + ((value >> 16) & 0x0F);
            const int seconds = ((value >> 12) & 0x0F) * 10 + ((value >> 8) & 0x0F);
            const int frames = ((value >> 4) & 0x0F) * 10 + (value & 0x0F);
            wchar_t text[64] = L"Timecode: 00:00:00:00";
            FormatTwoDigits(text, 10, hours);
            FormatTwoDigits(text, 13, minutes);
            FormatTwoDigits(text, 16, seconds);
            FormatTwoDigits(text, 19, frames);
            SetText(g_state.timecode, text);
        }
    }
}

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
        SendTransportCommand(ED_MODE_REW, L"Rewind command sent.");
        break;
    case L's':
    case L'S':
        SendTransportCommand(ED_MODE_STOP, L"Stop command sent.");
        break;
    case L'p':
    case L'P':
        SendTransportCommand(ED_MODE_PLAY, L"Play command sent.");
        break;
    case L'f':
    case L'F':
        SendTransportCommand(ED_MODE_FF, L"Fast-forward command sent.");
        break;
    case L'c':
    case L'C':
        SetStatus(L"Capture requested; capture workflow is not connected yet.");
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
        GetDlgItem(g_state.window, IdPlay), GetDlgItem(g_state.window, IdCapture),
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
        MakeControl(L"BUTTON", L"Capture", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdCapture, 18, 410, 100, 30);
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
        SetTimer(g_state.window, 1, 500, 0);
        return 0;
    }
    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_KEYDOWN:
        HandleTransportKey(static_cast<wchar_t>(wParam));
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
            SendTransportCommand(ED_MODE_REW, L"Rewind command sent.");
            return 0;
        case IdStop:
            SendTransportCommand(ED_MODE_STOP, L"Stop command sent.");
            return 0;
        case IdPlay:
            SendTransportCommand(ED_MODE_PLAY, L"Play command sent.");
            return 0;
        case IdCapture:
            SetStatus(L"Capture requested; capture workflow is not connected yet.");
            return 0;
        case IdFastForward:
            SendTransportCommand(ED_MODE_FF, L"Fast-forward command sent.");
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
    case WM_TIMER:
        if (wParam == 1) UpdateLiveStatus();
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        ReleaseCameraInterfaces();
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
    const HRESULT comResult = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
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
    if (SUCCEEDED(comResult)) {
        RefreshDeviceStatus();
    } else {
        SetStatus(L"COM initialization failed; device discovery unavailable.");
    }
    ShowWindow(g_state.window, showCommand);
    UpdateWindow(g_state.window);

    MSG message;
    while (GetMessageW(&message, 0, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}

#if !defined(GUI_EMBEDDED) && defined(GUI_XP)
extern "C" void WINAPI GuiXpEntry() {
    ExitProcess(static_cast<UINT>(RunGui(GetModuleHandleW(0), SW_SHOWNORMAL)));
}
#elif !defined(GUI_EMBEDDED)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    return RunGui(instance, showCommand);
}
#endif
