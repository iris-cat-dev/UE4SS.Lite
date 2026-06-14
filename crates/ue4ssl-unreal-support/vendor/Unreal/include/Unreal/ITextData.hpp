#pragma once

#include <unordered_map>

#include <String/StringType.hpp>
#include <Unreal/Common.hpp>
#include <Unreal/FString.hpp>

namespace RC::Unreal
{
    class RC_UE_API ITextData
    {
    public:
        static std::unordered_map<RC::StringType, uint32_t> VTableLayoutMap;

    public:
        auto GetDisplayString() const -> const FString&;

    private:
        void* vtable;
    };
}

