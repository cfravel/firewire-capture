#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <dshow.h>

int RunGui(HINSTANCE instance, int showCommand);

void* operator new(unsigned int size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void operator delete(void* data) noexcept {
    if (data != 0) HeapFree(GetProcessHeap(), 0, data);
}

void operator delete(void* data, unsigned int) noexcept {
    operator delete(data);
}

extern "C" int __cdecl memcmp(const void* left, const void* right,
                               unsigned int length) {
    const BYTE* a = static_cast<const BYTE*>(left);
    const BYTE* b = static_cast<const BYTE*>(right);
    while (length-- != 0) {
        if (*a != *b) return *a < *b ? -1 : 1;
        ++a;
        ++b;
    }
    return 0;
}

extern "C" int __cdecl wcscmp(const wchar_t* left, const wchar_t* right) {
    while (*left != L'\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right ? 0 : (*left < *right ? -1 : 1);
}

extern "C" int __cdecl wcscpy_s(wchar_t* destination, unsigned int capacity,
                                const wchar_t* source) {
    if (destination == 0 || source == 0 || capacity == 0) return 22;
    unsigned int index = 0;
    while (source[index] != L'\0') {
        if (index + 1 >= capacity) {
            destination[0] = L'\0';
            return 34;
        }
        destination[index] = source[index];
        ++index;
    }
    destination[index] = L'\0';
    return 0;
}


class Text {
public:
    Text() : length_(0) { buffer_[0] = L'\0'; }

    void Append(const wchar_t* text) {
        if (text == 0) return;
        while (*text != L'\0' && length_ + 1 < ARRAYSIZE(buffer_)) {
            buffer_[length_++] = *text++;
        }
        buffer_[length_] = L'\0';
    }

    void AppendUInt(ULONG value) {
        wchar_t digits[16];
        ULONG count = 0;
        do {
            digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
            value /= 10;
        } while (value != 0 && count < ARRAYSIZE(digits));
        while (count != 0) AppendChar(digits[--count]);
    }

    void AppendUInt64(ULONGLONG value) {
        wchar_t digits[32];
        ULONG count = 0;
        do {
            digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
            value /= 10;
        } while (value != 0 && count < ARRAYSIZE(digits));
        while (count != 0) AppendChar(digits[--count]);
    }

    void AppendHex(ULONG value) {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        Append(L"0x");
        for (int shift = 28; shift >= 0; shift -= 4) {
            AppendChar(digits[(value >> shift) & 0x0F]);
        }
    }

    void AppendGuid(const GUID& guid) {
        wchar_t text[64] = {};
        StringFromGUID2(guid, text, ARRAYSIZE(text));
        Append(text);
    }

    void AppendChar(wchar_t character) {
        if (length_ + 1 < ARRAYSIZE(buffer_)) {
            buffer_[length_++] = character;
            buffer_[length_] = L'\0';
        }
    }

    void Flush() const {
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output != INVALID_HANDLE_VALUE && output != 0) {
            DWORD written = 0;
            if (WriteConsoleW(output, buffer_, length_, &written, 0)) {
                return;
            }
            char bytes[4096];
            int byteCount = WideCharToMultiByte(
                CP_ACP, 0, buffer_, static_cast<int>(length_), bytes,
                ARRAYSIZE(bytes), 0, 0);
            if (byteCount > 0) {
                WriteFile(output, bytes, static_cast<DWORD>(byteCount),
                          &written, 0);
            }
        }
    }

private:
    wchar_t buffer_[1024];
    ULONG length_;
};

void ZeroBytes(void* data, ULONG length) {
    BYTE* bytes = static_cast<BYTE*>(data);
    while (length-- != 0) {
        *bytes++ = 0;
    }
}

extern "C" void* __cdecl memset(void* data, int value, unsigned int length) {
    BYTE* bytes = static_cast<BYTE*>(data);
    while (length-- != 0) {
        *bytes++ = static_cast<BYTE>(value);
    }
    return data;
}

void PrintLine(const wchar_t* text) {
    Text output;
    output.Append(text);
    output.Append(L"\n");
    output.Flush();
}

template <typename T>
class ComPtr {
public:
    ComPtr() : value_(0) {}
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T* Get() const { return value_; }
    T* operator->() const { return value_; }
    T** Put() {
        Reset();
        return &value_;
    }

    void Reset() {
        if (value_ != 0) {
            value_->Release();
            value_ = 0;
        }
    }

private:
    T* value_;
};

void PrintHResult(const wchar_t* label, HRESULT hr) {
    Text output;
    output.Append(label);
    output.Append(L": ");
    output.AppendHex(static_cast<ULONG>(hr));
    wchar_t text[256];
    const DWORD length = AMGetErrorTextW(hr, text, ARRAYSIZE(text));
    if (length != 0) {
        output.Append(L" (");
        output.Append(text);
        output.AppendChar(L')');
    }
    output.Append(L"\n");
    output.Flush();
}

bool g_productMode = false;
bool g_productVerbose = false;

void ProductDiagnosticLine(const wchar_t* text) {
    if (!g_productMode || g_productVerbose) PrintLine(text);
}

void ProductDiagnosticHResult(const wchar_t* label, HRESULT hr) {
    if (!g_productMode || g_productVerbose || FAILED(hr)) {
        PrintHResult(label, hr);
    }
}

void ClearMediaType(AM_MEDIA_TYPE* type) {
    if (type == 0) return;
    if (type->cbFormat != 0) {
        CoTaskMemFree(type->pbFormat);
    }
    if (type->pUnk != 0) {
        type->pUnk->Release();
    }
    ZeroBytes(type, sizeof(*type));
}

void FreeMediaType(AM_MEDIA_TYPE* type) {
    if (type == 0) return;
    ClearMediaType(type);
    CoTaskMemFree(type);
}

enum class CaptureKind {
    Dv,
    Hdv,
};

struct DvTimecode {
    int hours;
    int minutes;
    int seconds;
    int frames;
};

int BcdValue(BYTE value) {
    return (value & 0x0F) + 10 * ((value >> 4) & 0x0F);
}

bool FindDvTimecode(const BYTE* data, long length, DvTimecode* result) {
    if (data == 0 || result == 0 || length < 5) return false;
    for (long index = 0; index <= length - 5; ++index) {
        if (data[index] != 0x13) continue;
        const int frames = BcdValue(data[index + 1] & 0x3F);
        const int seconds = BcdValue(data[index + 2] & 0x7F);
        const int minutes = BcdValue(data[index + 3] & 0x7F);
        const int hours = BcdValue(data[index + 4] & 0x3F);
        if (frames >= 30 || seconds >= 60 || minutes >= 60 || hours >= 24) continue;
        result->hours = hours;
        result->minutes = minutes;
        result->seconds = seconds;
        result->frames = frames;
        return true;
    }
    return false;
}

static const GUID kMediaTypeAudioVideo = {
    0x73766169, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool IsFireWirePath(const wchar_t* path);

bool IsDvType(const AM_MEDIA_TYPE& type) {
    return (type.majortype == MEDIATYPE_Interleaved ||
            type.majortype == kMediaTypeAudioVideo) &&
           type.subtype == MEDIASUBTYPE_dvsd &&
           type.formattype == FORMAT_DvInfo;
}

bool IsHdvType(const AM_MEDIA_TYPE& type) {
    return type.majortype == MEDIATYPE_Stream &&
           (type.subtype == MEDIASUBTYPE_MPEG2_TRANSPORT ||
            type.subtype == MEDIASUBTYPE_MPEG2_TRANSPORT_STRIDE);
}

HRESULT CopyMediaType(AM_MEDIA_TYPE* destination, const AM_MEDIA_TYPE* source) {
    if (destination == 0 || source == 0) return E_POINTER;
    *destination = *source;
    if (source->cbFormat != 0) {
        destination->pbFormat = static_cast<BYTE*>(
            CoTaskMemAlloc(source->cbFormat));
        if (destination->pbFormat == 0) return E_OUTOFMEMORY;
        for (ULONG index = 0; index < source->cbFormat; ++index) {
            destination->pbFormat[index] = source->pbFormat[index];
        }
    }
    if (destination->pUnk != 0) destination->pUnk->AddRef();
    return S_OK;
}

class CapturePin;

class CaptureSink final : public IBaseFilter {
public:
    CaptureSink();
    ~CaptureSink();

    HRESULT Open(const wchar_t* path) {
        outputFile_ = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        return outputFile_ == INVALID_HANDLE_VALUE ? HRESULT_FROM_WIN32(GetLastError())
                                                    : S_OK;
    }
    void SetDiscard() { outputFile_ = INVALID_HANDLE_VALUE; }
    void SetHdv(bool hdv) { hdvStream_ = hdv ? 1 : 0; }
    HRESULT Write(IMediaSample* sample);
    ULONGLONG Bytes() {
        return static_cast<ULONGLONG>(
            InterlockedCompareExchange64(&bytes_, 0, 0));
    }
    ULONG Samples() const { return samples_; }
    ULONG LastSampleTick() const {
        return static_cast<ULONG>(
            InterlockedCompareExchange(const_cast<volatile LONG*>(&lastSampleTick_), 0, 0));
    }
    bool LatestTimecode(DvTimecode* result) const;
    bool MediaDuration(REFERENCE_TIME* duration) const;

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG value = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (value == 0) delete this;
        return value;
    }
    STDMETHODIMP GetClassID(CLSID* id) override {
        if (id == 0) return E_POINTER;
        *id = CLSID_NULL;
        return S_OK;
    }
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* info) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* graph, LPCWSTR) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* info) override {
        if (info == 0) return E_POINTER;
        *info = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP Stop() override { return S_OK; }
    STDMETHODIMP Pause() override { return S_OK; }
    STDMETHODIMP Run(REFERENCE_TIME) override { return S_OK; }
    STDMETHODIMP GetState(DWORD, FILTER_STATE* state) override {
        if (state == 0) return E_POINTER;
        *state = State_Running;
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock*) override { return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock** clock) override {
        if (clock == 0) return E_POINTER;
        *clock = 0;
        return S_OK;
    }
    STDMETHODIMP EnumPins(IEnumPins** pins) override;
    STDMETHODIMP FindPin(LPCWSTR id, IPin** pin) override;

private:
    LONG references_;
    CapturePin* pin_;
    IFilterGraph* graph_ = 0;
    HANDLE outputFile_;
    volatile LONGLONG bytes_;
    volatile LONG samples_;
    volatile LONG lastSampleTick_;
    volatile LONG lastFlushTick_;
    volatile LONG hasTimecode_;
    volatile LONG latestTimecode_;
    volatile LONG hasMediaTime_;
    volatile LONGLONG firstSampleTime_;
    volatile LONGLONG lastSampleEndTime_;
    volatile LONG hdvStream_;
};

class CapturePin final : public IPin, public IMemInputPin {
public:
    explicit CapturePin(CaptureSink* sink) : references_(1), sink_(sink) {}
    ~CapturePin() {}

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == 0) return E_POINTER;
        *object = 0;
        if (iid == IID_IUnknown || iid == IID_IPin) {
            *object = static_cast<IPin*>(this);
        } else if (iid == IID_IMemInputPin) {
            *object = static_cast<IMemInputPin*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG value = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (value == 0) delete this;
        return value;
    }
    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) override { return E_NOTIMPL; }
    STDMETHODIMP ReceiveConnection(IPin* pin, const AM_MEDIA_TYPE* type) override {
        if (pin == 0 || type == 0 || connectedPin_ != 0) return E_INVALIDARG;
        if (!IsDvType(*type) && !IsHdvType(*type)) return VFW_E_TYPE_NOT_ACCEPTED;
        connectedPin_ = pin;
        connectedPin_->AddRef();
        sink_->SetHdv(IsHdvType(*type));
        HRESULT hr = CopyMediaType(&connectedType_, type);
        if (FAILED(hr)) {
            connectedPin_->Release();
            connectedPin_ = 0;
            return hr;
        }
        return S_OK;
    }
    STDMETHODIMP Disconnect() override {
        if (connectedPin_ != 0) {
            connectedPin_->Release();
            connectedPin_ = 0;
        }
        ClearMediaType(&connectedType_);
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin** pin) override {
        if (pin == 0) return E_POINTER;
        *pin = 0;
        if (connectedPin_ == 0) return VFW_E_NOT_CONNECTED;
        *pin = connectedPin_;
        connectedPin_->AddRef();
        return S_OK;
    }
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* type) override {
        if (type == 0) return E_POINTER;
        if (connectedPin_ == 0) return VFW_E_NOT_CONNECTED;
        return CopyMediaType(type, &connectedType_);
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO* info) override {
        if (info == 0) return E_POINTER;
        ZeroBytes(info, sizeof(*info));
        info->dir = PINDIR_INPUT;
        wcscpy_s(info->achName, L"Input");
        info->pFilter = sink_;
        sink_->AddRef();
        return S_OK;
    }
    STDMETHODIMP QueryDirection(PIN_DIRECTION* direction) override {
        if (direction == 0) return E_POINTER;
        *direction = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR* id) override {
        if (id == 0) return E_POINTER;
        *id = static_cast<LPWSTR>(CoTaskMemAlloc(12 * sizeof(wchar_t)));
        if (*id == 0) return E_OUTOFMEMORY;
        wcscpy_s(*id, 12, L"Input");
        return S_OK;
    }
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* type) override {
        return type != 0 && (IsDvType(*type) || IsHdvType(*type)) ? S_OK : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*) override { return E_NOTIMPL; }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return S_OK; }
    STDMETHODIMP GetAllocator(IMemAllocator** allocator) override {
        if (allocator == 0) return E_POINTER;
        *allocator = 0;
        return VFW_E_NO_ALLOCATOR;
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator*, BOOL) override { return S_OK; }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) override { return E_NOTIMPL; }
    STDMETHODIMP Receive(IMediaSample* sample) override { return sink_->Write(sample); }
    STDMETHODIMP ReceiveMultiple(IMediaSample** samples, long count, long* processed) override {
        if (processed == 0) return E_POINTER;
        *processed = 0;
        for (long index = 0; index < count; ++index) {
            HRESULT hr = Receive(samples[index]);
            if (FAILED(hr)) return hr;
            ++*processed;
        }
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() override { return S_FALSE; }

private:
    LONG references_;
    CaptureSink* sink_;
    IPin* connectedPin_ = 0;
    AM_MEDIA_TYPE connectedType_ = {};
};

class SinkEnumPins final : public IEnumPins {
public:
    explicit SinkEnumPins(CapturePin* pin) : references_(1), pin_(pin) {
        pin_->AddRef();
    }
    ~SinkEnumPins() { pin_->Release(); }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == 0) return E_POINTER;
        *object = 0;
        if (iid == IID_IUnknown || iid == IID_IEnumPins) {
            *object = static_cast<IEnumPins*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG value = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (value == 0) delete this;
        return value;
    }
    STDMETHODIMP Next(ULONG count, IPin** pins, ULONG* fetched) override {
        if (count == 0) return E_INVALIDARG;
        if (pins == 0 || (count != 1 && fetched == 0)) return E_POINTER;
        if (fetched != 0) *fetched = 0;
        if (index_ != 0) return S_FALSE;
        pins[0] = pin_;
        pin_->AddRef();
        index_ = 1;
        if (fetched != 0) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG count) override {
        if (count == 0) return S_OK;
        if (index_ == 0 && count == 1) {
            index_ = 1;
            return S_OK;
        }
        index_ = 1;
        return S_FALSE;
    }
    STDMETHODIMP Reset() override { index_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins** clone) override {
        if (clone == 0) return E_POINTER;
        *clone = new SinkEnumPins(pin_);
        if (*clone == 0) return E_OUTOFMEMORY;
        static_cast<SinkEnumPins*>(*clone)->index_ = index_;
        return S_OK;
    }

private:
    LONG references_;
    CapturePin* pin_;
    ULONG index_ = 0;
};

CaptureSink::CaptureSink()
    : references_(1), pin_(new CapturePin(this)),
      outputFile_(INVALID_HANDLE_VALUE), bytes_(0), samples_(0),
      lastSampleTick_(0), hasTimecode_(0), latestTimecode_(0),
      hasMediaTime_(0), firstSampleTime_(0), lastSampleEndTime_(0),
      hdvStream_(0), lastFlushTick_(0) {}

CaptureSink::~CaptureSink() {
    if (pin_ != 0) {
        pin_->Disconnect();
        pin_->Release();
        pin_ = 0;
    }
    if (graph_ != 0) graph_->Release();
    if (outputFile_ != INVALID_HANDLE_VALUE) CloseHandle(outputFile_);
}

HRESULT CaptureSink::Write(IMediaSample* sample) {
    if (sample == 0) return E_POINTER;
    BYTE* data = 0;
    long length = sample->GetActualDataLength();
    if (length < 0) return E_INVALIDARG;
    HRESULT hr = sample->GetPointer(&data);
    if (FAILED(hr)) return hr;
    DWORD written = 0;
    if (outputFile_ != INVALID_HANDLE_VALUE) {
        if (!WriteFile(outputFile_, data, static_cast<DWORD>(length), &written, 0) ||
            written != static_cast<DWORD>(length)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const ULONG now = GetTickCount();
        const ULONG lastFlush = static_cast<ULONG>(
            InterlockedCompareExchange(&lastFlushTick_, 0, 0));
        if (static_cast<LONG>(now - lastFlush) >= 1000) {
            if (!FlushFileBuffers(outputFile_)) {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            InterlockedExchange(&lastFlushTick_, static_cast<LONG>(now));
        }
    }
    if (hdvStream_ == 0) {
        DvTimecode timecode;
        if (FindDvTimecode(data, length, &timecode)) {
            const LONG packed = (timecode.hours << 24) |
                                (timecode.minutes << 16) |
                                (timecode.seconds << 8) |
                                timecode.frames;
            InterlockedExchange(&latestTimecode_, packed);
            InterlockedExchange(&hasTimecode_, 1);
        }
    }
    REFERENCE_TIME start = 0;
    REFERENCE_TIME end = 0;
    if (SUCCEEDED(sample->GetTime(&start, &end))) {
        if (InterlockedCompareExchange(&hasMediaTime_, 1, 0) == 0) {
            InterlockedExchange64(&firstSampleTime_, start);
        }
        InterlockedExchange64(&lastSampleEndTime_, end);
    }
    InterlockedExchangeAdd64(&bytes_, length);
    InterlockedIncrement(&samples_);
    InterlockedExchange(&lastSampleTick_, static_cast<LONG>(GetTickCount()));
    return S_OK;
}

bool CaptureSink::LatestTimecode(DvTimecode* result) const {
    if (result == 0 || InterlockedCompareExchange(
                           const_cast<volatile LONG*>(&hasTimecode_), 0, 0) == 0) {
        return false;
    }
    const LONG packed = InterlockedCompareExchange(
        const_cast<volatile LONG*>(&latestTimecode_), 0, 0);
    result->hours = (packed >> 24) & 0xFF;
    result->minutes = (packed >> 16) & 0xFF;
    result->seconds = (packed >> 8) & 0xFF;
    result->frames = packed & 0xFF;
    return true;
}

bool CaptureSink::MediaDuration(REFERENCE_TIME* duration) const {
    if (duration == 0 || InterlockedCompareExchange(
                             const_cast<volatile LONG*>(&hasMediaTime_), 0, 0) == 0) {
        return false;
    }
    const LONGLONG first = InterlockedCompareExchange64(
        const_cast<volatile LONGLONG*>(&firstSampleTime_), 0, 0);
    const LONGLONG last = InterlockedCompareExchange64(
        const_cast<volatile LONGLONG*>(&lastSampleEndTime_), 0, 0);
    if (last < first) return false;
    *duration = last - first;
    return true;
}

STDMETHODIMP CaptureSink::QueryInterface(REFIID iid, void** object) {
    if (object == 0) return E_POINTER;
    *object = 0;
    if (iid == IID_IUnknown || iid == IID_IMediaFilter || iid == IID_IBaseFilter) {
        *object = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP CaptureSink::QueryFilterInfo(FILTER_INFO* info) {
    if (info == 0) return E_POINTER;
    ZeroBytes(info, sizeof(*info));
    wcscpy_s(info->achName, L"XP Probe Native Sink");
    info->pGraph = graph_;
    if (graph_ != 0) graph_->AddRef();
    return S_OK;
}

STDMETHODIMP CaptureSink::JoinFilterGraph(IFilterGraph* graph, LPCWSTR) {
    if (graph_ != 0) {
        graph_->Release();
        graph_ = 0;
    }
    graph_ = graph;
    if (graph_ != 0) graph_->AddRef();
    return S_OK;
}

STDMETHODIMP CaptureSink::EnumPins(IEnumPins** pins) {
    if (pins == 0) return E_POINTER;
    *pins = new SinkEnumPins(pin_);
    return *pins == 0 ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CaptureSink::FindPin(LPCWSTR id, IPin** pin) {
    if (id == 0 || pin == 0) return E_POINTER;
    *pin = 0;
    if (wcscmp(id, L"Input") != 0) return VFW_E_NOT_FOUND;
    *pin = pin_;
    pin_->AddRef();
    return S_OK;
}

void PrintVariantProperty(IPropertyBag* properties, const wchar_t* name) {
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = properties->Read(name, &value, 0);
    if (SUCCEEDED(hr) && value.vt == VT_BSTR && value.bstrVal != 0) {
        Text output;
        output.Append(L"    ");
        output.Append(name);
        output.Append(L": ");
        output.Append(value.bstrVal);
        output.Append(L"\n");
        output.Flush();
    } else {
        Text output;
        output.Append(L"    ");
        output.Append(name);
        output.Append(L": unavailable (");
        output.AppendHex(static_cast<ULONG>(hr));
        output.Append(L")\n");
        output.Flush();
    }
    VariantClear(&value);
}

void PrintMediaType(const AM_MEDIA_TYPE* type) {
    Text output;
    output.Append(L"      major=");
    output.AppendGuid(type->majortype);
    output.Append(L" subtype=");
    output.AppendGuid(type->subtype);
    output.Append(L" format=");
    output.AppendGuid(type->formattype);
    output.Append(L" format-bytes=");
    output.AppendUInt(static_cast<ULONG>(type->cbFormat));
    output.Append(L" sample-size=");
    output.AppendUInt(static_cast<ULONG>(type->lSampleSize));
    output.Append(L"\n");
    output.Flush();
}

void InspectPins(IBaseFilter* filter) {
    ComPtr<IEnumPins> pins;
    HRESULT hr = filter->EnumPins(pins.Put());
    if (FAILED(hr)) {
        PrintHResult(L"  EnumPins", hr);
        return;
    }

    ULONG pinIndex = 0;
    while (true) {
        ComPtr<IPin> pin;
        hr = pins->Next(1, pin.Put(), 0);
        if (hr != S_OK) break;

        PIN_DIRECTION direction = PINDIR_INPUT;
        pin->QueryDirection(&direction);
        PIN_INFO pinInfo;
        ZeroBytes(&pinInfo, sizeof(pinInfo));
        pin->QueryPinInfo(&pinInfo);
        if (pinInfo.pFilter != 0) pinInfo.pFilter->Release();

        Text pinText;
        pinText.Append(L"  Pin ");
        pinText.AppendUInt(pinIndex++);
        pinText.Append(L": name=");
        pinText.Append(pinInfo.achName);
        pinText.Append(L" direction=");
        pinText.Append(direction == PINDIR_OUTPUT ? L"output" : L"input");
        pinText.Append(L"\n");
        pinText.Flush();

        ComPtr<IEnumMediaTypes> types;
        hr = pin->EnumMediaTypes(types.Put());
        if (FAILED(hr)) {
            PrintHResult(L"    EnumMediaTypes", hr);
            continue;
        }

        ULONG typeIndex = 0;
        while (true) {
            AM_MEDIA_TYPE* type = 0;
            hr = types->Next(1, &type, 0);
            if (hr != S_OK) break;
            Text typeText;
            typeText.Append(L"    Media type ");
            typeText.AppendUInt(typeIndex++);
            typeText.Append(L":\n");
            typeText.Flush();
            PrintMediaType(type);
            FreeMediaType(type);
        }
    }
}

void InspectDevice(IMoniker* moniker, ULONG index) {
    Text deviceText;
    deviceText.Append(L"\nDevice ");
    deviceText.AppendUInt(index);
    deviceText.Append(L"\n");
    deviceText.Flush();

    ComPtr<IPropertyBag> properties;
    HRESULT hr = moniker->BindToStorage(0, 0, IID_IPropertyBag,
                                        reinterpret_cast<void**>(properties.Put()));
    if (SUCCEEDED(hr)) {
        PrintVariantProperty(properties.Get(), L"FriendlyName");
        PrintVariantProperty(properties.Get(), L"Description");
        PrintVariantProperty(properties.Get(), L"DevicePath");
    } else {
        PrintHResult(L"  BindToStorage(IPropertyBag)", hr);
    }

    CLSID classId = CLSID_NULL;
    hr = moniker->GetClassID(&classId);
    if (SUCCEEDED(hr)) {
        Text classText;
        classText.Append(L"    CLSID: ");
        classText.AppendGuid(classId);
        classText.Append(L"\n");
        classText.Flush();
    }

    ComPtr<IBaseFilter> filter;
    hr = moniker->BindToObject(0, 0, IID_IBaseFilter,
                               reinterpret_cast<void**>(filter.Put()));
    if (FAILED(hr)) {
        PrintHResult(L"  BindToObject(IBaseFilter)", hr);
        return;
    }

    ComPtr<IAMExtTransport> transport;
    hr = filter->QueryInterface(IID_IAMExtTransport,
                                reinterpret_cast<void**>(transport.Put()));
    Text transportText;
    transportText.Append(L"    IAMExtTransport: ");
    transportText.Append(SUCCEEDED(hr) ? L"yes" : L"no");
    transportText.Append(L"\n");
    transportText.Flush();

    ComPtr<IAMTimecodeReader> timecode;
    hr = filter->QueryInterface(IID_IAMTimecodeReader,
                                reinterpret_cast<void**>(timecode.Put()));
    Text timecodeText;
    timecodeText.Append(L"    IAMTimecodeReader: ");
    timecodeText.Append(SUCCEEDED(hr) ? L"yes" : L"no");
    timecodeText.Append(L"\n");
    timecodeText.Flush();

    InspectPins(filter.Get());
}

HRESULT FindCaptureSource(IBaseFilter** source,
                          IPin** output,
                          CaptureKind* kind,
                          wchar_t* deviceName,
                          ULONG deviceNameCapacity) {
    if (source == 0 || output == 0 || kind == 0 || deviceName == 0 ||
        deviceNameCapacity == 0) return E_POINTER;
    *source = 0;
    *output = 0;
    deviceName[0] = L'\0';

    ComPtr<ICreateDevEnum> devices;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, 0,
                                  CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                                  reinterpret_cast<void**>(devices.Put()));
    if (FAILED(hr)) return hr;

    ComPtr<IEnumMoniker> monikers;
    hr = devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
                                         monikers.Put(), 0);
    if (FAILED(hr)) return hr == S_FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : hr;

    while (true) {
        ComPtr<IMoniker> moniker;
        hr = monikers->Next(1, moniker.Put(), 0);
        if (hr != S_OK) break;

        ComPtr<IPropertyBag> deviceProperties;
        VARIANT devicePath;
        VariantInit(&devicePath);
        const bool hasProperties = SUCCEEDED(moniker->BindToStorage(
            0, 0, IID_IPropertyBag,
            reinterpret_cast<void**>(deviceProperties.Put())));
        const bool hasFireWirePath = hasProperties &&
                                     SUCCEEDED(deviceProperties->Read(
                                         L"DevicePath", &devicePath, 0)) &&
                                     devicePath.vt == VT_BSTR &&
                                     IsFireWirePath(devicePath.bstrVal);
        VariantClear(&devicePath);
        if (!hasFireWirePath) continue;

        ComPtr<IBaseFilter> filter;
        if (FAILED(moniker->BindToObject(
                0, 0, IID_IBaseFilter,
                reinterpret_cast<void**>(filter.Put())))) {
            continue;
        }

        ComPtr<IEnumPins> pins;
        if (FAILED(filter->EnumPins(pins.Put()))) continue;
        while (true) {
            ComPtr<IPin> pin;
            if (pins->Next(1, pin.Put(), 0) != S_OK) break;
            PIN_DIRECTION direction = PINDIR_INPUT;
            if (FAILED(pin->QueryDirection(&direction)) || direction != PINDIR_OUTPUT) {
                continue;
            }
            PIN_INFO info;
            ZeroBytes(&info, sizeof(info));
            if (FAILED(pin->QueryPinInfo(&info))) continue;
            if (info.pFilter != 0) info.pFilter->Release();

            CaptureKind candidate = CaptureKind::Dv;
            bool match = false;
            if (wcscmp(info.achName, L"DV A/V Out") == 0) {
                candidate = CaptureKind::Dv;
                match = true;
            } else if (wcscmp(info.achName, L"MPEG2TS Out") == 0) {
                candidate = CaptureKind::Hdv;
                match = true;
            }
            if (!match) continue;

            *source = filter.Get();
            (*source)->AddRef();
            *output = pin.Get();
            (*output)->AddRef();
            *kind = candidate;
            ComPtr<IPropertyBag> properties;
            if (SUCCEEDED(moniker->BindToStorage(
                    0, 0, IID_IPropertyBag,
                    reinterpret_cast<void**>(properties.Put())))) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(properties->Read(L"FriendlyName", &value, 0)) &&
                    value.vt == VT_BSTR && value.bstrVal != 0) {
                    wcscpy_s(deviceName, deviceNameCapacity, value.bstrVal);
                }
                VariantClear(&value);
            }
            if (deviceName[0] == L'\0') {
                wcscpy_s(deviceName, deviceNameCapacity,
                         candidate == CaptureKind::Dv
                             ? L"Microsoft DV Camera and VCR"
                             : L"Microsoft AV/C Tape Subunit Device");
            }
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

bool GetOutputArgument(wchar_t* output, ULONG capacity) {
    if (output == 0 || capacity == 0) return false;
    output[0] = L'\0';
    const wchar_t* command = GetCommandLineW();
    if (command == 0) return false;
    while (*command == L' ' || *command == L'\t') ++command;
    if (*command == L'\"') {
        ++command;
        while (*command != L'\0' && *command != L'\"') ++command;
        if (*command == L'\"') ++command;
    } else {
        while (*command != L'\0' && *command != L' ' && *command != L'\t') ++command;
    }
    while (*command == L' ' || *command == L'\t') ++command;
    if (*command == L'\0') return false;
    ULONG length = 0;
    bool quoted = false;
    if (*command == L'\"') {
        quoted = true;
        ++command;
    }
    while (*command != L'\0' && length + 1 < capacity &&
           (quoted ? *command != L'\"' : (*command != L' ' && *command != L'\t'))) {
        output[length++] = *command++;
    }
    output[length] = L'\0';
    return length != 0;
}

HRESULT ConnectNative(IGraphBuilder* graph, IPin* output, IPin* input) {
    if (graph == 0 || output == 0 || input == 0) return E_POINTER;
    ComPtr<IEnumMediaTypes> types;
    HRESULT hr = output->EnumMediaTypes(types.Put());
    if (FAILED(hr)) return hr;

    HRESULT lastError = VFW_E_NO_ACCEPTABLE_TYPES;
    while (true) {
        AM_MEDIA_TYPE* type = 0;
        hr = types->Next(1, &type, 0);
        if (hr != S_OK) break;
        if (!g_productMode || g_productVerbose) {
            Text attempt;
            attempt.Append(L"Capture: trying advertised media type major=");
            attempt.AppendGuid(type->majortype);
            attempt.Append(L" subtype=");
            attempt.AppendGuid(type->subtype);
            attempt.Append(L"\n");
            attempt.Flush();
        }

        hr = graph->ConnectDirect(output, input, type);
        FreeMediaType(type);
        if (SUCCEEDED(hr)) return S_OK;
        lastError = hr;
        // An older graph builder may call ReceiveConnection before rejecting
        // a media type. Reset both pins before trying the next advertisement.
        input->Disconnect();
        output->Disconnect();
    }
    return lastError;
}

bool EndsWithInsensitive(const wchar_t* text, const wchar_t* suffix) {
    ULONG textLength = 0;
    ULONG suffixLength = 0;
    while (text[textLength] != L'\0') ++textLength;
    while (suffix[suffixLength] != L'\0') ++suffixLength;
    if (suffixLength > textLength) return false;
    for (ULONG index = 0; index < suffixLength; ++index) {
        wchar_t left = text[textLength - suffixLength + index];
        wchar_t right = suffix[index];
        if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
        if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
        if (left != right) return false;
    }
    return true;
}

bool ContainsInsensitive(const wchar_t* text, const wchar_t* fragment) {
    for (ULONG offset = 0; text[offset] != L'\0'; ++offset) {
        ULONG index = 0;
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

bool IsFireWirePath(const wchar_t* path) {
    return path != 0 && (ContainsInsensitive(path, L"61883") ||
                         ContainsInsensitive(path, L"1394") ||
                         ContainsInsensitive(path, L"avc") ||
                         ContainsInsensitive(path, L"firewire"));
}

HRESULT NormalizeCapturePath(const wchar_t* requested,
                             CaptureKind kind,
                             wchar_t* result,
                             ULONG capacity) {
    if (requested == 0 || result == 0 || capacity == 0) return E_POINTER;
    ULONG length = 0;
    while (requested[length] != L'\0') {
        if (length + 1 >= capacity) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        result[length] = requested[length];
        ++length;
    }
    result[length] = L'\0';
    const wchar_t* suffix = kind == CaptureKind::Dv ? L".dv" : L".m2t";
    if (kind == CaptureKind::Hdv && EndsWithInsensitive(result, L".dv")) {
        length -= 3;
        result[length] = L'\0';
    } else if (kind == CaptureKind::Dv &&
               EndsWithInsensitive(result, L".m2t")) {
        length -= 4;
        result[length] = L'\0';
    } else if (EndsWithInsensitive(result, suffix)) {
        return S_OK;
    }
    ULONG suffixLength = 0;
    while (suffix[suffixLength] != L'\0') ++suffixLength;
    if (length + suffixLength + 1 > capacity) {
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
    for (ULONG index = 0; index < suffixLength; ++index) {
        result[length + index] = suffix[index];
    }
    result[length + suffixLength] = L'\0';
    return S_OK;
}

volatile LONG g_productStop = 0;

BOOL WINAPI ProductControlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_productStop, 1);
        return TRUE;
    }
    return FALSE;
}

void AppendTwoDigits(Text& output, ULONG value) {
    output.AppendChar(static_cast<wchar_t>(L'0' + (value / 10) % 10));
    output.AppendChar(static_cast<wchar_t>(L'0' + value % 10));
}

void AppendTimecode(Text& output, LONG packed, bool available) {
    if (!available) {
        output.Append(L"?:??:??:??");
        return;
    }
    AppendTwoDigits(output, (packed >> 24) & 0xFF);
    output.AppendChar(L':');
    AppendTwoDigits(output, (packed >> 16) & 0xFF);
    output.AppendChar(L':');
    AppendTwoDigits(output, (packed >> 8) & 0xFF);
    output.AppendChar(L':');
    AppendTwoDigits(output, packed & 0xFF);
}

bool ReadCurrentTimecode(CaptureKind kind,
                         CaptureSink* sink,
                         IAMTimecodeReader* reader,
                         LONG* packed) {
    if (packed == 0) return false;
    if (kind == CaptureKind::Dv) {
        DvTimecode value;
        if (!sink->LatestTimecode(&value)) return false;
        *packed = (value.hours << 24) | (value.minutes << 16) |
                  (value.seconds << 8) | value.frames;
        return true;
    }
    if (reader == 0) return false;
    TIMECODE_SAMPLE sample = {};
    sample.dwFlags = ED_DEVCAP_TIMECODE_READ;
    if (FAILED(reader->GetTimecode(&sample))) return false;
    const DWORD value = sample.timecode.dwFrames;
    const int hours = ((value >> 28) & 0x0F) * 10 + ((value >> 24) & 0x0F);
    const int minutes = ((value >> 20) & 0x0F) * 10 + ((value >> 16) & 0x0F);
    const int seconds = ((value >> 12) & 0x0F) * 10 + ((value >> 8) & 0x0F);
    const int frames = ((value >> 4) & 0x0F) * 10 + (value & 0x0F);
    if (hours >= 24 || minutes >= 60 || seconds >= 60 || frames >= 60) return false;
    *packed = (hours << 24) | (minutes << 16) | (seconds << 8) | frames;
    return true;
}

void AppendDuration(Text& output, CaptureSink* sink, ULONG elapsed) {
    REFERENCE_TIME duration = 0;
    if (sink->MediaDuration(&duration)) {
        LONGLONG remainder = duration;
        ULONG seconds = 0;
        while (remainder >= 10000000 && seconds != 0xFFFFFFFF) {
            remainder -= 10000000;
            ++seconds;
        }
        AppendTwoDigits(output, static_cast<ULONG>((seconds / 3600) % 100));
        output.AppendChar(L':');
        AppendTwoDigits(output, static_cast<ULONG>((seconds / 60) % 60));
        output.AppendChar(L':');
        AppendTwoDigits(output, static_cast<ULONG>(seconds % 60));
        return;
    }
    const ULONG seconds = elapsed / 1000;
    AppendTwoDigits(output, (seconds / 3600) % 100);
    output.AppendChar(L':');
    AppendTwoDigits(output, (seconds / 60) % 60);
    output.AppendChar(L':');
    AppendTwoDigits(output, seconds % 60);
}

void AppendWallDuration(Text& output, ULONG elapsed) {
    const ULONG seconds = elapsed / 1000;
    AppendTwoDigits(output, (seconds / 3600) % 100);
    output.AppendChar(L':');
    AppendTwoDigits(output, (seconds / 60) % 60);
    output.AppendChar(L':');
    AppendTwoDigits(output, seconds % 60);
}

void AppendTimecodeDuration(Text& output, LONG first, LONG last, bool available) {
    if (!available) {
        output.Append(L"");
        return;
    }
    const LONG firstFrames = (((first >> 24) & 0xFF) * 3600 +
                              ((first >> 16) & 0xFF) * 60 +
                              ((first >> 8) & 0xFF)) * 30 + (first & 0xFF);
    const LONG lastFrames = (((last >> 24) & 0xFF) * 3600 +
                             ((last >> 16) & 0xFF) * 60 +
                             ((last >> 8) & 0xFF)) * 30 + (last & 0xFF);
    if (lastFrames < firstFrames) {
        output.Append(L"");
        return;
    }
    const ULONG seconds = static_cast<ULONG>((lastFrames - firstFrames) / 30);
    AppendTwoDigits(output, (seconds / 3600) % 100);
    output.AppendChar(L':');
    AppendTwoDigits(output, (seconds / 60) % 60);
    output.AppendChar(L':');
    AppendTwoDigits(output, seconds % 60);
}

void PrintCaptureProgress(CaptureKind kind,
                          CaptureSink* sink,
                          IAMTimecodeReader* reader,
                          ULONG elapsed,
                          LONG* lastTimecode,
                          bool* hasTimecode,
                          LONG* firstTimecode,
                          bool* hasFirstTimecode) {
    LONG currentTimecode = 0;
    const bool currentAvailable = ReadCurrentTimecode(
        kind, sink, reader, &currentTimecode);
    if (currentAvailable) {
        if (!*hasFirstTimecode) {
            *firstTimecode = currentTimecode;
            *hasFirstTimecode = true;
        }
        *lastTimecode = currentTimecode;
        *hasTimecode = true;
    }
    Text progress;
    progress.Append(kind == CaptureKind::Dv ? L"DV" : L"HDV");
    progress.Append(L"  Timecode ");
    AppendTimecode(progress, currentTimecode, currentAvailable);
    progress.Append(L"  Duration ");
    AppendDuration(progress, sink, elapsed);
    progress.Append(L"  Bytes ");
    progress.AppendUInt64(sink->Bytes());
    progress.Append(L"   ");
    progress.Append(L"\r");
    progress.Flush();
}

const wchar_t* WaitForProductStop(CaptureKind kind,
                                  CaptureSink* sink,
                                  IAMExtTransport* transport,
                                  IMediaEventEx* events,
                                  IAMTimecodeReader* reader,
                                  LONG* lastTimecode,
                                  bool* hasTimecode,
                                  LONG* firstTimecode,
                                  bool* hasFirstTimecode,
                                  ULONG* elapsedResult) {
    InterlockedExchange(&g_productStop, 0);
    SetConsoleCtrlHandler(ProductControlHandler, TRUE);
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD consoleMode = 0;
    const bool consoleInput = input != INVALID_HANDLE_VALUE &&
                               GetConsoleMode(input, &consoleMode) != FALSE;
    const bool inputPipe = input != INVALID_HANDLE_VALUE &&
                           GetFileType(input) == FILE_TYPE_PIPE;
    ULONG started = GetTickCount();
    ULONG nextProgress = started;
    ULONG lastActivity = started;
    ULONGLONG lastBytes = sink->Bytes();
    const wchar_t* stopReason = L"Enter";
    while (InterlockedCompareExchange(&g_productStop, 0, 0) == 0) {
        if (consoleInput && WaitForSingleObject(input, 200) == WAIT_OBJECT_0) {
            INPUT_RECORD records[16];
            DWORD read = 0;
            if (ReadConsoleInputW(input, records, ARRAYSIZE(records), &read)) {
                for (DWORD index = 0; index < read; ++index) {
                    if (records[index].EventType == KEY_EVENT &&
                        records[index].Event.KeyEvent.bKeyDown &&
                        records[index].Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                        InterlockedExchange(&g_productStop, 1);
                        stopReason = L"Enter";
                    }
                }
            }
        } else if (inputPipe) {
            DWORD available = 0;
            if (PeekNamedPipe(input, 0, 0, 0, &available, 0) && available != 0) {
                char inputBytes[32] = {};
                DWORD read = 0;
                if (ReadFile(input, inputBytes, sizeof(inputBytes), &read, 0)) {
                    for (DWORD index = 0; index < read; ++index) {
                        if (inputBytes[index] == '\r' || inputBytes[index] == '\n') {
                            stopReason = L"GUI stop";
                            InterlockedExchange(&g_productStop, 1);
                            break;
                        }
                    }
                }
            } else {
                Sleep(200);
            }
        } else {
            Sleep(200);
        }
        ULONG now = GetTickCount();
        if (events != 0) {
            long eventCode = 0;
            LONG_PTR parameter1 = 0;
            LONG_PTR parameter2 = 0;
            while (events->GetEvent(&eventCode, &parameter1, &parameter2, 0) == S_OK) {
                events->FreeEventParams(eventCode, parameter1, parameter2);
                if (eventCode == EC_COMPLETE) {
                    stopReason = L"DirectShow end-of-stream";
                    InterlockedExchange(&g_productStop, 1);
                    break;
                }
                if (eventCode == EC_ERRORABORT || eventCode == EC_USERABORT) {
                    stopReason = L"DirectShow graph abort";
                    InterlockedExchange(&g_productStop, 1);
                    break;
                }
            }
        }
        if (transport != 0 &&
            InterlockedCompareExchange(&g_productStop, 0, 0) == 0) {
            long mode = 0;
            if (SUCCEEDED(transport->get_Mode(&mode)) && mode == ED_MODE_STOP) {
                stopReason = L"Transport STOP";
                InterlockedExchange(&g_productStop, 1);
            }
        }
        ULONG sampleTick = sink->LastSampleTick();
        if (sampleTick != 0 && static_cast<LONG>(sampleTick - lastActivity) > 0) {
            lastActivity = sampleTick;
        }
        const ULONGLONG currentBytes = sink->Bytes();
        if (currentBytes != lastBytes) {
            lastBytes = currentBytes;
            lastActivity = now;
        }
        if (static_cast<ULONG>(now - lastActivity) >= 10000 &&
            InterlockedCompareExchange(&g_productStop, 0, 0) == 0) {
            stopReason = L"No media activity for 10 seconds";
            InterlockedExchange(&g_productStop, 1);
        }
        if (static_cast<LONG>(now - nextProgress) >= 0) {
            PrintCaptureProgress(kind, sink, reader, now - started,
                                 lastTimecode, hasTimecode, firstTimecode,
                                 hasFirstTimecode);
            nextProgress = now + 1000;
        }
    }
    SetConsoleCtrlHandler(ProductControlHandler, FALSE);
    Text clear;
    clear.Append(L"\r");
    for (int index = 0; index < 120; ++index) clear.AppendChar(L' ');
    clear.Append(L"\r\n");
    clear.Flush();
    if (elapsedResult != 0) *elapsedResult = GetTickCount() - started;
    return stopReason;
}

int RunCapture(const wchar_t* outputPath,
               bool indefinite = false,
               bool discard = false,
               bool verbose = false,
               bool overwrite = false) {
    g_productVerbose = verbose;
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintHResult(L"CoInitializeEx", hr);
        return 1;
    }

    ComPtr<IGraphBuilder> graph;
    hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER,
                          IID_IGraphBuilder,
                          reinterpret_cast<void**>(graph.Put()));
    if (FAILED(hr)) {
        PrintHResult(L"Create filter graph", hr);
        CoUninitialize();
        return 1;
    }

    ComPtr<IMediaControl> control;
    hr = graph->QueryInterface(IID_IMediaControl,
                               reinterpret_cast<void**>(control.Put()));
    if (FAILED(hr)) {
        PrintHResult(L"Query media control", hr);
        graph.Reset();
        CoUninitialize();
        return 1;
    }

    ComPtr<IBaseFilter> source;
    ComPtr<IPin> output;
    CaptureKind kind = CaptureKind::Dv;
    wchar_t deviceName[256];
    hr = FindCaptureSource(source.Put(), output.Put(), &kind,
                           deviceName, ARRAYSIZE(deviceName));
    if (FAILED(hr)) {
        PrintHResult(L"Find DV or HDV capture source", hr);
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }
    Text selected;
    selected.Append(L"  Selected device: ");
    selected.Append(deviceName);
    selected.Append(L"\n");
    selected.Flush();

    wchar_t capturePath[1024];
    hr = NormalizeCapturePath(outputPath, kind, capturePath,
                              ARRAYSIZE(capturePath));
    if (FAILED(hr)) {
        PrintHResult(L"Normalize output path", hr);
        output.Reset();
        source.Reset();
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }
    wchar_t partialPath[1024];
    wcscpy_s(partialPath, ARRAYSIZE(partialPath), capturePath);
    ULONG partialLength = 0;
    while (partialPath[partialLength] != L'\0') ++partialLength;
    wcscpy_s(partialPath + partialLength,
             ARRAYSIZE(partialPath) - partialLength, L".partial");
    if (!discard && !overwrite &&
        GetFileAttributesW(capturePath) != INVALID_FILE_ATTRIBUTES) {
        PrintHResult(L"Refuse to overwrite existing output file",
                     HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
        output.Reset();
        source.Reset();
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }
    if (!discard && GetFileAttributesW(partialPath) != INVALID_FILE_ATTRIBUTES) {
        if (!overwrite || !DeleteFileW(partialPath)) {
            PrintHResult(L"Refuse to overwrite existing partial capture file",
                         HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
            output.Reset();
            source.Reset();
            control.Reset();
            graph.Reset();
            CoUninitialize();
            return 1;
        }
    }
    Text outputText;
    outputText.Append(L"  Output path: ");
    outputText.Append(capturePath);
    outputText.Append(L"\n");
    outputText.Flush();

    ComPtr<CaptureSink> sink;
    *sink.Put() = new CaptureSink();
    if (sink.Get() == 0) {
        PrintHResult(L"Create native capture sink", E_OUTOFMEMORY);
        output.Reset();
        source.Reset();
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }
    if (discard && kind == CaptureKind::Hdv) {
        sink->SetDiscard();
    } else {
        hr = sink->Open(partialPath);
        if (FAILED(hr)) {
            PrintHResult(L"Open output file", hr);
            sink.Reset();
            output.Reset();
            source.Reset();
            control.Reset();
            graph.Reset();
            CoUninitialize();
            return 1;
        }
    }

    ProductDiagnosticLine(L"Capture: adding source filter");
    hr = graph->AddFilter(source.Get(), deviceName);
    if (SUCCEEDED(hr)) {
        ProductDiagnosticLine(L"Capture: adding native sink");
        hr = graph->AddFilter(sink.Get(), L"Native file sink");
    }
    ComPtr<IPin> input;
    if (SUCCEEDED(hr)) {
        ProductDiagnosticLine(L"Capture: locating sink input pin");
        hr = sink->FindPin(L"Input", input.Put());
    }
    if (SUCCEEDED(hr)) {
        ProductDiagnosticLine(L"Capture: connecting native pins");
        hr = ConnectNative(graph.Get(), output.Get(), input.Get());
    }
    if (FAILED(hr)) {
        ProductDiagnosticHResult(L"Connect native capture graph", hr);
        input.Reset();
        sink.Reset();
        output.Reset();
        source.Reset();
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }

    ComPtr<IAMExtTransport> transport;
    source->QueryInterface(IID_IAMExtTransport,
                           reinterpret_cast<void**>(transport.Put()));
    long initialTransportMode = 0;
    bool transportModeKnown = false;
    if (transport.Get() != 0) {
        const HRESULT modeHr = transport->get_Mode(&initialTransportMode);
        ProductDiagnosticHResult(L"Read camera transport mode before graph start",
                                 modeHr);
        transportModeKnown = SUCCEEDED(modeHr);
        if (transportModeKnown) {
            Text modeText;
            modeText.Append(L"  Initial transport mode: ");
            modeText.AppendUInt(static_cast<ULONG>(initialTransportMode));
            modeText.Append(L"\n");
            modeText.Flush();
        } else {
            PrintLine(L"  Transport mode is unknown; PLAY will not be issued automatically.");
        }
    } else {
        PrintLine(L"  Automatic PLAY is unavailable; capture will continue and the tape may be started manually.");
    }
    ComPtr<IAMTimecodeReader> timecodeReader;
    const HRESULT timecodeHr = source->QueryInterface(
        IID_IAMTimecodeReader,
        reinterpret_cast<void**>(timecodeReader.Put()));
    ProductDiagnosticHResult(L"Query capture source for IAMTimecodeReader",
                             timecodeHr);
    ComPtr<IMediaEventEx> events;
    graph->QueryInterface(IID_IMediaEventEx,
                          reinterpret_cast<void**>(events.Put()));

    Text captureNotice;
    if (kind == CaptureKind::Dv) {
        captureNotice.Append(L"Capturing native DV to ");
        captureNotice.Append(capturePath);
    } else if (discard) {
        captureNotice.Append(L"HDV discard test is running; no file will be written.");
    } else {
        captureNotice.Append(L"Capturing to ");
        captureNotice.Append(capturePath);
    }
    captureNotice.Append(L". Press Enter to stop.\n");
    captureNotice.Flush();
    ProductDiagnosticLine(L"Capture: starting graph");
    hr = control->Run();
    ProductDiagnosticHResult(L"Run capture graph", hr);
    LONG lastTimecode = 0;
    bool hasTimecode = false;
    LONG firstTimecode = 0;
    bool hasFirstTimecode = false;
    ULONG captureElapsed = 0;
    const wchar_t* stopReason = L"Capture graph did not start";
    if (FAILED(hr)) {
        PrintLine(L"Capture graph did not start.");
    } else {
        if (transport.Get() != 0 && transportModeKnown &&
            initialTransportMode != ED_MODE_PLAY) {
            HRESULT playHr = transport->put_Mode(ED_MODE_PLAY);
            ProductDiagnosticHResult(L"Command camera transport PLAY", playHr);
            if (SUCCEEDED(playHr)) {
                Sleep(1000);
                long mode = 0;
                const HRESULT modeHr = transport->get_Mode(&mode);
                ProductDiagnosticHResult(L"Read camera transport mode after PLAY",
                                         modeHr);
                if (SUCCEEDED(modeHr)) {
                    Text modeText;
                    modeText.Append(L"  Transport mode: ");
                    modeText.AppendUInt(static_cast<ULONG>(mode));
                    modeText.Append(L"\n");
                    modeText.Flush();
                }
            }
        } else if (transport.Get() != 0 &&
                   initialTransportMode == ED_MODE_PLAY) {
            ProductDiagnosticLine(L"  Camera is already playing; PLAY was not issued.");
        }
        stopReason = L"Capture ended";
        if (indefinite) {
            stopReason = WaitForProductStop(
                kind, sink.Get(), transport.Get(), events.Get(),
                timecodeReader.Get(), &lastTimecode, &hasTimecode,
                &firstTimecode, &hasFirstTimecode, &captureElapsed);
        } else {
            Sleep(10000);
        }
    }
    HRESULT graphStopHr = control->Stop();
    ProductDiagnosticHResult(L"Stop capture graph", graphStopHr);
    if (transport.Get() != 0) {
        HRESULT stopTransportHr = transport->put_Mode(ED_MODE_STOP);
        ProductDiagnosticHResult(L"Command transport STOP", stopTransportHr);
        if (SUCCEEDED(stopTransportHr)) {
            Sleep(1000);
            long mode = 0;
            const HRESULT modeHr = transport->get_Mode(&mode);
            ProductDiagnosticHResult(L"Read camera transport mode after STOP",
                                     modeHr);
            if (SUCCEEDED(modeHr)) {
                Text modeText;
                modeText.Append(L"  Transport mode: ");
                modeText.AppendUInt(static_cast<ULONG>(mode));
                modeText.Append(L"\n");
                modeText.Flush();
            }
        }
    }

    if (indefinite) {
        Text summary;
        summary.Append(L"[SUMMARY] Format: ");
        summary.Append(kind == CaptureKind::Dv ? L"DV" : L"HDV");
        summary.Append(L"\n[SUMMARY] Stop reason: ");
        summary.Append(stopReason);
        summary.Append(L"\n[SUMMARY] Output: ");
        summary.Append(discard && kind == CaptureKind::Hdv ? L"(discarded)" : capturePath);
        summary.Append(L"\n[SUMMARY] Final timecode: ");
        AppendTimecode(summary, lastTimecode, hasTimecode);
        summary.Append(L"\n[SUMMARY] Total Capture Process Duration: ");
        AppendWallDuration(summary, captureElapsed);
        summary.Append(L"\n[SUMMARY] Video Duration: ");
        if (hasFirstTimecode && hasTimecode) {
            AppendTimecodeDuration(summary, firstTimecode, lastTimecode, true);
        } else {
            AppendDuration(summary, sink.Get(), captureElapsed);
        }
        summary.Append(L"\n[SUMMARY] Bytes: ");
        summary.AppendUInt64(sink->Bytes());
        summary.Append(L"\n[SUMMARY] ");
        summary.Append(kind == CaptureKind::Dv ? L"DV" : L"HDV");
        summary.Append(L" samples: ");
        summary.AppendUInt(sink->Samples());
        summary.Append(L"\n");
        summary.Flush();
    }

    if (!indefinite) {
        Text result;
        result.Append(L"Samples received: ");
        result.AppendUInt(sink->Samples());
        result.Append(discard && kind == CaptureKind::Hdv
                          ? L" bytes received: "
                          : L" bytes written: ");
        result.AppendUInt64(sink->Bytes());
        result.Append(L"\n");
        result.Flush();
    }

    bool finalized = true;
    if (!discard) {
        graph->RemoveFilter(sink.Get());
        sink.Reset();
        input.Reset();
        output.Reset();
        source.Reset();
        const DWORD moveFlags = overwrite ? MOVEFILE_REPLACE_EXISTING : 0;
        finalized = MoveFileExW(partialPath, capturePath, moveFlags) != FALSE;
        if (!finalized) {
            PrintHResult(L"Finalize partial capture file",
                         HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    transport.Reset();
    events.Reset();
    input.Reset();
    sink.Reset();
    output.Reset();
    source.Reset();
    control.Reset();
    graph.Reset();
    CoUninitialize();
    return FAILED(hr) || !finalized ? 1 : 0;
}

bool NextArgument(const wchar_t** cursor, wchar_t* argument, ULONG capacity) {
    if (cursor == 0 || *cursor == 0 || argument == 0 || capacity == 0) return false;
    const wchar_t* text = *cursor;
    while (*text == L' ' || *text == L'\t') ++text;
    if (*text == L'\0') {
        *cursor = text;
        return false;
    }
    bool quoted = false;
    if (*text == L'\"') {
        quoted = true;
        ++text;
    }
    ULONG length = 0;
    while (*text != L'\0' &&
           (quoted ? *text != L'\"' : (*text != L' ' && *text != L'\t'))) {
        if (length + 1 >= capacity) return false;
        argument[length++] = *text++;
    }
    if (quoted && *text == L'\"') ++text;
    argument[length] = L'\0';
    *cursor = text;
    return length != 0;
}

#ifdef FWCAP_XP
int ProductMain() {
    const wchar_t* cursor = GetCommandLineW();
    wchar_t argument[1024];
    wchar_t outputPath[1024];
    outputPath[0] = L'\0';
    bool verbose = false;
    bool hdvDiscard = false;
    bool overwrite = false;
    ULONG positional = 0;

    // Skip the executable name.
    NextArgument(&cursor, argument, ARRAYSIZE(argument));
    const wchar_t* argumentsAfterExecutable = cursor;
    if (!NextArgument(&argumentsAfterExecutable, argument, ARRAYSIZE(argument))) {
        FreeConsole();
        return RunGui(GetModuleHandleW(0), SW_SHOWNORMAL);
    }
    cursor = argumentsAfterExecutable;
    // The first argument was read only to distinguish GUI mode from CLI mode.
    // Rewind the parser to process it normally below.
    const wchar_t* commandLineAfterExecutable = GetCommandLineW();
    NextArgument(&commandLineAfterExecutable, argument, ARRAYSIZE(argument));
    cursor = commandLineAfterExecutable;
    while (NextArgument(&cursor, argument, ARRAYSIZE(argument))) {
        if (wcscmp(argument, L"-v") == 0 ||
            wcscmp(argument, L"--verbose") == 0) {
            verbose = true;
        } else if (wcscmp(argument, L"--hdv-discard") == 0) {
            hdvDiscard = true;
        } else if (wcscmp(argument, L"--overwrite") == 0) {
            overwrite = true;
        } else if (positional == 0) {
            wcscpy_s(outputPath, ARRAYSIZE(outputPath), argument);
            positional = 1;
        } else {
            PrintLine(L"Usage: fwcap-xp.exe [-v] [--overwrite] [--hdv-discard] <capture-name>");
            return 2;
        }
    }

    if (positional != 1) {
        PrintLine(L"Usage: fwcap-xp.exe [-v] [--overwrite] [--hdv-discard] <capture-name>");
        return 2;
    }
    g_productMode = true;
    g_productVerbose = verbose;
    if (verbose) PrintLine(L"fwcap-xp: verbose capture diagnostics enabled.");
    return RunCapture(outputPath, true, hdvDiscard, verbose, overwrite);
}
#endif

int ProbeMain() {
    wchar_t outputPath[1024];
    if (GetOutputArgument(outputPath, ARRAYSIZE(outputPath))) {
        return RunCapture(outputPath);
    }
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintHResult(L"CoInitializeEx", hr);
        return 1;
    }

    PrintLine(L"XP SP2 DirectShow probe");
    PrintLine(L"Target: x86; no capture graph is started by this diagnostic.");

    ComPtr<ICreateDevEnum> deviceEnumerator;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, 0, CLSCTX_INPROC_SERVER,
                          IID_ICreateDevEnum,
                          reinterpret_cast<void**>(deviceEnumerator.Put()));
    if (FAILED(hr)) {
        PrintHResult(L"Create system device enumerator", hr);
        CoUninitialize();
        return 1;
    }

    ComPtr<IEnumMoniker> monikers;
    hr = deviceEnumerator->CreateClassEnumerator(
        CLSID_VideoInputDeviceCategory, monikers.Put(), 0);
    if (hr == S_FALSE) {
        PrintLine(L"No video input devices found.");
        CoUninitialize();
        return 2;
    }
    if (FAILED(hr)) {
        PrintHResult(L"Enumerate video input devices", hr);
        CoUninitialize();
        return 1;
    }

    ULONG index = 0;
    while (true) {
        ComPtr<IMoniker> moniker;
        hr = monikers->Next(1, moniker.Put(), 0);
        if (hr != S_OK) break;
        InspectDevice(moniker.Get(), index++);
    }

    Text summary;
    summary.Append(L"\nDevices inspected: ");
    summary.AppendUInt(index);
    summary.Append(L"\n");
    summary.Flush();

    // Release COM interfaces while COM is still initialized. This matters on
    // older COM implementations, which may crash if Release occurs after
    // CoUninitialize has returned.
    monikers.Reset();
    deviceEnumerator.Reset();
    CoUninitialize();
    return index == 0 ? 2 : 0;
}

extern "C" void WINAPI XpProbeEntry() {
#ifdef FWCAP_XP
    ExitProcess(static_cast<UINT>(ProductMain()));
#else
    ExitProcess(static_cast<UINT>(ProbeMain()));
#endif
}
