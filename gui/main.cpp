#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dshow.h>

#include "resource.h"

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
    HWND previewWindow;
    HFONT headingFont;
    HFONT normalFont;
    HFONT compactFont;
    HFONT buttonFont;
    HBRUSH backgroundBrush;
    HBRUSH panelBrush;
    bool previewEnabled;
    bool hdv;
    PROCESS_INFORMATION captureProcess;
    HANDLE captureInput;
    HANDLE captureOutput;
    wchar_t capturePath[MAX_PATH];
    DWORD captureStartTick;
    char captureOutputBuffer[8192];
    int captureOutputLength;
    wchar_t captureVideoDuration[32];
    bool hasDisplayedTimecode;
    ULONG displayedTimecodeFrames;
    bool captureRunning;
};

GuiState g_state = {};
IBaseFilter* g_cameraFilter = 0;
IAMExtTransport* g_transport = 0;
IAMTimecodeReader* g_timecodeReader = 0;
IGraphBuilder* g_previewGraph = 0;
IMediaControl* g_previewControl = 0;
IVideoWindow* g_previewVideoWindow = 0;
IBasicVideo2* g_previewBasicVideo = 0;

void SetText(HWND control, const wchar_t* text);
void SetStatus(const wchar_t* text);
void BrowseForOutput();
void CopyText(wchar_t* destination, int capacity, const wchar_t* source);
void EnsureCaptureExtension();
void DrainCaptureOutput();
void SetCaptureSummary(const wchar_t* prefix);
bool EqualText(const wchar_t* left, const wchar_t* right);
bool GuiContainsInsensitive(const wchar_t* text, const wchar_t* fragment);

void ReleaseCameraInterfaces() {
    if (g_timecodeReader != 0 && !g_state.captureRunning) {
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
void StartDvPreview();
void StopDvPreview();

IPin* FindOutputPin(IBaseFilter* filter, const wchar_t* name) {
    if (filter == 0) return 0;
    IEnumPins* pins = 0;
    if (FAILED(filter->EnumPins(&pins)) || pins == 0) return 0;
    IPin* result = 0;
    IPin* pin = 0;
    while (pins->Next(1, &pin, 0) == S_OK) {
        PIN_DIRECTION direction = PINDIR_INPUT;
        PIN_INFO info = {};
        pin->QueryDirection(&direction);
        pin->QueryPinInfo(&info);
        if (info.pFilter != 0) info.pFilter->Release();
        if (direction == PINDIR_OUTPUT && EqualText(info.achName, name)) {
            result = pin;
            break;
        }
        pin->Release();
        pin = 0;
    }
    if (pin != 0 && result == 0) pin->Release();
    pins->Release();
    return result;
}

void ResizeDvPreview() {
    if (g_previewVideoWindow == 0 || g_state.previewWindow == 0) return;
    RECT rectangle = {};
    GetClientRect(g_state.previewWindow, &rectangle);
    long aspectX = 0;
    long aspectY = 0;
    if (g_previewBasicVideo != 0) {
        g_previewBasicVideo->GetPreferredAspectRatio(&aspectX, &aspectY);
    }
    if (aspectX <= 0 || aspectY <= 0) {
        long width = 0;
        long height = 0;
        if (g_previewBasicVideo == 0 ||
            FAILED(g_previewBasicVideo->GetVideoSize(&width, &height)) ||
            width <= 0 || height <= 0) {
            aspectX = 4;
            aspectY = 3;
        } else {
            aspectX = width;
            aspectY = height;
        }
    }
    int targetWidth = rectangle.right - rectangle.left;
    int targetHeight = targetWidth * aspectY / aspectX;
    if (targetHeight > rectangle.bottom - rectangle.top) {
        targetHeight = rectangle.bottom - rectangle.top;
        targetWidth = targetHeight * aspectX / aspectY;
    }
    const int left = ((rectangle.right - rectangle.left) - targetWidth) / 2;
    const int top = ((rectangle.bottom - rectangle.top) - targetHeight) / 2;
    g_previewVideoWindow->SetWindowPosition(left, top, targetWidth, targetHeight);
}

void StartDvPreview() {
    if (g_state.hdv) {
        CheckDlgButton(g_state.window, IdPreview, BST_UNCHECKED);
        g_state.previewEnabled = false;
        SetStatus(L"HDV preview is not supported; use the camcorder display. Native capture remains available.");
        return;
    }
    if (g_cameraFilter == 0) {
        SetStatus(L"DV preview unavailable because no camera is detected.");
        CheckDlgButton(g_state.window, IdPreview, BST_UNCHECKED);
        g_state.previewEnabled = false;
        return;
    }
    StopDvPreview();
    ShowWindow(g_state.previewWindow, SW_SHOW);
    HRESULT hr = CoCreateInstance(
        CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
        reinterpret_cast<void**>(&g_previewGraph));
    if (SUCCEEDED(hr)) {
        hr = g_previewGraph->AddFilter(g_cameraFilter, L"DV preview source");
    }
    const wchar_t* previewPinName = L"DV A/V Out";
    IPin* videoPin = SUCCEEDED(hr)
                         ? FindOutputPin(g_cameraFilter, previewPinName)
                         : 0;
    if (SUCCEEDED(hr) && videoPin != 0) {
        hr = g_previewGraph->Render(videoPin);
        videoPin->Release();
    } else if (SUCCEEDED(hr)) {
        hr = VFW_E_NOT_FOUND;
    }
    if (SUCCEEDED(hr)) {
        hr = g_previewGraph->QueryInterface(
            IID_IMediaControl, reinterpret_cast<void**>(&g_previewControl));
    }
    if (SUCCEEDED(hr)) {
        hr = g_previewGraph->QueryInterface(
            IID_IVideoWindow, reinterpret_cast<void**>(&g_previewVideoWindow));
    }
    if (SUCCEEDED(hr)) {
        g_previewGraph->QueryInterface(
            IID_IBasicVideo2, reinterpret_cast<void**>(&g_previewBasicVideo));
    }
    if (SUCCEEDED(hr)) {
        g_previewVideoWindow->put_Owner(
            reinterpret_cast<OAHWND>(g_state.previewWindow));
        g_previewVideoWindow->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS);
        g_previewVideoWindow->put_Visible(OATRUE);
        ResizeDvPreview();
        hr = g_previewControl->Run();
    }
    if (FAILED(hr)) {
        StopDvPreview();
        CheckDlgButton(g_state.window, IdPreview, BST_UNCHECKED);
        g_state.previewEnabled = false;
        SetStatus(L"DV preview could not be started; capture remains available.");
        return;
    }
    SetStatus(L"DV preview is running.");
}

void StopDvPreview() {
    if (g_previewVideoWindow != 0) {
        g_previewVideoWindow->put_Visible(OAFALSE);
        g_previewVideoWindow->put_Owner(0);
        g_previewVideoWindow->Release();
        g_previewVideoWindow = 0;
    }
    if (g_previewBasicVideo != 0) {
        g_previewBasicVideo->Release();
        g_previewBasicVideo = 0;
    }
    if (g_previewControl != 0) {
        g_previewControl->Stop();
        g_previewControl->Release();
        g_previewControl = 0;
    }
    if (g_previewGraph != 0) {
        g_previewGraph->Release();
        g_previewGraph = 0;
    }
    ShowWindow(g_state.previewWindow, SW_HIDE);
}

void StopGuiCapture() {
    if (!g_state.captureRunning) return;
    const char stop = '\n';
    DWORD written = 0;
    if (g_state.captureInput != 0) {
        WriteFile(g_state.captureInput, &stop, 1, &written, 0);
    }
    if (g_state.captureProcess.hProcess != 0) {
        if (WaitForSingleObject(g_state.captureProcess.hProcess, 5000) == WAIT_TIMEOUT) {
            TerminateProcess(g_state.captureProcess.hProcess, 1);
        }
        CloseHandle(g_state.captureProcess.hThread);
        CloseHandle(g_state.captureProcess.hProcess);
    }
    DrainCaptureOutput();
    if (g_state.captureInput != 0) CloseHandle(g_state.captureInput);
    if (g_state.captureOutput != 0) CloseHandle(g_state.captureOutput);
    g_state.captureProcess = PROCESS_INFORMATION();
    g_state.captureInput = 0;
    g_state.captureOutput = 0;
    g_state.captureRunning = false;
    EnableWindow(g_state.preview, TRUE);
    SetCaptureSummary(L"Capture finalized;");
}

void UpdateCaptureStatus() {
    if (!g_state.captureRunning) return;
    DrainCaptureOutput();
    if (WaitForSingleObject(g_state.captureProcess.hProcess, 0) != WAIT_TIMEOUT) {
        DWORD exitCode = 1;
        GetExitCodeProcess(g_state.captureProcess.hProcess, &exitCode);
        StopGuiCapture();
        if (exitCode == 0) {
            SetStatus(L"Capture process finished successfully.");
        } else {
            SetStatus(L"Capture process failed; see the final file/partial-file state.");
        }
        return;
    }
    LARGE_INTEGER size = {};
    HANDLE output = CreateFileW(g_state.capturePath, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    const bool hasSize = output != INVALID_HANDLE_VALUE &&
                         GetFileSizeEx(output, &size) != FALSE;
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (hasSize) {
        wchar_t progress[160] = L"Capture running; video duration: ";
        int offset = 0;
        while (progress[offset] != L'\0') ++offset;
        CopyText(progress + offset, ARRAYSIZE(progress) - offset,
                 g_state.captureVideoDuration[0] != L'\0'
                     ? g_state.captureVideoDuration : L"unknown");
        offset = 0;
        while (progress[offset] != L'\0') ++offset;
        CopyText(progress + offset, ARRAYSIZE(progress) - offset, L"; bytes: ");
        offset = 0;
        while (progress[offset] != L'\0') ++offset;
        wchar_t digits[32];
        ULONGLONG value = static_cast<ULONGLONG>(size.QuadPart);
        int count = 0;
        do {
            digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
            value /= 10;
        } while (value != 0 && count < ARRAYSIZE(digits));
        while (count != 0 && offset + 1 < ARRAYSIZE(progress)) {
            progress[offset++] = digits[--count];
        }
        progress[offset] = L'\0';
        SetText(g_state.progress, progress);
    } else {
        SetText(g_state.progress, L"Capture running; waiting for output data.");
    }
}

const char* FindAscii(const char* text, int length, const char* search) {
    int searchLength = 0;
    while (search[searchLength] != '\0') ++searchLength;
    for (int offset = 0; offset + searchLength <= length; ++offset) {
        int index = 0;
        while (index < searchLength && text[offset + index] == search[index]) ++index;
        if (index == searchLength) return text + offset;
    }
    return 0;
}

void ProcessCaptureOutputLine(const char* line, int length) {
    const char* timecode = FindAscii(line, length, "Timecode ");
    if (timecode != 0) {
        timecode += 9;
        wchar_t display[32] = L"Timecode: --:--:--:--";
        int index = 0;
        int digits[8] = {};
        while (index < 11 && timecode + index < line + length) {
            const char character = timecode[index];
            display[10 + index] = static_cast<unsigned char>(character);
            if (index == 0 || index == 1 || index == 3 || index == 4 ||
                index == 6 || index == 7 || index == 9 || index == 10) {
                if (character < '0' || character > '9') break;
                const int digitIndex = index - (index >= 3 ? 1 : 0) -
                                       (index >= 6 ? 1 : 0) -
                                       (index >= 9 ? 1 : 0);
                digits[digitIndex] = character - '0';
            }
            ++index;
        }
        if (index == 11) {
            const ULONG frames = (((digits[0] * 10 + digits[1]) * 60 +
                                   digits[2] * 10 + digits[3]) * 60 +
                                  digits[4] * 10 + digits[5]) * 30 +
                                 digits[6] * 10 + digits[7];
            if (!g_state.captureRunning || !g_state.hasDisplayedTimecode ||
                frames >= g_state.displayedTimecodeFrames) {
                g_state.hasDisplayedTimecode = true;
                g_state.displayedTimecodeFrames = frames;
                SetText(g_state.timecode, display);
            }
        }
    }
    const char* duration = FindAscii(line, length, "Duration ");
    if (duration != 0) {
        duration += 9;
        int index = 0;
        while (index < 8 && duration + index < line + length) {
            g_state.captureVideoDuration[index] =
                static_cast<unsigned char>(duration[index]);
            ++index;
        }
        if (index == 8) {
            g_state.captureVideoDuration[index] = L'\0';
            wchar_t progress[128] = L"Video duration: ";
            CopyText(progress + 16, ARRAYSIZE(progress) - 16,
                     g_state.captureVideoDuration);
            SetText(g_state.progress, progress);
        }
    }
}

void DrainCaptureOutput() {
    if (g_state.captureOutput == 0) return;
    DWORD available = 0;
    if (!PeekNamedPipe(g_state.captureOutput, 0, 0, 0, &available, 0) ||
        available == 0) return;
    char data[1024];
    DWORD read = 0;
    if (!ReadFile(g_state.captureOutput, data,
                  available < sizeof(data) ? available : sizeof(data),
                  &read, 0)) return;
    for (DWORD index = 0; index < read; ++index) {
        const char character = data[index];
        if (character == '\r' || character == '\n') {
            if (g_state.captureOutputLength != 0) {
                ProcessCaptureOutputLine(g_state.captureOutputBuffer,
                                         g_state.captureOutputLength);
                g_state.captureOutputLength = 0;
            }
        } else if (g_state.captureOutputLength + 1 <
                   ARRAYSIZE(g_state.captureOutputBuffer)) {
            g_state.captureOutputBuffer[g_state.captureOutputLength++] = character;
        }
    }
}

void SetCaptureSummary(const wchar_t* prefix) {
    LARGE_INTEGER size = {};
    HANDLE output = CreateFileW(g_state.capturePath, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    const bool hasSize = output != INVALID_HANDLE_VALUE &&
                         GetFileSizeEx(output, &size) != FALSE;
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    wchar_t summary[256] = {};
    CopyText(summary, ARRAYSIZE(summary), prefix);
    int offset = 0;
    while (summary[offset] != L'\0') ++offset;
    CopyText(summary + offset, ARRAYSIZE(summary) - offset,
             L" video duration: ");
    offset = 0;
    while (summary[offset] != L'\0') ++offset;
    CopyText(summary + offset, ARRAYSIZE(summary) - offset,
             g_state.captureVideoDuration[0] != L'\0'
                 ? g_state.captureVideoDuration : L"unknown");
    offset = 0;
    while (summary[offset] != L'\0') ++offset;
    CopyText(summary + offset, ARRAYSIZE(summary) - offset, L" bytes: ");
    offset = 0;
    while (summary[offset] != L'\0') ++offset;
    ULONGLONG value = hasSize ? static_cast<ULONGLONG>(size.QuadPart) : 0;
    wchar_t digits[32];
    int count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value != 0 && count < ARRAYSIZE(digits));
    while (count != 0 && offset + 1 < ARRAYSIZE(summary)) {
        summary[offset++] = digits[--count];
    }
    CopyText(summary + offset, ARRAYSIZE(summary) - offset, L" duration: ");
    while (summary[offset] != L'\0') ++offset;
    value = (GetTickCount() - g_state.captureStartTick) / 1000;
    count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value != 0 && count < ARRAYSIZE(digits));
    while (count != 0 && offset + 1 < ARRAYSIZE(summary)) {
        summary[offset++] = digits[--count];
    }
    CopyText(summary + offset, ARRAYSIZE(summary) - offset, L"s");
    while (summary[offset] != L'\0') ++offset;
    summary[offset] = L'\0';
    SetText(g_state.progress, summary);
}

void StartGuiCapture() {
    if (g_state.captureRunning) {
        SetStatus(L"Capture is already running.");
        return;
    }
    GetWindowTextW(g_state.outputPath, g_state.capturePath,
                   ARRAYSIZE(g_state.capturePath));
    if (g_state.capturePath[0] == L'\0') {
        long mode = ED_MODE_STOP;
        if (g_transport != 0 && SUCCEEDED(g_transport->get_Mode(&mode)) &&
            mode == ED_MODE_PLAY) {
            SetStatus(L"The tape is playing. Choose an output filename before starting capture.");
            return;
        }
        BrowseForOutput();
        GetWindowTextW(g_state.outputPath, g_state.capturePath,
                       ARRAYSIZE(g_state.capturePath));
    }
    if (g_state.capturePath[0] == L'\0') {
        SetStatus(L"Choose an output file before starting capture.");
        return;
    }
    EnsureCaptureExtension();
    long mode = ED_MODE_STOP;
    if (g_transport != 0 && SUCCEEDED(g_transport->get_Mode(&mode)) &&
        mode == ED_MODE_PLAY) {
        const int answer = MessageBoxW(
            g_state.window,
            L"The tape is currently playing. Capture from the current tape position?",
            L"Capture while playing",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDYES) {
            SetStatus(L"Capture cancelled; tape position was not changed.");
            return;
        }
    }
    bool overwrite = false;
    wchar_t partialPath[MAX_PATH];
    CopyText(partialPath, ARRAYSIZE(partialPath), g_state.capturePath);
    int partialLength = 0;
    while (partialPath[partialLength] != L'\0') ++partialLength;
    CopyText(partialPath + partialLength,
             ARRAYSIZE(partialPath) - partialLength, L".partial");
    const bool finalExists =
        GetFileAttributesW(g_state.capturePath) != INVALID_FILE_ATTRIBUTES;
    const bool partialExists =
        GetFileAttributesW(partialPath) != INVALID_FILE_ATTRIBUTES;
    if (finalExists || partialExists) {
        const int answer = MessageBoxW(
            g_state.window,
            partialExists
                ? L"An unfinished partial capture already exists. Delete it and start a new capture?"
                : L"The selected capture file already exists. Replace it?",
            partialExists ? L"Partial capture exists" : L"Capture file exists",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (answer != IDYES) {
            SetStatus(L"Capture cancelled; existing file was preserved.");
            return;
        }
        if (partialExists && !DeleteFileW(partialPath)) {
            SetStatus(L"Unable to remove the previous partial capture.");
            return;
        }
        overwrite = true;
    }

    const bool previewStopped = g_state.previewEnabled;
    if (previewStopped) {
        StopDvPreview();
        CheckDlgButton(g_state.window, IdPreview, BST_UNCHECKED);
        g_state.previewEnabled = false;
        SetStatus(L"Preview is unavailable during capture; use the camcorder display.");
    }
    if (previewStopped) EnableWindow(g_state.preview, FALSE);

    wchar_t executablePath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileNameW(
        0, executablePath, ARRAYSIZE(executablePath));
    if (pathLength == 0 || pathLength >= ARRAYSIZE(executablePath)) {
        SetStatus(L"Unable to locate the capture executable.");
        return;
    }
    wchar_t commandLine[2048] = L"\"";
    CopyText(commandLine + 1, ARRAYSIZE(commandLine) - 1, executablePath);
    int length = 1;
    while (commandLine[length] != L'\0') ++length;
    CopyText(commandLine + length, ARRAYSIZE(commandLine) - length,
             overwrite ? L"\" -v --overwrite \"" : L"\" -v \"");
    length = 0;
    while (commandLine[length] != L'\0') ++length;
    CopyText(commandLine + length, ARRAYSIZE(commandLine) - length,
             g_state.capturePath);
    length = 0;
    while (commandLine[length] != L'\0') ++length;
    CopyText(commandLine + length, ARRAYSIZE(commandLine) - length, L"\"");

    SECURITY_ATTRIBUTES security = {sizeof(security), 0, TRUE};
    HANDLE childInput = 0;
    HANDLE parentInput = 0;
    HANDLE childOutput = 0;
    HANDLE parentOutput = 0;
    if (!CreatePipe(&childInput, &parentInput, &security, 0) ||
        !CreatePipe(&parentOutput, &childOutput, &security, 0)) {
        if (childInput != 0) CloseHandle(childInput);
        if (parentInput != 0) CloseHandle(parentInput);
        if (childOutput != 0) CloseHandle(childOutput);
        if (parentOutput != 0) CloseHandle(parentOutput);
        SetStatus(L"Unable to create capture control pipe.");
        EnableWindow(g_state.preview, TRUE);
        if (previewStopped) StartDvPreview();
        return;
    }
    SetHandleInformation(parentInput, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parentOutput, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = childOutput;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(executablePath, commandLine, 0, 0, TRUE,
                        CREATE_NO_WINDOW, 0, 0, &startup, &process)) {
        CloseHandle(childInput);
        CloseHandle(parentInput);
        CloseHandle(childOutput);
        CloseHandle(parentOutput);
        SetStatus(L"Unable to start capture process.");
        EnableWindow(g_state.preview, TRUE);
        if (previewStopped) StartDvPreview();
        return;
    }
    CloseHandle(childInput);
    CloseHandle(childOutput);
    g_state.captureProcess = process;
    g_state.captureInput = parentInput;
    g_state.captureOutput = parentOutput;
    g_state.captureOutputLength = 0;
    g_state.captureVideoDuration[0] = L'\0';
    g_state.captureStartTick = GetTickCount();
    g_state.captureRunning = true;
    SetText(g_state.progress, L"Capture starting...");
    SetStatus(L"Native capture is running. Press Stop to finish.");
}

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

bool GuiEndsWithInsensitive(const wchar_t* text, const wchar_t* suffix) {
    int textLength = 0;
    int suffixLength = 0;
    while (text[textLength] != L'\0') ++textLength;
    while (suffix[suffixLength] != L'\0') ++suffixLength;
    if (suffixLength > textLength) return false;
    for (int index = 0; index < suffixLength; ++index) {
        wchar_t left = text[textLength - suffixLength + index];
        wchar_t right = suffix[index];
        if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
        if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
        if (left != right) return false;
    }
    return true;
}

void EnsureCaptureExtension() {
    const wchar_t* suffix = g_state.hdv ? L".m2t" : L".dv";
    if (g_state.hdv && GuiEndsWithInsensitive(g_state.capturePath, L".dv")) {
        int length = 0;
        while (g_state.capturePath[length] != L'\0') ++length;
        g_state.capturePath[length - 3] = L'\0';
    } else if (!g_state.hdv &&
               GuiEndsWithInsensitive(g_state.capturePath, L".m2t")) {
        int length = 0;
        while (g_state.capturePath[length] != L'\0') ++length;
        g_state.capturePath[length - 4] = L'\0';
    } else if ((g_state.hdv && GuiEndsWithInsensitive(g_state.capturePath, L".m2t")) ||
               (!g_state.hdv && GuiEndsWithInsensitive(g_state.capturePath, L".dv"))) {
        SetText(g_state.outputPath, g_state.capturePath);
        return;
    }
    int length = 0;
    while (g_state.capturePath[length] != L'\0') ++length;
    int suffixLength = 0;
    while (suffix[suffixLength] != L'\0') ++suffixLength;
    if (length + suffixLength + 1 >= ARRAYSIZE(g_state.capturePath)) return;
    CopyText(g_state.capturePath + length,
             ARRAYSIZE(g_state.capturePath) - length, suffix);
    SetText(g_state.outputPath, g_state.capturePath);
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
    g_state.hdv = result.hdv;
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
    if (g_timecodeReader != 0 && !g_state.captureRunning) {
        TIMECODE_SAMPLE sample = {};
        sample.dwFlags = ED_DEVCAP_TIMECODE_READ;
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
        HFONT font = className[0] == L'B' && id != IdPreview
                         ? g_state.buttonFont : g_state.normalFont;
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(font), TRUE);
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
        StartGuiCapture();
        break;
    default:
        break;
    }
}

void BrowseForOutput() {
    wchar_t path[MAX_PATH] = {};
    GetWindowTextW(g_state.outputPath, path, ARRAYSIZE(path));
    if (path[0] != L'\0') {
        CopyText(g_state.capturePath, ARRAYSIZE(g_state.capturePath), path);
        EnsureCaptureExtension();
        GetWindowTextW(g_state.outputPath, path, ARRAYSIZE(path));
    }
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_state.window;
    dialog.lpstrFilter = L"DV and HDV captures\0*.dv;*.m2t\0All files\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = g_state.hdv ? L"m2t" : L"dv";
    if (GetSaveFileNameW(&dialog)) {
        SetText(g_state.outputPath, path);
        CopyText(g_state.capturePath, ARRAYSIZE(g_state.capturePath), path);
        EnsureCaptureExtension();
        SetStatus(L"Output path selected.");
    }
}

void LayoutControls(int width, int height) {
    const int margin = 18;
    const int infoWidth = width > 1000 ? 330 : (width > 800 ? 300 : 190);
    const int previewLeft = margin + infoWidth + 18;
    const int previewTop = margin;
    const int bottomHeight = 112;
    const int previewMaxHeight = height - bottomHeight - previewTop - margin;

    const bool compact = width < 900 || height < 650;
    HFONT leftFont = compact ? g_state.compactFont : g_state.normalFont;
    HWND leftControls[] = {g_state.device, g_state.format, g_state.transport,
                           g_state.timecode, g_state.recordingDate,
                           g_state.preview, g_state.progress, g_state.status};
    for (int index = 0; index < ARRAYSIZE(leftControls); ++index) {
        SendMessageW(leftControls[index], WM_SETFONT,
                     reinterpret_cast<WPARAM>(leftFont), TRUE);
    }
    const int rowStep = compact ? 28 : 40;
    const int firstRow = compact ? 18 : 24;
    MoveWindow(g_state.device, margin, firstRow, infoWidth, 28, TRUE);
    MoveWindow(g_state.format, margin, firstRow + rowStep, infoWidth, 28, TRUE);
    MoveWindow(g_state.transport, margin, firstRow + rowStep * 2,
               infoWidth, 28, TRUE);
    MoveWindow(g_state.timecode, margin, firstRow + rowStep * 3,
               infoWidth, 28, TRUE);
    MoveWindow(g_state.recordingDate, margin, firstRow + rowStep * 4,
               infoWidth, 28, TRUE);
    const int previewToggleTop = compact ? 158 : 224;
    MoveWindow(g_state.preview, margin, previewToggleTop, infoWidth, 28, TRUE);
    const int aspectX = g_state.hdv ? 16 : 4;
    const int aspectY = g_state.hdv ? 9 : 3;
    int previewWidth = width - previewLeft - margin;
    int previewHeight = previewWidth * aspectY / aspectX;
    const int maxHeight = previewMaxHeight > 80 ? previewMaxHeight : 80;
    if (previewHeight > maxHeight) {
        previewHeight = maxHeight;
        previewWidth = previewHeight * aspectX / aspectY;
    }
    MoveWindow(g_state.previewWindow, previewLeft, previewTop,
               previewWidth, previewHeight, TRUE);

    const int controlsTop = height - 48;
    const int buttonWidth = (previewWidth - 20) / 5;
    HWND buttons[] = {
        GetDlgItem(g_state.window, IdRewind), GetDlgItem(g_state.window, IdStop),
        GetDlgItem(g_state.window, IdPlay), GetDlgItem(g_state.window, IdCapture),
        GetDlgItem(g_state.window, IdFastForward)};
    if (buttonWidth < 110) {
        SetText(buttons[0], L"Rew [R]");
        SetText(buttons[1], L"Stop [S]");
        SetText(buttons[2], L"Play [P]");
        SetText(buttons[3], L"Cap [C]");
        SetText(buttons[4], L"FF [F]");
    } else {
        SetText(buttons[0], L"Rewind [R]");
        SetText(buttons[1], L"Stop [S]");
        SetText(buttons[2], L"Play [P]");
        SetText(buttons[3], L"Capture [C]");
        SetText(buttons[4], L"Fast-forward [F]");
    }
    for (int index = 0; index < 5; ++index) {
        MoveWindow(buttons[index], previewLeft + index * (buttonWidth + 5),
                   controlsTop, buttonWidth, 30, TRUE);
    }
    const int outputTop = height - 92;
    const int outputX = margin + 82;
    MoveWindow(GetDlgItem(g_state.window, 2100), margin, outputTop, 76, 25, TRUE);
    MoveWindow(g_state.outputPath, outputX, outputTop,
               width - outputX - margin - 92, 25, TRUE);
    MoveWindow(GetDlgItem(g_state.window, IdBrowse), width - margin - 82,
               outputTop, 82, 25, TRUE);
    int statusTop = compact ? 240 : outputTop - 62;
    if (statusTop > 324) statusTop = 324;
    const int progressTop = compact ? 198 : statusTop - 50;
    MoveWindow(g_state.progress, margin, progressTop, infoWidth, 42, TRUE);
    MoveWindow(g_state.status, margin, statusTop, infoWidth, 48, TRUE);
    InvalidateRect(g_state.status, 0, TRUE);
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
        g_state.compactFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        g_state.buttonFont = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        g_state.backgroundBrush = CreateSolidBrush(RGB(31, 40, 52));
        g_state.panelBrush = CreateSolidBrush(RGB(8, 12, 17));

        g_state.device = MakeControl(L"STATIC", L"No FireWire camera detected",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT |
                                          SS_NOPREFIX | SS_ENDELLIPSIS,
                                      0, IdDevice,
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
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_VCENTER,
                                      0, IdPreview, 18, 116, 180, 24);
        g_state.previewWindow = MakeControl(
            L"STATIC", L"DV preview disabled",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_BLACKRECT,
            0, 2000, 18, 148, 700, 250);
        ShowWindow(g_state.previewWindow, SW_HIDE);

        MakeControl(L"BUTTON", L"Rewind [R]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdRewind, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Stop [S]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdStop, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Play [P]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdPlay, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Capture [C]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdCapture, 18, 410, 100, 30);
        MakeControl(L"BUTTON", L"Fast-forward [F]", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, IdFastForward, 18, 410, 100, 30);
        MakeControl(L"STATIC", L"Output:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
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
                                     WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                     0, IdStatus,
                                     18, 522, 700, 42);
        SetWindowTextW(g_state.window, L"FireWire Capture");
        SetTimer(g_state.window, 1, 500, 0);
        return 0;
    }
    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        ResizeDvPreview();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 640;
        limits->ptMinTrackSize.y = 480;
        return 0;
    }
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
            if (g_state.previewEnabled) {
                StartDvPreview();
            } else {
                StopDvPreview();
                SetStatus(L"Video preview disabled.");
            }
            return 0;
        case IdRewind:
            SendTransportCommand(ED_MODE_REW, L"Rewind command sent.");
            return 0;
        case IdStop:
            if (g_state.captureRunning) {
                StopGuiCapture();
                SetStatus(L"Capture stopped.");
            } else {
                SendTransportCommand(ED_MODE_STOP, L"Stop command sent.");
            }
            return 0;
        case IdPlay:
            SendTransportCommand(ED_MODE_PLAY, L"Play command sent.");
            return 0;
        case IdCapture:
            StartGuiCapture();
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
        if (wParam == 1) UpdateCaptureStatus();
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        StopGuiCapture();
        StopDvPreview();
        ReleaseCameraInterfaces();
        if (g_state.headingFont != 0) DeleteObject(g_state.headingFont);
        if (g_state.normalFont != 0) DeleteObject(g_state.normalFont);
        if (g_state.compactFont != 0) DeleteObject(g_state.compactFont);
        if (g_state.buttonFont != 0) DeleteObject(g_state.buttonFont);
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

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_FWCAP));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_FWCAP));
    windowClass.hCursor = LoadCursorW(0, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(31, 40, 52));
    windowClass.lpszClassName = L"FireWireCaptureGui";
    if (!RegisterClassExW(&windowClass)) return 1;

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
        if (message.message == WM_KEYDOWN && message.hwnd != g_state.window &&
            GetFocus() != g_state.outputPath) {
            HandleTransportKey(static_cast<wchar_t>(message.wParam));
        }
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
