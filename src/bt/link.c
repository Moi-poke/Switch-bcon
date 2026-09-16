/* 取込期限・再生期限の番。実体は link_cap / link_beacon の tick。 */

#include <stdint.h>

#include "link.h"

void link_poll(uint32_t now_ms)
{
    link_cap_tick(now_ms);
    link_beacon_tick(now_ms);
}
