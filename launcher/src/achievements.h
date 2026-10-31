// Local achievements, as the game saves them in the user data folder:
//   achievements/definitions.json  [{"id", "title", "description", "points"}, ...]  (written by the game)
//   achievements/unlocks.json      {"<id>": {"unlocked_at": "<ISO 8601 UTC>"}, ...}
#pragma once

#include <string>
#include <vector>

struct Achievement {
    std::string id, title, description;
    int points = 0;
    bool unlocked = false;
    std::string unlockedAt;   // ISO 8601, empty while locked
};

// Reads both files. Missing or broken files give an empty list (or no unlocks).
std::vector<Achievement> LoadAchievements();
