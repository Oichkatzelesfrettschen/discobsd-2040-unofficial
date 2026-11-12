#ifndef UFS_DISK_SCHEDULE_H
#define UFS_DISK_SCHEDULE_H

/* Return 0 when both jobs finish, or 1 when the switch budget expires. */
int disk_schedule_run(void (*first)(void), void (*second)(void),
    void (*switch_state)(int, int));
void disk_schedule_yield(void);
int disk_schedule_current(void);

#endif
