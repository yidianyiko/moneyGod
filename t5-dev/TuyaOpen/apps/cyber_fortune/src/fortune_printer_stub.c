/**
 * @file fortune_printer_stub.c
 * @brief Build-unblocking stub for the Beken USB-host printer API.
 *
 * fortune_printer.c calls two functions that are NOT part of upstream TuyaOpen:
 *
 *     int bk_usbh_printer_is_connected(void);
 *     int bk_usbh_printer_write(const uint8_t *buffer, uint32_t buflen, uint32_t timeout_ms);
 *
 * They are absent from every source file and every prebuilt .a under
 * platform/T5AI — CherryUSB ships class/printer/usbh_printer.c but that file is
 * not even listed in components/bk_usb/CMakeLists.txt, and it defines no
 * bk_usbh_printer_ wrapper. The working implementation only ever existed in a
 * developer's local platform/T5AI checkout, which TuyaOpen's own .gitignore
 * excludes via its "/platform" rule and tos.py re-clones on demand. It was never
 * committed to any repository, so a clean checkout fails at the final link.
 *
 * This stub restores a buildable tree. Ticket printing is disabled; everything
 * else (screen, TTS, BLE shake remote, network draw) is unaffected, because
 * fortune_printer.c's worker already skips the ticket when no printer reports in.
 *
 * === Replacing this stub ===
 * Define FORTUNE_HAVE_USB_PRINTER (e.g. in the app config) once a real
 * implementation is linked in, and this file compiles to nothing.
 *
 * Deliberately NOT using __attribute__((weak)): a weak definition sitting in
 * libtuyaapp.a would satisfy the reference during the archive scan and stop the
 * linker from ever pulling the strong definition out of a platform library —
 * printing would then silently stay dead. An explicit macro fails loudly instead.
 */

#ifndef FORTUNE_HAVE_USB_PRINTER

#include <stdint.h>

#include "tal_api.h"

/* The warnings below are deliberately distinct from fortune_printer.c's
 * "no printer attached" notice — otherwise a stubbed build is indistinguishable
 * from a genuinely unplugged printer, which is exactly the confusion that would
 * cost a demo. */

int bk_usbh_printer_is_connected(void)
{
    static int warned = 0;

    if (!warned) {
        warned = 1;
        PR_WARN("[fortune-printer] STUB BUILD — bk_usbh_printer_* driver is missing, "
                "ticket printing is permanently disabled in this firmware. "
                "See apps/cyber_fortune/src/fortune_printer_stub.c");
    }
    return 0;
}

int bk_usbh_printer_write(const uint8_t *buffer, uint32_t buflen, uint32_t timeout_ms)
{
    (void)buffer;
    (void)timeout_ms;

    /* Unreachable while is_connected() returns 0, but kept honest in case the
     * caller's guard is ever relaxed. */
    PR_WARN("[fortune-printer] STUB BUILD — discarding %u byte ticket.", (unsigned)buflen);
    return -1;
}

#endif /* !FORTUNE_HAVE_USB_PRINTER */
