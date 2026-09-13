#include "FileDialog.hpp"

#include <nfd.h>
#include <memory>
#include <stdexcept>

namespace
{
std::runtime_error dialogError()
{
    const char* error = NFD_GetError();
    return std::runtime_error(std::string("File dialog: ") +
                              (error ? error : "unknown error"));
}

struct DialogSession
{
    DialogSession()
    {
        if (NFD_Init() != NFD_OKAY)
            throw dialogError();
    }
    ~DialogSession() { NFD_Quit(); }
    DialogSession(const DialogSession&) = delete;
    DialogSession& operator=(const DialogSession&) = delete;
};
}

std::optional<std::string> FileDialog::openGltf()
{
    DialogSession session;
    nfdu8char_t* rawPath = nullptr;
    const nfdu8filteritem_t filters[] = {{"glTF Scene", "gltf,glb"}};
    const auto result = NFD_OpenDialogU8(&rawPath, filters, 1, nullptr);
    if (result == NFD_CANCEL)
        return std::nullopt;
    if (result != NFD_OKAY)
        throw dialogError();

    const std::unique_ptr<nfdu8char_t, decltype(&NFD_FreePathU8)>
        path(rawPath, &NFD_FreePathU8);
    return std::string(path.get());
}
