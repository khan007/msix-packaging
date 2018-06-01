//
//  Copyright (C) 2017 Microsoft.  All rights reserved.
//  See LICENSE file in the project root for full license information.
// 
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "Exceptions.hpp"
#include "StreamBase.hpp"
#include "StorageObject.hpp"
#include "ComHelper.hpp"

namespace MSIX {

    class DirectoryObject final : public ComClass<DirectoryObject, IStorageObject>
    {
    public:
        DirectoryObject(std::string root) : m_root(std::move(root)) {}

        // StorageObject methods
        const char*              GetPathSeparator() override;
        std::vector<std::string> GetFileNames(FileNameOptions options) override;
        ComPtr<IStream>          GetFile(const std::string& fileName) override;

        ComPtr<IStream>          OpenFile(const std::string& fileName, MSIX::FileStream::Mode mode) override;
        void                     CommitChanges() override;

    protected:
        std::map<std::string, ComPtr<IStream>> m_streams;
        std::string m_root;

    };//class DirectoryObject
}