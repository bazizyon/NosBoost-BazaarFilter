#define NOMINMAX
#include <windows.h>

#include "ItemIcons.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ItemIcons {
    namespace {
        constexpr char CryptoArray[] = {0x00, 0x20, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33,
                                        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x0A, 0x00};

        int ReadInt(std::ifstream& File) {
            int Value = 0;
            File.read(reinterpret_cast<char*>(&Value), 4);
            return Value;
        }

        std::string ReadString(std::ifstream& File, const size_t Length) {
            std::string Text(Length, '\0');
            File.read(Text.data(), static_cast<std::streamsize>(Length));
            return Text;
        }

        std::string DecryptDat(std::ifstream& File, const int Size) {
            std::string Result;
            int BytesRead = 0;
            while (BytesRead < Size && File) {
                uint8_t Control = 0;
                File.read(reinterpret_cast<char*>(&Control), 1);
                BytesRead++;
                if (Control == 0xFF) {
                    Result += '\r';
                    continue;
                }
                int Validate = Control & 0x7F;
                if (Control & 0x80) {
                    for (; Validate > 0; Validate -= 2) {
                        if (BytesRead >= Size) break;
                        uint8_t Byte = 0;
                        File.read(reinterpret_cast<char*>(&Byte), 1);
                        BytesRead++;
                        Result += CryptoArray[(Byte & 0xF0) >> 4];
                        if (Validate <= 1) break;
                        const char Second = CryptoArray[Byte & 0x0F];
                        if (!Second) break;
                        Result += Second;
                    }
                } else {
                    for (; Validate > 0; Validate--) {
                        if (BytesRead >= Size) break;
                        uint8_t Byte = 0;
                        File.read(reinterpret_cast<char*>(&Byte), 1);
                        BytesRead++;
                        Result += static_cast<char>(Byte ^ 0x33);
                    }
                }
            }
            return Result;
        }

        std::vector<std::string> Tokens(const std::string& Line) {
            std::vector<std::string> Result;
            std::istringstream Stream(Line);
            std::string Token;
            while (std::getline(Stream, Token, '\t')) Result.push_back(Token);
            return Result;
        }

        std::string DataPath() {
            char Path[MAX_PATH];
            GetModuleFileNameA(nullptr, Path, MAX_PATH);
            const std::string Dir(Path);
            return Dir.substr(0, Dir.find_last_of("\\/") + 1) + "NostaleData\\NSgtdData.NOS";
        }

        std::map<int, int> Load() {
            std::map<int, int> Result;
            std::ifstream File(DataPath(), std::ios::binary);
            if (!File.is_open()) return Result;

            const int FileCount = ReadInt(File);
            for (int i = 0; i < FileCount && File; i++) {
                ReadInt(File);
                const int NameSize = ReadInt(File);
                const std::string Name = ReadString(File, NameSize);
                ReadInt(File);
                const int FileSize = ReadInt(File);
                if (Name != "Item.dat") {
                    File.seekg(FileSize, std::ios::cur);
                    continue;
                }

                std::istringstream Content(DecryptDat(File, FileSize));
                std::string Line;
                int CurrentVnum = -1;
                while (std::getline(Content, Line, '\r')) {
                    if (Line.find("VNUM") != std::string::npos) {
                        const auto Parts = Tokens(Line);
                        if (Parts.size() > 2) CurrentVnum = std::atoi(Parts[2].c_str());
                    } else if (Line.find("INDEX") != std::string::npos) {
                        const auto Parts = Tokens(Line);
                        if (Parts.size() > 6 && CurrentVnum >= 0) Result[CurrentVnum] = std::atoi(Parts[6].c_str());
                    }
                }
                break;
            }
            return Result;
        }
    }

    const std::map<int, int>& ByVnum() {
        static const std::map<int, int> Icons = Load();
        return Icons;
    }
}
