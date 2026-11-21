#include <cstdint>
#include <cstdio>
#include <cstring>
#include <zlib.h>

#include "check_source.inc"

int main()
{
    static const uint8_t disc[] = "synthetic disc executable";
    static const uint8_t update[] = "synthetic update binary";
    static const uint8_t custom[] = "synthetic custom file";
    static const uint8_t changed[] = "synthetic disc executablE";
    const bool disc_build = strcmp(RACACIT_BUILD_PROFILE_ID, "BCUS98124-1.00") == 0;
    if (accepts_game_executable(disc, sizeof disc - 1) != disc_build ||
        accepts_game_executable(update, sizeof update - 1) == disc_build ||
        !accepts_game_executable(custom, sizeof custom - 1) ||
        !accepts_game_executable(changed, sizeof changed - 1)) {
        fprintf(stderr, "Unexpected profile decision for %s\n", RACACIT_BUILD_PROFILE_ID);
        return 1;
    }
    printf("Accepted the matching executable, rejected the known mismatch, and kept unknown-file behavior for %s.\n",
           RACACIT_BUILD_PROFILE_ID);
    return 0;
}
