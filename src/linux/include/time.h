/* sound/asound.h a besoin de struct timespec et time_t : ils viennent du noyau (pas de libc dans TitanOs) */
#pragma once
#include <linux/time.h>
typedef long time_t;
