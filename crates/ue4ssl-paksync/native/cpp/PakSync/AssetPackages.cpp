#include "PakSync/AssetPackages.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <unordered_set>

#include <DynamicOutput/DynamicOutput.hpp>

namespace RC::PakSync
{
    namespace
    {
        auto is_ascii_asset_path_char(uint8_t ch) -> bool
        {
            return (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') ||
                   ch == '/' || ch == '\\' || ch == '_' || ch == '-' ||
                   ch == '.' || ch == '+' || ch == ' ' || ch == '(' || ch == ')';
        }

        void append_raw_asset_filename_candidate(
                std::vector<std::wstring>& package_names,
                std::unordered_set<std::wstring>& asset_name_stems,
                const std::string& window,
                size_t extension_pos)
        {
            size_t start = extension_pos;
            while (start > 0 && is_ascii_asset_path_char(static_cast<uint8_t>(window[start - 1])))
            {
                --start;
            }

            const auto raw_candidate = std::string_view{window}.substr(
                    start,
                    extension_pos + std::wstring_view{L".uasset"}.size() - start);
            const auto package_name = normalize_raw_asset_filename_to_package(raw_candidate);
            add_unique_package_name(package_names, package_name);
            if (!package_name.empty())
            {
                asset_name_stems.insert(package_name_leaf(package_name));
                return;
            }

            const auto last_slash = raw_candidate.find_last_of("/\\", extension_pos - start);
            const auto stem_start = last_slash == std::string_view::npos ? 0 : last_slash + 1;
            if (stem_start < raw_candidate.size())
            {
                std::wstring stem{};
                for (const char ch : raw_candidate.substr(stem_start, extension_pos - start - stem_start))
                {
                    stem.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
                }
                if (!stem.empty())
                {
                    asset_name_stems.insert(std::move(stem));
                }
            }
        }

        void append_raw_game_package_candidate(
                std::vector<std::wstring>& candidates,
                const std::string& window,
                size_t game_pos)
        {
            size_t end = game_pos;
            while (end < window.size() && is_ascii_asset_path_char(static_cast<uint8_t>(window[end])))
            {
                ++end;
            }

            add_unique_package_name(
                    candidates,
                    normalize_raw_game_package_string(std::string_view{window}.substr(game_pos, end - game_pos)));
        }
    }

    auto package_name_is_under_path(const std::wstring& package_name, const std::wstring& path) -> bool
    {
        if (path.empty())
        {
            return false;
        }
        if (package_name == path)
        {
            return true;
        }
        return package_name.size() > path.size() &&
               package_name.starts_with(path) &&
               package_name[path.size()] == L'/';
    }

    auto add_unique_package_name(std::vector<std::wstring>& package_names, const std::wstring& package_name) -> bool
    {
        if (package_name.empty())
        {
            return false;
        }
        const auto found = std::find(package_names.begin(), package_names.end(), package_name);
        if (found != package_names.end())
        {
            return false;
        }
        package_names.push_back(package_name);
        return true;
    }

    auto add_unique_wstring(std::vector<std::wstring>& values, const std::wstring& value) -> bool
    {
        if (value.empty())
        {
            return false;
        }
        if (std::find(values.begin(), values.end(), value) != values.end())
        {
            return false;
        }
        values.push_back(value);
        return true;
    }

    auto lowercase_wstring(std::wstring value) -> std::wstring
    {
        for (auto& ch : value)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return value;
    }

    auto normalize_raw_asset_filename_to_package(std::string_view raw_path) -> std::wstring
    {
        std::wstring path{};
        path.reserve(raw_path.size());
        for (char ch : raw_path)
        {
            path.push_back(ch == '\\' ? L'/' : static_cast<wchar_t>(static_cast<unsigned char>(ch)));
        }

        const auto lower = lowercase_wstring(path);
        const auto uasset_pos = lower.find(L".uasset");
        const auto umap_pos = lower.find(L".umap");
        size_t ext_pos = std::wstring::npos;
        if (uasset_pos != std::wstring::npos && (umap_pos == std::wstring::npos || uasset_pos < umap_pos))
        {
            ext_pos = uasset_pos;
        }
        else if (umap_pos != std::wstring::npos)
        {
            ext_pos = umap_pos;
        }
        if (ext_pos == std::wstring::npos)
        {
            return {};
        }
        path.resize(ext_pos);

        std::wstring package{};
        const auto game_pos = lower.find(L"/game/");
        if (game_pos != std::wstring::npos)
        {
            package = path.substr(game_pos);
        }
        else
        {
            const auto content_pos = lower.find(L"/content/");
            if (content_pos != std::wstring::npos)
            {
                package = L"/Game/" + path.substr(content_pos + std::wstring_view{L"/content/"}.size());
            }
            else if (lower.starts_with(L"content/"))
            {
                package = L"/Game/" + path.substr(std::wstring_view{L"content/"}.size());
            }
        }

        while (package.find(L"//") != std::wstring::npos)
        {
            const auto pos = package.find(L"//");
            package.erase(pos, 1);
        }
        while (!package.empty() && package.back() == L'/')
        {
            package.pop_back();
        }

        if (!package.starts_with(L"/Game/") || package.find(L"..") != std::wstring::npos)
        {
            return {};
        }
        return package;
    }

    auto normalize_raw_game_package_string(std::string_view raw_path) -> std::wstring
    {
        std::wstring path{};
        path.reserve(raw_path.size());
        for (char ch : raw_path)
        {
            path.push_back(ch == '\\' ? L'/' : static_cast<wchar_t>(static_cast<unsigned char>(ch)));
        }

        const auto lower = lowercase_wstring(path);
        const auto game_pos = lower.find(L"/game/");
        if (game_pos == std::wstring::npos)
        {
            return {};
        }

        auto package = path.substr(game_pos);
        const auto dot = package.find(L'.');
        if (dot != std::wstring::npos)
        {
            package.resize(dot);
        }
        const auto uasset_pos = lowercase_wstring(package).find(L".uasset");
        if (uasset_pos != std::wstring::npos)
        {
            package.resize(uasset_pos);
        }
        const auto umap_pos = lowercase_wstring(package).find(L".umap");
        if (umap_pos != std::wstring::npos)
        {
            package.resize(umap_pos);
        }

        while (package.find(L"//") != std::wstring::npos)
        {
            const auto pos = package.find(L"//");
            package.erase(pos, 1);
        }
        while (!package.empty() && package.back() == L'/')
        {
            package.pop_back();
        }

        if (!package.starts_with(L"/Game/") || package.find(L"..") != std::wstring::npos)
        {
            return {};
        }
        return package;
    }

    auto package_name_leaf(const std::wstring& package_name) -> std::wstring
    {
        const auto slash = package_name.find_last_of(L'/');
        if (slash == std::wstring::npos)
        {
            return package_name;
        }
        return package_name.substr(slash + 1);
    }

    auto looks_like_map_package_name(const std::wstring& package_name) -> bool
    {
        const auto lower = lowercase_wstring(package_name);
        return lower.find(L"/maps/") != std::wstring::npos ||
               lower.find(L"/levels/") != std::wstring::npos ||
               lower.find(L"/worlds/") != std::wstring::npos;
    }

    auto build_ugc_asset_reference_variants(const std::vector<std::wstring>& package_names) -> std::vector<std::wstring>
    {
        std::vector<std::wstring> values{};
        values.reserve(package_names.size() * 4);

        for (const auto& package_name : package_names)
        {
            if (package_name.empty())
            {
                continue;
            }

            add_unique_wstring(values, package_name);

            const auto leaf = package_name_leaf(package_name);
            if (leaf.empty())
            {
                continue;
            }

            const auto object_path = package_name + L"." + leaf;
            add_unique_wstring(values, object_path);
            add_unique_wstring(values, object_path + L"_C");
            add_unique_wstring(values, package_name + L"_C");
        }

        return values;
    }

    auto extract_asset_package_names_from_pak_file(const std::filesystem::path& pak) -> std::vector<std::wstring>
    {
        std::ifstream file{pak, std::ios::binary};
        if (!file)
        {
            return {};
        }

        constexpr size_t ChunkSize = 256 * 1024;
        constexpr size_t OverlapSize = 4096;
        std::vector<char> buffer(ChunkSize);
        std::string window{};
        std::vector<std::wstring> package_names{};
        std::vector<std::wstring> game_package_candidates{};
        std::unordered_set<std::wstring> asset_name_stems{};

        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read_count = file.gcount();
            if (read_count <= 0)
            {
                break;
            }

            if (window.size() > OverlapSize)
            {
                window.erase(0, window.size() - OverlapSize);
            }
            window.append(buffer.data(), static_cast<size_t>(read_count));

            const auto lower_window = [&]() {
                std::string lower{};
                lower.reserve(window.size());
                for (unsigned char ch : window)
                {
                    lower.push_back(static_cast<char>(std::tolower(ch)));
                }
                return lower;
            }();

            for (const std::string_view ext : {std::string_view{".uasset"}, std::string_view{".umap"}})
            {
                size_t pos = 0;
                while ((pos = lower_window.find(ext, pos)) != std::string::npos)
                {
                    append_raw_asset_filename_candidate(package_names, asset_name_stems, window, pos);
                    pos += ext.size();
                }
            }

            size_t game_pos = 0;
            while ((game_pos = lower_window.find("/game/", game_pos)) != std::string::npos)
            {
                append_raw_game_package_candidate(game_package_candidates, window, game_pos);
                game_pos += std::string_view{"/game/"}.size();
            }

            if (package_names.size() > MaxSynthesizedUgcAssetPackages)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] pak file asset extraction exceeded {} entries; trimming pak={}\n"),
                        MaxSynthesizedUgcAssetPackages,
                        pak.wstring());
                package_names.resize(MaxSynthesizedUgcAssetPackages);
                break;
            }
        }

        for (const auto& package_name : game_package_candidates)
        {
            const auto leaf = package_name_leaf(package_name);
            if (asset_name_stems.empty() || asset_name_stems.find(leaf) != asset_name_stems.end())
            {
                add_unique_package_name(package_names, package_name);
            }
        }

        std::sort(package_names.begin(), package_names.end());
        return package_names;
    }
}
