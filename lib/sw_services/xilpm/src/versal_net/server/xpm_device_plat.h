/******************************************************************************
* Copyright (c) 2018 - 2022 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


#ifndef XPM_DEVICE_PLAT_H_
#define XPM_DEVICE_PLAT_H_

#include "xpm_node.h"
#include "xpm_power.h"
#include "xpm_clock.h"
#include "xpm_reset.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PSM RAM Base address */
#define XPM_PSM_RAM_BASE_ADDR		(0xEBC00000U)
#define XPM_PSM_RAM_SIZE		(0x40000U)

#define IS_DEV_AIE(ID)			(0U != 0U)

/************************** Function Prototypes ******************************/
maybe_unused static u8 XPmDevice_IsRequestable(u32 NodeId)
{
	u8 Requestable = 0U;

	switch (NODESUBCLASS(NodeId)) {
	case (u32)XPM_NODESUBCL_DEV_CORE:
	case (u32)XPM_NODESUBCL_DEV_PERIPH:
	case (u32)XPM_NODESUBCL_DEV_MEM:
	case (u32)XPM_NODESUBCL_DEV_PL:
	case (u32)XPM_NODESUBCL_DEV_AIE:
		Requestable = 1U;
		break;
	default:
		Requestable = 0U;
		break;
	}

	return Requestable;
}

/************************** Function Prototypes ******************************/
maybe_unused static inline void XPmDevice_ConfigureADMA(const u32 Id)
{
	(void)Id;
}
maybe_unused static inline XStatus XPmDevice_SdResetWorkaround(const struct XPm_DeviceNode *Device)
{
	(void)Device;
	return XST_SUCCESS;
}
maybe_unused static inline XStatus HandleDeviceAttr(struct XPm_Reqm *Reqm, u32 ReqCaps,
				       u32 PrevState, u32 Enable)
{
	(void)Reqm;
	(void)ReqCaps;
	(void)PrevState;
	(void)Enable;
	return XST_SUCCESS;
}
maybe_unused static inline XStatus XPmAieDevice_UpdateClockDiv(const struct XPm_DeviceNode *Device, const XPm_Subsystem *Subsystem,
		const u32 Divider)
{
	(void)Device;
	(void)Subsystem;
	(void)Divider;
	return XST_SUCCESS;
}
maybe_unused static inline XStatus XPmDevice_PlatSetNode(const u32 Id, struct XPm_DeviceNode *Device, u16 *DbgErr)
{
	(void)Id;
	(void)Device;
	(void)DbgErr;
	return XST_DEVICE_NOT_FOUND;
}
maybe_unused static inline struct XPm_DeviceNode **XPmDevice_PlatGetById(const u32 DeviceId)
{
	(void)DeviceId;
	return NULL;
}
maybe_unused static inline XStatus XPmDevice_PlatAddParent(const u32 Id, const u32 ParentId)
{
	(void)Id;
	(void)ParentId;
	return XST_SUCCESS;
}
maybe_unused static inline struct XPm_Reqm *XPmDevice_GetAieReqm(struct XPm_DeviceNode *Device,
						    XPm_Subsystem *Subsystem)
{
	(void)Device;
	(void)Subsystem;

	return NULL;
}
maybe_unused static inline void PlatDevRequest(struct XPm_DeviceNode *Device,
				   const XPm_Subsystem *Subsystem, const u32 QoS,
				   XStatus *Status)
{
	(void)Device;
	(void)Subsystem;
	(void)QoS;
	(void)Status;
}
maybe_unused static inline XStatus AddDevAttributes(const u32 *Args, const u32 NumArgs)
{
	(void)Args;
	(void)NumArgs;
	return XST_SUCCESS;
}

#ifdef __cplusplus
}
#endif

/** @} */
#endif /* XPM_DEVICE_PLAT_H_ */