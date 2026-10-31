// Pictures read from the player's own disc files, so no game art ships with the launcher.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;   // RGBA8, top row first
};

// Loads one texture from the disc's channel banner (files/opening.bnr), for example
// "ex_logo.tpl", the Excite Truck logo. Returns false if the file or texture isn't there.
bool LoadBannerTexture(const std::filesystem::path& openingBnr, const std::string& name, Image& out);
