#pragma once
#ifdef ISTAROTH_EMBED_ICON
#include "resident_icon.h"
#include "resident_embedded_png.h"

namespace istaroth {
// Candidate-only default. A valid external PNG remains an optional override.
inline HRESULT LoadResidentIconWithFallback(ID3D11Device* device,const wchar_t* path,ID3D11ShaderResourceView** output) {
    HRESULT result=LoadResidentIcon(device,path,output);
    if(SUCCEEDED(result)||!device||!output)return result;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    result=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(result))return result;
    ComPtr<IWICStream> stream;result=factory->CreateStream(&stream);
    if(FAILED(result))return result;
    result=stream->InitializeFromMemory(const_cast<BYTE*>(ResidentEmbeddedPng),sizeof(ResidentEmbeddedPng));
    if(FAILED(result))return result;
    ComPtr<IWICBitmapDecoder> decoder;
    result=factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder);
    if(FAILED(result))return result;
    ComPtr<IWICBitmapFrameDecode> frame;result=decoder->GetFrame(0,&frame);
    if(FAILED(result))return result;
    return CreateResidentIconFromFrame(device,factory.Get(),frame.Get(),output);
}
}
#endif
