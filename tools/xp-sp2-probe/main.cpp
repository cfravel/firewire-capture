#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <dshow.h>

#include <stdio.h>

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

void PrintGuid(const GUID& guid) {
    wchar_t text[64] = {};
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    wprintf(L"%ls", text);
}

void PrintHResult(const wchar_t* label, HRESULT hr) {
    wprintf(L"%ls: 0x%08lX\n", label, static_cast<unsigned long>(hr));
}

void ClearMediaType(AM_MEDIA_TYPE* type) {
    if (type == 0) return;
    if (type->cbFormat != 0) {
        CoTaskMemFree(type->pbFormat);
    }
    if (type->pUnk != 0) {
        type->pUnk->Release();
    }
    ZeroMemory(type, sizeof(*type));
}

void FreeMediaType(AM_MEDIA_TYPE* type) {
    if (type == 0) return;
    ClearMediaType(type);
    CoTaskMemFree(type);
}

void PrintVariantProperty(IPropertyBag* properties, const wchar_t* name) {
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = properties->Read(name, &value, 0);
    if (SUCCEEDED(hr) && value.vt == VT_BSTR && value.bstrVal != 0) {
        wprintf(L"    %ls: %ls\n", name, value.bstrVal);
    } else {
        wprintf(L"    %ls: unavailable (0x%08lX)\n", name,
                static_cast<unsigned long>(hr));
    }
    VariantClear(&value);
}

void PrintMediaType(const AM_MEDIA_TYPE* type) {
    wprintf(L"      major=");
    PrintGuid(type->majortype);
    wprintf(L" subtype=");
    PrintGuid(type->subtype);
    wprintf(L" format=");
    PrintGuid(type->formattype);
    wprintf(L" format-bytes=%lu sample-size=%lu\n",
            static_cast<unsigned long>(type->cbFormat),
            static_cast<unsigned long>(type->lSampleSize));
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
        ZeroMemory(&pinInfo, sizeof(pinInfo));
        pin->QueryPinInfo(&pinInfo);
        if (pinInfo.pFilter != 0) pinInfo.pFilter->Release();

        wprintf(L"  Pin %lu: name=%ls direction=%ls\n",
                static_cast<unsigned long>(pinIndex++), pinInfo.achName,
                direction == PINDIR_OUTPUT ? L"output" : L"input");

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
            wprintf(L"    Media type %lu:\n",
                    static_cast<unsigned long>(typeIndex++));
            PrintMediaType(type);
            FreeMediaType(type);
        }
    }
}

void InspectDevice(IMoniker* moniker, ULONG index) {
    wprintf(L"\nDevice %lu\n", static_cast<unsigned long>(index));

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
        wprintf(L"    CLSID: ");
        PrintGuid(classId);
        wprintf(L"\n");
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
    wprintf(L"    IAMExtTransport: %ls\n", SUCCEEDED(hr) ? L"yes" : L"no");

    ComPtr<IAMTimecodeReader> timecode;
    hr = filter->QueryInterface(IID_IAMTimecodeReader,
                                reinterpret_cast<void**>(timecode.Put()));
    wprintf(L"    IAMTimecodeReader: %ls\n", SUCCEEDED(hr) ? L"yes" : L"no");

    InspectPins(filter.Get());
}

int wmain() {
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        PrintHResult(L"CoInitializeEx", hr);
        return 1;
    }

    wprintf(L"XP SP2 DirectShow probe\n");
    wprintf(L"Target: x86; no capture graph is started by this diagnostic.\n");

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
        wprintf(L"No video input devices found.\n");
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

    wprintf(L"\nDevices inspected: %lu\n", static_cast<unsigned long>(index));
    CoUninitialize();
    return index == 0 ? 2 : 0;
}
