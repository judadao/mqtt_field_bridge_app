#ifndef BRIDGE_CONTROL_H
#define BRIDGE_CONTROL_H

void bridge_control_init(void);

/* Returns the number of valid, enabled peers applied. */
int bridge_control_apply_peers(void);

#endif /* BRIDGE_CONTROL_H */
