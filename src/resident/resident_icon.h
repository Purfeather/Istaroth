#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

namespace istaroth {
inline HRESULT CreateResidentIconFromFrame(ID3D11Device* device,IWICImagingFactory* factory,IWICBitmapFrameDecode* frame,
                                           ID3D11ShaderResourceView** output) {
    if(!device||!factory||!frame||!output)return E_INVALIDARG;
    *output=nullptr;
    UINT width=0,height=0;HRESULT result=frame->GetSize(&width,&height);
    if(FAILED(result))return result;
    if(!width||!height||width>1024||height>1024)return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;result=factory->CreateFormatConverter(&converter);
    if(FAILED(result))return result;
    result=converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(FAILED(result))return result;
    UINT stride=width*4;std::vector<BYTE> base_pixels((size_t)stride*height);
    result=converter->CopyPixels(nullptr,stride,(UINT)base_pixels.size(),base_pixels.data());
    if(FAILED(result))return result;

    std::vector<std::vector<BYTE>> mips;mips.push_back(std::move(base_pixels));
    std::vector<UINT> strides{stride};
    UINT cur_w=width,cur_h=height;
    while(cur_w>1||cur_h>1) {
        UINT next_w=(std::max)(1u,cur_w>>1),next_h=(std::max)(1u,cur_h>>1);
        const auto& src=mips.back();UINT src_stride=strides.back();
        std::vector<BYTE> dst((size_t)next_w*next_h*4);
        for(UINT y=0;y<next_h;++y) {
            UINT sy0=(std::min)(y*2,cur_h-1),sy1=(std::min)(y*2+1,cur_h-1);
            for(UINT x=0;x<next_w;++x) {
                UINT sx0=(std::min)(x*2,cur_w-1),sx1=(std::min)(x*2+1,cur_w-1);
                const BYTE* p[4]={
                    &src[(size_t)sy0*src_stride+sx0*4],&src[(size_t)sy0*src_stride+sx1*4],
                    &src[(size_t)sy1*src_stride+sx0*4],&src[(size_t)sy1*src_stride+sx1*4]
                };
                UINT sum_r=0,sum_g=0,sum_b=0,sum_a=0;
                for(int k=0;k<4;++k) {
                    UINT a=p[k][3];sum_a+=a;sum_r+=p[k][0]*a;sum_g+=p[k][1]*a;sum_b+=p[k][2]*a;
                }
                BYTE* out=&dst[((size_t)y*next_w+x)*4];
                out[3]=(BYTE)((sum_a+2)/4);
                if(sum_a>0) {
                    out[0]=(BYTE)((sum_r+sum_a/2)/sum_a);
                    out[1]=(BYTE)((sum_g+sum_a/2)/sum_a);
                    out[2]=(BYTE)((sum_b+sum_a/2)/sum_a);
                }
            }
        }
        mips.push_back(std::move(dst));strides.push_back(next_w*4);
        cur_w=next_w;cur_h=next_h;
    }

    std::vector<D3D11_SUBRESOURCE_DATA> initial(mips.size());
    for(size_t i=0;i<mips.size();++i)initial[i]={mips[i].data(),strides[i],0};
    D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;
    description.MipLevels=(UINT)mips.size();description.ArraySize=1;description.SampleDesc.Count=1;
    description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.Usage=D3D11_USAGE_IMMUTABLE;
    description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    result=device->CreateTexture2D(&description,initial.data(),&texture);
    return FAILED(result)?result:device->CreateShaderResourceView(texture.Get(),nullptr,output);
}

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
    return CreateResidentIconFromFrame(device,factory.Get(),frame.Get(),output);
}
}
