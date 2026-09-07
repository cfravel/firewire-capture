#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <edevdefs.h>
#include <errors.h>
#include <conio.h>

#include <cwchar>
#include <cwctype>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

bool g_verbose = false;

constexpr wchar_t kTransportPinName[] = L"MPEG2TS Out";
constexpr ULONGLONG kNoMediaTimeoutMs = 10000;

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
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
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

private:
    T* value_ = nullptr;
};

std::wstring HResultText(HRESULT hr) {
    wchar_t directShowText[MAX_ERROR_TEXT_LEN] = {};
    if (AMGetErrorTextW(hr, directShowText, MAX_ERROR_TEXT_LEN) != 0) {
        std::wstring text(directShowText);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
            text.pop_back();
        }
        return text;
    }

    wchar_t* systemText = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        0,
        reinterpret_cast<wchar_t*>(&systemText),
        0,
        nullptr);

    if (length == 0 || systemText == nullptr) {
        return SUCCEEDED(hr) ? L"Operation successful" : L"Unknown error";
    }

    std::wstring text(systemText, length);
    LocalFree(systemText);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return text;
}

std::wstring GuidText(const GUID& guid) {
    wchar_t text[64] = {};
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    return text;
}

void DescribeMediaType(const wchar_t* label, const AM_MEDIA_TYPE& type) {
    if (!g_verbose) return;
    std::wcout << L"  " << label << L": major=" << GuidText(type.majortype)
               << L", subtype=" << GuidText(type.subtype)
               << L", format=" << GuidText(type.formattype)
               << L", format-bytes=" << type.cbFormat
               << L", sample-size=" << type.lSampleSize
               << L", fixed=" << (type.bFixedSizeSamples ? L"yes" : L"no")
               << L", temporal-compression="
               << (type.bTemporalCompression ? L"yes" : L"no") << L'\n';
}

void Report(const wchar_t* stage, HRESULT hr) {
    if (SUCCEEDED(hr) && !g_verbose) {
        return;
    }
    std::wostream& output = FAILED(hr) ? std::wcerr : std::wcout;
    output << L"[" << (SUCCEEDED(hr) ? L"OK" : L"ERROR") << L"] " << stage
           << L": HRESULT=0x" << std::hex << std::uppercase
           << std::setfill(L'0') << std::setw(8)
           << static_cast<unsigned long>(hr) << std::dec << L" ("
           << HResultText(hr) << L")\n";
}

enum class CaptureKind {
    Hdv,
    Dv,
};

struct DvTimecode {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
};

int BcdValue(BYTE value) {
    return (value & 0x0F) + 10 * ((value >> 4) & 0x0F);
}

bool FindDvTimecode(const BYTE* data, long length, DvTimecode* result) {
    if (data == nullptr || result == nullptr || length < 5) {
        return false;
    }
    for (long index = 0; index <= length - 5; ++index) {
        if (data[index] != 0x13) {
            continue;
        }
        const int frames = BcdValue(data[index + 1] & 0x3F);
        const int seconds = BcdValue(data[index + 2] & 0x7F);
        const int minutes = BcdValue(data[index + 3] & 0x7F);
        const int hours = BcdValue(data[index + 4] & 0x3F);
        if (frames >= 30 || seconds >= 60 || minutes >= 60 || hours >= 24) {
            continue;
        }
        result->hours = hours;
        result->minutes = minutes;
        result->seconds = seconds;
        result->frames = frames;
        return true;
    }
    return false;
}

void FreeMediaType(AM_MEDIA_TYPE* type) {
    if (type->cbFormat != 0) {
        CoTaskMemFree(type->pbFormat);
    }
    if (type->pUnk != nullptr) {
        type->pUnk->Release();
    }
    CoTaskMemFree(type);
}

void ClearMediaType(AM_MEDIA_TYPE* type) {
    if (type->cbFormat != 0) {
        CoTaskMemFree(type->pbFormat);
    }
    if (type->pUnk != nullptr) {
        type->pUnk->Release();
    }
    ZeroMemory(type, sizeof(*type));
}

bool IsNativeDvType(const AM_MEDIA_TYPE& type) {
    return type.majortype == MEDIATYPE_Interleaved &&
           (type.subtype == MEDIASUBTYPE_dvsd ||
            type.subtype == MEDIASUBTYPE_dvhd ||
            type.subtype == MEDIASUBTYPE_dvsl) &&
           type.formattype == FORMAT_DvInfo;
}

bool IsNativeHdvType(const AM_MEDIA_TYPE& type) {
    return type.majortype == MEDIATYPE_Stream &&
           type.subtype == MEDIASUBTYPE_MPEG2_TRANSPORT &&
           type.formattype == FORMAT_None;
}

bool IsFireWireDevicePath(const VARIANT& value) {
    if (value.vt != VT_BSTR || value.bstrVal == nullptr) {
        return false;
    }
    std::wstring path(value.bstrVal);
    for (wchar_t& character : path) {
        character = static_cast<wchar_t>(towlower(character));
    }
    return path.find(L"61883") != std::wstring::npos ||
           path.find(L"1394") != std::wstring::npos ||
           path.find(L"avc") != std::wstring::npos ||
           path.find(L"firewire") != std::wstring::npos;
}

HRESULT CopyMediaType(AM_MEDIA_TYPE* destination,
                      const AM_MEDIA_TYPE* source) {
    *destination = *source;
    if (source->cbFormat != 0) {
        destination->pbFormat = static_cast<BYTE*>(
            CoTaskMemAlloc(source->cbFormat));
        if (destination->pbFormat == nullptr) {
            return E_OUTOFMEMORY;
        }
        CopyMemory(destination->pbFormat, source->pbFormat, source->cbFormat);
    }
    if (destination->pUnk != nullptr) {
        destination->pUnk->AddRef();
    }
    return S_OK;
}

HRESULT CreateDvMediaType(const GUID& subtype, AM_MEDIA_TYPE** result) {
    *result = static_cast<AM_MEDIA_TYPE*>(
        CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (*result == nullptr) {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(*result, sizeof(AM_MEDIA_TYPE));
    (*result)->majortype = MEDIATYPE_Interleaved;
    (*result)->subtype = subtype;
    (*result)->bFixedSizeSamples = TRUE;
    (*result)->formattype = FORMAT_DvInfo;
    (*result)->cbFormat = sizeof(DVINFO);
    (*result)->pbFormat = static_cast<BYTE*>(
        CoTaskMemAlloc((*result)->cbFormat));
    if ((*result)->pbFormat == nullptr) {
        CoTaskMemFree(*result);
        *result = nullptr;
        return E_OUTOFMEMORY;
    }
    ZeroMemory((*result)->pbFormat, (*result)->cbFormat);
    return S_OK;
}

class DvDiscardPin;

class DvDiscardFilter final : public IBaseFilter {
public:
    explicit DvDiscardFilter(bool hdvDiscard = false, bool writeOutput = true);
    ~DvDiscardFilter();

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetClassID(CLSID* classId) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* info) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph* graph, LPCWSTR name) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR* info) override;
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME start) override;
    STDMETHODIMP GetState(DWORD timeout, FILTER_STATE* state) override;
    STDMETHODIMP SetSyncSource(IReferenceClock* clock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock** clock) override;
    STDMETHODIMP EnumPins(IEnumPins** pins) override;
    STDMETHODIMP FindPin(LPCWSTR id, IPin** pin) override;

    LONG SamplesReceived() const;
    ULONGLONG BytesWritten();
    ULONGLONG LastSampleTick();
    bool MediaDuration(REFERENCE_TIME* duration) const;
    bool LatestTimecode(DvTimecode* result) const;
    HRESULT OpenOutputFile(const wchar_t* path);
    bool AcceptsMediaType(const AM_MEDIA_TYPE& type) const;
    bool IsHdvDiscard() const;
    HRESULT WriteSample(IMediaSample* sample);
    void UpdateMediaTime(IMediaSample* sample, long length);

private:
    LONG references_ = 1;
    DvDiscardPin* pin_ = nullptr;
    IFilterGraph* graph_ = nullptr;
    HANDLE outputFile_ = INVALID_HANDLE_VALUE;
    volatile LONGLONG bytesWritten_ = 0;
    volatile LONGLONG lastSampleTick_ = 0;
    bool hdvDiscard_ = false;
    bool writeOutput_ = true;
    mutable std::mutex timecodeMutex_;
    bool hasTimecode_ = false;
    DvTimecode latestTimecode_;
    bool hasMediaTime_ = false;
    REFERENCE_TIME firstSampleTime_ = 0;
    REFERENCE_TIME lastSampleEndTime_ = 0;
    FILTER_STATE state_ = State_Stopped;
};

class DvDiscardPin final : public IPin, public IMemInputPin {
public:
    explicit DvDiscardPin(DvDiscardFilter* filter) : filter_(filter) {}
    ~DvDiscardPin() { Disconnect(); }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Connect(IPin*, const AM_MEDIA_TYPE*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP ReceiveConnection(IPin* pin,
                                   const AM_MEDIA_TYPE* mediaType) override {
        if (pin == nullptr || mediaType == nullptr || connectedPin_ != nullptr) {
            return E_INVALIDARG;
        }
        if (!filter_->AcceptsMediaType(*mediaType)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        AM_MEDIA_TYPE copy = {};
        HRESULT hr = CopyMediaType(&copy, mediaType);
        if (FAILED(hr)) {
            return hr;
        }
        connectedPin_ = pin;
        connectedPin_->AddRef();
        connectedType_ = copy;
        return S_OK;
    }
    STDMETHODIMP Disconnect() override {
        if (connectedPin_ != nullptr) {
            connectedPin_->Release();
            connectedPin_ = nullptr;
        }
        if (connectedType_.cbFormat != 0 || connectedType_.pUnk != nullptr) {
            FreeMediaType(&connectedType_);
            ZeroMemory(&connectedType_, sizeof(connectedType_));
        }
        return S_OK;
    }
    STDMETHODIMP ConnectedTo(IPin** pin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* mediaType) override {
        if (mediaType == nullptr || connectedPin_ == nullptr) {
            return VFW_E_NOT_CONNECTED;
        }
        return CopyMediaType(mediaType, &connectedType_);
    }
    STDMETHODIMP QueryPinInfo(PIN_INFO* info) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION* direction) override {
        if (direction == nullptr) return E_POINTER;
        *direction = PINDIR_INPUT;
        return S_OK;
    }
    STDMETHODIMP QueryId(LPWSTR* id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* mediaType) override {
        return mediaType != nullptr && filter_->AcceptsMediaType(*mediaType)
                   ? S_OK
                   : S_FALSE;
    }
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** types) override;
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP EndOfStream() override { return S_OK; }
    STDMETHODIMP BeginFlush() override { return S_OK; }
    STDMETHODIMP EndFlush() override { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override {
        return S_OK;
    }

    STDMETHODIMP GetAllocator(IMemAllocator** allocator) override {
        if (allocator == nullptr) return E_POINTER;
        *allocator = nullptr;
        return VFW_E_NO_ALLOCATOR;
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator*, BOOL) override {
        return S_OK;
    }
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Receive(IMediaSample* sample) override {
        const HRESULT hr = filter_->WriteSample(sample);
        if (SUCCEEDED(hr)) {
            InterlockedIncrement(&samplesReceived_);
        }
        return hr;
    }
    STDMETHODIMP ReceiveMultiple(IMediaSample** samples,
                                 long count,
                                 long* processed) override {
        if (processed == nullptr) return E_POINTER;
        *processed = 0;
        for (long index = 0; index < count; ++index) {
            const HRESULT hr = Receive(samples[index]);
            if (FAILED(hr)) {
                return hr;
            }
            ++*processed;
        }
        return S_OK;
    }
    STDMETHODIMP ReceiveCanBlock() override { return S_FALSE; }

    LONG SamplesReceived() const { return samplesReceived_; }

private:
    LONG references_ = 1;
    volatile LONG samplesReceived_ = 0;
    DvDiscardFilter* filter_ = nullptr;
    IPin* connectedPin_ = nullptr;
    AM_MEDIA_TYPE connectedType_ = {};
};

LONG DvDiscardFilter::SamplesReceived() const {
    return pin_->SamplesReceived();
}

ULONGLONG DvDiscardFilter::BytesWritten() {
    return static_cast<ULONGLONG>(
        InterlockedCompareExchange64(&bytesWritten_, 0, 0));
}

ULONGLONG DvDiscardFilter::LastSampleTick() {
    return static_cast<ULONGLONG>(
        InterlockedCompareExchange64(&lastSampleTick_, 0, 0));
}

void DvDiscardFilter::UpdateMediaTime(IMediaSample* sample, long length) {
    UNREFERENCED_PARAMETER(length);
    REFERENCE_TIME start = 0;
    REFERENCE_TIME end = 0;
    if (SUCCEEDED(sample->GetTime(&start, &end))) {
        std::lock_guard<std::mutex> lock(timecodeMutex_);
        if (!hasMediaTime_) {
            firstSampleTime_ = start;
            hasMediaTime_ = true;
        }
        if (end > lastSampleEndTime_) {
            lastSampleEndTime_ = end;
        }
    }
}

bool DvDiscardFilter::MediaDuration(REFERENCE_TIME* duration) const {
    if (duration == nullptr) return false;
    std::lock_guard<std::mutex> lock(timecodeMutex_);
    if (!hasMediaTime_ || lastSampleEndTime_ < firstSampleTime_) {
        return false;
    }
    *duration = lastSampleEndTime_ - firstSampleTime_;
    return true;
}

bool DvDiscardFilter::LatestTimecode(DvTimecode* result) const {
    if (result == nullptr) return false;
    std::lock_guard<std::mutex> lock(timecodeMutex_);
    if (!hasTimecode_) return false;
    *result = latestTimecode_;
    return true;
}

HRESULT DvDiscardFilter::OpenOutputFile(const wchar_t* path) {
    if (path == nullptr) return E_POINTER;
    if (outputFile_ != INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    outputFile_ = CreateFileW(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (outputFile_ == INVALID_HANDLE_VALUE) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

HRESULT DvDiscardFilter::WriteSample(IMediaSample* sample) {
    if (sample == nullptr) return E_POINTER;
    if (outputFile_ == INVALID_HANDLE_VALUE && writeOutput_) return E_HANDLE;

    BYTE* data = nullptr;
    HRESULT hr = sample->GetPointer(&data);
    if (FAILED(hr)) return hr;

    const long length = sample->GetActualDataLength();
    if (length < 0) return E_INVALIDARG;

    if (hdvDiscard_ && !writeOutput_) {
        InterlockedExchangeAdd64(&bytesWritten_, length);
        UpdateMediaTime(sample, length);
        InterlockedExchange64(
            &lastSampleTick_, static_cast<LONGLONG>(GetTickCount64()));
        return S_OK;
    }

    DWORD offset = 0;
    while (offset < static_cast<DWORD>(length)) {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(length) - offset;
        if (!WriteFile(outputFile_, data + offset, remaining, &written, nullptr)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (written == 0) {
            return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
        offset += written;
        InterlockedExchangeAdd64(&bytesWritten_, written);
    }
    DvTimecode timecode;
    if (FindDvTimecode(data, length, &timecode)) {
        std::lock_guard<std::mutex> lock(timecodeMutex_);
        latestTimecode_ = timecode;
        hasTimecode_ = true;
    }
    UpdateMediaTime(sample, length);
    InterlockedExchange64(
        &lastSampleTick_, static_cast<LONGLONG>(GetTickCount64()));
    return S_OK;
}

HRESULT CreateHdvMediaType(AM_MEDIA_TYPE** result) {
    *result = static_cast<AM_MEDIA_TYPE*>(
        CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (*result == nullptr) return E_OUTOFMEMORY;
    ZeroMemory(*result, sizeof(AM_MEDIA_TYPE));
    (*result)->majortype = MEDIATYPE_Stream;
    (*result)->subtype = MEDIASUBTYPE_MPEG2_TRANSPORT;
    (*result)->bFixedSizeSamples = TRUE;
    (*result)->formattype = FORMAT_None;
    return S_OK;
}

class DvEnumPins final : public IEnumPins {
public:
    explicit DvEnumPins(DvDiscardPin* pin) : pin_(pin) { pin_->AddRef(); }
    ~DvEnumPins() { pin_->Release(); }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumPins) {
            *object = static_cast<IEnumPins*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }
    STDMETHODIMP Next(ULONG count, IPin** pins, ULONG* fetched) override;
    STDMETHODIMP Skip(ULONG count) override {
        if (count == 0) return S_OK;
        index_ = 1;
        return S_FALSE;
    }
    STDMETHODIMP Reset() override { index_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumPins** clone) override {
        if (clone == nullptr) return E_POINTER;
        *clone = new DvEnumPins(pin_);
        return *clone == nullptr ? E_OUTOFMEMORY : S_OK;
    }

private:
    LONG references_ = 1;
    DvDiscardPin* pin_ = nullptr;
    ULONG index_ = 0;
};

class DvEnumMediaTypes final : public IEnumMediaTypes {
public:
    explicit DvEnumMediaTypes(bool hdv) : hdv_(hdv) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumMediaTypes) {
            *object = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG references = --references_;
        if (references == 0) delete this;
        return references;
    }
    STDMETHODIMP Next(ULONG count,
                      AM_MEDIA_TYPE** types,
                      ULONG* fetched) override {
        if (types == nullptr || (count != 1 && fetched == nullptr)) {
            return E_POINTER;
        }
        const ULONG limit = hdv_ ? 1 : 3;
        if (index_ >= limit) {
            if (fetched != nullptr) *fetched = 0;
            return S_FALSE;
        }
        static const GUID subtypes[] = {
            MEDIASUBTYPE_dvsd, MEDIASUBTYPE_dvhd, MEDIASUBTYPE_dvsl};
        HRESULT hr = hdv_ ? CreateHdvMediaType(types)
                          : CreateDvMediaType(subtypes[index_], types);
        ++index_;
        if (fetched != nullptr) *fetched = SUCCEEDED(hr) ? 1 : 0;
        return hr;
    }
    STDMETHODIMP Skip(ULONG count) override {
        index_ += count;
        const ULONG limit = hdv_ ? 1 : 3;
        if (index_ > limit) index_ = limit;
        return index_ == limit ? S_FALSE : S_OK;
    }
    STDMETHODIMP Reset() override { index_ = 0; return S_OK; }
    STDMETHODIMP Clone(IEnumMediaTypes** clone) override {
        if (clone == nullptr) return E_POINTER;
        auto* result = new DvEnumMediaTypes(hdv_);
        if (result == nullptr) return E_OUTOFMEMORY;
        result->index_ = index_;
        *clone = result;
        return S_OK;
    }

private:
    LONG references_ = 1;
    ULONG index_ = 0;
    bool hdv_ = false;
};

DvDiscardFilter::DvDiscardFilter(bool hdvDiscard, bool writeOutput)
    : pin_(new DvDiscardPin(this)),
      hdvDiscard_(hdvDiscard),
      writeOutput_(writeOutput) {}

DvDiscardFilter::~DvDiscardFilter() {
    if (outputFile_ != INVALID_HANDLE_VALUE) {
        CloseHandle(outputFile_);
    }
    delete pin_;
}

bool DvDiscardFilter::AcceptsMediaType(const AM_MEDIA_TYPE& type) const {
    return hdvDiscard_ ? IsNativeHdvType(type) : IsNativeDvType(type);
}

bool DvDiscardFilter::IsHdvDiscard() const { return hdvDiscard_; }

STDMETHODIMP DvDiscardFilter::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IMediaFilter ||
        iid == IID_IBaseFilter) {
        *object = static_cast<IBaseFilter*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DvDiscardFilter::AddRef() { return ++references_; }

STDMETHODIMP_(ULONG) DvDiscardFilter::Release() {
    const ULONG references = --references_;
    if (references == 0) delete this;
    return references;
}

STDMETHODIMP DvDiscardFilter::GetClassID(CLSID* classId) {
    if (classId == nullptr) return E_POINTER;
    *classId = CLSID_NULL;
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::QueryFilterInfo(FILTER_INFO* info) {
    if (info == nullptr) return E_POINTER;
    ZeroMemory(info, sizeof(*info));
    wcscpy_s(info->achName, L"DV Discard Sink");
    info->pGraph = graph_;
    if (info->pGraph != nullptr) {
        info->pGraph->AddRef();
    }
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::JoinFilterGraph(IFilterGraph* graph, LPCWSTR) {
    graph_ = graph;
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::QueryVendorInfo(LPWSTR* info) {
    if (info == nullptr) return E_POINTER;
    *info = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP DvDiscardFilter::Stop() { state_ = State_Stopped; return S_OK; }

STDMETHODIMP DvDiscardFilter::Pause() { state_ = State_Paused; return S_OK; }

STDMETHODIMP DvDiscardFilter::Run(REFERENCE_TIME) {
    state_ = State_Running;
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::GetState(DWORD, FILTER_STATE* state) {
    if (state == nullptr) return E_POINTER;
    *state = state_;
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::SetSyncSource(IReferenceClock*) { return S_OK; }

STDMETHODIMP DvDiscardFilter::GetSyncSource(IReferenceClock** clock) {
    if (clock == nullptr) return E_POINTER;
    *clock = nullptr;
    return S_OK;
}

STDMETHODIMP DvDiscardFilter::EnumPins(IEnumPins** pins) {
    if (pins == nullptr) return E_POINTER;
    *pins = new DvEnumPins(pin_);
    return *pins == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP DvDiscardFilter::FindPin(LPCWSTR id, IPin** pin) {
    if (id == nullptr || pin == nullptr) return E_POINTER;
    *pin = nullptr;
    if (wcscmp(id, L"Input") != 0) return VFW_E_NOT_FOUND;
    *pin = pin_;
    pin_->AddRef();
    return S_OK;
}

STDMETHODIMP DvDiscardPin::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
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

STDMETHODIMP_(ULONG) DvDiscardPin::AddRef() { return ++references_; }

STDMETHODIMP_(ULONG) DvDiscardPin::Release() {
    const ULONG references = --references_;
    if (references == 0) delete this;
    return references;
}

STDMETHODIMP DvDiscardPin::ConnectedTo(IPin** pin) {
    if (pin == nullptr) return E_POINTER;
    *pin = nullptr;
    if (connectedPin_ == nullptr) return VFW_E_NOT_CONNECTED;
    *pin = connectedPin_;
    connectedPin_->AddRef();
    return S_OK;
}

STDMETHODIMP DvDiscardPin::QueryPinInfo(PIN_INFO* info) {
    if (info == nullptr) return E_POINTER;
    ZeroMemory(info, sizeof(*info));
    info->dir = PINDIR_INPUT;
    wcscpy_s(info->achName, L"Input");
    info->pFilter = filter_;
    filter_->AddRef();
    return S_OK;
}

STDMETHODIMP DvDiscardPin::QueryId(LPWSTR* id) {
    if (id == nullptr) return E_POINTER;
    *id = static_cast<LPWSTR>(CoTaskMemAlloc(12 * sizeof(wchar_t)));
    if (*id == nullptr) return E_OUTOFMEMORY;
    wcscpy_s(*id, 12, L"Input");
    return S_OK;
}

STDMETHODIMP DvDiscardPin::EnumMediaTypes(IEnumMediaTypes** types) {
    if (types == nullptr) return E_POINTER;
    *types = new DvEnumMediaTypes(filter_->IsHdvDiscard());
    return *types == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP DvEnumPins::Next(ULONG count, IPin** pins, ULONG* fetched) {
    if (pins == nullptr || (count != 1 && fetched == nullptr)) return E_POINTER;
    if (index_ != 0 || count == 0) {
        if (fetched != nullptr) *fetched = 0;
        return S_FALSE;
    }
    pins[0] = pin_;
    pin_->AddRef();
    index_ = 1;
    if (fetched != nullptr) *fetched = 1;
    return S_OK;
}

HRESULT FindCaptureOutput(IBaseFilter* filter,
                          CaptureKind* kind,
                          IPin** result) {
    *result = nullptr;

    ComPtr<IEnumPins> pins;
    HRESULT hr = filter->EnumPins(pins.Put());
    Report(L"Enumerate filter pins", hr);
    if (FAILED(hr)) {
        return hr;
    }

    while (true) {
        ComPtr<IPin> pin;
        hr = pins->Next(1, pin.Put(), nullptr);
        if (hr != S_OK) {
            break;
        }

        PIN_DIRECTION direction = PINDIR_INPUT;
        if (FAILED(pin->QueryDirection(&direction)) || direction != PINDIR_OUTPUT) {
            continue;
        }

        PIN_INFO info = {};
        if (FAILED(pin->QueryPinInfo(&info))) {
            continue;
        }
        const bool namedMpegTransport =
            wcscmp(info.achName, kTransportPinName) == 0;
        std::wcout << L"  Candidate pin: " << info.achName << L'\n';
        if (info.pFilter != nullptr) {
            info.pFilter->Release();
        }

        ComPtr<IEnumMediaTypes> mediaTypes;
        if (SUCCEEDED(pin->EnumMediaTypes(mediaTypes.Put()))) {
            while (true) {
                AM_MEDIA_TYPE* mediaType = nullptr;
                hr = mediaTypes->Next(1, &mediaType, nullptr);
                if (hr != S_OK) {
                    break;
                }
                const bool nativeDv = IsNativeDvType(*mediaType);
                if (namedMpegTransport) {
                    DescribeMediaType(L"MPEG2TS Out advertised media type", *mediaType);
                }
                if (nativeDv) {
                    std::wcout << L"  Native DV media: MEDIATYPE_Interleaved, "
                               << L"DV subtype, FORMAT_DvInfo, sample size "
                               << mediaType->lSampleSize << L" bytes\n";
                }
                FreeMediaType(mediaType);
                if (nativeDv) {
                    *kind = CaptureKind::Dv;
                    *result = pin.Get();
                    (*result)->AddRef();
                    return S_OK;
                }
            }
        }

        if (namedMpegTransport) {
            *kind = CaptureKind::Hdv;
            *result = pin.Get();
            (*result)->AddRef();
            return S_OK;
        }
    }

    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT FindCamera(IBaseFilter** camera,
                   IPin** captureOutput,
                   CaptureKind* kind,
                   std::wstring* friendlyName) {
    *camera = nullptr;
    *captureOutput = nullptr;

    ComPtr<ICreateDevEnum> deviceEnumerator;
    HRESULT hr = CoCreateInstance(
        CLSID_SystemDeviceEnum,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(deviceEnumerator.Put()));
    Report(L"Create DirectShow system device enumerator", hr);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IEnumMoniker> monikers;
    hr = deviceEnumerator->CreateClassEnumerator(
        CLSID_VideoInputDeviceCategory, monikers.Put(), 0);
    Report(L"Enumerate DirectShow video input devices", hr);
    if (hr == S_FALSE) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    if (FAILED(hr)) {
        return hr;
    }

    while (true) {
        ComPtr<IMoniker> moniker;
        hr = monikers->Next(1, moniker.Put(), nullptr);
        if (hr == S_FALSE) {
            Report(L"Finish enumerating video input devices", hr);
            break;
        }
        Report(L"Read next video input device", hr);
        if (FAILED(hr)) {
            return hr;
        }

        ComPtr<IPropertyBag> properties;
        hr = moniker->BindToStorage(
            nullptr, nullptr, IID_PPV_ARGS(properties.Put()));
        Report(L"Open video device property bag", hr);
        if (FAILED(hr)) {
            continue;
        }

        VARIANT friendlyNameValue;
        VariantInit(&friendlyNameValue);
        hr = properties->Read(L"FriendlyName", &friendlyNameValue, nullptr);
        Report(L"Read video device FriendlyName", hr);
        if (FAILED(hr) || friendlyNameValue.vt != VT_BSTR ||
            friendlyNameValue.bstrVal == nullptr) {
            VariantClear(&friendlyNameValue);
            continue;
        }
        std::wstring candidateName(friendlyNameValue.bstrVal);
        VariantClear(&friendlyNameValue);

        std::wcout << L"  Device: " << candidateName << L'\n';
        VARIANT devicePath;
        VariantInit(&devicePath);
        hr = properties->Read(L"DevicePath", &devicePath, nullptr);
        Report(L"Read video device DevicePath", hr);
        std::wcout << L"  DevicePath present: "
                   << (SUCCEEDED(hr) && devicePath.vt == VT_BSTR &&
                               devicePath.bstrVal != nullptr
                           ? L"yes\n"
                           : L"no\n");
        const bool isFireWire = IsFireWireDevicePath(devicePath);
        std::wcout << L"  FireWire path classification: "
                   << (isFireWire ? L"yes\n" : L"no\n");
        VariantClear(&devicePath);
        if (!isFireWire) {
            std::wcout << L"  Skip non-FireWire video device before source activation.\n";
            continue;
        }

        ComPtr<IBaseFilter> candidate;
        hr = moniker->BindToObject(
            nullptr, nullptr, IID_PPV_ARGS(candidate.Put()));
        Report(L"Bind video input source filter", hr);
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IPin> candidateOutput;
        CaptureKind candidateKind = CaptureKind::Hdv;
        hr = FindCaptureOutput(
            candidate.Get(), &candidateKind, candidateOutput.Put());
        if (SUCCEEDED(hr)) {
            candidate->AddRef();
            *camera = candidate.Get();
            candidateOutput->AddRef();
            *captureOutput = candidateOutput.Get();
            *kind = candidateKind;
            *friendlyName = candidateName;
            Report(L"Find native HDV or DV capture device", S_OK);
            return S_OK;
        }
    }

    hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    Report(L"Find native HDV or DV capture device", hr);
    return hr;
}

HRESULT FindPin(IBaseFilter* filter,
                PIN_DIRECTION requiredDirection,
                const wchar_t* requiredName,
                IPin** result) {
    *result = nullptr;

    ComPtr<IEnumPins> pins;
    HRESULT hr = filter->EnumPins(pins.Put());
    Report(L"Enumerate filter pins", hr);
    if (FAILED(hr)) {
        return hr;
    }

    while (true) {
        ComPtr<IPin> pin;
        hr = pins->Next(1, pin.Put(), nullptr);
        if (hr == S_FALSE) {
            break;
        }
        if (FAILED(hr)) {
            Report(L"Read next filter pin", hr);
            return hr;
        }

        PIN_DIRECTION direction = PINDIR_INPUT;
        hr = pin->QueryDirection(&direction);
        Report(L"Read filter pin direction", hr);
        if (FAILED(hr) || direction != requiredDirection) {
            continue;
        }

        PIN_INFO info = {};
        hr = pin->QueryPinInfo(&info);
        Report(L"Read filter pin name", hr);
        if (FAILED(hr)) {
            continue;
        }

        const bool matches = requiredName == nullptr ||
                             wcscmp(info.achName, requiredName) == 0;
        std::wcout << L"  Candidate pin: " << info.achName << L'\n';
        if (info.pFilter != nullptr) {
            info.pFilter->Release();
        }

        if (matches) {
            *result = pin.Get();
            (*result)->AddRef();
            Report(requiredName == nullptr
                       ? L"Find filter input pin"
                       : (wcscmp(requiredName, kTransportPinName) == 0
                              ? L"Find MPEG2TS Out pin"
                              : L"Find named filter pin"),
                   S_OK);
            return S_OK;
        }
    }

    hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    Report(requiredName == nullptr
               ? L"Find filter input pin"
               : (wcscmp(requiredName, kTransportPinName) == 0
                      ? L"Find MPEG2TS Out pin"
                      : L"Find named filter pin"),
           hr);
    return hr;
}

bool QueryTransportMode(IBaseFilter* camera,
                        ComPtr<IAMExtTransport>& transport,
                        long* mode) {
    HRESULT hr = camera->QueryInterface(IID_PPV_ARGS(transport.Put()));
    Report(L"Query capture source filter for IAMExtTransport", hr);
    if (FAILED(hr)) {
        std::wcout << L"  Automatic PLAY is unavailable; capture will continue "
                   << L"and the tape may be started manually.\n";
        return false;
    }

    hr = transport->get_Mode(mode);
    Report(L"Read camera transport mode before graph start", hr);
    if (FAILED(hr)) {
        std::wcout << L"  Transport mode is unknown; PLAY will not be issued automatically.\n";
        return false;
    }

    std::wcout << L"  Initial transport mode: " << *mode
               << (*mode == ED_MODE_PLAY ? L" (ED_MODE_PLAY)\n" : L"\n");
    return true;
}

void StartTransportIfNeeded(IAMExtTransport* transport,
                            bool modeKnown,
                            long initialMode) {
    if (transport == nullptr || !modeKnown) {
        return;
    }
    if (initialMode == ED_MODE_PLAY) {
        std::wcout << L"  Camera is already playing; PLAY was not issued.\n";
        return;
    }

    const HRESULT hr = transport->put_Mode(ED_MODE_PLAY);
    Report(L"Command camera transport PLAY", hr);
    if (FAILED(hr)) {
        std::wcout << L"  Automatic PLAY failed; capture will continue and the "
                   << L"tape may be started manually.\n";
        return;
    }

    // FireWire transports accept PLAY immediately but report the mode asynchronously.
    Sleep(1000);
    long mode = 0;
    const HRESULT modeHr = transport->get_Mode(&mode);
    Report(L"Read camera transport mode after PLAY", modeHr);
    if (SUCCEEDED(modeHr)) {
        std::wcout << L"  Transport mode: " << mode
                   << (mode == ED_MODE_PLAY ? L" (ED_MODE_PLAY)\n" : L"\n");
    }
}

void StopTransport(IAMExtTransport* transport) {
    HRESULT hr = transport->put_Mode(ED_MODE_STOP);
    Report(L"Command camera transport STOP", hr);
    if (FAILED(hr)) {
        std::wcout << L"  Automatic STOP failed; stop the tape manually.\n";
        return;
    }

    // FireWire transports accept STOP immediately but report the mode asynchronously.
    Sleep(1000);
    long mode = 0;
    hr = transport->get_Mode(&mode);
    Report(L"Read camera transport mode after STOP", hr);
    if (SUCCEEDED(hr)) {
        std::wcout << L"  Transport mode: " << mode
                   << (mode == ED_MODE_STOP ? L" (ED_MODE_STOP)\n" : L"\n");
    }
}

const wchar_t* FilterStateName(OAFilterState state) {
    switch (state) {
    case State_Stopped: return L"stopped";
    case State_Paused: return L"paused";
    case State_Running: return L"running";
    default: return L"unknown";
    }
}

void ReportGraphFailureState(IMediaControl* control,
                             IMediaEventEx* events,
                             IAMExtTransport* transport) {
    OAFilterState state = State_Stopped;
    HRESULT hr = control->GetState(1000, &state);
    Report(L"Read graph state after failed Run", hr);
    if (SUCCEEDED(hr)) {
        std::wcerr << L"  Graph state: " << FilterStateName(state) << L'\n';
    }

    if (events != nullptr) {
        long eventCode = 0;
        LONG_PTR parameter1 = 0;
        LONG_PTR parameter2 = 0;
        while (events->GetEvent(
                   &eventCode, &parameter1, &parameter2, 0) == S_OK) {
            std::wcerr << L"  Graph event after failed Run: 0x" << std::hex
                       << std::uppercase << eventCode << std::dec << L'\n';
            events->FreeEventParams(eventCode, parameter1, parameter2);
        }
    }

    if (transport != nullptr) {
        long mode = 0;
        hr = transport->get_Mode(&mode);
        Report(L"Read camera transport mode after failed Run", hr);
        if (SUCCEEDED(hr)) {
            std::wcerr << L"  Transport mode after failed Run: " << mode << L'\n';
        }
    }
}

ULONGLONG GetFileSizeBytes(const wchar_t* path) {
    HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER size = {};
    const BOOL success = GetFileSizeEx(file, &size);
    CloseHandle(file);
    return success ? static_cast<ULONGLONG>(size.QuadPart) : 0;
}

std::wstring FormatDuration(std::chrono::steady_clock::duration elapsed) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    const auto hours = seconds / 3600;
    const auto minutes = (seconds / 60) % 60;
    const auto remainder = seconds % 60;
    std::wostringstream text;
    text << std::setfill(L'0') << std::setw(2) << hours << L':'
         << std::setw(2) << minutes << L':' << std::setw(2) << remainder;
    return text.str();
}

std::wstring FormatMediaDuration(REFERENCE_TIME duration) {
    if (duration < 0) return L"?:??:??";
    const LONGLONG seconds = duration / 10000000;
    const LONGLONG hours = seconds / 3600;
    const LONGLONG minutes = (seconds / 60) % 60;
    const LONGLONG remainder = seconds % 60;
    std::wostringstream text;
    text << std::setfill(L'0') << std::setw(2) << hours << L':'
         << std::setw(2) << minutes << L':' << std::setw(2) << remainder;
    return text.str();
}

bool ParseTimecode(const std::wstring& text, LONGLONG* frames) {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frame = 0;
    if (swscanf_s(text.c_str(), L"%d:%d:%d:%d", &hours, &minutes,
                  &seconds, &frame) != 4 || hours < 0 || minutes >= 60 ||
        seconds >= 60 || frame >= 30) {
        return false;
    }
    *frames = (static_cast<LONGLONG>(hours) * 3600 +
               static_cast<LONGLONG>(minutes) * 60 + seconds) * 30 + frame;
    return true;
}

std::wstring FormatTimecodeDuration(const std::wstring& first,
                                    const std::wstring& last) {
    LONGLONG firstFrames = 0;
    LONGLONG lastFrames = 0;
    if (!ParseTimecode(first, &firstFrames) ||
        !ParseTimecode(last, &lastFrames) || lastFrames < firstFrames) {
        return L"";
    }
    return FormatMediaDuration((lastFrames - firstFrames) * 10000000 / 30);
}

std::wstring FormatCaptureDuration(
    DvDiscardFilter* sink,
    std::chrono::steady_clock::time_point started) {
    REFERENCE_TIME mediaDuration = 0;
    if (sink != nullptr && sink->MediaDuration(&mediaDuration)) {
        return FormatMediaDuration(mediaDuration);
    }
    return FormatDuration(std::chrono::steady_clock::now() - started);
}

std::wstring FormatTimecode(CaptureKind kind,
                            DvDiscardFilter* dvSink,
                            IAMTimecodeReader* timecodeReader) {
    DvTimecode timecode;
    if (kind == CaptureKind::Dv && dvSink != nullptr &&
        dvSink->LatestTimecode(&timecode)) {
        std::wostringstream text;
        text << std::setfill(L'0') << std::setw(2) << timecode.hours << L':'
             << std::setw(2) << timecode.minutes << L':'
             << std::setw(2) << timecode.seconds << L':'
             << std::setw(2) << timecode.frames;
        return text.str();
    }
    if (kind == CaptureKind::Hdv && timecodeReader != nullptr) {
        TIMECODE_SAMPLE sample = {};
        sample.timecode.dwFrames = 0;
        sample.dwFlags = ED_DEVCAP_TIMECODE_READ;
        if (SUCCEEDED(timecodeReader->GetTimecode(&sample))) {
            const DWORD packed = sample.timecode.dwFrames;
            const int hours = ((packed >> 28) & 0x0F) * 10 + ((packed >> 24) & 0x0F);
            const int minutes = ((packed >> 20) & 0x0F) * 10 + ((packed >> 16) & 0x0F);
            const int seconds = ((packed >> 12) & 0x0F) * 10 + ((packed >> 8) & 0x0F);
            const int frames = ((packed >> 4) & 0x0F) * 10 + (packed & 0x0F);
            if (hours < 24 && minutes < 60 && seconds < 60 && frames < 60) {
                std::wostringstream text;
                text << std::setfill(L'0') << std::setw(2) << hours << L':'
                     << std::setw(2) << minutes << L':'
                     << std::setw(2) << seconds << L':'
                     << std::setw(2) << frames;
                return text.str();
            }
        }
    }
    return L"?:??:??:??";
}

void PrintProgressLine(CaptureKind kind,
                       const wchar_t* outputPath,
                       DvDiscardFilter* dvSink,
                       const std::wstring& timecode,
                       std::chrono::steady_clock::time_point started) {
    const ULONGLONG bytes = dvSink != nullptr
                                ? dvSink->BytesWritten()
                                : GetFileSizeBytes(outputPath);
    std::wostringstream line;
    line << (kind == CaptureKind::Dv ? L"DV" : L"HDV")
         << L"  Timecode " << timecode << L"  Duration "
         << FormatCaptureDuration(dvSink, started)
         << L"  Bytes " << bytes;
    std::wcout << L'\r' << line.str() << L"   " << std::flush;
}

void PrintFinalSummary(CaptureKind kind,
                       const wchar_t* outputPath,
                       DvDiscardFilter* dvSink,
                       const std::wstring& finalTimecode,
                       const std::wstring& firstTimecode,
                       std::chrono::steady_clock::time_point started,
                       std::chrono::steady_clock::time_point stopIssued,
                       bool discardMode,
                       bool* hasMediaDuration = nullptr) {
    const ULONGLONG bytes = dvSink != nullptr
                                ? dvSink->BytesWritten()
                                : GetFileSizeBytes(outputPath);
    std::wcout << L"[SUMMARY] Format: "
               << (kind == CaptureKind::Dv ? L"DV" : L"HDV") << L"\n"
               << L"[SUMMARY] Output: "
               << (discardMode ? L"discard mode; no file written\n"
                               : outputPath)
               << (discardMode ? L"" : L"\n")
               << L"[SUMMARY] Final timecode: " << finalTimecode << L"\n"
               << L"[SUMMARY] Total Capture Process Duration: "
               << FormatDuration(stopIssued - started)
               << L"\n"
               << L"[SUMMARY] Video Duration: ";
    REFERENCE_TIME mediaDuration = 0;
    const std::wstring timecodeDuration =
        FormatTimecodeDuration(firstTimecode, finalTimecode);
    const bool mediaDurationAvailable =
        dvSink != nullptr && dvSink->MediaDuration(&mediaDuration);
    if (hasMediaDuration != nullptr) *hasMediaDuration = mediaDurationAvailable;
    std::wcout << (!timecodeDuration.empty()
                       ? timecodeDuration
                       : (mediaDurationAvailable
                       ? FormatMediaDuration(mediaDuration)
                       : FormatDuration(stopIssued - started)))
               << L"\n"
               << L"[SUMMARY] Bytes: " << bytes << L"\n";
    if (dvSink != nullptr) {
        std::wcout << L"[SUMMARY] "
                   << (kind == CaptureKind::Dv ? L"DV" : L"HDV")
                   << L" samples: " << dvSink->SamplesReceived() << L"\n";
    }
}

int RunCapture(const wchar_t* outputPath, bool hdvDiscardMode) {
    ComPtr<IGraphBuilder> graph;
    HRESULT hr = CoCreateInstance(
        CLSID_FilterGraph,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(graph.Put()));
    Report(L"Create DirectShow filter graph", hr);
    if (FAILED(hr)) {
        return 1;
    }

    ComPtr<IBaseFilter> camera;
    ComPtr<IPin> captureOutput;
    CaptureKind captureKind = CaptureKind::Hdv;
    std::wstring deviceName;
    hr = FindCamera(
        camera.Put(), captureOutput.Put(), &captureKind, &deviceName);
    if (FAILED(hr)) {
        return 1;
    }

    ComPtr<IAMExtTransport> transport;
    long initialTransportMode = 0;
    const bool transportModeKnown =
        QueryTransportMode(camera.Get(), transport, &initialTransportMode);
    auto captureStarted = std::chrono::steady_clock::time_point{};
    if (transportModeKnown && initialTransportMode == ED_MODE_PLAY) {
        captureStarted = std::chrono::steady_clock::now();
    }
    ComPtr<IAMTimecodeReader> timecodeReader;
    hr = camera->QueryInterface(IID_PPV_ARGS(timecodeReader.Put()));
    Report(L"Query capture source for IAMTimecodeReader", hr);
    if (SUCCEEDED(hr) && g_verbose) {
        std::wcout << L"  IAMTimecodeReader: supported\n";
    }

    std::wcout << L"  Selected device: " << deviceName << L'\n';
    std::wstring normalizedOutputPath(outputPath);
    const wchar_t* extension = wcsrchr(normalizedOutputPath.c_str(), L'.');
    if (captureKind == CaptureKind::Dv) {
        if (extension == nullptr || _wcsicmp(extension, L".dv") != 0) {
            normalizedOutputPath += L".dv";
        }
    } else if (extension == nullptr || _wcsicmp(extension, L".m2t") != 0) {
        normalizedOutputPath += L".m2t";
    }
    const wchar_t* capturePath = normalizedOutputPath.c_str();
    std::wcout << L"  Output path: " << capturePath << L'\n';
    hr = graph->AddFilter(camera.Get(), deviceName.c_str());
    Report(L"Add capture source to graph", hr);
    if (FAILED(hr)) {
        return 1;
    }

    ComPtr<IBaseFilter> dvSink;
    DvDiscardFilter* dvDiscardSink = nullptr;
    bool outputConfigured = false;
    if (captureKind == CaptureKind::Dv) {
        dvDiscardSink = new DvDiscardFilter();
        *dvSink.Put() = dvDiscardSink;
        if (dvSink.Get() == nullptr) {
            Report(L"Create native DV discard sink", E_OUTOFMEMORY);
            return 1;
        }

        hr = dvDiscardSink->OpenOutputFile(capturePath);
        Report(L"Create native DV output file", hr);
        if (FAILED(hr)) {
            return 1;
        }

        hr = graph->AddFilter(dvSink.Get(), L"DV Discard Sink");
        Report(L"Add native DV discard sink to graph", hr);
        if (FAILED(hr)) {
            return 1;
        }

        ComPtr<IPin> sinkInput;
        hr = FindPin(dvSink.Get(), PINDIR_INPUT, L"Input", sinkInput.Put());
        Report(L"Find native DV discard sink input", hr);
        if (FAILED(hr)) {
            return 1;
        }

        hr = graph->ConnectDirect(captureOutput.Get(), sinkInput.Get(), nullptr);
        Report(L"Connect native DV output to discard sink", hr);
        if (FAILED(hr)) {
            return 1;
        }

        std::wcout << L"  Native DV samples will be written to the requested .dv file.\n";
    }

    if (captureKind == CaptureKind::Hdv) {
        dvDiscardSink = new DvDiscardFilter(true, !hdvDiscardMode);
        *dvSink.Put() = dvDiscardSink;
        if (dvSink.Get() == nullptr) {
            Report(L"Create native HDV discard sink", E_OUTOFMEMORY);
            return 1;
        }
        hr = graph->AddFilter(
            dvSink.Get(), hdvDiscardMode ? L"HDV Discard Sink" : L"Native HDV Sink");
        Report(hdvDiscardMode ? L"Add native HDV discard sink to graph"
                             : L"Add native HDV sink to graph",
               hr);
        if (FAILED(hr)) return 1;

        if (!hdvDiscardMode) {
            hr = dvDiscardSink->OpenOutputFile(capturePath);
            Report(L"Create native HDV output file", hr);
            if (FAILED(hr)) return 1;
            outputConfigured = true;
        }

        ComPtr<IPin> sinkInput;
        hr = FindPin(dvSink.Get(), PINDIR_INPUT, L"Input", sinkInput.Put());
        Report(L"Find native HDV discard sink input", hr);
        if (FAILED(hr)) return 1;

        hr = graph->ConnectDirect(captureOutput.Get(), sinkInput.Get(), nullptr);
        Report(L"Connect MPEG2TS Out to HDV discard sink", hr);
        if (FAILED(hr)) return 1;
        if (hdvDiscardMode) {
            std::wcout << L"  HDV samples will be accepted and discarded; no file is written.\n";
        } else {
            std::wcout << L"  Native HDV samples will be written to the requested .m2t file.\n";
        }
    }

    ComPtr<IMediaControl> mediaControl;
    hr = graph->QueryInterface(IID_PPV_ARGS(mediaControl.Put()));
    Report(L"Query graph for IMediaControl", hr);
    if (FAILED(hr)) {
        return 1;
    }

    hr = mediaControl->Run();
    Report(L"Run capture graph", hr);
    if (FAILED(hr)) {
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)) {
            std::wcerr << L"Power cycling the camcorder may fix this problem.\n";
        }
        ComPtr<IMediaEventEx> failedRunEvents;
        const HRESULT eventsHr = graph->QueryInterface(
            IID_PPV_ARGS(failedRunEvents.Put()));
        Report(L"Query graph events after failed Run", eventsHr);
        ReportGraphFailureState(
            mediaControl.Get(), failedRunEvents.Get(), transport.Get());
        const HRESULT stopHr = mediaControl->Stop();
        Report(L"Stop graph after failed run", stopHr);
        return 1;
    }

    // Arm the graph before starting a stopped tape so the first packets have a sink.
    if (captureStarted == std::chrono::steady_clock::time_point{}) {
        captureStarted = std::chrono::steady_clock::now();
    }
    StartTransportIfNeeded(
        transport.Get(), transportModeKnown, initialTransportMode);

    if (captureKind == CaptureKind::Dv) {
        std::wcout << L"Capturing native DV to " << capturePath << L". ";
    } else if (outputConfigured) {
        std::wcout << L"Capturing to " << capturePath << L". ";
    } else if (hdvDiscardMode) {
        std::wcout << L"HDV discard test is running; no file will be written. ";
    } else {
        std::wcout << L"Capture graph is running without a configured output filename. ";
    }
    std::wcout << L"Press Enter to stop.\n";

    const auto progressStarted = captureStarted;
    ComPtr<IMediaEventEx> mediaEvent;
    hr = graph->QueryInterface(IID_PPV_ARGS(mediaEvent.Put()));
    Report(L"Query graph for IMediaEventEx", hr);

    ULONGLONG lastActivityTick = GetTickCount64();
    ULONGLONG lastFileBytes = GetFileSizeBytes(capturePath);
    ULONGLONG nextProgressTick = 0;
    ULONGLONG lastTimecodeSampleTick = 0;
    std::wstring firstGoodTimecode = L"?:??:??:??";
    std::wstring lastGoodTimecode = L"?:??:??:??";
    const wchar_t* stopReason = L"Enter";
    bool stopping = false;
    while (!stopping) {
        if (_kbhit()) {
            const int key = _getch();
            if (key == '\r' || key == '\n') {
                stopReason = L"Enter";
                stopping = true;
                continue;
            }
        }

        if (mediaEvent.Get() != nullptr) {
            long eventCode = 0;
            LONG_PTR parameter1 = 0;
            LONG_PTR parameter2 = 0;
            while (mediaEvent->GetEvent(
                       &eventCode, &parameter1, &parameter2, 0) == S_OK) {
                mediaEvent->FreeEventParams(eventCode, parameter1, parameter2);
                if (eventCode == EC_COMPLETE) {
                    stopReason = L"DirectShow end-of-stream";
                    stopping = true;
                    break;
                }
                if (eventCode == EC_ERRORABORT || eventCode == EC_USERABORT) {
                    stopReason = L"DirectShow graph abort";
                    stopping = true;
                    break;
                }
            }
        }

        long transportMode = 0;
        if (!stopping && transport.Get() != nullptr &&
            SUCCEEDED(transport->get_Mode(&transportMode)) &&
            transportMode == ED_MODE_STOP) {
            stopReason = L"Transport STOP";
            stopping = true;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG currentBytes = dvDiscardSink != nullptr
                                           ? dvDiscardSink->BytesWritten()
                                           : GetFileSizeBytes(capturePath);
        if (currentBytes != lastFileBytes) {
            lastFileBytes = currentBytes;
            lastActivityTick = now;
        }
        if (dvDiscardSink != nullptr) {
            const ULONGLONG sampleTick = dvDiscardSink->LastSampleTick();
            if (sampleTick > lastActivityTick) {
                lastActivityTick = sampleTick;
            }
        }
        if (!stopping && now - lastActivityTick >= kNoMediaTimeoutMs) {
            stopReason = L"No media activity for 10 seconds";
            stopping = true;
        }

        if (!stopping && now >= nextProgressTick) {
            std::wstring currentTimecode = L"?:??:??:??";
            const ULONGLONG sampleTick =
                dvDiscardSink == nullptr ? 0 : dvDiscardSink->LastSampleTick();
            if (dvDiscardSink == nullptr || sampleTick != lastTimecodeSampleTick) {
                currentTimecode =
                    FormatTimecode(captureKind, dvDiscardSink, timecodeReader.Get());
                if (currentTimecode != L"?:??:??:??") {
                    if (firstGoodTimecode == L"?:??:??:??") {
                        firstGoodTimecode = currentTimecode;
                    }
                    lastGoodTimecode = currentTimecode;
                }
                lastTimecodeSampleTick = sampleTick;
            }
            PrintProgressLine(
                captureKind, capturePath, dvDiscardSink, currentTimecode,
                progressStarted);
            nextProgressTick = now + 1000;
        }
        if (!stopping) {
            Sleep(100);
        }
    }
    std::wcout << L"\r" << std::wstring(100, L' ') << L"\r";
    std::wcout << L"[SUMMARY] Stop reason: " << stopReason << L'\n';

    hr = mediaControl->Stop();
    Report(L"Stop capture graph", hr);
    auto stopIssued = std::chrono::steady_clock::now();
    if (transport.Get()) {
        stopIssued = std::chrono::steady_clock::now();
        StopTransport(transport.Get());
    }
    if (dvDiscardSink != nullptr) {
        std::wcout << L"  Native "
                   << (captureKind == CaptureKind::Dv ? L"DV" : L"HDV")
                   << L" samples received: "
                   << dvDiscardSink->SamplesReceived() << L'\n';
        std::wcout << L"  Native "
                   << (captureKind == CaptureKind::Dv ? L"DV" : L"HDV")
                   << L" bytes "
                   << (hdvDiscardMode ? L"received" : L"written") << L": "
                   << dvDiscardSink->BytesWritten() << L'\n';
    }
    PrintFinalSummary(
        captureKind, capturePath, dvDiscardSink, lastGoodTimecode,
        firstGoodTimecode,
        progressStarted, stopIssued, hdvDiscardMode);
    return FAILED(hr) ? 1 : 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    int outputIndex = 0;
    int positionalCount = 0;
    bool hdvDiscardMode = false;
    for (int index = 1; index < argc; ++index) {
        if (wcscmp(argv[index], L"-v") == 0) {
            g_verbose = true;
        } else if (wcscmp(argv[index], L"--hdv-discard") == 0) {
            hdvDiscardMode = true;
        } else if (positionalCount == 0) {
            outputIndex = index;
            positionalCount = 1;
        } else {
            positionalCount = 2;
        }
    }
    if (outputIndex == 0 || positionalCount != 1 || argc < 2) {
        std::wcerr << L"Usage: fwcap.exe [-v] [--hdv-discard] "
                   << L"<output.m2t|output.dv>\n";
        return 2;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Report(L"Initialize COM apartment", hr);
    if (FAILED(hr)) {
        return 1;
    }

    const int result = RunCapture(argv[outputIndex], hdvDiscardMode);
    CoUninitialize();
    if (g_verbose) {
        std::wcout << L"[OK] Uninitialize COM: completed\n";
    }
    return result;
}
