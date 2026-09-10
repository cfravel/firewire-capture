#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <dshow.h>

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
    output.Append(L"\n");
    output.Flush();
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

static const GUID kMediaTypeAudioVideo = {
    0x73766169, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

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
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        return outputFile_ == INVALID_HANDLE_VALUE ? HRESULT_FROM_WIN32(GetLastError())
                                                    : S_OK;
    }
    HRESULT Write(IMediaSample* sample);
    ULONGLONG Bytes() const { return bytes_; }
    ULONG Samples() const { return samples_; }

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
      outputFile_(INVALID_HANDLE_VALUE), bytes_(0), samples_(0) {}

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
    HRESULT hr = sample->GetPointer(&data);
    if (FAILED(hr)) return hr;
    DWORD written = 0;
    if (!WriteFile(outputFile_, data, static_cast<DWORD>(length), &written, 0) ||
        written != static_cast<DWORD>(length)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    InterlockedExchangeAdd64(&bytes_, length);
    InterlockedIncrement(&samples_);
    return S_OK;
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

HRESULT FindCaptureSource(IBaseFilter** source, IPin** output, CaptureKind* kind) {
    if (source == 0 || output == 0 || kind == 0) return E_POINTER;
    *source = 0;
    *output = 0;

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
        Text attempt;
        attempt.Append(L"Capture: trying advertised media type major=");
        attempt.AppendGuid(type->majortype);
        attempt.Append(L" subtype=");
        attempt.AppendGuid(type->subtype);
        attempt.Append(L"\n");
        attempt.Flush();

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

int RunCapture(const wchar_t* outputPath) {
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
    hr = FindCaptureSource(source.Put(), output.Put(), &kind);
    if (FAILED(hr)) {
        PrintHResult(L"Find DV or HDV capture source", hr);
        control.Reset();
        graph.Reset();
        CoUninitialize();
        return 1;
    }

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
    hr = sink->Open(outputPath);
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

    PrintLine(L"Capture: adding source filter");
    hr = graph->AddFilter(source.Get(), L"FireWire source");
    if (SUCCEEDED(hr)) {
        PrintLine(L"Capture: adding native sink");
        hr = graph->AddFilter(sink.Get(), L"Native file sink");
    }
    ComPtr<IPin> input;
    if (SUCCEEDED(hr)) {
        PrintLine(L"Capture: locating sink input pin");
        hr = sink->FindPin(L"Input", input.Put());
    }
    if (SUCCEEDED(hr)) {
        PrintLine(L"Capture: connecting native pins");
        hr = ConnectNative(graph.Get(), output.Get(), input.Get());
    }
    if (FAILED(hr)) {
        PrintHResult(L"Connect native capture graph", hr);
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

    PrintLine(kind == CaptureKind::Dv ?
              L"Capture mode: native DV; running for 10 seconds." :
              L"Capture mode: native HDV; running for 10 seconds.");
    PrintLine(L"Capture: starting graph");
    hr = control->Run();
    PrintHResult(L"Run capture graph", hr);
    if (FAILED(hr)) {
        PrintLine(L"Capture graph did not start.");
    } else {
        if (transport.Get() != 0) {
            long mode = 0;
            HRESULT modeHr = transport->get_Mode(&mode);
            PrintHResult(L"Read transport mode before PLAY", modeHr);
            if (SUCCEEDED(modeHr)) {
                Text modeText;
                modeText.Append(L"Transport mode before PLAY: ");
                modeText.AppendUInt(static_cast<ULONG>(mode));
                modeText.Append(L"\n");
                modeText.Flush();
            }
            HRESULT playHr = transport->put_Mode(ED_MODE_PLAY);
            PrintHResult(L"Command transport PLAY", playHr);
        }
        Sleep(10000);
        if (transport.Get() != 0) {
            HRESULT stopTransportHr = transport->put_Mode(ED_MODE_STOP);
            PrintHResult(L"Command transport STOP", stopTransportHr);
        }
    }
    HRESULT graphStopHr = control->Stop();
    PrintHResult(L"Stop capture graph", graphStopHr);

    Text result;
    result.Append(L"Samples received: ");
    result.AppendUInt(sink->Samples());
    result.Append(L" bytes written: ");
    result.AppendUInt(static_cast<ULONG>(sink->Bytes()));
    result.Append(L"\n");
    result.Flush();

    transport.Reset();
    input.Reset();
    sink.Reset();
    output.Reset();
    source.Reset();
    control.Reset();
    graph.Reset();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}

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
    ExitProcess(static_cast<UINT>(ProbeMain()));
}
