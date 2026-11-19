#include "LeonHorror/FLeonHorrorSaveGame.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Leon {

    namespace {
        const char* kMagic = "LeonHorrorSave";

        /** Shortest text that reads back to the same float. */
        std::string Number(float InValue) {
            char buffer[48];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), InValue);
            return std::string(buffer, result.ptr);
        }

        /** Rest of the line after the fields already read, without the leading space. */
        std::string RestOfLine(std::istringstream& InStream) {
            std::string rest;
            std::getline(InStream, rest);
            const size_t first = rest.find_first_not_of(' ');
            return first == std::string::npos ? std::string() : rest.substr(first);
        }
    } // namespace

    std::string FLeonHorrorSaveGame::ToText() const {
        std::ostringstream out;
        out << kMagic << ' ' << CurrentVersion << '\n';
        out << "Map " << MapName << '\n';
        out << "Player " << Number(PlayerLocation.x) << ' ' << Number(PlayerLocation.y) << ' '
            << Number(PlayerLocation.z) << ' ' << Number(PlayerYaw) << ' ' << Number(PlayerPitch) << '\n';
        out << "Health " << Number(Health) << '\n';
        out << "Stamina " << Number(Stamina) << '\n';
        out << "Flashlight " << (bFlashlightOn ? 1 : 0) << '\n';
        out << "Weapon " << EquippedWeapon << ' ' << LoadedRounds << '\n';
        // Free text goes last on its line so it may contain spaces.
        for (const auto& [item, count] : Items)
            out << "Item " << count << ' ' << item << '\n';
        for (const auto& guid : CollectedPickups)
            out << "Pickup " << guid << '\n';
        for (const auto& [guid, door] : Doors)
            out << "Door " << (door.bLocked ? 1 : 0) << ' ' << (door.bOpen ? 1 : 0) << ' ' << guid << '\n';
        for (const auto& guid : DeadEnemies)
            out << "Enemy " << guid << '\n';
        for (const auto& guid : ReadNotes)
            out << "Note " << guid << '\n';
        for (const auto& group : PoweredGroups)
            out << "Power " << group << '\n';
        return out.str();
    }

    bool FLeonHorrorSaveGame::FromText(const std::string& InText, FLeonHorrorSaveGame& OutSave) {
        std::istringstream lines(InText);
        std::string line;
        if (!std::getline(lines, line))
            return false;
        {
            std::istringstream header(line);
            std::string magic;
            int32_t version = 0;
            if (!(header >> magic >> version) || magic != kMagic || version < 1 || version > CurrentVersion)
                return false;
        }

        FLeonHorrorSaveGame save;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream record(line);
            std::string kind;
            record >> kind;
            if (kind == "Map") {
                save.MapName = RestOfLine(record);
            } else if (kind == "Player") {
                if (!(record >> save.PlayerLocation.x >> save.PlayerLocation.y >> save.PlayerLocation.z >>
                      save.PlayerYaw >> save.PlayerPitch))
                    return false;
            } else if (kind == "Health") {
                if (!(record >> save.Health))
                    return false;
            } else if (kind == "Stamina") {
                if (!(record >> save.Stamina))
                    return false;
            } else if (kind == "Flashlight") {
                int on = 1;
                if (!(record >> on))
                    return false;
                save.bFlashlightOn = on != 0;
            } else if (kind == "Weapon") {
                if (!(record >> save.EquippedWeapon >> save.LoadedRounds))
                    return false;
            } else if (kind == "Item") {
                int32_t count = 0;
                if (!(record >> count) || count <= 0)
                    return false;
                const std::string item = RestOfLine(record);
                if (item.empty())
                    return false;
                save.Items[item] = count;
            } else if (kind == "Pickup") {
                save.CollectedPickups.insert(RestOfLine(record));
            } else if (kind == "Door") {
                int locked = 0;
                int open = 0;
                if (!(record >> locked >> open))
                    return false;
                save.Doors[RestOfLine(record)] = {locked != 0, open != 0};
            } else if (kind == "Enemy") {
                save.DeadEnemies.insert(RestOfLine(record));
            } else if (kind == "Note") {
                save.ReadNotes.insert(RestOfLine(record));
            } else if (kind == "Power") {
                save.PoweredGroups.insert(RestOfLine(record));
            }
            // Unknown record kinds are skipped: a newer minor writer may add some.
        }
        OutSave = std::move(save);
        return true;
    }

    bool FLeonHorrorSaveGame::SaveToFile(const std::string& InPath) const {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path target(InPath);
        if (target.has_parent_path())
            fs::create_directories(target.parent_path(), ec);
        fs::path temp = target;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!out.is_open())
                return false;
            out << ToText();
            out.flush();
            if (!out.good()) {
                out.close();
                fs::remove(temp, ec);
                return false;
            }
        }
        fs::rename(temp, target, ec);
        if (ec) {
            std::error_code cleanup;
            fs::remove(temp, cleanup);
            return false;
        }
        return true;
    }

    bool FLeonHorrorSaveGame::LoadFromFile(const std::string& InPath, FLeonHorrorSaveGame& OutSave) {
        std::ifstream in(InPath, std::ios::in | std::ios::binary);
        if (!in.is_open())
            return false;
        std::stringstream buffer;
        buffer << in.rdbuf();
        return FromText(buffer.str(), OutSave);
    }

} // namespace Leon
