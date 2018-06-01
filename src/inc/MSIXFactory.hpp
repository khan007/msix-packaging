//
//  Copyright (C) 2017 Microsoft.  All rights reserved.
//  See LICENSE file in the project root for full license information.
// 
#pragma once
#include "MSIXWindows.hpp"
#include "ComHelper.hpp"

#include <vector>

// internal interface
EXTERN_C const IID IID_IMSIXFactory;   
#ifndef WIN32
// {1f850db4-32b8-4db6-8bf4-5a897eb611f1}
interface IMSIXFactory : public IUnknown
#else
#include "UnKnwn.h"
#include "Objidl.h"
class IMSIXFactory : public IUnknown
#endif
{
public:
    virtual HRESULT MarshalOutString(std::string& internal, LPWSTR *result) = 0;
    virtual HRESULT MarshalOutBytes(std::vector<std::uint8_t>& data, UINT32* size, BYTE** buffer) = 0;
    virtual MSIX_VALIDATION_OPTION GetValidationOptions() = 0;
    virtual MSIX::ComPtr<IStream> GetResource(const std::string& resource) = 0;
};

SpecializeUuidOfImpl(IMSIXFactory);