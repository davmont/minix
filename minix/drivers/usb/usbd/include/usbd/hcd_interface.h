/*
 * Interface for HCD
 *
 * This file holds prototypes that must be implemented by HCD
 * and call that should be used for asynchronous events
 * (interrupts, UBR submits, hub events, ...)
 */

#ifndef _HCD_INTERFACE_H_
#define _HCD_INTERFACE_H_

#include <usbd/hcd_common.h>


/*===========================================================================*
 *    HCD additional defines                                                 *
 *===========================================================================*/
/* Can be returned by 'read_data' to indicate error */
#define HCD_READ_ERR -1

/* Maximum number of root-hub ports per host controller.
 * EHCI supports up to 15 ports; OHCI/UHCI typically have 2–4. */
#define HCD_MAX_PORTS 15

/* Possible states of USB device address */
typedef enum {

	HCD_ADDR_AVAILABLE = 0,		/* Default for reset */
	HCD_ADDR_USED
}
hcd_addr_state;


/*===========================================================================*
 *    HCD driver structure to be filled                                      *
 *===========================================================================*/
struct hcd_driver_state {
	/* Standard USB controller procedures */
	void	(*setup_device)		(void *, hcd_reg1, hcd_reg1,
					hcd_datatog *, hcd_datatog *);
	int	(*reset_device)		(void *, hcd_speed *);
	void	(*setup_stage)		(void *, hcd_ctrlrequest *);
	void	(*rx_stage)		(void *, hcd_datarequest *);
	void	(*tx_stage)		(void *, hcd_datarequest *);
	void	(*in_data_stage)	(void *);
	void	(*out_data_stage)	(void *);
	void	(*in_status_stage)	(void *);
	void	(*out_status_stage)	(void *);
	int	(*read_data)		(void *, hcd_reg1 *, hcd_reg1);
	int	(*check_error)		(void *, hcd_transfer, hcd_reg1,
					hcd_direction);

	/* Controller's private data (like mapped registers) */
	void *		private_data;

	/* Index of this controller in the platform's driver table (0-based).
	 * Set by the platform init function (e.g. ehci_init). */
	int		controller_id;

	/* One device slot per root-hub port.  Index matches the hardware
	 * port number.  NULL means no device is currently attached. */
	hcd_device_state * port_device[HCD_MAX_PORTS];

	/*
	 * Root-hub port of the device whose transfer is currently being
	 * programmed.  Set by the generic HCD layer immediately before each
	 * setup_device() call; the per-controller driver uses it to route the
	 * completion interrupt back to the right port_device[] slot.  Because
	 * all transfers are serialised through the URB scheduler, exactly one
	 * device is "active" at any instant.
	 */
	int		active_port;

	/*
	 * Root-hub port of the device currently being enumerated.  Set before
	 * reset_device() under the enumeration lock so reset_device() resets
	 * the correct port (enumeration of distinct root devices is
	 * serialised, since two unaddressed devices would both answer to the
	 * default address 0).
	 */
	int		enum_port;

	/* Array to hold information of unused device addresses */
	hcd_addr_state dev_addr[HCD_TOTAL_ADDR];
};


/*===========================================================================*
 *    HCD event handling routine                                             *
 *===========================================================================*/
/* Handle asynchronous event */
void hcd_handle_event(hcd_device_state *, hcd_event, hcd_reg1);

/* This resolves port's device structure for given driver, event and port index */
void hcd_update_port(hcd_driver_state *, hcd_event, int);


#endif /* !_HCD_INTERFACE_H_ */
