/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SATCOMPUTE_FREQUENCY_PROTECTION_CONTROLLER_H
#define SATCOMPUTE_FREQUENCY_PROTECTION_CONTROLLER_H
#include "../policy/compfrr/compfrr-controller.h"
namespace ns3::protection
{
/** Compatibility alias; the event adapter belongs to CompFRR-F, not shared runtime. */
using FrequencyProtectionController = CompFrrController;
}
#endif
