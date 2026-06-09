/*
 * xHCI core driver interface.
 *
 * Phase 4a (this commit) brings the controller up to the Running state and
 * reports its root-hub ports; the hcd_driver_state transfer operations are
 * deferred to Phase 4b.
 */

#ifndef _XHCI_CORE_H_
#define _XHCI_CORE_H_

int  xhci_init(void);
void xhci_deinit(void);

#endif /* !_XHCI_CORE_H_ */
