#ifndef __BMWDT_H__
#define __BMWDT_H__

int bmwdt_register_device(struct watchdog_device *wdd);
void bmwdt_unregister_device(struct watchdog_device *wdd);

#endif
