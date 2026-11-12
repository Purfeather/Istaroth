#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

namespace istaroth {
// Optional static PNG data, uploaded on the caller's private UI device.
inline HRESULT LoadResidentIcon(ID3D11Device* device,const wchar_t* path,ID3D11ShaderResourceView** output) {
    if(!output)return E_INVALIDARG;
    *output=nullptr;
    if(!device||!path||!*path)return E_INVALIDARG;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if(!GetFileAttributesExW(path,GetFileExInfoStandard,&attributes))return HRESULT_FROM_WIN32(GetLastError());
    if((attributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||attributes.nFileSizeHigh||attributes.nFileSizeLow>4*1024*1024)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    HRESULT result=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(result))return result;
    ComPtr<IWICBitmapDecoder> decoder;
    result=factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder);
    if(FAILED(result))return result;
    GUID format{};result=decoder->GetContainerFormat(&format);
    if(FAILED(result))return result;
    if(format!=GUID_ContainerFormatPng)return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
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
