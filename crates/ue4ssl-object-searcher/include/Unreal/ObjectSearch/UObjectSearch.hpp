#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <String/StringType.hpp>
#include <Unreal/Common.hpp>
#include <Unreal/Core/HAL/Platform.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UnrealFlags.hpp>

namespace RC::Unreal
{
    class UObject;
    class UClass;
    struct ObjectSearcher;
}

namespace RC::Unreal::UObjectGlobals
{
    // UE-compatible exact object lookup: class + outer + object name/path.
    RC_UE_API UObject* FindObject(UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass = false, ObjectSearcher* = nullptr);
    RC_UE_API UObject* FindObject(UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass = false, ObjectSearcher* = nullptr);

    // Public convenience overload for the UE-compatible 'FindObject' overload.
    // It exists so that you don't have to specify all the optional params in order to specify a searcher.
    RC_UE_API UObject* FindObject(ObjectSearcher&, UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass = false);
    RC_UE_API UObject* FindObject(ObjectSearcher&, UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass = false);

    // UE4SS short-name lookup: class short name + object short name.
    RC_UE_API auto FindObjectByClassAndName(const FName ClassName, const FName ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {}) -> UObject*;
    RC_UE_API auto FindObjectByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {}) -> UObject*;

    // Find a specified number of objects with the specified class short name (or none) and object short name (or none).
    // Must have at least either class or name, or both.
    // Specify 0 for 'NumObjectsToFind' to not limit to number of objects to find.
    RC_UE_API auto FindObjectsByClassAndName(size_t NumObjectsToFind, const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;
    RC_UE_API auto FindObjectsByClassAndName(size_t NumObjectsToFind, const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;

    // Find all matching objects.
    RC_UE_API auto FindObjectsByClassAndName(const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;
    RC_UE_API auto FindObjectsByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;

    // Deprecated short-name lookup aliases. Prefer FindObjectByClassAndName / FindObjectsByClassAndName.
    [[deprecated("Use FindObjectByClassAndName instead.")]] RC_UE_API auto FindObject(const FName ClassName, const FName ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {}) -> UObject*;
    [[deprecated("Use FindObjectByClassAndName instead.")]] RC_UE_API auto FindObject(const CharType* ClassName, const CharType* ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {}) -> UObject*;
    [[deprecated("Use FindObjectsByClassAndName instead.")]] RC_UE_API auto FindObjects(size_t NumObjectsToFind, const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;
    [[deprecated("Use FindObjectsByClassAndName instead.")]] RC_UE_API auto FindObjects(size_t NumObjectsToFind, const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;
    [[deprecated("Use FindObjectsByClassAndName instead.")]] RC_UE_API auto FindObjects(const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;
    [[deprecated("Use FindObjectsByClassAndName instead.")]] RC_UE_API auto FindObjects(const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true) -> void;

    template<typename ObjectType>
    ObjectType* FindObject(UObject* Outer, const TCHAR* Name, bool ExactClass = false)
    {
        return static_cast<ObjectType*>(FindObject(ObjectType::StaticClass(), Outer, Name, ExactClass));
    }

    // Public StaticFindObject compatibility wrappers.
    template<typename ObjectType = UObject*>
    auto StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, const CharType* OrigInName, bool bExactClass = false) -> ObjectType
    {
        return static_cast<ObjectType>(FindObject(ObjectClass, InObjectPackage, OrigInName, bExactClass));
    }

    template<typename ObjectType = UObject*>
    auto StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, StringViewType OrigInName, bool bExactClass = false) -> ObjectType
    {
        return static_cast<ObjectType>(FindObject(ObjectClass, InObjectPackage, OrigInName.data(), bExactClass));
    }

    template<typename ObjectType = UObject*>
    auto StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, const StringType& OrigInName, bool bExactClass = false) -> ObjectType
    {
        return static_cast<ObjectType>(FindObject(ObjectClass, InObjectPackage, OrigInName.c_str(), bExactClass));
    }

    // Class instance enumeration helpers.
    // Find the first live instance of a class.
    // Does not find ClassDefaultObjects (CDO) or non-instances of classes.
    // Takes inheritance into account.
    RC_UE_API auto FindFirstInstanceOfClass(FName ClassName) -> UObject*;
    RC_UE_API auto FindFirstInstanceOfClass(const CharType* ClassName) -> UObject*;
    RC_UE_API auto FindFirstInstanceOfClass(StringViewType ClassName) -> UObject*;
    RC_UE_API auto FindFirstInstanceOfClass(const StringType& ClassName) -> UObject*;
    RC_UE_API auto FindFirstInstanceOfClass(std::string_view ClassName) -> UObject*;
    RC_UE_API auto FindFirstInstanceOfClass(const std::string& ClassName) -> UObject*;

    // Find all instances of a class. Follows the same rules as 'FindFirstInstanceOfClass'.
    RC_UE_API auto FindAllInstancesOfClass(FName ClassName, std::vector<UObject*>& OutStorage) -> void;
    RC_UE_API auto FindAllInstancesOfClass(const CharType* ClassName, std::vector<UObject*>& OutStorage) -> void;
    RC_UE_API auto FindAllInstancesOfClass(StringViewType ClassName, std::vector<UObject*>& OutStorage) -> void;
    RC_UE_API auto FindAllInstancesOfClass(const StringType& ClassName, std::vector<UObject*>& OutStorage) -> void;
    RC_UE_API auto FindAllInstancesOfClass(std::string_view ClassName, std::vector<UObject*>& OutStorage) -> void;
    RC_UE_API auto FindAllInstancesOfClass(const std::string& ClassName, std::vector<UObject*>& OutStorage) -> void;

    // Deprecated class instance lookup aliases. Prefer FindFirstInstanceOfClass / FindAllInstancesOfClass.
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(FName ClassName) -> UObject*;
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(const CharType* ClassName) -> UObject*;
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(StringViewType ClassName) -> UObject*;
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(const StringType& ClassName) -> UObject*;
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(std::string_view ClassName) -> UObject*;
    [[deprecated("Use FindFirstInstanceOfClass instead.")]] RC_UE_API auto FindFirstOf(const std::string& ClassName) -> UObject*;

    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(FName ClassName, std::vector<UObject*>& OutStorage) -> void;
    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(const CharType* ClassName, std::vector<UObject*>& OutStorage) -> void;
    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(StringViewType ClassName, std::vector<UObject*>& OutStorage) -> void;
    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(const StringType& ClassName, std::vector<UObject*>& OutStorage) -> void;
    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(std::string_view ClassName, std::vector<UObject*>& OutStorage) -> void;
    [[deprecated("Use FindAllInstancesOfClass instead.")]] RC_UE_API auto FindAllOf(const std::string& ClassName, std::vector<UObject*>& OutStorage) -> void;

    // Internal bootstrap/search support.
    // These are used by UE4SS initialization and generated Unreal type helpers only; do not export them as public DLL API.
    auto SetupStaticFindObjectFastAddress(void* FunctionAddress) -> void;
    auto StaticFindObject_InternalSlow(UClass* Object, UObject* ChunkIndex, const CharType* OrigInName, bool bExactClass = false) -> UObject*;
    auto StaticFindObject_InternalNoToStringFromStrings(const std::vector<StringViewType>& NameParts) -> UObject*;
    auto StaticFindObject_InternalNoToStringFromNames(const std::vector<FName>& NameParts) -> UObject*;
}
