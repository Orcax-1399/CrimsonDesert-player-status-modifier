#include "scanner.h"

#include "logger.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct PatternDefinition {
    const char* name = nullptr;
    const char* text = nullptr;
    std::size_t hook_offset = 0;
};

struct SectionSpan {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
    std::string section_name;
    bool executable = false;
};

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct MatchResult {
    uintptr_t address = 0;
    std::string section_name;
};

std::vector<SectionSpan> EnumerateMainModuleSections(const bool text_only) {
    std::vector<SectionSpan> sections;

    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
        return sections;
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return sections;
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return sections;
    }

    const auto first_section = IMAGE_FIRST_SECTION(nt_headers);
    for (unsigned i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
        const auto& section = first_section[i];

        char name_buffer[9]{};
        std::memcpy(name_buffer, section.Name, 8);
        const std::string section_name(name_buffer);

        if (text_only && section_name != ".text") {
            continue;
        }

        if (section.Misc.VirtualSize == 0) {
            continue;
        }

        sections.push_back({
            module + section.VirtualAddress,
            static_cast<std::size_t>(section.Misc.VirtualSize),
            section_name,
            (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0,
        });
    }

    Log("scanner: enumerated %zu main-module section(s)%s",
        sections.size(),
        text_only ? " [text only]" : " [fallback]");
    return sections;
}

std::vector<PatternByte> ParsePattern(const char* text) {
    std::vector<PatternByte> bytes;
    std::string token;

    for (const char* cursor = text; *cursor != '\0';) {
        while (*cursor == ' ') {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        const char* token_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }

        token.assign(token_start, cursor);
        if (token == "?" || token == "??" || token == "*") {
            bytes.push_back({0, true});
        } else {
            bytes.push_back({static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }

    return bytes;
}

std::vector<MatchResult> ScanPattern(const std::vector<SectionSpan>& spans,
                                     const std::vector<PatternByte>& pattern,
                                     const std::size_t max_results) {
    std::vector<MatchResult> results;
    if (pattern.empty()) {
        return results;
    }

    for (const auto& span : spans) {
        if (span.begin == nullptr || span.size < pattern.size()) {
            continue;
        }

        const auto end = span.size - pattern.size();
        for (std::size_t offset = 0; offset <= end; ++offset) {
            bool matched = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (!pattern[index].wildcard && span.begin[offset + index] != pattern[index].value) {
                    matched = false;
                    break;
                }
            }

            if (!matched) {
                continue;
            }

            results.push_back({
                reinterpret_cast<uintptr_t>(span.begin + offset),
                span.section_name,
            });

            if (results.size() >= max_results) {
                return results;
            }
        }
    }

    return results;
}

uintptr_t ScanInSections(const PatternDefinition* definitions,
                         const std::size_t definition_count,
                         const char* scan_name) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool text_only = pass == 0;
        const auto sections = EnumerateMainModuleSections(text_only);
        if (sections.empty()) {
            continue;
        }

        for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
            const auto& definition = definitions[definition_index];
            const auto pattern = ParsePattern(definition.text);
            const auto matches = ScanPattern(sections, pattern, 16);

            Log("scanner: %s pattern '%s' found %zu match(es)", scan_name, definition.name, matches.size());
            for (const auto& match : matches) {
                Log("scanner:   CrimsonDesert.exe!%s @ 0x%p -> hook 0x%p",
                    match.section_name.c_str(),
                    reinterpret_cast<void*>(match.address),
                    reinterpret_cast<void*>(match.address + definition.hook_offset));
            }

            if (matches.size() == 1) {
                return matches.front().address + definition.hook_offset;
            }

            if (!matches.empty()) {
                uintptr_t executable_match = 0;
                std::size_t executable_match_count = 0;
                for (const auto& match : matches) {
                    for (const auto& section : sections) {
                        if (section.section_name == match.section_name && section.executable) {
                            executable_match = match.address;
                            ++executable_match_count;
                            break;
                        }
                    }
                }

                if (executable_match_count == 1) {
                    Log("scanner: %s pattern '%s' selecting unique executable-section match", scan_name, definition.name);
                    return executable_match + definition.hook_offset;
                }
            }
        }
    }

    return 0;
}

}  // namespace

uintptr_t ScanForPlayerPointerCapture() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 8B 42 68 48 8D 56 58 ?? 8D ?? 24 ?? ?? 00 00 ?? 8B ?? ?? ?? 00 00 E8 ?? ?? ?? ?? 84 C0 75",
            0,
        },
    };

    const auto match = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]), "player-pointer");
    if (match == 0) {
        Log("scanner: player-pointer pattern not found");
    }

    return match;
}

uintptr_t ScanForDamageValueAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "49 8B 77 38 44 8B 24 88 48 8D 4C 24 ?? 4A 8B 1C E3", 17},
    };

    const auto match = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]), "damage");
    if (match == 0) {
        Log("scanner: damage pattern not found");
    }

    return match;
}

uintptr_t ScanForItemGainAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "49 01 4C 38 10", 0},
    };

    // Known item-loss opcode kept for future work:
    //   49 29 4C 07 10    sub [r15+rax+10], rcx
    const auto match = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]), "item-gain");
    if (match == 0) {
        Log("scanner: item-gain pattern not found");
    }

    return match;
}

uintptr_t ScanForStatsAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24", 12},
        {"fallback", "48 C1 E0 04 48 03 46 58", 8},
    };

    const auto match = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]), "stats");
    if (match == 0) {
        Log("scanner: stats pattern not found");
    }

    return match;
}

uintptr_t ScanForStatWriteAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 2B 47 18 48 39 5F 18 48 0F 4F C2 48 89 47 20 48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38 66 89 6F 50",
            20,
        },
        {
            "fallback",
            "48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38",
            4,
        },
    };

    const auto match = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]), "stat-write");
    if (match == 0) {
        Log("scanner: stat-write pattern not found");
    }

    return match;
}
