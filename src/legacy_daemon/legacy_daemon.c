/* Enter the service domain using a PIE binary without text relocations. */
#include <stdio.h>
#include <unistd.h>

#ifndef LEGACY_DAEMON
#error LEGACY_DAEMON must name the vendor executable
#endif

int main(int argc, char **argv) {
    (void)argc;
    execv("/system/vendor/libexec/batman/" LEGACY_DAEMON, argv);
    perror("execv " LEGACY_DAEMON);
    return 127;
}
