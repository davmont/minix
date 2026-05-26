/*
 * OHCI (Open Host Controller Interface) register definitions.
 *
 * Based on the OpenHCI Open Host Controller Interface Specification for
 * USB, release 1.0a (Compaq/Microsoft/National Semiconductor, 1999).
 *
 * Unlike EHCI, OHCI has no separate "capability" register window: all
 * registers are operational and live at the PCI BAR0 MMIO base.  The whole
 * register file is exactly 256 bytes (port-status array sized for the
 * maximum 15 root-hub ports the spec allows).
 */

#ifndef _OHCI_REGS_H_
#define _OHCI_REGS_H_


/*===========================================================================*
 *    PCI identification                                                     *
 *===========================================================================*/
/* PCI class/subclass/progif for OHCI (USB 1.1) */
#define OHCI_PCI_CLASS		0x0C	/* Serial Bus Controller */
#define OHCI_PCI_SUBCLASS	0x03	/* USB Controller */
#define OHCI_PCI_PROGIF		0x10	/* OHCI */

/* PCI register offsets used during device discovery */
#define OHCI_PCI_BAR0		0x10	/* Base Address Register 0 (MMIO) */
#define OHCI_PCI_BAR0_MMIO_MASK	(~0x0Fu)/* Mask off lower 4 flags */
#define OHCI_PCI_IRQ		0x3C	/* Interrupt Line register */

/* Minimum MMIO region size to map.  Spec says 256 bytes is enough for the
 * fixed register file plus 15 root-hub port-status registers; round up to a
 * page so hcd_os_regs_init has something sane to work with. */
#define OHCI_MMIO_SIZE		0x1000u


/*===========================================================================*
 *    Operational registers (at MMIO base)                                   *
 *===========================================================================*/
#define OHCI_HC_REVISION	0x00u	/* HcRevision           — RO */
#define OHCI_HC_CONTROL		0x04u	/* HcControl            — RW */
#define OHCI_HC_CMD_STATUS	0x08u	/* HcCommandStatus      — RW */
#define OHCI_HC_INT_STATUS	0x0Cu	/* HcInterruptStatus    — RW1C */
#define OHCI_HC_INT_ENABLE	0x10u	/* HcInterruptEnable    — RW */
#define OHCI_HC_INT_DISABLE	0x14u	/* HcInterruptDisable   — RW */
#define OHCI_HC_HCCA		0x18u	/* HcHCCA               — RW (phys ptr) */
#define OHCI_HC_PERIOD_CUR_ED	0x1Cu	/* HcPeriodCurrentED    — RW */
#define OHCI_HC_CONTROL_HEAD_ED	0x20u	/* HcControlHeadED      — RW */
#define OHCI_HC_CONTROL_CUR_ED	0x24u	/* HcControlCurrentED   — RW */
#define OHCI_HC_BULK_HEAD_ED	0x28u	/* HcBulkHeadED         — RW */
#define OHCI_HC_BULK_CUR_ED	0x2Cu	/* HcBulkCurrentED      — RW */
#define OHCI_HC_DONE_HEAD	0x30u	/* HcDoneHead           — RO */
#define OHCI_HC_FM_INTERVAL	0x34u	/* HcFmInterval         — RW */
#define OHCI_HC_FM_REMAINING	0x38u	/* HcFmRemaining        — RO */
#define OHCI_HC_FM_NUMBER	0x3Cu	/* HcFmNumber           — RO */
#define OHCI_HC_PERIODIC_START	0x40u	/* HcPeriodicStart      — RW */
#define OHCI_HC_LS_THRESHOLD	0x44u	/* HcLSThreshold        — RW */
#define OHCI_HC_RH_DESC_A	0x48u	/* HcRhDescriptorA      — RW */
#define OHCI_HC_RH_DESC_B	0x4Cu	/* HcRhDescriptorB      — RW */
#define OHCI_HC_RH_STATUS	0x50u	/* HcRhStatus           — RW */
#define OHCI_HC_RH_PORT_STATUS(p)					\
				(0x54u + 4u * (unsigned)(p))	/* per-port RW */


/*===========================================================================*
 *    HcRevision (0x00)                                                      *
 *===========================================================================*/
#define OHCI_REV_MASK		0xFFu	/* Compliant spec revision (e.g. 0x10) */
#define OHCI_REV_LEGACY		(1u << 8)/* "Legacy support" emulation present */


/*===========================================================================*
 *    HcControl (0x04)                                                       *
 *===========================================================================*/
#define OHCI_CTL_CBSR		0x00000003u	/* Control/Bulk service ratio */
#define OHCI_CTL_PLE		(1u << 2)	/* PeriodicListEnable */
#define OHCI_CTL_IE		(1u << 3)	/* IsochronousEnable */
#define OHCI_CTL_CLE		(1u << 4)	/* ControlListEnable */
#define OHCI_CTL_BLE		(1u << 5)	/* BulkListEnable */
#define OHCI_CTL_HCFS_SHIFT	6
#define OHCI_CTL_HCFS_MASK	(0x3u << OHCI_CTL_HCFS_SHIFT)
#define OHCI_CTL_HCFS_RESET	(0x0u << OHCI_CTL_HCFS_SHIFT)
#define OHCI_CTL_HCFS_RESUME	(0x1u << OHCI_CTL_HCFS_SHIFT)
#define OHCI_CTL_HCFS_OPER	(0x2u << OHCI_CTL_HCFS_SHIFT)
#define OHCI_CTL_HCFS_SUSPEND	(0x3u << OHCI_CTL_HCFS_SHIFT)
#define OHCI_CTL_IR		(1u << 8)	/* InterruptRouting (SMM) */
#define OHCI_CTL_RWC		(1u << 9)	/* RemoteWakeupConnected */
#define OHCI_CTL_RWE		(1u << 10)	/* RemoteWakeupEnable */


/*===========================================================================*
 *    HcCommandStatus (0x08)                                                 *
 *===========================================================================*/
#define OHCI_CMD_HCR		(1u << 0)	/* HostControllerReset */
#define OHCI_CMD_CLF		(1u << 1)	/* ControlListFilled */
#define OHCI_CMD_BLF		(1u << 2)	/* BulkListFilled */
#define OHCI_CMD_OCR		(1u << 3)	/* OwnershipChangeRequest */
#define OHCI_CMD_SOC_SHIFT	16
#define OHCI_CMD_SOC_MASK	(0x3u << OHCI_CMD_SOC_SHIFT)


/*===========================================================================*
 *    HcInterruptStatus / Enable / Disable (0x0C / 0x10 / 0x14)              *
 *                                                                           *
 *    Same bit layout in all three registers.  W1C semantics for Status.    *
 *===========================================================================*/
#define OHCI_INT_SO		(1u << 0)	/* SchedulingOverrun */
#define OHCI_INT_WDH		(1u << 1)	/* WritebackDoneHead */
#define OHCI_INT_SF		(1u << 2)	/* StartOfFrame */
#define OHCI_INT_RD		(1u << 3)	/* ResumeDetected */
#define OHCI_INT_UE		(1u << 4)	/* UnrecoverableError */
#define OHCI_INT_FNO		(1u << 5)	/* FrameNumberOverflow */
#define OHCI_INT_RHSC		(1u << 6)	/* RootHubStatusChange */
#define OHCI_INT_OC		(1u << 30)	/* OwnershipChange */
#define OHCI_INT_MIE		(1u << 31)	/* MasterInterruptEnable */
#define OHCI_INT_ALL_EVENTS	(OHCI_INT_SO | OHCI_INT_WDH | OHCI_INT_SF |  \
				 OHCI_INT_RD | OHCI_INT_UE | OHCI_INT_FNO |  \
				 OHCI_INT_RHSC | OHCI_INT_OC)


/*===========================================================================*
 *    HcFmInterval (0x34)                                                    *
 *===========================================================================*/
#define OHCI_FMI_FI_MASK	0x3FFFu		/* FrameInterval (default 11999) */
#define OHCI_FMI_FI_DEFAULT	11999u
#define OHCI_FMI_FSMPS_SHIFT	16
#define OHCI_FMI_FSMPS_MASK	(0x7FFFu << OHCI_FMI_FSMPS_SHIFT)
#define OHCI_FMI_FIT		(1u << 31)	/* FrameIntervalToggle */

/* PeriodicStart is conventionally FI - (FI/10) = 90 % of frame */
#define OHCI_PERIODIC_START_DEFAULT \
				((OHCI_FMI_FI_DEFAULT * 9u) / 10u)

/* Low-speed threshold default per spec §7.3.5 */
#define OHCI_LS_THRESHOLD_DEFAULT 0x0628u


/*===========================================================================*
 *    HcRhDescriptorA (0x48)                                                 *
 *===========================================================================*/
#define OHCI_RHDA_NDP_MASK	0xFFu		/* NumberDownstreamPorts */
#define OHCI_RHDA_PSM		(1u << 8)	/* PowerSwitchingMode */
#define OHCI_RHDA_NPS		(1u << 9)	/* NoPowerSwitching */
#define OHCI_RHDA_DT		(1u << 10)	/* DeviceType (must be 0) */
#define OHCI_RHDA_OCPM		(1u << 11)	/* OverCurrentProtectionMode */
#define OHCI_RHDA_NOCP		(1u << 12)	/* NoOverCurrentProtection */
#define OHCI_RHDA_POTPGT_SHIFT	24
#define OHCI_RHDA_POTPGT_MASK	(0xFFu << OHCI_RHDA_POTPGT_SHIFT)
#define OHCI_RHDA_NDP(p)	((p) & OHCI_RHDA_NDP_MASK)


/*===========================================================================*
 *    HcRhStatus (0x50)                                                      *
 *===========================================================================*/
#define OHCI_RHS_LPS		(1u << 0)	/* LocalPowerStatus (W: clear) */
#define OHCI_RHS_OCI		(1u << 1)	/* OverCurrentIndicator */
#define OHCI_RHS_DRWE		(1u << 15)	/* DeviceRemoteWakeupEnable */
#define OHCI_RHS_LPSC		(1u << 16)	/* LocalPowerStatusChange (W: set global power) */
#define OHCI_RHS_OCIC		(1u << 17)	/* OverCurrentIndicatorChange */
#define OHCI_RHS_CRWE		(1u << 31)	/* ClearRemoteWakeupEnable */


/*===========================================================================*
 *    HcRhPortStatus[i] (0x54 + 4*i)                                         *
 *===========================================================================*/
#define OHCI_PORT_CCS		(1u << 0)	/* CurrentConnectStatus */
#define OHCI_PORT_PES		(1u << 1)	/* PortEnableStatus */
#define OHCI_PORT_PSS		(1u << 2)	/* PortSuspendStatus */
#define OHCI_PORT_POCI		(1u << 3)	/* PortOverCurrentIndicator */
#define OHCI_PORT_PRS		(1u << 4)	/* PortResetStatus */
#define OHCI_PORT_PPS		(1u << 8)	/* PortPowerStatus */
#define OHCI_PORT_LSDA		(1u << 9)	/* LowSpeedDeviceAttached */
#define OHCI_PORT_CSC		(1u << 16)	/* ConnectStatusChange */
#define OHCI_PORT_PESC		(1u << 17)	/* PortEnableStatusChange */
#define OHCI_PORT_PSSC		(1u << 18)	/* PortSuspendStatusChange */
#define OHCI_PORT_OCIC		(1u << 19)	/* OverCurrentIndicatorChange */
#define OHCI_PORT_PRSC		(1u << 20)	/* PortResetStatusChange */
#define OHCI_PORT_ALL_CHANGES	(OHCI_PORT_CSC  | OHCI_PORT_PESC |	\
				 OHCI_PORT_PSSC | OHCI_PORT_OCIC |	\
				 OHCI_PORT_PRSC)


/*===========================================================================*
 *    Endpoint Descriptor (ED) — spec §4.2                                  *
 *                                                                           *
 *    These bit fields live in ED.control (the first DWord).                *
 *===========================================================================*/
#define OHCI_ED_FA_MASK		0x0000007Fu	/* FunctionAddress */
#define OHCI_ED_EN_SHIFT	7
#define OHCI_ED_EN_MASK		(0xFu << OHCI_ED_EN_SHIFT)	/* EndpointNumber */
#define OHCI_ED_D_SHIFT		11
#define OHCI_ED_D_MASK		(0x3u << OHCI_ED_D_SHIFT)	/* Direction */
#define OHCI_ED_D_FROM_TD	(0x0u << OHCI_ED_D_SHIFT)
#define OHCI_ED_D_OUT		(0x1u << OHCI_ED_D_SHIFT)
#define OHCI_ED_D_IN		(0x2u << OHCI_ED_D_SHIFT)
#define OHCI_ED_S		(1u << 13)	/* Speed: 1=low, 0=full */
#define OHCI_ED_K		(1u << 14)	/* sKip (skip this ED) */
#define OHCI_ED_F		(1u << 15)	/* Format: 1=isoc, 0=general */
#define OHCI_ED_MPS_SHIFT	16
#define OHCI_ED_MPS_MASK	(0x7FFu << OHCI_ED_MPS_SHIFT)	/* MaximumPacketSize */

/* Bits packed into ED.headp (the third DWord, low bits below 4-byte align) */
#define OHCI_ED_HEADP_H		(1u << 0)	/* Halted (HC sets on error) */
#define OHCI_ED_HEADP_C		(1u << 1)	/* dataToggleCarry */
#define OHCI_ED_HEADP_PTR_MASK	0xFFFFFFF0u


/*===========================================================================*
 *    Transfer Descriptor (TD) — spec §4.3 (general TD only; ISO TD later)  *
 *===========================================================================*/
#define OHCI_TD_R		(1u << 18)	/* bufferRounding */
#define OHCI_TD_DP_SHIFT	19
#define OHCI_TD_DP_MASK		(0x3u << OHCI_TD_DP_SHIFT)	/* Direction/PID */
#define OHCI_TD_DP_SETUP	(0x0u << OHCI_TD_DP_SHIFT)
#define OHCI_TD_DP_OUT		(0x1u << OHCI_TD_DP_SHIFT)
#define OHCI_TD_DP_IN		(0x2u << OHCI_TD_DP_SHIFT)
#define OHCI_TD_DI_SHIFT	21
#define OHCI_TD_DI_MASK		(0x7u << OHCI_TD_DI_SHIFT)	/* DelayInterrupt */
#define OHCI_TD_DI_NO_INT	(0x7u << OHCI_TD_DI_SHIFT)	/* never interrupt */
#define OHCI_TD_T_SHIFT		24
#define OHCI_TD_T_MASK		(0x3u << OHCI_TD_T_SHIFT)	/* DataToggle */
#define OHCI_TD_T_DATA0		(0x2u << OHCI_TD_T_SHIFT)
#define OHCI_TD_T_DATA1		(0x3u << OHCI_TD_T_SHIFT)
#define OHCI_TD_EC_SHIFT	26
#define OHCI_TD_EC_MASK		(0x3u << OHCI_TD_EC_SHIFT)	/* ErrorCount */
#define OHCI_TD_CC_SHIFT	28
#define OHCI_TD_CC_MASK		(0xFu << OHCI_TD_CC_SHIFT)	/* ConditionCode */
#define OHCI_TD_CC_NOERROR	(0x0u << OHCI_TD_CC_SHIFT)
#define OHCI_TD_CC_NOT_ACCESSED	(0xEu << OHCI_TD_CC_SHIFT)	/* HC has not touched it yet */

/* nextTD field: lower 4 bits are reserved (must be 0); use mask to extract */
#define OHCI_TD_NEXT_PTR_MASK	0xFFFFFFF0u


/*===========================================================================*
 *    HCCA (Host Controller Communications Area) — spec §4.4                *
 *===========================================================================*/
#define OHCI_HCCA_INT_TABLE_LEN	32u		/* hccaInterruptTable[] entries */
#define OHCI_HCCA_SIZE		256u		/* total bytes, 256-byte aligned */


#endif /* !_OHCI_REGS_H_ */
