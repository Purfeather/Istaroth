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
    UINT width=0,height=0;result=frame->GetSize(&width,&height);
    if(FAILED(result))return result;
    if(!width||!height||width>1024||height>1024)return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    ComPtr<IWICFormatConverter> converter;result=factory->CreateFormatConverter(&converter);
    if(FAILED(result))return result;
    result=converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(FAILED(result))return result;
    UINT stride=width*4;std::vector<BYTE> pixels((size_t)stride*height);
    result=converter->CopyPixels(nullptr,stride,(UINT)pixels.size(),pixels.data());
    if(FAILED(result))return result;
    D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;
    description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.Usage=D3D11_USAGE_IMMUTABLE;
    description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(),stride,0};ComPtr<ID3D11Texture2D> texture;
    result=device->CreateTexture2D(&description,&initial,&texture);
    return FAILED(result)?result:device->CreateShaderResourceView(texture.Get(),nullptr,output);
}
}
#endif
