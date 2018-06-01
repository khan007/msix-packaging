//
//  Copyright (C) 2017 Microsoft.  All rights reserved.
//  See LICENSE file in the project root for full license information.
// 
#include "AppxBlockMapObject.hpp"
#include <algorithm>
#include <iterator>
#include "IXml.hpp"
#include "BlockMapStream.hpp"
#include "MSIXResource.hpp"

/* Example XML:
<?xml version="1.0" encoding="UTF-8"?>
<BlockMap HashMethod="http://www.w3.org/2001/04/xmlenc#sha256" xmlns="http://schemas.microsoft.com/appx/2010/blockmap">
...
<File Name="assets\icon150.png" Size="0" LfhSize="48"/>
...
<File LfhSize="65" Size="187761" Name="Assets\video_offline_demo_page1.jpg">
	<Block Hash="NQL/PSheCSB3yZzKyZ6nHbsfzJt1EZJxOXLllMVvtEI="/>
	<Block Hash="2Udxo8Nwie7rvy4g0T5yfz9qccDNMVWh2mfMD1YCQao="/>
	<Block Hash="MmXnlptT/u+ilMKCIriWR49k99rBqwXKO3s60zGwZKg="/>
</File>
...
<File LfhSize="57" Size="47352" Name="Resources\Fonts\SegMVR2.ttf">
    <Block Size="27777" Hash="LGaGnk3EtFymriM9cRmeX7eZI+b2hpwOIlJIXdeE1ik="/>
</File>
...
</BlockMap>
*/

namespace MSIX {

    template <class T>
    static T GetNumber(const ComPtr<IXmlElement>& element, XmlAttributeName attribute, T defaultValue)
    {
        const auto& attributeValue = element->GetAttributeValue(attribute);
        bool hasValue = !attributeValue.empty();
        T value = defaultValue;
        if (hasValue) { value = static_cast<T>(std::stoul(attributeValue)); }
        return value;        
    }

    static Block GetBlock(const ComPtr<IXmlElement>& element)
    {
        Block result {0};
        result.compressedSize = GetNumber<std::uint64_t>(element, XmlAttributeName::BlockMap_File_Block_Size, BLOCKMAP_BLOCK_SIZE);
        result.hash = element->GetBase64DecodedAttributeValue(XmlAttributeName::BlockMap_File_Block_Hash);
        return result;
    }

    AppxBlockMapObject::AppxBlockMapObject(IMSIXFactory* factory, const ComPtr<IStream>& stream) : m_factory(factory), m_stream(stream)
    {
        ComPtr<IXmlFactory> xmlFactory;
        ThrowHrIfFailed(factory->QueryInterface(UuidOfImpl<IXmlFactory>::iid, reinterpret_cast<void**>(&xmlFactory)));        
        auto dom = xmlFactory->CreateDomFromStream(XmlContentType::AppxBlockMapXml, stream);

        struct _context
        {
            AppxBlockMapObject* self;
            IMSIXFactory*       factory;
            size_t              countFilesFound;
            IXmlDom*            dom;
        };
        _context context = { this, factory, 0, dom.Get() };

        XmlVisitor visitor(static_cast<void*>(&context), [](void* c, const ComPtr<IXmlElement>& fileNode)->bool
        {
            const auto& name = fileNode->GetAttributeValue(XmlAttributeName::BlockMap_File_Name);
            ThrowErrorIf(Error::BlockMapSemanticError, (name == "[Content_Types].xml"), "[Content_Types].xml cannot be in the AppxBlockMap.xml file");

            _context* context = reinterpret_cast<_context*>(c);
            std::ostringstream builder;
            builder << "Duplicate file: '" << name << "' specified in AppxBlockMap.xml.";
            ThrowErrorIf(Error::BlockMapSemanticError, (context->self->m_blockMap.find(name) != context->self->m_blockMap.end()), builder.str().c_str());

            std::vector<Block> blocks;
            XmlVisitor visitor(static_cast<void*>(&blocks), [](void* b, const ComPtr<IXmlElement>& blockNode)->bool
            {
                std::vector<Block>* blocks = reinterpret_cast<std::vector<Block>*>(b);       
                blocks->push_back(GetBlock(blockNode));
                return true;
            });
            context->dom->ForEachElementIn(fileNode, XmlQueryName::BlockMap_File_Block, visitor);

            std::uint64_t sizeAttribute = GetNumber<std::uint64_t>(fileNode, XmlAttributeName::BlockMap_File_Block_Size, BLOCKMAP_BLOCK_SIZE);
            ThrowErrorIf(Error::BlockMapSemanticError, (0 == blocks.size() && 0 != sizeAttribute), "If size is non-zero, then there must be 1+ blocks.");
            
            context->self->m_blockMap.insert(std::make_pair(name, std::move(blocks)));
            context->self->m_blockMapFiles.insert(std::make_pair(name,
                ComPtr<IAppxBlockMapFile>::Make<AppxBlockMapFile>(
                    context->factory,
                    &(context->self->m_blockMap[name]),
                    GetNumber<std::uint32_t>(fileNode, XmlAttributeName::BlockMap_File_LocalFileHeaderSize, 0),
                    name,
                    sizeAttribute
                )));
            context->countFilesFound++;    
            return true;            
        });
        dom->ForEachElementIn(dom->GetDocument(), XmlQueryName::BlockMap_File, visitor);
        ThrowErrorIf(Error::BlockMapSemanticError, (0 == context.countFilesFound), "Empty AppxBlockMap.xml");
    }

    ComPtr<IStream> AppxBlockMapObject::GetValidationStream(const std::string& part, const ComPtr<IStream>& stream)
    {
        ThrowErrorIf(Error::InvalidParameter, (part.empty() || !stream), "bad input");
        auto item = m_blockMap.find(part);
        std::ostringstream builder;
        builder << "file: '" << part << "' not tracked by blockmap.";
        ThrowErrorIf(Error::BlockMapSemanticError, item == m_blockMap.end(), builder.str().c_str());
        return ComPtr<IStream>::Make<BlockMapStream>(m_factory, part, stream, item->second);
    }

    HRESULT STDMETHODCALLTYPE AppxBlockMapObject::GetFile(LPCWSTR filename, IAppxBlockMapFile **file) noexcept try
    {
        ThrowErrorIf(Error::InvalidParameter, (
            filename == nullptr || *filename == '\0' || file == nullptr || *file != nullptr
        ), "bad pointer");
        auto blockMapFile = m_blockMapFiles.find(utf16_to_utf8(filename));
        ThrowErrorIf(Error::InvalidParameter, (blockMapFile == m_blockMapFiles.end()), "File not found!");
        MSIX::ComPtr<IAppxBlockMapFile> result = blockMapFile->second;
        *file = result.Detach();
        return static_cast<HRESULT>(Error::OK);
    } CATCH_RETURN();

    HRESULT STDMETHODCALLTYPE AppxBlockMapObject::GetFiles(IAppxBlockMapFilesEnumerator **enumerator) noexcept try
    {
        ThrowErrorIf(Error::InvalidParameter, (enumerator == nullptr || *enumerator != nullptr), "bad pointer");
        ComPtr<IAppxBlockMapReader> self;
        ThrowHrIfFailed(QueryInterface(UuidOfImpl<IAppxBlockMapReader>::iid, reinterpret_cast<void**>(&self)));
        *enumerator = ComPtr<IAppxBlockMapFilesEnumerator>::Make<AppxBlockMapFilesEnumerator>(
            self,
            std::move(GetFileNames())).Detach();
        return static_cast<HRESULT>(Error::OK);
    } CATCH_RETURN();

    HRESULT STDMETHODCALLTYPE AppxBlockMapObject::GetHashMethod(IUri **hashMethod) noexcept
    {   // Ultimately, this IUri object represents the HashMethod attribute in the blockmap:
        return static_cast<HRESULT>(Error::NotImplemented);
    }

    HRESULT STDMETHODCALLTYPE AppxBlockMapObject::GetStream(IStream **blockMapStream) noexcept try
    {
        ThrowErrorIf(Error::InvalidParameter, (blockMapStream == nullptr || *blockMapStream != nullptr), "bad pointer");
        auto stream = GetStream();
        LARGE_INTEGER li{0};
        ThrowHrIfFailed(stream->Seek(li, StreamBase::Reference::START, nullptr));
        *blockMapStream = stream.Detach();
        return static_cast<HRESULT>(Error::OK);
    } CATCH_RETURN();

    // IAppxBlockMapInternal methods
    std::vector<std::string> AppxBlockMapObject::GetFileNames()
    {
        std::vector<std::string> fileNames;
        std::transform(
            m_blockMapFiles.begin(),
            m_blockMapFiles.end(),
            std::back_inserter(fileNames),
            [](auto keyValuePair){ return keyValuePair.first; }
        );
        return fileNames;
    }

    std::vector<Block> AppxBlockMapObject::GetBlocks(const std::string& fileName)
    {   
        auto index = m_blockMap.find(fileName);
        ThrowErrorIf(Error::FileNotFound, (index == m_blockMap.end()), "File not in blockmap");
        return index->second;
    }

    ComPtr<IAppxBlockMapFile> AppxBlockMapObject::GetFile(const std::string& fileName)
    {
        auto index = m_blockMapFiles.find(fileName);
        ThrowErrorIf(Error::FileNotFound, (index == m_blockMapFiles.end()), "File not in blockmap");
        return index->second;
    }
}