#include "save-files.h"

#include "pch.h"

#include <shellapi.h>

// PrepareFileSavePath resolves SLocalSavePath and any profile folder under it; a miss is not cached
const std::filesystem::path& SaferSaving::SaveFiles::Directory()
{
    static std::filesystem::path directory;
    if (!directory.empty())
    {
        return directory;
    }

    // only the folder is wanted, so the name given here does not matter
    char resolved[0x104]{};
    auto* utility = RE::BSWin32SaveDataSystemUtility::GetSingleton();
    if (utility && utility->PrepareFileSavePath("SaferSaving", resolved, false, false) == 0)
    {
        directory = std::filesystem::path{resolved}.parent_path();
    }

    return directory;
}

std::string SaferSaving::SaveFiles::OwnId(const RE::BGSSaveLoadManager& a_manager)
{
    return std::format("_{:08X}_", a_manager.currentCharacterID);
}

bool SaferSaving::SaveFiles::Recycle(const std::vector<std::filesystem::path>& a_paths)
{
    if (a_paths.empty())
    {
        return true;
    }

    // one buffer of null terminated paths, closed by a second null, is what SHFileOperation takes
    std::wstring buffer;
    for (const auto& path : a_paths)
    {
        buffer += path.wstring();
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');

    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = buffer.c_str();
    // ALLOWUNDO is the bin rather than the unlink; the rest keeps it off the screen
    operation.fFlags = static_cast<FILEOP_FLAGS>(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);

    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}
