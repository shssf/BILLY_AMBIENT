#pragma once

/** \file mdns_support.h
 *  \brief Public API for mDNS service (ESP‑IDF v5 uses external component).
 *  All public symbols use the `mdns_` prefix.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Start mDNS responder, set hostname/instance and advertise services.
 *  \param hostname  host name used by mDNS (e.g., "arduino_1") -> "arduino_1.local"
 *  \param instance  human‑friendly instance name (shown in browsers)
 *  \return 0 on success; negative on failure
 */
int mdns_start(const char *hostname, const char *instance);

/** \brief Stop mDNS and free resources. */
void mdns_stop(void);

#ifdef __cplusplus
}
#endif
